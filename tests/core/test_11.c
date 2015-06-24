/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <syslog.h>

#include "xo.h"

void
test_syslog_open (void)
{
    printf("syslog open\n");
}

void
test_syslog_close (void)
{
    printf("syslog close\n");
}

void
test_syslog_send (const char *full_msg, const char *v0_hdr,
		  const char *text_only)
{
    printf("{{%s}}\n{{%s}}\n{{%s}}\n\n", full_msg, v0_hdr, text_only);
}

int
main (int argc, char **argv)
{
    int unit_test = 1;
    int fire = 0;
    const char *tzone = "EST";

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "full") == 0)
	    unit_test = 0;
	else if (strcmp(argv[argc], "fire") == 0)
	    fire = 1;
	else if (strcmp(argv[argc], "tz") == 0)
	    tzone = argv[++argc];
    }

    setenv("TZ", tzone, 1);
    tzset();

    if (!fire) {
	xo_set_syslog_handler(test_syslog_open, test_syslog_send,
			      test_syslog_close);
    }

    if (unit_test) {
	xo_set_unit_test_mode(1);
	xo_open_log("test-program", LOG_PERROR, 0);
    }

    xo_set_version("3.1.4");
    xo_set_syslog_enterprise_id(42); /* SunOs */

    xo_open_container_h(NULL, "top");

    xo_syslog(LOG_INFO | LOG_KERN, "animal-status",
	      "The {:animal} is {:state}", "snake", "loose");
    xo_syslog(LOG_INFO | LOG_MAIL, "animal-consumed",
	      "My {:animal} ate your {:pet}", "snake", "hamster");
    xo_syslog(LOG_NOTICE | LOG_DAEMON, "animal-talk",
	      "{:count/%d} {:animal} said {:quote}", 1, "owl", "\"e=m\\c[2]\"");

    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}
