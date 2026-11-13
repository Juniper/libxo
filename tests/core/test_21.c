/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Exercises the fixes for the skill's pitfalls:
 *
 *   grouping: "%'" and {i:} on integers and floats, with widths, under
 *       "grouping=,+3" and ".+3+2", then the "group" option
 *   stars: '*' widths with no encoding format, and encoding formats
 *       whose '*' count differs from the display format's (repaired)
 *   precision: an integer's precision (leading zeroes) dropped from
 *       the default encoding format, but kept for floats and strings
 *   humanize: {h:} with a width, left and right, narrower than the
 *       value, and with no width
 *   names: invalid field and container names (a space, '%', ':', a
 *       leading digit or '.', and an empty name), reported under "warn"
 *   bytes: xo_emit's return value
 *
 * Each star case is followed by a "{:NAME-next/%d}" sentinel; if a
 * field pops the wrong number of arguments, the sentinels go wrong.
 *
 * main returns with the top container still open and without calling
 * xo_finish, so the output is completed by the xo_finish_atexit that
 * xo_parse_args registers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "xo.h"

static int sentinel;

static void
grouping (const char *name, const char *options)
{
    xo_set_options(NULL, options);

    xo_open_instance("grouping");
    xo_emit("{k:name} {:options}\n", name, options);
    xo_emit("[{:quote/%'d}] [{:quote-long/%'ld}] [{:quote-unsigned/%'u}] "
	    "[{:quote-intmax/%'jd}]\n",
	    1234567, 1234567890L, 4000000000U, (intmax_t) 9876543210LL);
    xo_emit("[{i:int/%d}] [{i:int-long/%ld}] [{i:int-unsigned/%u}] "
	    "[{i:int-small/%d}] [{i:int-negative/%d}]\n",
	    1234567, 1234567890L, 4000000000U, 123, -1234567);
    xo_emit("[{:quote-width/%'12d}] [{:quote-left/%-'12d}] "
	    "[{i:int-width/%12d}] [{i:int-left/%-12d}] "
	    "[{i:int-narrow/%4d}]\n",
	    1234567, 1234567, 1234567, 1234567, 1234567);
    xo_emit("[{:quote-float/%'.2f}] [{i:int-float/%.2f}] "
	    "[{i:int-float-width/%14.1f}] [{i:int-exp/%e}] "
	    "[{i:int-general/%g}]\n",
	    1234567.5, 1234567.5, 1234567.5, 1234567.5, 1234567.0);
    xo_emit("[{i:int-hex/%x}] [{i:int-octal/%o}] [{i:int-zero/%08d}] "
	    "[{:quote-zero/%'010d}]\n",
	    1234567, 1234567, 1234567, 1234567);
    xo_emit("[{:quote-encoded/%'d/%d}] [{:plain/%d}] [{:plain-float/%.1f}]\n",
	    1234567, 1234567, 1234567.5);
    xo_close_instance("grouping");
}

static void
stars (void)
{
    xo_open_container("stars");

    /* '*' widths with no encoding format */
    xo_emit("[{:right/%*d}] ({:right-next/%d})\n", 8, 1234, ++sentinel);
    xo_emit("[{:left/%-*d}] ({:left-next/%d})\n", 8, 1234, ++sentinel);
    xo_emit("[{:intmax/%*jd}] ({:intmax-next/%d})\n",
	    8, (intmax_t) 1234, ++sentinel);
    xo_emit("[{:string/%-*.*s}] ({:string-next/%d})\n",
	    8, 3, "abcdef", ++sentinel);
    xo_emit("[{:string-right/%*.*s}] ({:string-right-next/%d})\n",
	    8, 3, "abcdef", ++sentinel);

    /* "%@*@" encoding formats; correct already */
    xo_emit("[{:at-int/%*d/%@*@d}] ({:at-int-next/%d})\n",
	    8, 1234, ++sentinel);
    xo_emit("[{:at-intmax/%*jd/%@*@jd}] ({:at-intmax-next/%d})\n",
	    8, (intmax_t) 1234, ++sentinel);
    xo_emit("[{:at-string/%*.*s/%@**@s}] ({:at-string-next/%d})\n",
	    8, 3, "abcdef", ++sentinel);
    xo_emit("[{:at-float/%*.2f/%@*@.2f}] ({:at-float-next/%d})\n",
	    8, 3.14159, ++sentinel);

    /* The encoding format has fewer '*'s: the extras are popped */
    xo_emit("[{:fewer/%*jd/%jd}] ({:fewer-next/%d})\n",
	    8, (intmax_t) 1234, ++sentinel);
    xo_emit("[{:fewer-string/%*.*s/%s}] ({:fewer-string-next/%d})\n",
	    8, 3, "abcdef", ++sentinel);

    /* The encoding format has more '*'s: they're ignored */
    xo_emit("[{:more/%jd/%*jd}] ({:more-next/%d})\n",
	    (intmax_t) 1234, ++sentinel);

    xo_close_container("stars");
}

