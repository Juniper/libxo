/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, October 2026
 */

/*
 * Tests for the UTF-8 versions of the string(3) functions, and for
 * the handling of invalid UTF-8 input.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "xo.h"
#include "xo_encoder.h"
#include "xo_utf8.h"

/*
 * Strings are built from hex escapes so the bytes under test are
 * plain to see.  Adjacent literals keep an escape from swallowing a
 * following hex digit.
 */
#define E_ACUTE "\xc3\xa9"	   /* U+00E9, 2 bytes */
#define E_ACUTE_UP "\xc3\x89"	   /* U+00C9, 2 bytes */
#define EURO "\xe2\x82\xac"	   /* U+20AC, 3 bytes */
#define SMILE "\xf0\x9f\x98\x80"   /* U+1F600, 4 bytes */

#define GUARD '#'		/* Fills buffers to catch overruns */

static const char sample[] = "caf" E_ACUTE " " EURO "5 " SMILE " caf" E_ACUTE;

/* Return the offset of a pointer in a string, or -1 for NULL */
static long
offset (const char *base, const char *cp)
{
    return cp ? (long) (cp - base) : -1;
}

/*
 * Emit a buffer as hex bytes, so invalid output can't hide.  Each use
 * gets its own container, since one instance may show several buffers.
 */
static void
emit_hex (const char *tag, const char *str)
{
    char buf[BUFSIZ], *cp = buf;

    for ( ; *str; str++)
	cp += snprintf(cp, buf + sizeof(buf) - cp, "%s%02x",
		       (cp == buf) ? "" : " ", (unsigned char) *str);
    *cp = '\0';

    xo_open_container(tag);
    xo_emit(" [{:hex}]\n", buf);
    xo_close_container(tag);
}

static void
test_valid (void)
{
    static struct {
	const char *name;
	char data[16];
    } tests[] = {
	{ "ascii", "abc" },
	{ "two-byte", E_ACUTE },
	{ "three-byte", EURO },
	{ "four-byte", SMILE },
	{ "last-codepoint", "\xf4\x8f\xbf\xbf" },
	{ "before-surrogates", "\xed\x9f\xbf" },
	{ "after-surrogates", "\xee\x80\x80" },
	{ "bad-first-ff", "\xff" },
	{ "bad-first-f8", "a\xf8z" },
	{ "lone-secondary", "\x80" },
	{ "surrogate", "\xed\xa0\x80" },
	{ "beyond-10ffff", "\xf4\x90\x80\x80" },
	{ "non-shortest", "\xc0\xaf" },
	{ "truncated", "ab\xe2\x82" },
	{ "bad-trailing", "\xe2\x41\x42" },
	{ NULL, "" }
    };

    xo_open_list("valid");

    for (int i = 0; tests[i].name; i++) {
	char *str = tests[i].data;
	char *bad = xo_utf8_valid(str);
	const char *msg = "valid";

	if (bad) {
	    xo_codepoint_t wc = xo_utf8_codepoint(bad, strlen(bad),
						  xo_utf8_len(*bad), 0);
	    msg = xo_utf8_wchar_errmsg(wc);
	}

	xo_open_instance("valid");
	xo_emit("valid {k:name}: {:offset/%ld} {:message}", tests[i].name,
		offset(str, bad), msg);

	int rc = xo_utf8_makevalid(str, '?');
	xo_emit(" {:fixed/%d}", rc);
	emit_hex("made-valid", str);
	xo_close_instance("valid");
    }

    xo_close_list("valid");
}

static void
test_encode (void)
{
    static xo_codepoint_t points[] = {
	0x41, 0x7f, 0x80, 0xe9, 0x7ff, 0x800, 0x20ac, 0xffff,
	0x10000, 0x1f600, 0x10ffff, 0
    };

    xo_open_list("encode");

    for (int i = 0; points[i]; i++) {
	char buf[8];
	ssize_t len = xo_utf8_to_len(points[i]);

	xo_utf8_to_bytes(buf, len, points[i]);
	buf[len] = '\0';

	xo_codepoint_t back = xo_utf8_codepoint(buf, len, len, 0);

	xo_open_instance("encode");
	xo_emit("encode {k:codepoint/%#x}: {:length/%zd} {:decoded/%#x}",
		points[i], len, back);
	emit_hex("bytes", buf);
	xo_close_instance("encode");
    }

    xo_close_list("encode");
}

