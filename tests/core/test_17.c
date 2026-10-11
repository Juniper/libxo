/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Regression tests for faults in the core library that had no test:
 * each one sits in its own container so the output can be reviewed
 * one case at a time.  Most of them were memory faults, so this test
 * is worth the most when run under a checker ("make valgrind").
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include "xo.h"
#include "xo_encoder.h"

/*
 * The anchor's width format makes far more output than xo_data
 * holds, so the buffer moves while the width is being formatted.
 * This has to be the first thing emitted, since the buffer only
 * has to grow once.
 */
static void
test_anchor_width (void)
{
    xo_open_container("anchor-width");

    xo_emit("[{[:/%20000d}{:right/%s}{]:}]\n", 12, "abc");
    xo_emit("[{[:/%20000d}{:left/%s}{]:}]\n", -12, "abc");

    xo_close_container("anchor-width");
}

/* More mappings than the map's first allocation holds */
static void
test_map_growth (void)
{
    char from[16], to[16];
    int i;

    for (i = 0; i < 40; i++) {
	snprintf(from, sizeof(from), "src-%d", i);
	snprintf(to, sizeof(to), "mapped-%d", i);
	xo_map_add(NULL, from, strlen(from), to, strlen(to));
    }

    xo_open_container("map-growth");

    xo_emit("{:src-0/%d} {:src-15/%d} {:src-16/%d} {:src-17/%d} "
	    "{:src-39/%d} {:src-40/%d}\n", 0, 15, 16, 17, 39, 40);

    xo_close_container("map-growth");
}

/*
 * A name that is the start of another is a name of its own: "pre"
 * is neither the mapped "prefix" nor the excused "error", and adding
 * a map for "pre" must not replace the one for "prefix".
 */
static void
test_prefix_names (void)
{
    xo_handle_t *xop = xo_create(XO_STYLE_XML, XOF_WARN | XOF_PRETTY);
    if (xop == NULL)
	return;

    xo_map_add(xop, "prefix", 6, "long-name", 9);

    xo_open_container_h(xop, "prefix-names");
    xo_emit_h(xop, "{:prefix/%d}{:pre/%d}{:pref/%d}", 1, 2, 3);
    xo_close_container_h(xop, "prefix-names");

    xo_map_add(xop, "pre", 3, "short-name", 10);

    xo_open_container_h(xop, "prefix-added");
    xo_emit_h(xop, "{:prefix/%d}{:pre/%d}", 4, 5);
    xo_close_container_h(xop, "prefix-added");

    /* The first two warn once each; only the last is an excused name */
    xo_open_container_h(xop, "siblings");
    xo_emit_h(xop, "{:err/%d}{:err/%d}", 1, 2);
    xo_emit_h(xop, "{:mes/%d}{:mes/%d}", 1, 2);
    xo_emit_h(xop, "{:error/%d}{:error/%d}", 1, 2);
    xo_close_container_h(xop, "siblings");

    xo_finish_h(xop);
    xo_destroy(xop);
}

/* Names on either side of the size of a stack frame's own name buffer */
static void
test_long_names (void)
{
    static const int lengths[] = { 62, 63, 64, 65, 130 };
    char name[160];
    unsigned i;

    xo_open_container("long-names");

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
	memset(name, 'a' + i, lengths[i]);
	name[lengths[i]] = '\0';

	xo_open_container(name);
	xo_emit("{:length/%d}\n", lengths[i]);
	xo_close_container(name);
    }

    xo_close_container("long-names");
}

/* Control characters before, between and after the escaped ones */
static void
test_control_chars (void)
{
    xo_open_container("control-chars");

    xo_emit("{:interior}\n", "a\001b\002c");
    xo_emit("{:trailing}\n", "a\001b\002");
    xo_emit("{:before-escape}\n", "a\001b<c\002d&e");
    xo_emit("{:after-escape}\n", "a<b\001c&d\002");
    xo_emit("{:only}\n", "\001\002\003");

    xo_close_container("control-chars");
}

