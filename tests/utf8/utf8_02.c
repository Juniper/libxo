/*
 * Copyright (c) 2022, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, November 2022
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "xo.h"
#include "xo_encoder.h"
#include "xo_utf8.h"

int
main (int argc, char **argv)
{
    int dump_lower = 0, dump_upper = 0;
    char name[] = "str_01.test";  /* test trimming of xo_program */
    argv[0] = name;
    
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (argc = 1; argv[argc]; argc++) {
	if (xo_streq(argv[argc], "xml"))
	    xo_set_style(NULL, XO_STYLE_XML);
	else if (xo_streq(argv[argc], "json"))
	    xo_set_style(NULL, XO_STYLE_JSON);
	else if (xo_streq(argv[argc], "text"))
	    xo_set_style(NULL, XO_STYLE_TEXT);
	else if (xo_streq(argv[argc], "html"))
	    xo_set_style(NULL, XO_STYLE_HTML);
	else if (xo_streq(argv[argc], "pretty"))
	    xo_set_flags(NULL, XOF_PRETTY);
	else if (xo_streq(argv[argc], "lower"))
	    dump_lower = 1;
	else if (xo_streq(argv[argc], "upper"))
	    dump_upper = 1;
    }

    const char *data[] = {
	"\x40\x41\x42",
	"\x81\x82\x83",
	"xx\x81\x82\x83",
	"0123456789",
	"ახლა",
	"გაიარო",
	"საერთაშორისო",
	"საერთაშორისო\xc0",
	"საერთაშორისო\x9d",
	"საერთაშორისო\x9dო",
	"෴ණ්ණ෴෴ණ්ණ෴",
	"෴ණ්ණ෴෴ණ්ණ\xc0\x0f෴෴ණ්ණ෴෴෴",
	"Reverse Retro | oɿɟɘЯ ɘƨɿɘvɘЯ",
	"ði ıntəˈnæʃənəl fəˈnɛtık əsoʊsiˈeıʃn",
	"Äaa",
	NULL
    };
    char buf[BUFSIZ];

    xo_open_container_h(NULL, "top");

    if (dump_lower) {
	wchar_t wc, lc;
	for (wc = 0x0041; wc <= 0xff3a; wc += 1) {
	    lc = xo_utf8_wtolower(wc);
	    if (lc != wc)
		printf("%04X %04X: %#06x - %#06x = %#06x ('%lc'->'%lc')\n",
		       lc, wc, lc, wc, lc - wc, wc, lc);
	}
	xo_finish();
	return 0;
    }

    if (dump_upper) {
	wchar_t wc, uc;
	for (wc = 0x0061; wc <= 0xff5a; wc += 1) {
	    uc = xo_utf8_wtoupper(wc);
	    if (uc != wc)
		printf("%04X %04X: %#06x - %#06x = %#06x ('%lc'->'%lc')\n",
		       wc, uc, wc, uc, wc - uc, wc, uc);
	}
	xo_finish();
	return 0;
    }

    xo_open_container("xo_utf8_valid");

    for (int i = 0; data[i]; i++) {
	xo_open_instance("item");
	strncpy(buf, data[i], sizeof(buf));

	char *cp = xo_utf8_valid(buf);
	xo_emit("{:item/%d}: '{:data}' {:test} {:offset/%d}\n", i,
		buf,
		cp ? "F" : "T",
		cp ? cp - buf : 0);
	xo_close_instance("item");
    }

    xo_close_container(NULL);

    int rc, rc1, rc2;

    xo_open_container("xo_utf8_makevalid");

    for (int i = 0; data[i]; i++) {
	xo_open_instance("item");
	xo_open_container("space");
	strncpy(buf, data[i], sizeof(buf));

	rc = xo_utf8_makevalid(buf, ' ');
	xo_emit("{:item/%d}: '{:data}' {:errors/%d}\n", i,
		buf, rc);
	xo_close_container("space");

	xo_open_container("nul");
	strncpy(buf, data[i], sizeof(buf));

	rc = xo_utf8_makevalid(buf, '\0');
	xo_emit("{:item/%d}: '{:data}' {:errors/%d}\n", i,
		buf, rc);
	xo_close_container("nul");
	xo_close_instance("item");
    }

    xo_close_container(NULL);
    xo_open_container("upper_lower");

    for (int i = 0; data[i]; i++) {
	xo_open_instance("item");
	strncpy(buf, data[i], sizeof(buf));

	size_t len = strlen(buf);
	size_t ulen = 0;

	for (char *cp = buf; cp; cp = xo_utf8_nnext(cp, len)) {
	    len -= ulen;	/* Substract last time's length */
	    if (len <= 0)
		break;

	    ulen = xo_utf8_len(*cp);
	    wchar_t wc = xo_utf8_codepoint(cp, len, ulen, 0);
	    
	    char isup = xo_utf8_isupper(cp) ? 'U' : '-';
	    char islw = xo_utf8_islower(cp) ? 'L' : '-';

	    xo_emit("{:item/%d}: wc={:data/%#x:%d} {:case/%c%c} {:len/%d:%d:%d}\n", i,
		    wc, wc, isup, islw, len, ulen, cp - buf);
	}

	xo_close_instance("item");
    }

    xo_close_container(NULL);

    xo_open_container("xo_ustrcasecmp");

    for (int i = 0; data[i]; i++) {
	xo_open_instance("item");
	strncpy(buf, data[i], sizeof(buf));
	xo_emit("{:base}:\n", buf);

	xo_open_container("lower");
	xo_utf8_tolower(buf);
	rc1 = xo_ustrcasecmp(buf, data[i]);
	rc2 = xo_ustrcasecmp(data[i], buf);
	xo_emit("  {:item/%d}: '{:data}' {:rc1/%d}/{:rc2/%d}\n", i,
		buf, rc1, rc2);
	xo_close_container("lower");

	xo_open_container("upper");
	xo_utf8_toupper(buf);
	rc1 = xo_ustrcasecmp(buf, data[i]);
	rc2 = xo_ustrcasecmp(data[i], buf);
	xo_emit("  {:item/%d}: '{:data}' {:rc1/%d}/{:rc2/%d}\n", i,
		buf, rc1, rc2);
	xo_close_container("upper");
	xo_close_instance("item");
    }

    xo_close_container(NULL);

    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}