static void
test_length (void)
{
    static struct {
	const char *name;
	const char *data;
	size_t maxlen;
    } tests[] = {
	{ "empty", "", 10 },
	{ "ascii", "abc", 10 },
	{ "mixed", sample, sizeof(sample) },
	{ "limit-ascii", "abcdef", 3 },
	{ "limit-whole", "a" EURO "b", 4 },
	{ "limit-splits", "a" EURO "b", 3 },
	{ "invalid", "a\xff" "b\x80", 10 },
	{ NULL, NULL, 0 }
    };

    xo_open_list("length");

    for (int i = 0; tests[i].name; i++) {
	xo_open_instance("length");
	xo_emit("length {k:name}: {:nlen/%zu} {:len/%zu} {:bytes/%zu}\n",
		tests[i].name, xo_ustrnlen(tests[i].data, tests[i].maxlen),
		xo_ustrlen(tests[i].data), strlen(tests[i].data));
	xo_emit("  clen {:clen/%zd} {:buf-clen/%zd}\n",
		xo_utf8_clen(tests[i].data),
		xo_utf8_buf_clen(tests[i].data, tests[i].maxlen));
	xo_close_instance("length");
    }

    xo_close_list("length");

    /* A NUL always stops the count; a negative length means no limit */
    xo_emit("clen nul {:clen-nul/%zd} {:clen-half/%zd}\n",
	    xo_utf8_buf_clen("a\0" EURO, 6),
	    xo_utf8_buf_clen("ab\xf0\x9f", -1));
    xo_emit("clen none {:clen-null/%zd} {:clen-negative/%zd}\n",
	    xo_utf8_buf_clen(NULL, 4), xo_utf8_buf_clen("abc", -1));
}

static void
test_search (void)
{
    static struct {
	const char *name;
	xo_codepoint_t wc;
    } tests[] = {
	{ "ascii", 'f' },
	{ "two-byte", 0xe9 },
	{ "three-byte", 0x20ac },
	{ "four-byte", 0x1f600 },
	{ "absent", 0x3b1 },
	{ "surrogate", 0xd800 },
	{ "too-big", 0x110000 },
	{ NULL, 0 }
    };

    xo_open_list("search");

    for (int i = 0; tests[i].name; i++) {
	xo_codepoint_t wc = tests[i].wc;

	xo_open_instance("search");
	xo_emit("search {k:name}: {:chr/%ld} {:rchr/%ld} {:chrnul/%ld} "
		"{:chr-long/%ld} {:rchr-long/%ld} {:chrnul-long/%ld}\n",
		tests[i].name,
		offset(sample, xo_ustrchr(sample, wc)),
		offset(sample, xo_ustrrchr(sample, wc)),
		offset(sample, xo_ustrchrnul(sample, wc)),
		offset(sample, xo_ustrchr_long(sample, wc)),
		offset(sample, xo_ustrrchr_long(sample, wc)),
		offset(sample, xo_ustrchrnul_long(sample, wc)));
	xo_close_instance("search");
    }

    xo_close_list("search");
}

static void
test_span (void)
{
    static struct {
	const char *name;
	const char *data;
	const char *charset;
    } tests[] = {
	{ "ascii", "aabbcc", "ab" },
	{ "multi-byte", E_ACUTE EURO E_ACUTE "x" EURO, E_ACUTE EURO },
	{ "shared-bytes", E_ACUTE "\xc3\xa8" "z", "\xc3\xa8" },
	{ "none", "xyz", "abc" },
	{ "all", "abc", "cba" },
	{ "empty-set", "abc", "" },
	{ "invalid", "a\xff" "b", "ab\xff" },
	{ "late", "plain " SMILE " text", SMILE },
	{ NULL, NULL, NULL }
    };

    xo_open_list("span");

    for (int i = 0; tests[i].name; i++) {
	const char *data = tests[i].data;

	xo_open_instance("span");
	xo_emit("span {k:name}: {:spn/%zu} {:cspn/%zu} {:pbrk/%ld}\n",
		tests[i].name,
		xo_ustrspn(data, tests[i].charset),
		xo_ustrcspn(data, tests[i].charset),
		offset(data, xo_ustrpbrk(data, tests[i].charset)));
	xo_close_instance("span");
    }

    xo_close_list("span");
}

