/*
  - libxo/xo_syslog.c:455-466 – syslog writer computes left - 1 and assigns it back to len even when left is 0. That sets len to -1, which is
    then passed to memcpy, causing a huge copy and buffer overflow when the 2 KB syslog buffer fills. Repro: build a message longer than ~2048
    bytes, e.g. char buf[3000]; memset(buf,'A',sizeof(buf)-1); buf[2999]=0; xo_syslog(LOG_INFO,"demo","%s",buf); → overflow/crash during syslog
    formatting.
*/

/*
 * Build (adjust -L to your libxo build): cc -I../src/contrib/libxo/libxo -L../build/libxo -lxo -fsanitize=address -o repro_syslog_overflow repro_syslog_overflow.c
 * Run: ./repro_syslog_overflow
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <syslog.h>

#include <xo.h>
#include "xo_encoder.h"

static void
stub_send (const char *full_msg, const char *v0_hdr, const char *text_only)
{
    (void) full_msg;
    (void) v0_hdr;
    fprintf(stderr, "captured length=%zu\n", text_only ? strlen(text_only) : 0);
}

static int
test_docs (void)
{
    int code = 42;
    const char *filename = "bloopers.mov";
    const char *protocol = "https";
    const char *remote = "/tmp/";
    unsigned state = 5;
    unsigned conn = 15;
    const char *user = "phil";
    const char *addr = "127.0.0.1";

    xo_syslog(LOG_ERR, "upload-failed",
              "error <{:code/%d}> uploading file '{:filename}' "
              "as '{:target/%s:%s}'",
              code, filename, protocol, remote);

    xo_syslog(LOG_INFO, "poofd-invalid-state",
              "state {:current/%u} is invalid {:connection/%u}",
              state, conn);

    xo_syslog(LOG_ERR | LOG_AUTH, "login-failed",
	      "Login failed; user '{:user}' from host '{:address}'",
	      user, addr);

    errno = ENOMEM;
    xo_syslog(LOG_ERR, "poofd-missing-file",
              "'{:filename}' not found: {:error/%m}", filename);

    return 0;
}

static int
test_fail (void)
{
    xo_syslog(LOG_ERR, "no-content", "%s", "message");
    return 0;
}

static int
test_big_message (void)
{
    static char msg[128];
    memset(msg, 'A', sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';

    /* Very long msgid to force the header builder to fill the 2 KB buffer */
    size_t nlen = 4096;
    char *name = malloc(nlen + 1);
    memset(name, 'N', nlen);
    name[nlen] = '\0';

    xo_set_unit_test_mode(1);        /* avoid touching real syslog */
    xo_set_syslog_handler(NULL, stub_send, NULL);

    /*
     * The oversize name makes the header construction (via xo_snprintf)
     * fill the 2048-byte stack buffer before the writer runs. When the
     * writer is called, left==0 and the len=-1 path triggers memcpy
     * with a huge length, overflowing the buffer (ASan/valgrind will fault).
     */
    xo_syslog(LOG_INFO, name, "{:message}", msg);

    free(name);
    return 0;
}

static void
test_syslog_open (void)
{
    printf("syslog open\n");
}

static void
test_syslog_close (void)
{
    printf("syslog close\n");
}

static void
test_syslog_send (const char *full_msg, const char *v0_hdr,
		  const char *text_only)
{
    printf("{{%s}}\n{{%s}}\n{{%s}}\n\n", full_msg, v0_hdr, text_only);
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    int unit_test = 1;
    int fire = 0;
    const char *tzone = "EST";

    for (argc = 1; argv[argc]; argc++) {
	if (xo_streq(argv[argc], "full"))
	    unit_test = 0;
	else if (xo_streq(argv[argc], "fire"))
	    fire = 1;
	else if (xo_streq(argv[argc], "tz"))
	    tzone = argv[++argc];
    }

    setenv("TZ", tzone, 1);
    tzset();

    if (!fire)
	xo_set_syslog_handler(test_syslog_open, test_syslog_send,
			      test_syslog_close);

    xo_open_container("top");

    if (unit_test) {
	xo_set_unit_test_mode(1);
	xo_open_log("test-program", LOG_PERROR, 0);
    }

    xo_set_version("3.1.4");
    xo_set_syslog_enterprise_id(42); /* SunOs */

    test_fail();
    test_docs();

    test_big_message();
    xo_set_syslog_bufsiz(10 * 1024);
    test_big_message();

    xo_close_container("top");
    xo_finish();

    return 0;
}

