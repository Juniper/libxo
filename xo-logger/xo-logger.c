/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * xo-logger: a command-line-compatible replacement for the FreeBSD
 * "logger(1)" utility, built on libxo.  The message argument (or each
 * line read via -f/--filename or stdin) supports the same xo_emit
 * format-string/field-descriptor syntax that "xo --logger" used to
 * provide; that is the one deliberate extension beyond stock logger.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "xo_config.h"
#include "xo.h"

#include <getopt.h>		/* Include after xo.h for testing */

#include "xo_cli_helpers.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

static int xo_need_argv;	/* Set when the message consumes argv[] */

/*
 * chomp() trims a trailing newline (and/or carriage return) from a
 * line read via -f/--filename or stdin.  Only xo-logger needs this;
 * it does not belong in the shared cli-helpers header.
 */
static void
chomp (char *buf)
{
    int len = strlen(buf);
    char *ep = buf + len;
    for (; ep > buf && (ep[-1] == '\n' || ep[-1] == '\r'); ep--)
	continue;
    *ep = '\0';
}

static void
xo_logger_setup (xo_handle_t *xop, unsigned op)
{
    if (op == XSUP_INIT) {
	if (xo_need_argv) {
	    checkpoint_argv = save_argv;
	    xo_set_formatter(xop, formatter, checkpoint);
	}
    } else if (op == XSUP_REINIT) {
	save_argv = checkpoint_argv;
    }
}

typedef struct xo_nmap_s {
    const char *xn_name;		/* Name */
    int xn_value;		/* Facility/severity */
} xo_nmap_t;

static xo_nmap_t xo_map_severity[] = {
    { "emerg", LOG_EMERG },
    { "emergency", LOG_EMERG },
    { "alert", LOG_ALERT },
    { "crit", LOG_CRIT },
    { "critical", LOG_CRIT },
    { "err", LOG_ERR },
    { "error", LOG_ERR },
    { "warning", LOG_WARNING },
    { "notice", LOG_NOTICE },
    { "info", LOG_INFO },
    { "information", LOG_INFO },
    { "debug", LOG_DEBUG },
    { NULL, 0 }
};

static xo_nmap_t xo_map_facility[] = {
    { "auth", LOG_AUTH },
    { "authpriv", LOG_AUTHPRIV },
#ifdef LOG_CONSOLE
    { "console", LOG_CONSOLE },
#endif /* LOG_CONSOLE */
    { "cron", LOG_CRON },
    { "daemon", LOG_DAEMON },
    { "ftp", LOG_FTP },
    /* { "kern", LOG_KERN }, -- We shouldn't be emitting kernel messages */
    { "lpr", LOG_LPR },
    { "mail", LOG_MAIL },
    { "news", LOG_NEWS },
#ifdef LOG_NTP
    { "ntp", LOG_NTP },
#endif /* LOG_NTP */
#ifdef LOG_SECURITY
    { "security", LOG_SECURITY },
#endif /* LOG_SECURITY */
    { "syslog", LOG_SYSLOG },
    { "user", LOG_USER },
    { "uucp", LOG_UUCP },
    { "local0", LOG_LOCAL0 },
    { "local1", LOG_LOCAL1 },
    { "local2", LOG_LOCAL2 },
    { "local3", LOG_LOCAL3 },
    { "local4", LOG_LOCAL4 },
    { "local5", LOG_LOCAL5 },
    { "local6", LOG_LOCAL6 },
    { "local7", LOG_LOCAL7 },
    { NULL, 0 }
};

static int
xo_find_map (xo_nmap_t *map, const char *name, const char *error)
{
    const char *cp = name;

    if (strncmp(cp, "LOG_", 4) == 0)
	cp += 4;

    for (; map->xn_name; map++)
	if (strcasecmp(map->xn_name, cp) == 0)
	    return map->xn_value;

    if (error)
	xo_errx(1, "unknown %s: '%s'", error, name);

    return 0;
}