static void
test_find (void)
{
    static struct {
	const char *name;
	const char *big;
	const char *little;
	size_t len;
    } tests[] = {
	{ "ascii", "hello world", "world", 11 },
	{ "multi-byte", sample, EURO "5", sizeof(sample) },
	{ "second", sample, "caf" E_ACUTE " " EURO, sizeof(sample) },
	{ "case-ascii", "Hello World", "WORLD", 11 },
	{ "case-multi", "CAF" E_ACUTE_UP " au lait", "caf" E_ACUTE, 20 },
	{ "limit-short", "hello world", "world", 10 },
	{ "limit-exact", "hello world", "wor", 9 },
	{ "empty-little", "abc", "", 3 },
	{ "empty-big", "", "a", 0 },
	{ "longer-little", "ab", "abc", 2 },
	{ "absent", sample, "tea", sizeof(sample) },
	{ "partial-tail", "ab", "bc", 2 },
	{ NULL, NULL, NULL, 0 }
    };

    xo_open_list("find");

    for (int i = 0; tests[i].name; i++) {
	const char *big = tests[i].big, *little = tests[i].little;

	xo_open_instance("find");
	xo_emit("find {k:name}: {:nstr/%ld} {:str/%ld} {:casestr/%ld}\n",
		tests[i].name,
		offset(big, xo_ustrnstr(big, little, tests[i].len)),
		offset(big, xo_ustrstr(big, little)),
		offset(big, xo_ustrcasestr(big, little)));
	xo_close_instance("find");
    }

    xo_close_list("find");
}

static void
test_compare (void)
{
    static struct {
	const char *name;
	const char *s1;
	const char *s2;
    } tests[] = {
	{ "equal", "abc", "abc" },
	{ "case-ascii", "ABC", "abc" },
	{ "case-multi", "CAF" E_ACUTE_UP, "caf" E_ACUTE },
	{ "less", "abc", "abd" },
	{ "greater", "abd", "ABC" },
	{ "prefix", "ab", "abc" },
	{ "longer", "abc", "ab" },
	{ "ascii-vs-multi", "cafe", "caf" E_ACUTE },
	{ "invalid", "a\xff", "A\xff" },
	{ "empty", "", "" },
	{ NULL, NULL, NULL }
    };

    xo_open_list("compare");

    for (int i = 0; tests[i].name; i++) {
	xo_open_instance("compare");
	xo_emit("compare {k:name}: {:forward/%d} {:backward/%d}\n",
		tests[i].name,
		xo_ustrcasecmp(tests[i].s1, tests[i].s2),
		xo_ustrcasecmp(tests[i].s2, tests[i].s1));
	xo_close_instance("compare");
    }

    xo_close_list("compare");

    /* The "cut-" tests are where strncmp differs */
    static struct {
	const char *name;
	const char *s1;
	const char *s2;
	size_t len;
    } ntests[] = {
	{ "equal", "abc", "abc", 3 },
	{ "limit-hides", "abcx", "abcy", 3 },
	{ "limit-shows", "abcx", "abcy", 4 },
	{ "limit-beyond", "abc", "abc", 10 },
	{ "shorter", "ab", "abc", 3 },
	{ "limit-splits", "a" E_ACUTE, "a\xc3\xb1", 2 },
	{ "limit-split-same", "a" EURO "x", "a" EURO "y", 2 },
	{ "limit-whole", E_ACUTE "x", E_ACUTE "y", 2 },
	{ "ascii-vs-multi", "abc", "ab" EURO, 3 },
	{ "zero", "abc", "xyz", 0 },
	{ "cut-inside", "a" EURO, "a\xe2\x83\xac", 3 },
	{ "cut-first-bytes", "a" EURO, "a" SMILE, 3 },
	{ "cut-vs-ascii", "ab", "a" E_ACUTE, 2 },
	{ "cut-vs-whole", "a" E_ACUTE, "a" EURO, 3 },
	{ "cut-vs-nul", "a", "a" EURO, 3 },
	{ "whole-differs", "a" EURO, "a\xe2\x83\xac", 4 },
	{ "stray-secondary", "a\x82" "b", "a\x83" "b", 2 },
	{ NULL, NULL, NULL, 0 }
    };

    xo_open_list("ncompare");

    for (int i = 0; ntests[i].name; i++) {
	xo_open_instance("ncompare");
	xo_emit("ncompare {k:name}: {:forward/%d} {:backward/%d} "
		"{:strncmp/%d}\n", ntests[i].name,
		xo_ustrncmp(ntests[i].s1, ntests[i].s2, ntests[i].len),
		xo_ustrncmp(ntests[i].s2, ntests[i].s1, ntests[i].len),
		strncmp(ntests[i].s1, ntests[i].s2, ntests[i].len) != 0);
	xo_close_instance("ncompare");
    }

    xo_close_list("ncompare");
}

