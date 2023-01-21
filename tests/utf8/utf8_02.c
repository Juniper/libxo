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
    }

    char *data[] = {
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
	NULL
    };
    char buf[BUFSIZ];

    xo_open_container_h(NULL, "top");

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

    int rc;

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
	    xo_utf8_wchar_t wc = xo_utf8_codepoint(cp, len, ulen, 0);
	    
	    char isup = xo_utf8_isupper(cp) ? 'U' : '-';
	    char islw = xo_utf8_islower(cp) ? 'L' : '-';

	    xo_emit("{:item/%d}: wc={:data/%#x:%d} {:case/%c%c} {:len/%d:%d:%d}\n", i,
		    wc, wc, isup, islw, len, ulen, cp - buf);
	}

	xo_close_instance("item");
    }

    xo_close_container(NULL);

    xo_open_container("xo_utf8_ncasecmp");

    for (int i = 0; data[i]; i++) {
	xo_open_instance("item");
	xo_open_container("lower");
	strncpy(buf, data[i], sizeof(buf));

	xo_utf8_tolower(buf);
	rc = xo_utf8_ncasecmp(buf, data[i]);
	xo_emit("{:item/%d}: '{:data}' {:rc/%d}\n", i,
		buf, rc);
	xo_close_container("lower");
	xo_open_container("upper");
	strncpy(buf, data[i], sizeof(buf));

	xo_utf8_toupper(buf);
	rc = xo_utf8_ncasecmp(buf, data[i]);
	xo_emit("{:item/%d}: '{:data}' {:rc/%d}\n", i,
		buf, rc);
	xo_close_container("upper");
	xo_close_instance("item");
    }

    xo_close_container(NULL);

'Äaa', 'äaa'

    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}