static void
xo_list_map (xo_nmap_t *map, const char *tag)
{
    char buf[1024];

    snprintf(buf, sizeof(buf), "%s-information", tag);
    xo_open_container(buf);

    for (; map->xn_name; map++)
	xo_emit("{la:}\n", tag, map->xn_name);

    xo_close_container(buf);

    xo_finish();
    exit(0);
}

/*
 * Parse the argument to -p/--priority: either a bare number, or a
 * "facility.level" pair (either half may be omitted, defaulting to
 * "user" and "notice" respectively), matching logger(1).
 */
static int
xo_logger_priority (const char *str)
{
    char *ep;
    long val = strtol(str, &ep, 0);
    if (*str != '\0' && *ep == '\0')
	return (int) val;

    int len = strlen(str);
    char buf[len + 1];
    memcpy(buf, str, len + 1);

    int fac = LOG_USER, sev = LOG_NOTICE;
    char *cp = strchr(buf, '.');

    if (cp) {
	*cp++ = '\0';
	if (*buf)
	    fac = xo_find_map(xo_map_facility, buf, "facility");
	if (*cp)
	    sev = xo_find_map(xo_map_severity, cp, "severity");
    } else {
	sev = xo_find_map(xo_map_severity, buf, "severity");
    }

    return fac | sev;
}

/*
 * Parse the argument to -P/--port: either a bare number, or a service
 * name resolved via getservbyname(3).  An unknown service name falls
 * back to the standard syslog port (514) with a warning, rather than
 * failing outright.
 */
static int
xo_logger_resolve_port (const char *arg)
{
    if (arg == NULL)
	return 514;

    char *ep;
    long val = strtol(arg, &ep, 10);
    if (*arg != '\0' && *ep == '\0')
	return (int) val;

    struct servent *sp = getservbyname(arg, "udp");
    if (sp == NULL) {
	fprintf(stderr,
		"xo-logger: warning: unknown service '%s', "
		"using default syslog port 514\n", arg);
	return 514;
    }

    return ntohs(sp->s_port);
}

/*
 * Resolve the argument to -h/--host and hand the result to the
 * library.  A leading '/' indicates an AF_LOCAL socket path (e.g.
 * "/var/run/log") rather than a network hostname.  Address-family
 * selection comes from -4/--inet4 or -6/--inet6; absent those, IPv4
 * is tried first, falling back to IPv6.
 */
static void
xo_logger_resolve_host (const char *host, int family)
{
    if (host[0] == '/') {
	xo_log_set_host_path(host);
	return;
    }

    struct hostent *hp = NULL;

    if (family != AF_UNSPEC) {
	hp = gethostbyname2(host, family);
    } else {
	hp = gethostbyname2(host, AF_INET);
	if (hp == NULL)
	    hp = gethostbyname2(host, AF_INET6);
    }

    if (hp == NULL)
	xo_errx(1, "unknown host: '%s'", host);

    xo_log_set_host(hp);
}

/*
 * Parse the argument to -S/--source, an "addr:port" pair (or
 * "[addr]:port" for IPv6), and hand the resulting socket address to
 * the library to bind() before sending.
 */
static void
xo_logger_set_source (const char *arg)
{
    char buf[256];
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *host = buf;
    char *portstr = NULL;

    if (buf[0] == '[') {
	char *rb = strchr(buf, ']');
	if (rb == NULL)
	    xo_errx(1, "invalid source address: '%s'", arg);
	host = buf + 1;
	*rb = '\0';
	if (rb[1] == ':')
	    portstr = rb + 2;
    } else {
	char *colon = strrchr(buf, ':');
	if (colon) {
	    *colon = '\0';
	    portstr = colon + 1;
	}
    }

    int port = portstr ? xo_logger_resolve_port(portstr) : 0;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);

    int rc = getaddrinfo(host[0] ? host : NULL, portbuf, &hints, &res);
    if (rc != 0)
	xo_errx(1, "invalid source address: '%s'", arg);

    xo_log_set_source(res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
}