static void
test_sep (void)
{
    static struct {
	const char *name;
	const char *data;
	const char *delim;
    } tests[] = {
	{ "ascii", "a,b,c", "," },
	{ "multi-byte", "one" EURO "two" SMILE "three", EURO SMILE },
	{ "shared-bytes", "x\xc3\xa8y" E_ACUTE "z", E_ACUTE },
	{ "empty-fields", ",a,,b,", "," },
	{ "no-delimiter", "abc", "," },
	{ "empty-string", "", "," },
	{ "empty-delim", "a,b", "" },
	{ NULL, NULL, NULL }
    };
    char buf[64], out[128];

    xo_open_list("sep");

    for (int i = 0; tests[i].name; i++) {
	char *next = buf, *tok, *cp = out;
	int count = 0;

	xo_ustrlcpy(buf, tests[i].data, sizeof(buf));

	while ((tok = xo_ustrsep(&next, tests[i].delim)) != NULL) {
	    cp += snprintf(cp, out + sizeof(out) - cp, "[%s]", tok);
	    count += 1;
	}

	xo_open_instance("sep");
	xo_emit("sep {k:name}: {:count/%d} {:tokens}\n",
		tests[i].name, count, out);
	xo_close_instance("sep");
    }

    xo_close_list("sep");
}

/*
 * Each copy test runs in a buffer filled with guard bytes, and
 * reports the byte just past the space the function may use.
 */
