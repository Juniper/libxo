/*
 * Copyright (c) 2022, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, October 2022
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <wctype.h>

#include "xo.h"
#include "xo_encoder.h"
#include "xo_utf8.h"

int
main (int argc, char **argv)
{
    int print = 1;
    int raw = 0;
    char *file = NULL;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (argc = 1; argv[argc]; argc++) {
	if (xo_streq(argv[argc], "print"))
	    print = 1;
	else if (xo_streq(argv[argc], "null"))
	    print = 0;
	else if (xo_streq(argv[argc], "raw"))
	    raw = 1;
	else if (xo_streq(argv[argc], "file")) {
	    if (argv[argc + 1])
		file = argv[++argc];
	}
    }

    int fd = 0;

    if (file) {
	fd = open(file, O_RDONLY);
	if (fd < 0)
	    xo_err(1, "could not open file: '%s'", file);
    }

    char buf[BUFSIZ], *cp, *ep;
    int len, rc;
    xo_utf8_wchar_t wc;
    unsigned long offset = 0;

    xo_open_container("top");

    for (;;) {
	rc = read(fd, buf, sizeof(buf));
	if (rc <= 0)
	    break;

	for (cp = buf, ep = buf + rc; cp < ep; cp += len) {
	    len = xo_utf8_len(*cp);
	    if (len <= 0)
		break;

	    wc = xo_utf8_codepoint(cp, ep - cp, len, ' ');
	    if (wc < 0)
		break;

	    if (raw) {
		xo_emit("{:byte/%lc}", (wchar_t) wc);

	    } else if (print) {
		wchar_t real = wc;
		if (!iswprint(real))
		    real = ' ';

		xo_emit("[{:offset/%lu}] [{:hex/%#x}] [{:byte/%lc}] "
			"[{:upper/%lc}] [{:lower/%lc}]\n",
			offset + cp - buf, wc, real,
			xo_utf8_wtoupper(real), xo_utf8_wtolower(real));
	    }
	}

	offset += rc;
    }

    xo_finish();

    return 0;
}