/* Option words that are accepted and ignored must not draw a warning */
static void
test_deprecated_options (void)
{
    xo_open_container("deprecated-options");

    xo_emit("{:retain/%d}\n", xo_set_options(NULL, "retain"));
    xo_emit("{:no-retain/%d}\n", xo_set_options(NULL, "no-retain"));

    xo_close_container("deprecated-options");
}

static int
test_encoder_handler (XO_ENCODER_HANDLER_ARGS)
{
    return 0;
}

/* The op name tables at their last entry and beyond it */
static void
test_encoder_names (void)
{
    xo_open_container("encoder-names");

    xo_emit("{:first}\n", xo_encoder_op_name(0));
    xo_emit("{:last}\n", xo_encoder_op_name(XO_OP_DEADEND));
    xo_emit("{:beyond}\n", xo_encoder_op_name(XO_OP_DEADEND + 1));
    xo_emit("{:far-beyond}\n", xo_encoder_op_name(1000));

    xo_emit("{:wb-last}\n", xo_whiteboard_op_name(5));
    xo_emit("{:wb-beyond}\n", xo_whiteboard_op_name(6));

    /* A registered encoder has no library; removing it must not close one */
    xo_encoder_register("test-17", test_encoder_handler);
    xo_encoder_register("test-17", test_encoder_handler);
    xo_encoder_unregister("test-17");
    xo_encoder_unregister("test-17");
    xo_encoder_register("test-17", test_encoder_handler);
    xo_emit("{:registered}\n", "done");

    xo_close_container("encoder-names");
}

/*
 * The structured-data style writes nothing for containers, lists
 * and instances, but each close still has to match its open.
 */
static void
test_sdparams (void)
{
    static const char *names[] = { "gum", "rope", "ladder" };
    unsigned i;

    xo_handle_t *xop = xo_create(XO_STYLE_SDPARAMS, XOF_WARN);
    if (xop == NULL)
	return;

    xo_open_container_h(xop, "top");
    xo_open_container_h(xop, "data");
    xo_emit_h(xop, "{:before/%s}", "one");

    xo_open_list_h(xop, "item");
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
	xo_open_instance_h(xop, "item");
	xo_emit_h(xop, "{k:name}{:count/%u}", names[i], i);
	xo_close_instance_h(xop, "item");
    }
    xo_close_list_h(xop, "item");

    xo_emit_h(xop, "{:after/%s}", "two");
    xo_close_container_h(xop, "data");
    xo_close_container_h(xop, "top");

    /* The stack is empty again, so this is the one complaint we expect */
    xo_close_container_h(xop, "extra");

    xo_finish_h(xop);
    xo_destroy(xop);

    printf("\n");
}

/*
 * Opens that arrive while a list or a leaf-list is still open close
 * it on their own, and the close has to be of the same kind as the
 * open.  The encoder output is the one that shows which was used.
 */
static void
test_implied_close (void)
{
    xo_open_container("implied-close");

    xo_open_list("list-then-container");
    xo_open_container("after-list");
    xo_emit("{:one/%d}\n", 1);
    xo_close_container("after-list");

    xo_emit("{l:leaf-then-container/%d}\n", 2);
    xo_open_container("after-leaf");
    xo_emit("{:two/%d}\n", 2);
    xo_close_container("after-leaf");

    xo_emit("{l:leaf-then-leaf/%d}\n", 3);
    xo_emit("{l:second-leaf/%d}\n", 4);

    xo_open_list("list-after-leaf");
    xo_open_list("list-after-list");

    xo_close_container("implied-close");
}

/*
 * A marker keeps what is beneath it from being closed.  When that is
 * a list, an open that is not an instance has nowhere to go: it is
 * discarded, with all that is inside it, and its closes are matched
 * so the stack comes out even.  A leaf-list is never the caller's,
 * so it is closed even with a marker on top of it.
 */