static void
test_copy (void)
{
    static struct {
	const char *name;
	const char *src;
	size_t len;
    } tests[] = {
	{ "ascii", "abcdef", 6 },
	{ "ascii-short", "abcdef", 3 },
	{ "whole", "a" EURO "b", 5 },
	{ "fits-exactly", "a" EURO "b", 4 },
	{ "splits", "a" EURO "b", 3 },
	{ "splits-early", "a" EURO "b", 2 },
	{ "four-byte", SMILE "!", 4 },
	{ "four-byte-split", SMILE "!", 3 },
	{ "invalid", "a\xff" "b\x80" "c", 5 },
	{ "truncated-input", "ab\xe2\x82", 4 },
	{ "empty", "", 4 },
	{ "zero", "abc", 0 },
	{ NULL, NULL, 0 }
    };
    char buf[64];

    xo_open_list("copy");

    for (int i = 0; tests[i].name; i++) {
	const char *src = tests[i].src;
	size_t len = tests[i].len;

	xo_open_instance("copy");
	xo_emit("copy {k:name}:\n", tests[i].name);

	/* Room for 'len' bytes and the NUL */
	memset(buf, GUARD, sizeof(buf));
	char *res = xo_ustrncpy(buf, src, len);
	xo_emit("  ncpy {:ncpy-same/%d} {:ncpy-guard/%c}",
		res == buf, buf[len + 1]);
	emit_hex("ncpy", buf);

	memset(buf, GUARD, sizeof(buf));
	res = xo_ustpncpy(buf, src, len);
	xo_emit("  stpncpy {:stpncpy-offset/%ld} {:stpncpy-guard/%c}",
		offset(buf, res), buf[len + 1]);
	emit_hex("stpncpy", buf);

	/* Here 'len' is the size of the buffer, including the NUL */
	memset(buf, GUARD, sizeof(buf));
	size_t rc = xo_ustrlcpy(buf, src, len);
	xo_emit("  lcpy {:lcpy-rc/%zu} {:lcpy-guard/%c}", rc, buf[len]);
	if (len == 0)
	    buf[0] = '\0';	/* Nothing was written, so nothing to show */
	emit_hex("lcpy", buf);

	char *dup = xo_ustrndup(src, len);
	if (dup) {
	    xo_emit("  ndup {:ndup-length/%zu}", strlen(dup));
	    emit_hex("ndup", dup);
	    free(dup);
	}

	xo_close_instance("copy");
    }

    xo_close_list("copy");

    memset(buf, GUARD, sizeof(buf));
    xo_ustrcpy(buf, sample);
    xo_emit("cpy {:cpy-equal/%d}\n", strcmp(buf, sample) == 0);

    memset(buf, GUARD, sizeof(buf));
    xo_emit("stpcpy {:stpcpy-offset/%ld}\n",
	    offset(buf, xo_ustpcpy(buf, sample)));

    char *dup = xo_ustrdup(sample);
    if (dup) {
	xo_emit("dup {:dup-equal/%d}\n", strcmp(dup, sample) == 0);
	free(dup);
    }

    /* A source that stops inside a character must not leave half of it */
    static const char half[] = "ab\xf0\x9f\x98";

    memset(buf, GUARD, sizeof(buf));
    xo_ustrcpy(buf, half);
    xo_emit("half cpy");
    emit_hex("half-cpy", buf);

    memset(buf, GUARD, sizeof(buf));
    xo_emit("half stpcpy {:half-stpcpy-offset/%ld}",
	    offset(buf, xo_ustpcpy(buf, half)));
    emit_hex("half-stpcpy", buf);

    dup = xo_ustrdup(half);
    if (dup) {
	xo_emit("half dup");
	emit_hex("half-dup", dup);
	free(dup);
    }
}

static void
test_concat (void)
{
    static struct {
	const char *name;
	const char *dst;
	const char *append;
	size_t dstsize;
	size_t count;
    } tests[] = {
	{ "room", "abc", E_ACUTE, 32, 2 },
	{ "fits-exactly", "abc", E_ACUTE, 6, 2 },
	{ "splits", "abc", E_ACUTE, 5, 2 },
	{ "ascii-truncated", "abc", "defgh", 6, 5 },
	{ "count-limits", "abc", "defgh", 32, 2 },
	{ "count-splits", "abc", EURO "x", 32, 2 },
	{ "count-beyond", "abc", "de", 32, 10 },
	{ "invalid", "abc", "x\xff" "y", 32, 3 },
	{ "full", "abcde", "xy", 6, 2 },
	{ "empty-dst", "", SMILE "!", 32, 5 },
	{ "half-append", "abc", "x\xe2\x82", 32, 3 },
	{ NULL, NULL, NULL, 0, 0 }
    };
    char buf[64];

    xo_open_list("concat");

    for (int i = 0; tests[i].name; i++) {
	size_t dstsize = tests[i].dstsize;

	xo_open_instance("concat");

	memset(buf, GUARD, sizeof(buf));
	strcpy(buf, tests[i].dst);
	size_t rc = xo_ustrlncat(buf, tests[i].append, dstsize,
				 tests[i].count);
	xo_emit("concat {k:name}: {:lncat-rc/%zu} {:lncat-guard/%c}",
		tests[i].name, rc, buf[dstsize]);
	emit_hex("lncat", buf);

	memset(buf, GUARD, sizeof(buf));
	strcpy(buf, tests[i].dst);
	rc = xo_ustrlcat(buf, tests[i].append, dstsize);
	xo_emit("  lcat {:lcat-rc/%zu} {:lcat-guard/%c}", rc, buf[dstsize]);
	emit_hex("lcat", buf);

	xo_close_instance("concat");
    }

    xo_close_list("concat");

    /* A destination with no NUL inside 'dstsize' must be left alone */
    memset(buf, GUARD, sizeof(buf));
    memcpy(buf, "abcde", 5);
    size_t rc = xo_ustrlncat(buf, "xy", 5, 2);
    xo_emit("concat no-nul: {:no-nul-rc/%zu} {:no-nul-guard/%c}\n",
	    rc, buf[5]);
}