static void
precision (void)
{
    xo_open_container("precision");

    /* An integer's precision is dropped from the default encoding */
    xo_emit("[{:digits/%.10d}] [{:zero-width/%08.5d}] "
	    "[{:left/%-12.6ld}] [{:hex/%.6x}] [{:octal/%.4o}]\n",
	    42, 42, 42L, 255, 8);
    xo_emit("[{:star/%.*d}] ({:star-next/%d})\n", 6, 42, ++sentinel);
    xo_emit("[{:star-both/%*.*jd}] ({:star-both-next/%d})\n",
	    9, 6, (intmax_t) 42, ++sentinel);
    xo_emit("[{:at-star/%@*@.5d}] ({:at-star-next/%d})\n",
	    9, 42, ++sentinel);

    /* Kept: floats, strings, a second conversion, an explicit encoding */
    xo_emit("[{:float/%.2f}] [{:string/%.3s}] [{:second/%lu.%06lu}] "
	    "[{:explicit/%d/%.6x}]\n",
	    3.14159, "abcdef", 5UL, 42UL, 42);

    xo_close_container("precision");
}

static void
humanize (void)
{
    xo_open_container("humanize");

    xo_emit("[{h:right/%7ju}] [{h:left/%-7ju}] [{h:narrow/%3ju}] "
	    "[{h:none/%ju}]\n",
	    (uintmax_t) 123456789, (uintmax_t) 123456789,
	    (uintmax_t) 123456789, (uintmax_t) 123456789);
    xo_emit("[{h,hn-space:space/%8ju}] [{h,hn-decimal:decimal/%-8ju}] "
	    "[{h,hn-1000:metric/%8ju}] [{h:small/%5ju}]\n",
	    (uintmax_t) 123456789, (uintmax_t) 4600000,
	    (uintmax_t) 123456789, (uintmax_t) 12);

    xo_close_container("humanize");
}

static void
names (void)
{
    xo_ssize_t rc;

    xo_set_flags(NULL, XOF_WARN);

    xo_open_container("names");
    xo_emit("{:bad name/%d}\n", 1);
    xo_emit("{:bad%name/%d}\n", 2);
    xo_emit("{:state:LISTEN/%d}\n", 3);
    xo_emit("{:2fast/%d}\n", 4);
    xo_emit("{:.dot/%d}\n", 5);
    xo_emit("{:good-name.v2_x/%d}\n", 6);
    xo_emit("{:/%d}\n", 8);

    rc = xo_open_container("bad container");
    if (rc >= 0)
	xo_close_container("bad container");
    rc = xo_open_list("bad:list");
    if (rc >= 0) {
	xo_open_instance("bad:list");
	xo_emit("{:value/%d}\n", 7);
	xo_close_instance("bad:list");
	xo_close_list("bad:list");
    }
    xo_close_container("names");

    xo_clear_flags(NULL, XOF_WARN);
}

static void
bytes (void)
{
    xo_ssize_t rc[4];

    xo_open_container("bytes");
    rc[0] = xo_emit("{:one/%s}\n", "abc");
    rc[1] = xo_emit("{L:Label} {:two/%d} {N:note}\n", 42);
    rc[2] = xo_emit("text only\n");
    rc[3] = xo_emit("{:utf8/%s}\n", "caf\xc3\xa9");
    xo_emit("{:one-bytes/%zd} {:two-bytes/%zd} {:text-bytes/%zd} "
	    "{:utf8-bytes/%zd}\n",
	    rc[0], rc[1], rc[2], rc[3]);
    xo_close_container("bytes");
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("top");

    xo_open_list("grouping");
    grouping("comma", "grouping=+3");
    grouping("dot-indian", "grouping=.+3+2");
    grouping("group-option", "group,grouping=+3");
    xo_clear_flags(NULL, XOF_GROUP);
    xo_close_list("grouping");

    stars();
    precision();
    humanize();
    names();
    bytes();

    /* Leave "top" open and skip xo_finish(): atexit finishes it */
    return 0;
}