static void
test_markers (void)
{
    xo_open_container("markers");

    /* A container in a list is discarded, and what follows is not */
    xo_open_container("list-container");
    xo_open_list("item");
    xo_open_marker("m1");
    xo_open_container("lost");
    xo_open_list("inner");
    xo_open_instance("inner");
    xo_emit("{k:name}{:value/%d}{l:tags}\n", "lost", 1, "lost");
    xo_open_marker("m2");
    xo_open_container("deeper");
    xo_close_marker("m2");
    xo_close_instance("inner");
    xo_close_list("inner");
    xo_close_container("lost");
    xo_open_instance("item");
    xo_emit("{:kept/%d}\n", 1);
    xo_close_instance("item");
    xo_close_marker("m1");
    xo_close_list("item");
    xo_emit("{:after/%d}\n", 2);
    xo_close_container("list-container");

    /* A list in a list is discarded, as is the instance meant for it */
    xo_open_container("list-list");
    xo_open_list("item");
    xo_open_marker("m1");
    xo_open_list("lost");
    xo_open_instance("lost");
    xo_emit("{:value/%d}\n", 1);
    xo_close_instance("lost");
    xo_close_list("lost");
    xo_close_marker("m1");
    xo_emit("{:after/%d}\n", 2);
    xo_close_container("list-list");

    /* A refused field leaves the list able to close afterward */
    xo_open_container("list-field");
    xo_open_list("item");
    xo_open_marker("m1");
    xo_emit("{:lost/%d}\n", 1);
    xo_close_marker("m1");
    xo_emit("{:after/%d}\n", 2);
    xo_close_container("list-field");

    /* A close that matches nothing discarded is refused */
    xo_open_container("stray-close");
    xo_open_list("item");
    xo_open_marker("m1");
    xo_open_container("lost");
    xo_close_container("stray");
    xo_close_container("lost");
    xo_close_marker("m1");
    xo_emit("{:after/%d}\n", 2);
    xo_close_container("stray-close");

    /* A leaf-list beneath a marker closes for whatever comes next */
    xo_open_container("leaf-container");
    xo_emit("{l:leaf/%d}\n", 1);
    xo_open_marker("m1");
    xo_open_container("kept");
    xo_emit("{:value/%d}\n", 2);
    xo_close_container("kept");
    xo_close_marker("m1");
    xo_emit("{:after/%d}\n", 3);
    xo_close_container("leaf-container");

    xo_open_container("leaf-leaf");
    xo_emit("{l:leaf/%d}\n", 1);
    xo_open_marker("m1");
    xo_open_marker("m2");
    xo_emit("{l:other/%d}\n", 2);
    xo_emit("{l:other/%d}\n", 3);
    xo_close_marker("m2");
    xo_close_marker("m1");
    xo_emit("{:after/%d}\n", 4);
    xo_close_container("leaf-leaf");

    xo_open_container("leaf-field");
    xo_emit("{l:leaf/%d}\n", 1);
    xo_open_marker("m1");
    xo_emit("{:kept/%d}\n", 2);
    xo_close_marker("m1");
    xo_emit("{:after/%d}\n", 3);
    xo_close_container("leaf-field");

    /* The same leaf-list carries on across a marker */
    xo_open_container("leaf-same");
    xo_emit("{l:leaf/%d}\n", 1);
    xo_open_marker("m1");
    xo_emit("{l:leaf/%d}\n", 2);
    xo_close_marker("m1");
    xo_emit("{l:leaf/%d}\n", 3);
    xo_close_container("leaf-same");

    /* An instance after a leaf-list gets a list of its own */
    xo_open_container("leaf-instance");
    xo_emit("{l:leaf/%d}\n", 1);
    xo_open_instance("item");
    xo_emit("{:value/%d}\n", 2);
    xo_close_instance("item");
    xo_emit("{:after/%d}\n", 3);
    xo_close_container("leaf-instance");

    xo_close_container("markers");
}

/* Leave something other than zeros where the next call's locals go */
static void
test_dirty_stack (void)
{
    volatile char junk[8192];
    size_t i;

    for (i = 0; i < sizeof(junk); i++)
	junk[i] = 'V';
}

/*
 * xo_emit_field() makes one field, and the gettext code that a "G"
 * role calls on walks the fields until it finds the end of them.
 */
static void
test_emit_field (void)
{
    xo_open_container("emit-field");

    test_dirty_stack();
    xo_emit_field("G", "test-domain", NULL, NULL);
    xo_emit_field("V", "after-domain", "%d", NULL, 1);
    xo_emit_field("L", "label", NULL, NULL);
    xo_emit_field("Vq", "quoted", "%s", NULL, "value");
    xo_emit("\n");

    xo_close_container("emit-field");
}