/* Walk a string forward with xo_utf8_next and back with xo_utf8_prev */
static void
test_walk (void)
{
    static const char *tests[] = {
	"a" E_ACUTE EURO SMILE "z",
	"plain",
	"",
	"x\xa9" "y",		/* Stray secondary byte */
	"x" "\xe2\x82",		/* Truncated character at the end */
	"\x82\xac" "x",		/* Leading secondary bytes */
	NULL
    };

    xo_open_list("walk");

    for (int i = 0; tests[i]; i++) {
	const char *str = tests[i], *cp;

	xo_open_instance("walk");
	xo_emit("walk {k:test/%d}: next", i);

	for (cp = str; cp && *cp; cp = xo_utf8_next(cp))
	    xo_emit(" {l:next/%ld}", offset(str, cp));
	xo_emit(" {:next-end/%ld} prev", offset(str, cp));

	for (cp = str + strlen(str); cp; cp = xo_utf8_prev(str, cp))
	    xo_emit(" {l:prev/%ld}", offset(str, cp));
	xo_emit("\n");

	xo_close_instance("walk");
    }

    xo_close_list("walk");

    /* The n-version must not step beyond its length */
    const char *str = "a" EURO "z";

    xo_open_list("nnext");
    for (size_t len = 0; len <= 5; len++) {
	xo_open_instance("nnext");
	xo_emit("nnext {k:len/%zu}: {:from-start/%ld} {:from-euro/%ld}\n",
		len, offset(str, xo_utf8_nnext(str, len)),
		len ? offset(str, xo_utf8_nnext(str + 1, len - 1)) : -1);
	xo_close_instance("nnext");
    }
    xo_close_list("nnext");

    xo_emit("walk edges: {:next-at-nul/%ld} {:prev-null/%ld} "
	    "{:prev-at-start/%ld}\n",
	    offset(str, xo_utf8_next(str + strlen(str))),
	    offset(str, xo_utf8_prev(str, NULL)),
	    offset(str, xo_utf8_prev(str, str)));
}

static void
test_trunc (void)
{
    static const char src[] = "a" E_ACUTE EURO SMILE "z";
    char buf[32];

    xo_open_list("trunc");

    for (size_t len = 0; len < sizeof(src); len++) {
	xo_open_instance("trunc");

	strcpy(buf, src);
	size_t rc = xo_utrunc(buf, len);
	xo_emit("trunc {k:len/%zu}: {:trunc-rc/%zu} {:valid/%s}",
		len, rc, xo_utf8_valid(buf) ? "invalid" : "valid");
	emit_hex("result", buf);

	xo_close_instance("trunc");
    }

    xo_close_list("trunc");
}

