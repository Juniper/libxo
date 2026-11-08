/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Exercises the "B" (boolean) field role.  Each container holds one
 * group of cases, emitted once with a true argument and once with a
 * false one:
 *
 *   basic: the examples from the docs (default %s, f, words, "*")
 *   types: every argument type, true and false
 *   stars: "%*.*s" and "%@*@d" pop their ints before the value
 *   strings: NULL, "" and non-empty strings ("0" and "false" are true)
 *   pairs: "*" pairs (good, NULL, malformed) and escaped words
 *   full: full-boolean, with and without pairs, encoding formats and n
 *   mods: q, e, d, k and l
 *
 * Each value is followed by a "{:NAME-next/%d}" sentinel; if a B field
 * pops the wrong number of arguments, the sentinels go wrong.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include "xo.h"

static int sentinel;

static void
basic (int t)
{
    int x = t ? 7 : 0;
    void *ptr = t ? &sentinel : NULL;
    const char *str = t ? "yes" : NULL;

    xo_emit("{B:str} ({:str-next/%d})\n", str, ++sentinel);
    xo_emit("{Bf:capital} ({:capital-next/%d})\n", str, ++sentinel);
    xo_emit("{B:int/%d} ({:int-next/%d})\n", x, ++sentinel);
    xo_emit("{B:ptr/%p} ({:ptr-next/%d})\n", ptr, ++sentinel);
    xo_emit("{B:power/on+off} ({:power-next/%d})\n", str, ++sentinel);
    xo_emit("{B:power-ptr/on+off%p} ({:power-ptr-next/%d})\n",
	    ptr, ++sentinel);
    xo_emit("{Bf:power-u/on+off%u} ({:power-u-next/%d})\n",
	    (unsigned) x, ++sentinel);
    xo_emit("{Bf:power-64/on+off%!64u} ({:power-64-next/%d})\n",
	    (uint64_t) x << 40, ++sentinel);
    xo_emit("{B:state/enabled+disabled%d} ({:state-next/%d})\n",
	    x, ++sentinel);
    xo_emit("{Bf:power-arg/*%u} ({:power-arg-next/%d})\n",
	    "on+off", (unsigned) x, ++sentinel);
    xo_emit("{B,full-boolean:full/T+F%p/1+0%p} ({:full-next/%d})\n",
	    ptr, ++sentinel);
    xo_emit("{Lwc:Label}{B,boolean:long-name/up+down%d} "
	    "({:long-name-next/%d})\n",
	    x, ++sentinel);
}

static void
types (int t)
{
    xo_emit("{B:t-int/%d} ({:t-int-next/%d})\n", t, ++sentinel);
    xo_emit("{B:t-char/%c} ({:t-char-next/%d})\n", t ? 'c' : 0, ++sentinel);
    xo_emit("{B:t-short/%hd} ({:t-short-next/%d})\n", (short) t, ++sentinel);
    xo_emit("{B:t-long/%ld} ({:t-long-next/%d})\n", (long) t, ++sentinel);
    xo_emit("{B:t-long-long/%lld} ({:t-long-long-next/%d})\n",
	    (long long) t, ++sentinel);
    xo_emit("{B:t-intmax/%jd} ({:t-intmax-next/%d})\n",
	    (intmax_t) t, ++sentinel);
    xo_emit("{B:t-ptrdiff/%td} ({:t-ptrdiff-next/%d})\n",
	    (ptrdiff_t) t, ++sentinel);
    xo_emit("{B:t-size/%zu} ({:t-size-next/%d})\n", (size_t) t, ++sentinel);
    xo_emit("{B:t-quad/%qd} ({:t-quad-next/%d})\n", (quad_t) t, ++sentinel);
    xo_emit("{B:t-int64/%!64d} ({:t-int64-next/%d})\n",
	    (int64_t) t, ++sentinel);
    xo_emit("{B:t-double/%f} ({:t-double-next/%d})\n",
	    t ? 0.5 : -0.0, ++sentinel);
    xo_emit("{B:t-long-double/%Lf} ({:t-long-double-next/%d})\n",
	    (long double) (t ? 0.25 : 0.0), ++sentinel);
    xo_emit("{B:t-wint/%lc} ({:t-wint-next/%d})\n",
	    (wint_t) (t ? L'w' : 0), ++sentinel);
    xo_emit("{B:t-wide/%ls} ({:t-wide-next/%d})\n",
	    t ? L"wide" : L"", ++sentinel);
    xo_emit("{B:t-string/%s} ({:t-string-next/%d})\n",
	    t ? "s" : "", ++sentinel);
    xo_emit("{B:t-pointer/%p} ({:t-pointer-next/%d})\n",
	    t ? (void *) &sentinel : NULL, ++sentinel);
    xo_emit("{B:t-bool/%d} ({:t-bool-next/%d})\n", (_Bool) t, ++sentinel);
}

