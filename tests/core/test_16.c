/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Exercises the RTOON encoder (encoder/rtoon/enc_rtoon.c) against the
 * scenario list in build/rtoon-plan.md Section 6.  Each scenario lives
 * in its own top-level container, named "sNN-*" to match the plan's
 * enumeration, so the merged output stays easy to review scenario by
 * scenario.  Filtered rollback is not exercised here; it needs an
 * XPath filter and is covered separately.
 *
 * Scenarios 17 onward are a broader "transition matrix": this-after-
 * that combinations and unusual field orderings/nestings that go
 * beyond the plan's base-case enumeration, added because it's the
 * unexpected transitions - not the clean base cases - that tend to
 * find real problems.  One of them (24) is a regression test for a
 * libxo core bug (not RTOON-specific) found and fixed via this file's
 * scenario 24: xo_format_value() in libxo.c only checked whether a
 * leaf-list was currently open, never whether it was the *same*
 * leaf-list, so a second differently-named leaf-list emitted with no
 * intervening field silently merged into the first one's name and
 * values.  Confirmed cross-style (JSON/XML/text/RTOON) before the fix.
 */

#include <stdio.h>
#include "xo.h"

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    xo_open_container("rtoon-test");

    /* 1. empty container */
    xo_open_container("s01-empty-container");
    xo_close_container("s01-empty-container");

    /* 2. empty list (two-line spelling, no instances ever opened) */
    xo_open_list("s02-empty-list");
    xo_close_list("s02-empty-list");

    /* 3. plain nested containers, three deep */
    xo_open_container("s03-a");
    xo_open_container("s03-b");
    xo_open_container("s03-c");
    xo_emit("{:leaf}\n", "value");
    xo_close_container("s03-c");
    xo_close_container("s03-b");
    xo_close_container("s03-a");

    /* 4. flat sparse list, single key field */
    xo_open_list("s04-disk");
    {
        static const char *names[] = { "sda", "sdb", "sdc" };
        static const int sizes[] = { 100, 200, 300 };
        int i;
        for (i = 0; i < 3; i++) {
            xo_open_instance("s04-disk");
            xo_emit("{k:name}{:size/%d}\n", names[i], sizes[i]);
            xo_close_instance("s04-disk");
        }
    }
    xo_close_list("s04-disk");

    /* 5. sparse list, composite (multi-field) key */
    xo_open_list("s05-route");
    {
        static const struct { const char *dest; int plen; const char *proto; } r[] = {
            { "10.0.0.0", 24, "static" },
            { "10.0.1.0", 24, "static" },
            { "0.0.0.0", 0, "default" },
        };
        int i;
        for (i = 0; i < 3; i++) {
            xo_open_instance("s05-route");
            xo_emit("{k:dest}{k:prefix-len/%d}{:protocol}\n",
                    r[i].dest, r[i].plen, r[i].proto);
            xo_close_instance("s05-route");
        }
    }
    xo_close_list("s05-route");

    /* 6. sparse list with a nested-container field on one instance */
    xo_open_list("s06-host");
    xo_open_instance("s06-host");
    xo_emit("{k:name}\n", "alpha");
    xo_close_instance("s06-host");
    xo_open_instance("s06-host");
    xo_emit("{k:name}\n", "beta");
    xo_open_container("stats");
    xo_emit("{:up-time/%d}\n", 100);
    xo_close_container("stats");
    xo_close_instance("s06-host");
    xo_close_list("s06-host");

    /* 7. dense list, uniform fields, single key field (trailing '=') */
    xo_open_list_hf(NULL, XOF_DENSE, "s07-cpu");
    {
        int i;
        for (i = 0; i < 3; i++) {
            xo_open_instance("s07-cpu");
            xo_emit("{k:num/%d}{:load/%d}\n", i, i * 7);
            xo_close_instance("s07-cpu");
        }
    }
    xo_close_list("s07-cpu");

    /* 8. dense list, composite key (two trailing-'=' fields) */
    xo_open_list_hf(NULL, XOF_DENSE, "s08-route2");
    {
        static const struct { const char *dest; int plen; const char *proto; } r[] = {
            { "10.0.0.0", 24, "static" },
            { "10.0.1.0", 25, "static" },
        };
        int i;
        for (i = 0; i < 2; i++) {
            xo_open_instance("s08-route2");
            xo_emit("{k:dest}{k:prefix-len/%d}{:protocol}\n",
                    r[i].dest, r[i].plen, r[i].proto);
            xo_close_instance("s08-route2");
        }
    }
    xo_close_list("s08-route2");

    /*
     * 9. dense list widening mid-body: each record adds a field the
     * prior ones didn't have, and the third leaves "price" present but
     * explicitly empty rather than absent - confirming absent (no slot
     * at all) vs. present-but-empty (bare-comma "") stay distinct, and
     * that the key marker is re-emitted on every widened fields-line.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s09-item");
    xo_open_instance("s09-item");
    xo_emit("{k:sku}{:qty/%d}{:price}\n", "WIDGET1", 10, "9.99");
    xo_close_instance("s09-item");
    xo_open_instance("s09-item");
    xo_emit("{k:sku}{:qty/%d}{:price}{:cost}\n", "WIDGET2", 5, "4.50", "2.00");
    xo_close_instance("s09-item");
    xo_open_instance("s09-item");
    xo_emit("{k:sku}{:qty/%d}{:price}{:cost}{:sell-by}\n",
            "WIDGET3", 0, "", "1.00", "2026-01-01");
    xo_close_instance("s09-item");
    xo_close_list("s09-item");

    /*
     * 10. leaf-list: 3+ values, trailing comma, an internal empty cell,
     * and a value that needs quoting because it contains a comma.
     */
    xo_open_container("s10-colors");
    xo_emit("{l:tag}", "a");
    xo_emit("{l:tag}", "");
    xo_emit("{l:tag}", "c");
    xo_emit("{l:tag}", "");
    xo_emit("{l:tag}", "needs,quote");
    xo_close_container("s10-colors");

    /* 11a. leaf-list with exactly one value */
    xo_open_container("s11a-tags");
    xo_emit("{l:tag}", "red");
    xo_close_container("s11a-tags");

    /* 11b. companion: single value that is itself the empty string */
    xo_open_container("s11b-tags");
    xo_emit("{l:tag}", "");
    xo_close_container("s11b-tags");

    /* 12. plain data-value, no trailing comma - confirm NOT read as a leaf-list */
    xo_open_container("s12-plain");
    xo_emit("{:tag}\n", "red");
    xo_close_container("s12-plain");

    /* 13a. explicit dense signal: XOF_DENSE on xo_open_list_hf() */
    xo_open_list_hf(NULL, XOF_DENSE, "s13a-entry");
    xo_open_instance("s13a-entry");
    xo_emit("{k:num/%d}{:name}\n", 1, "one");
    xo_close_instance("s13a-entry");
    xo_close_list("s13a-entry");

    /*
     * 13b. explicit dense signal via the "dense" field modifier, given
     * mid-way through the first instance's fields - fields buffered
     * before it ("id") become the row's leading columns.
     */
    xo_open_list("s13b-entry");
    xo_open_instance("s13b-entry");
    xo_emit("{k:num/%d}{,dense:name}{:score/%d}\n", 1, "one", 42);
    xo_close_instance("s13b-entry");
    xo_open_instance("s13b-entry");
    xo_emit("{k:num/%d}{:name}{:score/%d}\n", 2, "two", 43);
    xo_close_instance("s13b-entry");
    xo_close_list("s13b-entry");

    /* 13c. neither signal given - sparse is the default */
    xo_open_list("s13c-entry");
    xo_open_instance("s13c-entry");
    xo_emit("{k:num/%d}{:name}\n", 1, "one");
    xo_close_instance("s13c-entry");
    xo_close_list("s13c-entry");

    /* 14. attributes are dropped entirely, not just display-suppressed */
    xo_open_container("s14-thing");
    xo_attr("attr1", "%s", "should-not-appear");
    xo_emit("{:leaf}\n", "value");
    xo_close_container("s14-thing");

    /*
     * 15. flattening: a dense list whose second record adds a nested
     * "via" container, flattened into via/interface and
     * via/source-address columns; the first record omits those columns
     * entirely (absent, not empty).
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s15-route-entry");
    xo_open_instance("s15-route-entry");
    xo_emit("{k:destination}{:protocol}\n", "10.0.0.0/24", "direct");
    xo_close_instance("s15-route-entry");
    xo_open_instance("s15-route-entry");
    xo_emit("{k:destination}{:protocol}\n", "10.0.1.0/24", "static");
    xo_open_container("via");
    xo_emit("{:interface}{:source-address}\n", "eth0", "10.0.1.1");
    xo_close_container("via");
    xo_close_instance("s15-route-entry");
    xo_close_list("s15-route-entry");

    /*
     * 15b. negative case: the nested field is itself a *list*, not a
     * container, so it can't be flattened - this must be a hard error,
     * not a silent downgrade to sparse form.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s15b-entry");
    xo_open_instance("s15b-entry");
    xo_emit("{:name}\n", "one");
    xo_open_list("sub");
    xo_open_instance("sub");
    xo_emit("{:val}\n", "1");
    xo_close_instance("sub");
    xo_close_list("sub");
    xo_close_instance("s15b-entry");
    xo_close_list("s15b-entry");

    /*
     * 16. scalar typing: a numeric field and a boolean field both
     * arrive as XO_OP_CONTENT and render bare; string fields whose
     * text would otherwise collide with a typed literal (a number, or
     * the tokens true/null) arrive as XO_OP_STRING and are quoted so
     * they round-trip as strings, not the corresponding typed value.
     */
    xo_open_container("s16-types");
    xo_emit("{:count/%d}\n", 42);
    xo_emit("{:active/}\n");
    xo_emit("{n:disabled/%s}\n", "false");
    xo_emit("{n:raw-null/%s}\n", "null");
    xo_emit("{:code/%s}\n", "42");
    xo_emit("{:label/%s}\n", "true");
    xo_emit("{:mid/%s}\n", "null");
    xo_close_container("s16-types");

    /*
     * 17. dense list: fields for one instance split across multiple
     * xo_emit() calls vs. all given in a single call - confirms the
     * dense-row buffering in enc_rtoon.c keys off the instance
     * boundary (xo_open_instance/xo_close_instance), not the
     * xo_emit() call boundary, so both styles produce identical rows.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s17-split");
    xo_open_instance("s17-split");
    xo_emit("{k:num/%d}{:aval/%d}{:bval/%d}\n", 1, 10, 20);
    xo_close_instance("s17-split");
    xo_open_instance("s17-split");
    xo_emit("{k:num/%d}", 2);
    xo_emit("{:aval/%d}", 30);
    xo_emit("{:bval/%d}\n", 40);
    xo_close_instance("s17-split");
    xo_close_list("s17-split");

    /*
     * 18. field order: a non-key field emitted *before* the key
     * field, with the key arriving mid-instance rather than first.
     * libxo warns ("key field emitted after normal value field")
     * because keys are conventionally expected first, but still
     * renders the key marker at the field's actual position rather
     * than silently dropping it or forcing it first - confirms
     * RTOON's key-tracking is positional, not an implicit
     * first-field assumption.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s18-order");
    xo_open_instance("s18-order");
    xo_emit("{:pre}{k:name}{:post}\n", "before", "n1", "after");
    xo_close_instance("s18-order");
    xo_close_list("s18-order");

    /*
     * 19. two key fields with a non-key field between them (not
     * adjacent).  Same libxo warning as scenario 18 fires for the
     * second key ("key field emitted after normal value field:
     * 'bkey'"), but both key markers still land correctly in the
     * fields-line.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s19-twokeys");
    xo_open_instance("s19-twokeys");
    xo_emit("{k:akey}{:mid}{k:bkey}\n", "ka", "middata", "kb");
    xo_close_instance("s19-twokeys");
    xo_close_list("s19-twokeys");

    /*
     * 20. dense widen-then-narrow: record 2 adds a field beyond
     * record 1's set (widening the fields-line), then record 3
     * supplies only the *original* narrower set of fields again. Per
     * rtoon-spec.md Section 6.3, a row renders under the most
     * recently emitted fields-line, not a line matched to its own
     * field count - so record 3 still renders under record 2's
     * widened 3-column header, with an explicit empty cell (not a
     * dropped column) for the field it didn't supply.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s20-narrow");
    xo_open_instance("s20-narrow");
    xo_emit("{k:name}{:aval/%d}\n", "r1", 1);
    xo_close_instance("s20-narrow");
    xo_open_instance("s20-narrow");
    xo_emit("{k:name}{:aval/%d}{:bval/%d}\n", "r2", 2, 20);
    xo_close_instance("s20-narrow");
    xo_open_instance("s20-narrow");
    xo_emit("{k:name}{:aval/%d}\n", "r3", 3);
    xo_close_instance("s20-narrow");
    xo_close_list("s20-narrow");

    /*
     * 21. duplicate field name within a single dense instance. This
     * is a general libxo structural restriction, not RTOON-specific:
     * libxo warns ("duplicate sibling name: 'val'") and only the
     * first value survives - confirms RTOON doesn't second-guess or
     * "fix" libxo's own de-duplication, it just renders whatever
     * libxo hands it.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s21-dupfield");
    xo_open_instance("s21-dupfield");
    xo_emit("{k:name}{:val/%d}{:val/%d}\n", "dup", 1, 2);
    xo_close_instance("s21-dupfield");
    xo_close_list("s21-dupfield");

    /*
     * 22. sparse-list instance containing a full nested *list* (not
     * just a container) - unlike a dense row, a sparse instance isn't
     * restricted to flat scalar cells, so an ordinary nested list
     * renders exactly like it would anywhere else in the tree.
     */
    xo_open_list("s22-outer-sparse");
    xo_open_instance("s22-outer-sparse");
    xo_emit("{k:name}\n", "parent1");
    xo_open_list("inner");
    xo_open_instance("inner");
    xo_emit("{k:iname}{:ival/%d}\n", "c1", 1);
    xo_close_instance("inner");
    xo_open_instance("inner");
    xo_emit("{k:iname}{:ival/%d}\n", "c2", 2);
    xo_close_instance("inner");
    xo_close_list("inner");
    xo_close_instance("s22-outer-sparse");
    xo_close_list("s22-outer-sparse");

    /*
     * 23. mixed nesting depth: a dense list living inside a container
     * that itself lives inside a sparse-list instance - confirms
     * dense-row buffering state is scoped per dense-list frame and
     * unaffected by how deeply it's nested under sparse/container
     * ancestors.
     */
    xo_open_list("s23-mixed");
    xo_open_instance("s23-mixed");
    xo_emit("{k:name}\n", "wrap");
    xo_open_container("holder");
    xo_open_list_hf(NULL, XOF_DENSE, "densekids");
    xo_open_instance("densekids");
    xo_emit("{k:kid}{:kval/%d}\n", "k1", 1);
    xo_close_instance("densekids");
    xo_open_instance("densekids");
    xo_emit("{k:kid}{:kval/%d}\n", "k2", 2);
    xo_close_instance("densekids");
    xo_close_list("densekids");
    xo_close_container("holder");
    xo_close_instance("s23-mixed");
    xo_close_list("s23-mixed");

    /*
     * 24. leaf-list interleaving: a leaf-list resumed after a
     * non-leaf field closes and reopens under the same name (two
     * separate "tag" lines), and a *second, differently-named*
     * leaf-list ("color2") immediately following the first with no
     * intervening field gets its own line rather than being silently
     * folded into "tag"'s buffer.  The second case was a real libxo
     * core bug (see the file header) - fixed this session; kept here
     * as a regression test.
     */
    xo_open_container("s24-leafmix");
    xo_emit("{:before}\n", "x");
    xo_emit("{l:tag}", "r");
    xo_emit("{l:tag}", "g");
    xo_emit("{:between}\n", "y");
    xo_emit("{l:tag}", "b");
    xo_emit("{l:color2}", "cyan");
    xo_emit("{l:color2}", "magenta");
    xo_close_container("s24-leafmix");

    /*
     * 25. sibling ordering: a container immediately followed by a
     * sibling list at the same depth (not nested).
     */
    xo_open_container("s25-sib-a");
    xo_emit("{:xval}\n", "1");
    xo_close_container("s25-sib-a");
    xo_open_list("s25-sib-b");
    xo_open_instance("s25-sib-b");
    xo_emit("{k:name}\n", "b1");
    xo_close_instance("s25-sib-b");
    xo_close_list("s25-sib-b");

    /*
     * 26. two sibling lists directly inside one container - confirms
     * list open/close cleanly resets dense/sparse mode tracking
     * between same-depth sibling lists rather than leaking state from
     * one to the next.
     */
    xo_open_container("s26-twolists");
    xo_open_list("listone");
    xo_open_instance("listone");
    xo_emit("{k:name}\n", "one-a");
    xo_close_instance("listone");
    xo_close_list("listone");
    xo_open_list("listtwo");
    xo_open_instance("listtwo");
    xo_emit("{k:name}\n", "two-a");
    xo_close_instance("listtwo");
    xo_close_list("listtwo");
    xo_close_container("s26-twolists");

    /*
     * 27. sparse-list instance whose very first field is a nested
     * container, with the key field arriving only afterward -
     * confirms an instance doesn't require its key to be the literal
     * first thing written.
     */
    xo_open_list("s27-firstcontainer");
    xo_open_instance("s27-firstcontainer");
    xo_open_container("info");
    xo_emit("{:detail}\n", "d1");
    xo_close_container("info");
    xo_emit("{k:name}\n", "after-container");
    xo_close_instance("s27-firstcontainer");
    xo_close_list("s27-firstcontainer");

    /*
     * 28. display-only ({d:}) fields never reach the encoder at all -
     * they format their printf varargs for the formatting machinery
     * but return before calling xo_encoder_handle() - while
     * encode-only ({e:}) fields reach RTOON normally despite being
     * display-suppressed for text/HTML.
     */
    xo_open_container("s28-dispenc");
    xo_emit("{d:disp-only}\n", "should-not-appear");
    xo_emit("{e:enc-only}\n", "should-appear");
    xo_emit("{:normal}\n", "always-appears");
    xo_close_container("s28-dispenc");

    /*
     * 29. key-only row - no non-key fields at all - in both dense and
     * sparse form.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s29-keyonly-dense");
    xo_open_instance("s29-keyonly-dense");
    xo_emit("{k:name}\n", "solo");
    xo_close_instance("s29-keyonly-dense");
    xo_close_list("s29-keyonly-dense");

    xo_open_list("s29-keyonly-sparse");
    xo_open_instance("s29-keyonly-sparse");
    xo_emit("{k:name}\n", "solo");
    xo_close_instance("s29-keyonly-sparse");
    xo_close_list("s29-keyonly-sparse");

    /*
     * 30. sparse list where one instance emits zero fields at all
     * (not even a key) - rtoon-spec.md Section 4.1 calls this case
     * "not produced by libxo's own emit calls in practice" and
     * Section 10 resolves it as a hard invariant the spec relies on
     * (every instance has at least one field); enc_rtoon.c
     * nonetheless keeps this well formed defensively, rendering a
     * lone "-" for the empty instance rather than a dangling or
     * malformed line.
     */
    xo_open_list("s30-sparse-empty");
    xo_open_instance("s30-sparse-empty");
    xo_close_instance("s30-sparse-empty");
    xo_open_instance("s30-sparse-empty");
    xo_emit("{k:name}\n", "second");
    xo_close_instance("s30-sparse-empty");
    xo_close_list("s30-sparse-empty");

    /*
     * 31/32. dense-list instances that emit zero fields - the same
     * spec-declared "every instance has at least one field" invariant
     * as scenario 30, but on the *dense* side, which has no
     * per-instance "-" marker to fall back on the way sparse does.
     * The encoder currently emits a bare blank line for each such
     * instance (rtoon_flush_dense_row() unconditionally writes the
     * line prefix and a trailing newline even when the instance
     * contributed no cells and no fields-line has ever been
     * established).  Since this input is explicitly outside what the
     * spec guarantees, this documents current behavior rather than
     * asserting it's correct - worth a hand-inspection decision on
     * whether a stray blank line is acceptable, or whether dense mode
     * wants its own defensive fallback mirroring sparse's.
     *
     * 31: the first instance is empty; the second instance
     * establishes the fields-line - the blank line for instance 1
     * appears *before* any header.
     */
    xo_open_list_hf(NULL, XOF_DENSE, "s31-emptyfirst");
    xo_open_instance("s31-emptyfirst");
    xo_close_instance("s31-emptyfirst");
    xo_open_instance("s31-emptyfirst");
    xo_emit("{k:name}{:val/%d}\n", "later", 99);
    xo_close_instance("s31-emptyfirst");
    xo_close_list("s31-emptyfirst");

    /* 32: no instance in the list ever emits a single field. */
    xo_open_list_hf(NULL, XOF_DENSE, "s32-alwaysempty");
    xo_open_instance("s32-alwaysempty");
    xo_close_instance("s32-alwaysempty");
    xo_open_instance("s32-alwaysempty");
    xo_close_instance("s32-alwaysempty");
    xo_close_list("s32-alwaysempty");

    /*
     * 33. Escaping of embedded C0 control characters (added alongside
     * the fix that taught rtoon_write_escaped()/rtoon_value_needs_quote()
     * about them - previously only '"' and '\\' were escaped, so a raw
     * control byte in a value went out unescaped and could corrupt
     * rtoon's line-oriented structure). Covers the LF/CR/HTAB mnemonic
     * escapes, the \uXXXX fallback for a control byte with no mnemonic,
     * a control char combined with the pre-existing comma-quoting
     * trigger, the same path through a dense-list row cell, and a
     * container name (a "key") carrying a control character.
     */

    /* 33a. plain data-value: LF, CR, and HTAB together */
    xo_open_container("s33a-newlines");
    xo_emit("{:msg}\n", "line1\nline2\r\nline3\ttabbed");
    xo_close_container("s33a-newlines");

    /*
     * 33b: control bytes with no mnemonic escape - fall back to
     * \uXXXX. SOH (0x01) and US (0x1f) are used here rather than, say,
     * BEL (0x07) or ESC (0x1b), since those two are silently ignored
     * by every common terminal instead of ringing the bell or being
     * parsed as the start of an escape sequence when this test's
     * output is viewed directly (e.g. under "make test").
     */
    xo_open_container("s33b-nonmnemonic");
    xo_emit("{:msg}\n", "ctrl\x01here\x1fnext");
    xo_close_container("s33b-nonmnemonic");

    /*
     * 33c: leaf-list value combining an embedded newline with the
     * comma-quoting trigger already exercised in scenario 10, to
     * confirm the two quoting reasons compose correctly.
     */
    xo_open_container("s33c-colors");
    xo_emit("{l:tag}", "needs,quote\nand-newline");
    xo_close_container("s33c-colors");

    /* 33d: dense-list row cell (rtoon_write_cell_value's non-RAW path) */
    xo_open_list_hf(NULL, XOF_DENSE, "s33d-row");
    xo_open_instance("s33d-row");
    xo_emit("{k:num/%d}{:note}\n", 1, "tab\there");
    xo_close_instance("s33d-row");
    xo_close_list("s33d-row");

    /* 33e: a container name (key position) carrying an embedded tab */
    xo_open_container("s33e-odd\tname");
    xo_close_container("s33e-odd\tname");

    /*
     * 34. Escaping of non-ASCII UTF-8 (added alongside the fix that
     * taught rtoon_write_escaped()/rtoon_value_needs_quote()/
     * rtoon_key_needs_quote() to decode UTF-8: a BMP codepoint now
     * gets a \uXXXX escape rather than going out as literal UTF-8,
     * while a supplementary-plane codepoint (above U+FFFF, which has
     * no \uXXXX form a decoder can accept) stays literal.
     */

    /* 34a: a BMP non-ASCII character ('e' with acute accent, U+00E9) */
    xo_open_container("s34a-bmp");
    xo_emit("{:msg}\n", "caf\xc3\xa9");
    xo_close_container("s34a-bmp");

    /* 34b: a supplementary-plane character (U+1F600, four UTF-8 bytes) */
    xo_open_container("s34b-supplementary");
    xo_emit("{:msg}\n", "\xf0\x9f\x98\x80hi");
    xo_close_container("s34b-supplementary");

    /* 34c: a container name (key position) carrying a BMP character */
    xo_open_container("s34c-caf\xc3\xa9");
    xo_close_container("s34c-caf\xc3\xa9");

    xo_close_container("rtoon-test");

    xo_finish();
    return 0;
}
