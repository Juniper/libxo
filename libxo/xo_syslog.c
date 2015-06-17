/*
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)syslog.c	8.5 (Berkeley) 4/29/95";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netdb.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

#include "xo.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

static int xo_logfile = -1;		/* fd for log */
static int xo_status;			/* connection xo_status */
static int xo_opened;			/* have done openlog() */
static int xo_logstat = 0;		/* xo_status bits, set by openlog() */
static const char *xo_logtag = NULL;	/* string to tag the entry with */
static int xo_logfacility = LOG_USER;	/* default facility code */
static int xo_logmask = 0xff;		/* mask of priorities to be logged */
static pthread_mutex_t xo_syslog_mutex UNUSED = PTHREAD_MUTEX_INITIALIZER;

#if 0
#define    THREAD_LOCK()                 \
    do {                                 \
        if (__isthreaded) _pthread_mutex_lock(&xo_syslog_mutex);    \
    } while(0)
#define    THREAD_UNLOCK()               \
    do {                                 \
        if (__isthreaded) _pthread_mutex_unlock(&xo_syslog_mutex);    \
    } while(0)
#else
#define    THREAD_LOCK()
#define    THREAD_UNLOCK()
#endif

static void xo_disconnectlog(void); /* disconnect from syslogd */
static void xo_connectlog(void);    /* (re)connect to syslogd */
static void xo_openlog_unlocked(const char *, int, int);

enum {
    NOCONN = 0,
    CONNDEF,
    CONNPRIV,
};

/*
 * Format of the magic cookie passed through the stdio hook
 */
struct bufcookie {
    char *base;    /* start of buffer */
    int left;
};

/*
 * stdio write hook for writing to a static string buffer
 * XXX: Maybe one day, dynamically allocate it so that the line length
 *      is `unlimited'.
 */
static int
xo_writehook (void *cookie, const char *buf, int len)
{
    struct bufcookie *h;    /* private `handle' */

    h = (struct bufcookie *) cookie;
    if (len > h->left) {
        /* clip in case of wraparound */
        len = h->left;
    }
    if (len > 0) {
        (void) memcpy(h->base, buf, len); /* `write' it. */
        h->base += len;
        h->left -= len;
    }
    return len;
}

/*
 * syslog, vsyslog --
 *    print message on log file; output is intended for syslogd(8).
 */
void
xo_syslog (int pri, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsyslog(pri, fmt, ap);
    va_end(ap);
}