static void
stars (int t)
{
    xo_emit("{B:star-string/%*.*s} ({:star-string-next/%d})\n",
	    10, 20, t ? "abc" : "", ++sentinel);
    xo_emit("{B:star-int/%*d} ({:star-int-next/%d})\n", 8, t, ++sentinel);
    xo_emit("{B:at-star/%@*@d} ({:at-star-next/%d})\n", 99, t, ++sentinel);
    xo_emit("{B:pointless/%-8hs} ({:pointless-next/%d})\n",
	    t ? "x" : "", ++sentinel);
}

static void
strings (int t)
{
    xo_emit("{B:null-or-zero/%s} ({:null-or-zero-next/%d})\n",
	    t ? "0" : NULL, ++sentinel);
    xo_emit("{B:empty-or-false/%s} ({:empty-or-false-next/%d})\n",
	    t ? "false" : "", ++sentinel);
}

static void
pairs (int t)
{
    xo_emit("{B:arg-pair/*%d} ({:arg-pair-next/%d})\n",
	    "yes+no", t, ++sentinel);
    xo_emit("{B:arg-null/*%d} ({:arg-null-next/%d})\n", NULL, t, ++sentinel);
    xo_emit("{B:arg-bad/*%d} ({:arg-bad-next/%d})\n", "a+b+c", t, ++sentinel);
    xo_emit("{B:arg-escape/*%d} ({:arg-escape-next/%d})\n",
	    "100%+50\\", t, ++sentinel);
    xo_emit("{B:escaped/a\\+b+c\\/d%d} ({:escaped-next/%d})\n", t, ++sentinel);
    xo_emit("{B:percent/100%%+0%%%d} ({:percent-next/%d})\n", t, ++sentinel);
    xo_emit("{B:brace/\\}+\\*%d} ({:brace-next/%d})\n", t, ++sentinel);
    xo_emit("{B:empty-word/+off%d} ({:empty-word-next/%d})\n", t, ++sentinel);
    xo_emit("{B:xml-chars/<a>+&b%d} ({:xml-chars-next/%d})\n", t, ++sentinel);
}

static void
full (int t)
{
    const char *str = t ? "yes" : NULL;

    xo_emit("{B,full-boolean:full-words/yup+nope} ({:full-words-next/%d})\n",
	    str, ++sentinel);
    xo_emit("{B,full-boolean:full-none} ({:full-none-next/%d})\n",
	    str, ++sentinel);
    xo_emit("{B,full-boolean:full-enc/T+F%d/1+0%d} ({:full-enc-next/%d})\n",
	    t, ++sentinel);
    xo_emit("{Bn,full-boolean:full-enc-n/T+F%d/1+0%d} "
	    "({:full-enc-n-next/%d})\n",
	    t, ++sentinel);
    xo_emit("{Bfg,full-boolean:full-raw/on+off%d} ({:full-raw-next/%d})\n",
	    t, ++sentinel);
    xo_emit("{B,full-boolean:full-arg/*%d} ({:full-arg-next/%d})\n",
	    "si+no", t, ++sentinel);
    xo_emit("{B,full-boolean:full-arg-enc/*%d/*%d} "
	    "({:full-arg-enc-next/%d})\n",
	    "si+no", t, ++sentinel);
    xo_emit("{B,full-boolean:full-xml/<a>+&b%d} ({:full-xml-next/%d})\n",
	    t, ++sentinel);
    xo_emit("{B:enc-only/T+F%d/1+0%d} ({:enc-only-next/%d})\n",
	    t, ++sentinel);
}

static void
mods (int t)
{
    xo_emit("{Bq:quoted/%d} ({:quoted-next/%d})\n", t, ++sentinel);
    xo_emit("{Be:encode-only/on+off%d} ({:encode-only-next/%d})\n",
	    t, ++sentinel);
    xo_emit("{Bd:display-only/on+off%d} ({:display-only-next/%d})\n",
	    t, ++sentinel);
    xo_emit("{Bh:humanize/%d} ({:humanize-next/%d})\n",
	    t ? 4096 : 0, ++sentinel);

    xo_open_list("item");
    xo_open_instance("item");
    xo_emit("{Bk:key/on+off%d} ({:key-next/%d})\n", t, ++sentinel);
    xo_close_instance("item");
    xo_close_list("item");

    xo_emit("{Bl:flag/%d}{Bl:flag/%d} ({:flag-next/%d})\n", t, !t, ++sentinel);
}

static void
run (const char *name, void (*func)(int))
{
    xo_open_container(name);

    xo_emit("Test: {:test-name-1} ({B:boolean-1/%d}) ::\n", name, 1);

    xo_open_container("true");
    func(1);
    xo_close_container("true");

    xo_emit("Test: {:test-name-2} ({B:boolean-2/%d}) ::\n", name, 0);
    xo_open_container("false");
    func(0);
    xo_close_container("false");

    xo_close_container(name);
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("boolean-test");

    run("basic", basic);
    run("types", types);
    run("stars", stars);
    run("strings", strings);
    run("pairs", pairs);
    run("full", full);
    run("mods", mods);

    xo_close_container("boolean-test");

    xo_finish();

    return 0;
}