/* Count what a handle writes, since the output is too long to show */
static xo_ssize_t
test_count_writer (void *opaque, const char *data)
{
    size_t *countp = opaque;

    *countp += strlen(data);
    return strlen(data);
}

/* Attribute names and values are escaped; a missing name is refused */
static void
test_attributes (void)
{
    xo_open_container("attributes");

    xo_attr("quoted", "%s", "say \"hi\" & <go>");
    xo_attr("bad name<&\"", "%s", "value");
    xo_attr(NULL, "%s", "no-name");
    xo_attr("", "%s", "empty-name");
    xo_emit("{:escaped/%d}\n", 1);

    xo_close_container("attributes");
}

/*
 * A value that grows when escaped needs room for all of it.  These
 * use a new handle so the buffers are still at their first size: a
 * string of 2500 "<" fits, and so does its growth, but not the two
 * together.
 */
static void
test_long_escapes (void)
{
    char big[2501];
    size_t count = 0;

    xo_handle_t *xop = xo_create(XO_STYLE_XML, 0);
    if (xop == NULL)
	return;

    xo_set_writer(xop, &count, test_count_writer, NULL, NULL);

    memset(big, '<', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    xo_open_container_h(xop, "top");
    xo_emit_h(xop, "{:value}", big);
    xo_attr_h(xop, "long", "%s", big);
    xo_emit_h(xop, "{:with-attribute/%d}", 1);
    xo_close_container_h(xop, "top");

    xo_finish_h(xop);
    xo_destroy(xop);

    printf("long values wrote %zu bytes\n", count);
}

/*
 * The stack moves when it grows past its first size, and each frame
 * that keeps its name inside itself has to move its name with it.
 * Then a handle is destroyed with frames still open, where there are
 * names too long to live in the frame, keys and leaf-list members to
 * be released.
 */
static void
test_deep_stack (void)
{
    char name[100];
    size_t count = 0;
    int i;

    xo_handle_t *xop = xo_create(XO_STYLE_JSON, XOF_WARN);
    if (xop == NULL)
	return;

    xo_set_writer(xop, &count, test_count_writer, NULL, NULL);

    for (i = 0; i < 600; i++) {
	snprintf(name, sizeof(name), "level-%d", i);
	xo_open_container_h(xop, name);
    }

    xo_emit_h(xop, "{:bottom/%d}", 1);

    for (i = 600 - 1; i >= 0; i--) {
	snprintf(name, sizeof(name), "level-%d", i);
	xo_close_container_h(xop, name);
    }

    xo_finish_h(xop);
    xo_destroy(xop);

    printf("deep stack wrote %zu bytes\n", count);

    count = 0;
    xop = xo_create(XO_STYLE_JSON, XOF_WARN);
    if (xop == NULL)
	return;

    xo_set_writer(xop, &count, test_count_writer, NULL, NULL);

    /* Depth is relative: this one would put us above the top */
    xo_set_depth(xop, -3);

    xo_set_depth(xop, 700);
    xo_set_depth(xop, -700);

    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    xo_open_container_h(xop, name);
    xo_open_list_h(xop, "item");
    xo_open_instance_h(xop, "item");
    xo_emit_h(xop, "{k:name}{k:other}{l:member}{l:member}", "one", "two",
	      "three", "four");

    xo_destroy(xop);

    printf("open frames wrote %zu bytes\n", count);
}

/* Keep the two ends of what a handle writes, and count the rest */
typedef struct test_ends_s {
    size_t te_count;		/* Number of bytes written */
    char te_head[24];		/* First bytes written */
    char te_tail[40];		/* Last bytes of the last write */
} test_ends_t;

static xo_ssize_t
test_ends_writer (void *opaque, const char *data)
{
    test_ends_t *tep = opaque;
    size_t len = strlen(data);

    if (tep->te_count == 0)
	snprintf(tep->te_head, sizeof(tep->te_head), "%s", data);

    size_t off = (len < sizeof(tep->te_tail)) ? 0
	: len - (sizeof(tep->te_tail) - 1);
    snprintf(tep->te_tail, sizeof(tep->te_tail), "%s", data + off);

    /* Leave the trailing newline out of what we show */
    char *cp = strchr(tep->te_tail, '\n');
    if (cp)
	*cp = '\0';

    tep->te_count += len;
    return len;
}

/*
 * A warning or message that does not fit the buffer is formatted a
 * second time; it must come out the same as one that fit, with the
 * program name in front and the error string behind.
 */
static void
test_long_messages (void)
{
    static int sizes[] = { 10, 1000, 1015, 1030, 8170, 9000, 0 };
    char big[9001];
    test_ends_t ends;
    xo_handle_t *xop;
    int i;

    for (i = 0; sizes[i]; i++) {
	memset(big, 'x', sizes[i]);
	big[sizes[i]] = '\0';

	bzero(&ends, sizeof(ends));
	xop = xo_create(XO_STYLE_XML, XOF_WARN | XOF_WARN_XML);
	if (xop == NULL)
	    return;

	xo_set_writer(xop, &ends, test_ends_writer, NULL, NULL);
	xo_warn_hc(xop, -1, "%s<%d>", big, sizes[i]);
	xo_destroy(xop);

	printf("warning %d: %zu bytes, '%s' ... '%s'\n",
	       sizes[i], ends.te_count, ends.te_head, ends.te_tail);

	bzero(&ends, sizeof(ends));
	xop = xo_create(XO_STYLE_XML, 0);
	if (xop == NULL)
	    return;

	xo_set_writer(xop, &ends, test_ends_writer, NULL, NULL);
	xo_message_hc(xop, ENOENT, "%s<%d>", big, sizes[i]);
	xo_destroy(xop);

	printf("xml message %d: %zu bytes, '%s' ... '%s'\n",
	       sizes[i], ends.te_count, ends.te_head, ends.te_tail);

	bzero(&ends, sizeof(ends));
	xop = xo_create(XO_STYLE_HTML, 0);
	if (xop == NULL)
	    return;

	xo_set_writer(xop, &ends, test_ends_writer, NULL, NULL);
	xo_message_hc(xop, ENOENT, "%s<%d>", big, sizes[i]);
	xo_flush_h(xop);
	xo_destroy(xop);

	printf("html message %d: %zu bytes, '%s' ... '%s'\n",
	       sizes[i], ends.te_count, ends.te_head, ends.te_tail);
    }
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

/* The messages are too long to show whole; the length and the ends do */
static void
test_syslog_send (const char *full_msg, const char *v0_hdr,
		  const char *text_only)
{
    size_t flen = strlen(full_msg), tlen = strlen(text_only);

    printf("syslog send: full %zu, header %s, text %zu\n",
	   flen, v0_hdr ? "yes" : "no", tlen);
    printf("  full ends '%s'\n", full_msg + (flen > 24 ? flen - 24 : 0));
    printf("  text ends '%s'\n", text_only + (tlen > 24 ? tlen - 24 : 0));
}

/* Messages that come close to, fill and overflow the syslog buffer */
static void
test_syslog_long (void)
{
    static const int lengths[] = { 100, 1960, 1980, 2040, 2048, 4000 };
    char msg[4096];
    unsigned i;

    xo_set_syslog_handler(test_syslog_open, test_syslog_send,
			  test_syslog_close);
    xo_set_unit_test_mode(1);
    xo_open_log("test-program", 0, 0);
    xo_set_syslog_enterprise_id(42);

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
	memset(msg, 'x', lengths[i]);
	memcpy(msg + lengths[i] - 4, "tail", 5);

	fflush(stdout);
	xo_syslog(LOG_INFO | LOG_KERN, "long-message", "{:msg/%s}", msg);
    }

    fflush(stdout);
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("top");

    test_anchor_width();
    test_map_growth();
    test_long_names();
    test_control_chars();
    test_deprecated_options();
    test_encoder_names();
    test_attributes();
    test_implied_close();
    test_markers();
    test_emit_field();

    xo_close_container("top");
    xo_finish();

    /* These write on their own, so they come after the document */
    test_sdparams();
    test_long_escapes();
    test_long_messages();
    test_deep_stack();
    test_prefix_names();
    test_syslog_long();

    return 0;
}