void
xo_vsyslog (int pri, const char *fmt, va_list ap)
{
    int cnt;
    char ch, *p;
    time_t now;
    int fd, saved_errno;
    char *stdp, tbuf[2048], fmt_cpy[1024], timbuf[26], errstr[64];
    FILE *fp, *fmt_fp;
    struct bufcookie tbuf_cookie;
    struct bufcookie fmt_cookie;

#define    INTERNALLOG    LOG_ERR|LOG_CONS|LOG_PERROR|LOG_PID
    /* Check for invalid bits. */
    if (pri & ~(LOG_PRIMASK|LOG_FACMASK)) {
        syslog(INTERNALLOG,
            "syslog: unknown facility/priority: %x", pri);
        pri &= LOG_PRIMASK|LOG_FACMASK;
    }

    saved_errno = errno;

    THREAD_LOCK();

    /* Check priority against setlogmask values. */
    if (!(LOG_MASK(LOG_PRI(pri)) & xo_logmask)) {
        THREAD_UNLOCK();
        return;
    }

    /* Set default facility if none specified. */
    if ((pri & LOG_FACMASK) == 0)
        pri |= xo_logfacility;

    /* Create the primary stdio hook */
    tbuf_cookie.base = tbuf;
    tbuf_cookie.left = sizeof(tbuf);
    fp = fwopen(&tbuf_cookie, xo_writehook);
    if (fp == NULL) {
        THREAD_UNLOCK();
        return;
    }

    /* Build the message. */
    (void) time(&now);
    (void) fprintf(fp, "<%d>", pri);
    (void) fprintf(fp, "%.15s ", ctime_r(&now, timbuf) + 4);
    if (xo_logstat & LOG_PERROR) {
        /* Transfer to string buffer */
        (void) fflush(fp);
        stdp = tbuf + (sizeof(tbuf) - tbuf_cookie.left);
    }
    if (xo_logtag == NULL)
        xo_logtag = getprogname();
    if (xo_logtag != NULL)
        (void) fprintf(fp, "%s", xo_logtag);
    if (xo_logstat & LOG_PID)
        (void) fprintf(fp, "[%d]", getpid());
    if (xo_logtag != NULL) {
        (void) fprintf(fp, ": ");
    }

    /* Check to see if we can skip expanding the %m */
    if (strstr(fmt, "%m")) {

        /* Create the second stdio hook */
        fmt_cookie.base = fmt_cpy;
        fmt_cookie.left = sizeof(fmt_cpy) - 1;
        fmt_fp = fwopen(&fmt_cookie, xo_writehook);
        if (fmt_fp == NULL) {
            fclose(fp);
            THREAD_UNLOCK();
            return;
        }

        /*
         * Substitute error message for %m.  Be careful not to
         * molest an escaped percent "%%m".  We want to pass it
         * on untouched as the format is later parsed by vfprintf.
         */
        for ( ; (ch = *fmt); ++fmt) {
            if (ch == '%' && fmt[1] == 'm') {
                ++fmt;
                strerror_r(saved_errno, errstr, sizeof(errstr));
                fputs(errstr, fmt_fp);
            } else if (ch == '%' && fmt[1] == '%') {
                ++fmt;
                fputc(ch, fmt_fp);
                fputc(ch, fmt_fp);
            } else {
                fputc(ch, fmt_fp);
            }
        }

        /* Null terminate if room */
        fputc(0, fmt_fp);
        fclose(fmt_fp);

        /* Guarantee null termination */
        fmt_cpy[sizeof(fmt_cpy) - 1] = '\0';

        fmt = fmt_cpy;
    }

    (void) vfprintf(fp, fmt, ap);
    (void) fclose(fp);

    cnt = sizeof(tbuf) - tbuf_cookie.left;

    /* Remove a trailing newline */
    if (tbuf[cnt - 1] == '\n')
        cnt--;

    /* Output to stderr if requested. */
    if (xo_logstat & LOG_PERROR) {
        struct iovec iov[2];
        struct iovec *v = iov;
        char newline[] = "\n";

        v->iov_base = stdp;
        v->iov_len = cnt - (stdp - tbuf);
        ++v;
        v->iov_base = newline;
        v->iov_len = 1;
        (void) writev(STDERR_FILENO, iov, 2);
    }

    /* Get connected, output the message to the local logger. */
    if (!xo_opened)
        xo_openlog_unlocked(xo_logtag, xo_logstat | LOG_NDELAY, 0);
    xo_connectlog();

    /*
     * If the send() fails, there are two likely scenarios: 
     *  1) syslogd was restarted
     *  2) /var/run/log is out of socket buffer space, which
     *     in most cases means local DoS.
     * If the error does not indicate a full buffer, we address
     * case #1 by attempting to reconnect to /var/run/log[priv]
     * and resending the message once.
     *
     * If we are working with a privileged socket, the retry
     * attempts end there, because we don't want to freeze a
     * critical application like su(1) or sshd(8).
     *
     * Otherwise, we address case #2 by repeatedly retrying the
     * send() to give syslogd a chance to empty its socket buffer.
     */

    if (send(xo_logfile, tbuf, cnt, 0) < 0) {
        if (errno != ENOBUFS) {
            /*
             * Scenario 1: syslogd was restarted
             * reconnect and resend once
             */
            xo_disconnectlog();
            xo_connectlog();
            if (send(xo_logfile, tbuf, cnt, 0) >= 0) {
                THREAD_UNLOCK();
                return;
            }
            /*
             * if the resend failed, fall through to
             * possible scenario 2
             */
        }
        while (errno == ENOBUFS) {
            /*
             * Scenario 2: out of socket buffer space
             * possible DoS, fail fast on a privileged
             * socket
             */
            if (xo_status == CONNPRIV)
                break;
            usleep(1);
            if (send(xo_logfile, tbuf, cnt, 0) >= 0) {
                THREAD_UNLOCK();
                return;
            }
        }
    } else {
        THREAD_UNLOCK();
        return;
    }

    /*
     * Output the message to the console; try not to block
     * as a blocking console should not stop other processes.
     * Make sure the error reported is the one from the syslogd failure.
     */
    int flags = O_WRONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif /* O_CLOEXEC */

    if (xo_logstat & LOG_CONS
        && (fd = open(_PATH_CONSOLE, flags, 0)) >= 0) {
        struct iovec iov[2];
        struct iovec *v = iov;
        char crnl[] = "\r\n";

        p = strchr(tbuf, '>') + 1;
        v->iov_base = p;
        v->iov_len = cnt - (p - tbuf);
        ++v;
        v->iov_base = crnl;
        v->iov_len = 2;
        (void) writev(fd, iov, 2);
        (void) close(fd);
    }

    THREAD_UNLOCK();
}

