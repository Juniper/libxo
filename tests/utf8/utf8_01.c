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
    int len, rc, left = 0;
    wchar_t wc;
    unsigned long offset = 0;

    xo_open_container("top");

    for (;;) {
	rc = read(fd, buf + left, sizeof(buf) - left);
	if (rc <= 0)
	    break;

	for (cp = buf, ep = buf + rc + left; cp < ep; cp += len, left = 0) {
	    len = xo_utf8_len(*cp);
	    if (len <= 0)
		break;

	    left = ep - cp;
	    if (len > left) {
		xo_emit("{:offset/%lu}: {:message}\n",
			offset + cp - buf, "end of buffer");
		memcpy(buf, cp, left);
		break;
	    }

	    wc = xo_utf8_codepoint(cp, ep - cp, len, 0);
	    if (xo_utf8_wchar_is_err(wc)) {
		const char *msg = xo_utf8_wchar_errmsg(wc);
		xo_emit("{:offset/%lu}: {:error/%lc} {:message}\n",
			offset + cp - buf, (wchar_t) wc, msg);

		/* Now get a real one (meaning ' ') */
		wc = xo_utf8_codepoint(cp, ep - cp, len, ' ');
	    }

	    if (raw) {
		xo_emit("{:byte/%lc}", (wchar_t) wc);

	    } else if (print) {
		wchar_t real = wc;
		if (!iswprint(real))
		    real = ' ';

		xo_emit("[{:offset/%lu}] "
			"[{:hex/%#x}/{:hex-upper/%x}/{:hex-lower/%x}] "
			"[{:byte/%lc}] "
			"[{:upper/%lc}] [{:lower/%lc}]\n",
			offset + cp - buf,
			wc, xo_utf8_wtoupper(real), xo_utf8_wtolower(real),
			real, xo_utf8_wtoupper(real), xo_utf8_wtolower(real));
	    }
	}

	offset += rc - left;
    }

    xo_finish();

    return 0;
}