/*
 * Support for "make check": rather than actually opening a socket to
 * syslogd, capture the fully formatted RFC 5424 message and print it
 * to stdout, where it can be compared against a saved baseline.
 * Combined with xo_set_unit_test_mode(), which fakes the timestamp,
 * hostname, and process id, this makes output deterministic.  Enabled
 * by the undocumented --test-mode option; not for general use.
 */
static void
xo_logger_test_open (void)
{
}

static void
xo_logger_test_send (const char *full_msg, const char *v0_hdr,
		      const char *text_only)
{
    /* Replicate the default transport's -s/--print echo to stderr */
    if (v0_hdr)
	fprintf(stderr, "%s%s\n", v0_hdr, text_only);

    printf("%s\n", full_msg);
}

static void
xo_logger_test_close (void)
{
}

static const char *
xo_logger_default_tag (void)
{
    const char *login = getlogin();
    if (login != NULL)
	return login;

    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL)
	return pw->pw_name;

    return "-";
}

static void
print_version (void)
{
    fprintf(stderr, "libxo version %s%s\n",
	    xo_version, xo_version_extra);
    fprintf(stderr, "xo-logger version %s%s\n",
	    LIBXO_VERSION, LIBXO_VERSION_EXTRA);
}

static void
print_help (const char *message)
{
    if (message)
	fprintf(stderr, "xo-logger: invalid arguments: %s\n\n", message);

    fprintf(stderr,
"Usage: xo-logger [options] [message [fields]]\n"
"    -4, --inet4            Resolve --host as IPv4 only\n"
"    -6, --inet6            Resolve --host as IPv6 only\n"
"    -A, --all-addresses    Send to every address --host resolves to\n"
"    -f, --filename <file>  Read message body from file, one line each\n"
"    -H, --hostname <name>  Override the HOSTNAME field\n"
"    -h, --host <host>      Send remotely instead of the local syslog socket\n"
"    -i                     Accepted for compatibility; no effect\n"
"    -P, --port <port>      Destination port (name or number, default 514)\n"
"    -p, --priority <pri>   Facility.level or numeric priority "
				"(default user.notice)\n"
"    -s, --print            Also print the message to stderr\n"
"    -S, --source <addr:port> Local source address/port to bind to\n"
"    -t, --tag <tag>        Tag recorded with the message "
				"(default: login name)\n"
"    --list-facilities      List all valid logging facilities\n"
"    --list-severities      List all valid logging severities\n"
"    --help                 Display this help text\n"
"    --version              Display version information\n"
"\n"
"The message argument (if given) may use xo_emit format-string field\n"
"descriptors, consuming the remaining arguments as field values.  If no\n"
"message argument is given, lines are read from --filename or, lacking\n"
"that, from standard input, one syslog message per line.\n");
}

enum {
    OPT_LIST_FACILITIES = 256,
    OPT_LIST_SEVERITIES,
    OPT_HELP,
    OPT_VERSION,
    OPT_TEST_MODE,
};

static struct option long_opts[] = {
    { "inet4", no_argument, NULL, '4' },
    { "inet6", no_argument, NULL, '6' },
    { "all-addresses", no_argument, NULL, 'A' },
    { "filename", required_argument, NULL, 'f' },
    { "hostname", required_argument, NULL, 'H' },
    { "host", required_argument, NULL, 'h' },
    { "port", required_argument, NULL, 'P' },
    { "priority", required_argument, NULL, 'p' },
    { "print", no_argument, NULL, 's' },
    { "source", required_argument, NULL, 'S' },
    { "tag", required_argument, NULL, 't' },
    { "list-facilities", no_argument, NULL, OPT_LIST_FACILITIES },
    { "list-severities", no_argument, NULL, OPT_LIST_SEVERITIES },
    { "help", no_argument, NULL, OPT_HELP },
    { "version", no_argument, NULL, OPT_VERSION },
    { "test-mode", no_argument, NULL, OPT_TEST_MODE },
    { NULL, 0, NULL, 0 }
};