/* Should be called with mutex acquired */
static void
xo_disconnectlog (void)
{
    /*
     * If the user closed the FD and opened another in the same slot,
     * that's their problem.  They should close it before calling on
     * system services.
     */
    if (xo_logfile != -1) {
        close(xo_logfile);
        xo_logfile = -1;
    }
    xo_status = NOCONN;            /* retry connect */
}

/* Should be called with mutex acquired */
static void
xo_connectlog (void)
{
    struct sockaddr_un SyslogAddr;    /* AF_UNIX address of local logger */

    if (xo_logfile == -1) {
        int flags = SOCK_DGRAM;
#ifdef SOCK_CLOEXEC
        flags |= SOCK_CLOEXEC;
#endif /* SOCK_CLOEXEC */
        if ((xo_logfile = socket(AF_UNIX, flags, 0)) == -1)
            return;
    }
    if (xo_logfile != -1 && xo_status == NOCONN) {
        SyslogAddr.sun_len = sizeof(SyslogAddr);
        SyslogAddr.sun_family = AF_UNIX;

        /*
         * First try privileged socket. If no success,
         * then try default socket.
         */

#ifdef _PATH_LOG_PRIV
        (void) strncpy(SyslogAddr.sun_path, _PATH_LOG_PRIV,
            sizeof SyslogAddr.sun_path);
        if (connect(xo_logfile, (struct sockaddr *) &SyslogAddr,
            sizeof(SyslogAddr)) != -1)
            xo_status = CONNPRIV;
#endif /* _PATH_LOG_PRIV */

#ifdef _PATH_LOG
        if (xo_status == NOCONN) {
            (void) strncpy(SyslogAddr.sun_path, _PATH_LOG,
                sizeof SyslogAddr.sun_path);
            if (connect(xo_logfile, (struct sockaddr *)&SyslogAddr,
                sizeof(SyslogAddr)) != -1)
                xo_status = CONNDEF;
        }
#endif /* _PATH_LOG */

#ifdef _PATH_OLDLOG
        if (xo_status == NOCONN) {
            /*
             * Try the old "/dev/log" path, for backward
             * compatibility.
             */
            (void) strncpy(SyslogAddr.sun_path, _PATH_OLDLOG,
                sizeof SyslogAddr.sun_path);
            if (connect(xo_logfile, (struct sockaddr *)&SyslogAddr,
                sizeof(SyslogAddr)) != -1)
                xo_status = CONNDEF;
        }
#endif /* _PATH_OLDLOG */

        if (xo_status == NOCONN) {
            (void) close(xo_logfile);
            xo_logfile = -1;
        }
    }
}

static void
xo_openlog_unlocked (const char *ident, int logstat, int logfac)
{
    if (ident != NULL)
        xo_logtag = ident;
    xo_logstat = logstat;
    if (logfac != 0 && (logfac &~ LOG_FACMASK) == 0)
        xo_logfacility = logfac;

    if (xo_logstat & LOG_NDELAY)    /* open immediately */
        xo_connectlog();

    xo_opened = 1;    /* ident and facility has been set */
}

void
xo_openlog (const char *ident, int logstat, int logfac)
{
    THREAD_LOCK();
    xo_openlog_unlocked(ident, logstat, logfac);
    THREAD_UNLOCK();
}


void
xo_closelog (void) 
{
    THREAD_LOCK();
    if (xo_logfile != -1) {
        (void) close(xo_logfile);
        xo_logfile = -1;
    }
    xo_logtag = NULL;
    xo_status = NOCONN;
    THREAD_UNLOCK();
}

/* setlogmask -- set the log mask level */
int
xo_setlogmask (int pmask)
{
    int omask;

    THREAD_LOCK();
    omask = xo_logmask;
    if (pmask != 0)
        xo_logmask = pmask;
    THREAD_UNLOCK();
    return (omask);
}