static void
test_case (void)
{
    static struct {
	const char *name;
	xo_codepoint_t wc;
    } wide[] = {
	{ "a", 'a' },
	{ "A", 'A' },
	{ "digit", '5' },
	{ "nul", 0 },
	{ "e-acute", 0xe9 },
	{ "E-acute", 0xc9 },
	{ "sharp-s", 0xdf },
	{ "greek-sigma", 0x3c3 },
	{ "greek-Sigma", 0x3a3 },
	{ "cyrillic-zhe", 0x436 },
	{ "cyrillic-Zhe", 0x416 },
	{ "euro", 0x20ac },
	{ "smile", 0x1f600 },
	{ "deseret-long-i", 0x10428 },
	{ "deseret-Long-I", 0x10400 },
	{ "surrogate", 0xd800 },
	{ "beyond", 0x110000 },
	{ NULL, 0 }
    };

    xo_open_list("wide");

    for (int i = 0; wide[i].name; i++) {
	xo_codepoint_t wc = wide[i].wc;

	xo_open_instance("wide");
	xo_emit("wide {k:name}: {:lower/%d} {:upper/%d} "
		"{:to-lower/%#x} {:to-upper/%#x}\n",
		wide[i].name, xo_utf8_wislower(wc) ? 1 : 0,
		xo_utf8_wisupper(wc) ? 1 : 0,
		xo_utf8_wtolower(wc), xo_utf8_wtoupper(wc));
	xo_close_instance("wide");
    }

    xo_close_list("wide");

    static const char *strs[] = {
	"abc", "Abc", E_ACUTE "t" E_ACUTE, E_ACUTE_UP "t" E_ACUTE,
	EURO, "\xff", "", NULL
    };

    xo_open_list("first");

    for (int i = 0; strs[i]; i++) {
	xo_open_instance("first");
	xo_emit("first {k:test/%d}: {:islower/%d} {:isupper/%d} "
		"{:nislower-1/%d} {:nisupper-1/%d}\n", i,
		xo_utf8_islower(strs[i]) ? 1 : 0,
		xo_utf8_isupper(strs[i]) ? 1 : 0,
		xo_utf8_nislower(strs[i], 1) ? 1 : 0,
		xo_utf8_nisupper(strs[i], 1) ? 1 : 0);
	xo_close_instance("first");
    }

    xo_close_list("first");

    xo_emit("first null: {:nislower-null/%d} {:nisupper-null/%d} "
	    "{:nislower-zero/%d} {:nisupper-zero/%d}\n",
	    xo_utf8_nislower(NULL, 4) ? 1 : 0,
	    xo_utf8_nisupper(NULL, 4) ? 1 : 0,
	    xo_utf8_nislower("a", 0) ? 1 : 0,
	    xo_utf8_nisupper("A", 0) ? 1 : 0);
}

/*
 * The n-versions must stay inside their length, even when it falls
 * short of the NUL or lands inside a character.
 */
static void
test_bounded (void)
{
    static const char src[] = "Ab" E_ACUTE_UP "c" E_ACUTE "D" EURO "\xff" "E";
    char buf[32];

    xo_open_list("bounded");

    for (size_t len = 0; len < sizeof(src); len++) {
	xo_open_instance("bounded");

	xo_emit("bounded {k:len/%zu}: {:nvalid/%ld}",
		len, offset(src, xo_utf8_nvalid(src, len)));

	strcpy(buf, src);
	xo_utf8_ntolower(buf, len);
	emit_hex("ntolower", buf);

	strcpy(buf, src);
	xo_utf8_ntoupper(buf, len);
	xo_emit(" ");
	emit_hex("ntoupper", buf);

	strcpy(buf, src);
	int rc = xo_utf8_nmakevalid(buf, len, '?');
	xo_emit("  {:nmakevalid-rc/%d}", rc);
	emit_hex("nmakevalid", buf);

	xo_close_instance("bounded");
    }

    xo_close_list("bounded");

    /* A NUL replacement ends the string at the first bad byte */
    strcpy(buf, "ab\xff" "cd\xff" "e");
    int rc = xo_utf8_makevalid(buf, '\0');
    xo_emit("makevalid nul: {:makevalid-rc/%d}", rc);
    emit_hex("makevalid", buf);

    /* Case-blind compares must stop at a NUL inside the given length */
    xo_emit("ncasecmp: {:nul-stops/%d} {:prefix/%d} {:length-stops/%d} "
	    "{:half/%d}\n",
	    xo_ustrncasecmp("ABC\0x", 5, "abc\0y", 5),
	    xo_ustrncasecmp("abc", 100, "ABCD", 100),
	    xo_ustrncasecmp("abcX", 3, "ABCy", 3),
	    xo_ustrncasecmp("a\xc3", 100, "A\xc3", 100));
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("top");

    test_valid();
    test_encode();
    test_length();
    test_search();
    test_span();
    test_find();
    test_compare();
    test_sep();
    test_copy();
    test_concat();
    test_walk();
    test_trunc();
    test_case();
    test_bounded();

    xo_close_container("top");
    xo_finish();

    return 0;
}