int
main (int argc, char **argv)
{
    int opt_inet4 = 0, opt_inet6 = 0, opt_all_addresses = 0, opt_print = 0;
    const char *opt_filename = NULL, *opt_hostname = NULL, *opt_host = NULL;
    const char *opt_port_arg = NULL, *opt_priority_arg = NULL;
    const char *opt_source_arg = NULL, *opt_tag = NULL;
    int rc;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    while ((rc = getopt_long(argc, argv, "46Af:H:h:iP:p:sS:t:",
			      long_opts, NULL)) != -1) {
	switch (rc) {
	case '4':
	    opt_inet4 = 1;
	    break;

	case '6':
	    opt_inet6 = 1;
	    break;

	case 'A':
	    opt_all_addresses = 1;
	    break;

	case 'f':
	    opt_filename = get_arg(optarg, "filename for log input");
	    break;

	case 'H':
	    opt_hostname = get_arg(optarg, "hostname");
	    break;

	case 'h':
	    opt_host = get_arg(optarg, "host");
	    break;

	case 'i':
	    /* Accepted for compatibility; RFC 5424 always includes PROCID */
	    break;

	case 'P':
	    opt_port_arg = get_arg(optarg, "port");
	    break;

	case 'p':
	    opt_priority_arg = get_arg(optarg, "priority");
	    break;

	case 's':
	    opt_print = 1;
	    break;

	case 'S':
	    opt_source_arg = get_arg(optarg, "source address");
	    break;

	case 't':
	    opt_tag = get_arg(optarg, "tag");
	    break;

	case OPT_LIST_FACILITIES:
	    xo_list_map(xo_map_facility, "facility");
	    break;

	case OPT_LIST_SEVERITIES:
	    xo_list_map(xo_map_severity, "severity");
	    break;

	case OPT_HELP:
	    print_help(NULL);
	    return 0;

	case OPT_VERSION:
	    print_version();
	    return 0;

	case OPT_TEST_MODE:
	    xo_set_unit_test_mode(1);
	    xo_set_syslog_handler(xo_logger_test_open, xo_logger_test_send,
				  xo_logger_test_close);
	    break;

	case ':':
	    xo_errx(1, "missing argument");
	    break;

	default:
	    print_help(NULL);
	    return 1;
	}
    }

    if (opt_inet4 && opt_inet6)
	xo_errx(1, "only one of -4/--inet4 or -6/--inet6 may be given");

    argc -= optind;
    argv += optind;

    int priority = opt_priority_arg
	? xo_logger_priority(opt_priority_arg)
	: (LOG_USER | LOG_NOTICE);

    int logopts = opt_print ? LOG_PERROR : 0;
    const char *tag = opt_tag ? opt_tag : xo_logger_default_tag();

    xo_open_log(tag, logopts, priority);

    if (opt_all_addresses)
	xo_log_set_all_addresses(1);

    if (opt_hostname)
	xo_log_set_hostname(opt_hostname);

    if (opt_source_arg)
	xo_logger_set_source(opt_source_arg);

    if (opt_host) {
	int family = opt_inet6 ? AF_INET6 : opt_inet4 ? AF_INET : AF_UNSPEC;
	xo_logger_resolve_host(opt_host, family);
	xo_log_set_port(xo_logger_resolve_port(opt_port_arg));
    }

    xo_syslog_set_setup(xo_logger_setup);

    if (argc > 0) {
	char *fmt = argv[0];

	xo_need_argv = 1;
	checkpoint_argv = save_argv = argv + 1;
	prep_arg(fmt);
	xo_syslog(priority, NULL, fmt);

    } else {
	FILE *fp = stdin;

	if (opt_filename) {
	    fp = fopen(opt_filename, "r");
	    if (fp == NULL)
		xo_err(1, "could not open input file: '%s'", opt_filename);
	}

	char buf[BUFSIZ];
	while (fgets(buf, sizeof(buf), fp) != NULL) {
	    chomp(buf);
	    xo_syslog(priority, NULL, "{F:/%s}", buf);
	}

	if (opt_filename)
	    fclose(fp);
    }

    xo_close_log();

    return 0;
}
