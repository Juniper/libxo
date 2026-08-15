/*
 * Fine-grained text rendering micro-benchmark for libxo.
 *
 * Goal: decompose the 6.24 µs/iter text baseline into:
 *   - parse overhead (3 format-string scans per call)
 *   - per-field render cost
 *   - container open/close overhead
 *   - output/buffer flush cost
 *
 * Strategy: vary field count in format strings and measure the slope.
 *   Intercept ≈ parse overhead + dispatch overhead
 *   Slope     ≈ per-field render cost
 *
 * Run: ./test_bm.test [xml|json|html] [discard]
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>

#include "xo.h"
#include "xo_private.h"
#include "xo_encoder.h"

static int opt_count = 1000;

static uint64_t
now_ns (void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define N_REPS    5        /* best-of-N timing repetitions */
#define N_INNER   2000     /* calls per timing trial */

static xo_handle_t *g_xo;

#define NUM_OUT  10
#define SIZE_OUT 32

static char output[NUM_OUT][SIZE_OUT];
static int outnum;

static const char *
format_ns (uint64_t ns)
{
    if (outnum == NUM_OUT)
	outnum = 0;
    char *cp = output[outnum++];

    uint64_t zs = ns % 1000;
    uint64_t ms = ns / 1000;
    uint64_t ss = ms / 1000;
    uint64_t ds = ss / 1000;
    ms = ms % 1000;
    ss = ss % 1000;

    if (ds > 0)
	snprintf(cp, SIZE_OUT, "%" PRIu64 ",%03" PRIu64 ",%03"
		 PRIu64 ".%03" PRIu64,
		 ds, ss, ms, zs);
    else if (ss > 0)
	snprintf(cp, SIZE_OUT, "%" PRIu64 ",%03" PRIu64 ".%03" PRIu64,
		 ss, ms, zs);
    else
	snprintf(cp, SIZE_OUT, "%" PRIu64 ".%03" PRIu64,
		 ms, zs);
    return cp;
}

/* best-of-<N_REPS>, returns ns per call */
static uint64_t
time_fn (void (*fn)(int), int arg)
{
    uint64_t best = UINT64_MAX;
    for (int r = 0; r < N_REPS; r++) {
        uint64_t t0 = now_ns();
        fn(arg);
        uint64_t t1 = now_ns();
        uint64_t dt = t1 - t0;
        if (dt < best) best = dt;
    }
    return best;
}

/* ---------- workloads ---------- */

/* empty format string — absolute floor: xo_do_emit entry + exit */
static void
bench_empty (int n XO_UNUSED) {
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "");
}

/* pure literal, no fields — tests parse + output cost with 0 fields */
static void
bench_literal (int n XO_UNUSED) {
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "hello world\n");
}

/* single newline literal */
static void
bench_newline (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "\n");
}

/* one integer field */
static void
bench_1int (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "{:count/%d}\n", 42);
}

/* one string field */
static void
bench_1str (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "{:name/%s}\n", "gum");
}

/* two fields */
static void
bench_2f (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "{:name/%s} {:count/%d}\n", "gum", 42);
}

/* four fields (close to typical xo_emit call in test_01) */
static void
bench_4f (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo, "{:name/%s} {:sold/%d} {:instock/%d} {:sku/%s}\n",
                  "gum", 1412, 54, "GRO-415");
}

/* eight fields */
static void
bench_8f (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++)
        xo_emit_h(g_xo,
            "{:a/%s} {:b/%d} {:c/%d} {:d/%s} "
            "{:e/%s} {:f/%d} {:g/%d} {:h/%s}\n",
            "gum", 1412, 54, "GRO", "rope", 85, 4, "HRD");
}

/* container open+close — tests state machine / stack overhead */
static void
bench_container (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++) {
        xo_open_container_h(g_xo, "item");
        xo_close_container_h(g_xo, "item");
    }
}

/* open instance + 4-field emit + close instance */
static void
bench_instance4 (int n XO_UNUSED)
{
    for (int i = 0; i < N_INNER; i++) {
        xo_open_instance_h(g_xo, "item");
        xo_emit_h(g_xo, "{:name/%s} {:sold/%d} {:instock/%d} {:sku/%s}\n",
                  "gum", 1412, 54, "GRO-415");
        xo_close_instance_h(g_xo, "item");
    }
}

static void
bench_top10 (int n XO_UNUSED)
{
    static char base_grocery[] = "GRO";
    static char base_hardware[] = "HRD";
    static char base_other[] = "OTH";
    struct item {
	const char *i_title;
	int i_sold;
	int i_instock;
	int i_onorder;
	const char *i_sku_base;
	int i_sku_num;
    };
    struct item list[] = {
	{ "gum", 1412, 54, 10, base_grocery, 415 },
	{ "rope", 85, 4, 2, base_hardware, 212 },
	{ "ladder", 0, 2, 1, base_hardware, 517 },
	{ "bolt", 4123, 144, 42, base_hardware, 632 },
	{ "water", 17, 14, 2, base_grocery, 2331 },
	{ "fish food", 234, 12, 4, base_other, 432 },
	{ "coffee", 324, 21, 14, base_other, 423 },
	{ "macroons", 674, 102, 823, base_other, 342 },
	{ "fish", 1321, 45, 1, base_grocery, 533 },
	{ NULL, 0, 0, 0, NULL, 0 }
    };
    struct item *ip;
    xo_info_t info[] = {
	{ "in-stock", "number", "Number of items in stock" },
	{ "name", "string", "Name of the item" },
	{ "on-order", "number", "Number of items on order" },
	{ "sku", "string", "Stock Keeping Unit" },
	{ "sold", "number", "Number of items sold" },
	{ XO_INFO_NULL },
    };

    xo_set_info(g_xo, info, -1);
    xo_set_flags(g_xo, XOF_KEYS);

    for (int i = 0; i < 1 /* N_INNER */; i++) {
	xo_open_container_h(g_xo, "data");
	xo_open_list_h(g_xo, "item");

	xo_emit_h(g_xo, "{T:Item/%-10s}{T:Total Sold/%12s}{T:In Stock/%12s}"
		"{T:On Order/%12s}{T:SKU/%5s}\n");

	for (ip = list; ip->i_title; ip++) {
	    xo_open_instance_h(g_xo, "item");
	    xo_attr_h(g_xo, "test3", "value3");

	    xo_emit_h(g_xo, "{keq:sku/%s-%u/%s-000-%u}"
		    "{k:name/%-10s/%s}{n:sold/%12u/%u}{:in-stock/%12u/%u}"
		    "{:on-order/%12u/%u}{qkd:sku/%5s-000-%u/%s-000-%u}\n",
		    ip->i_sku_base, ip->i_sku_num,
		    ip->i_title, ip->i_sold, ip->i_instock, ip->i_onorder,
		    ip->i_sku_base, ip->i_sku_num);

	    xo_close_instance_h(g_xo, "item");
	}

	xo_close_list_h(g_xo, "item");
	xo_close_container_h(g_xo, "data");
	xo_emit_h(g_xo, "\n\n");
    }
}

typedef struct {
    const char *b_label;
    void (*b_fn)(int);
    int b_nfields;  /* number of value fields; -1 means skip field count */
    uint64_t b_ns_total;
    uint64_t b_ns_min;
    uint64_t b_ns_max;
} bench_t;

static bench_t benches[] = {
    { "empty",        bench_empty,     0, 0, 0, 0 },
    { "literal",      bench_literal,   0, 0, 0, 0 },
    { "newline",      bench_newline,   0, 0, 0, 0 },
    { "1-int",        bench_1int,      1, 0, 0, 0 },
    { "1-str",        bench_1str,      1, 0, 0, 0 },
    { "2-field",      bench_2f,        2, 0, 0, 0 },
    { "4-field",      bench_4f,        4, 0, 0, 0 },
    { "8-field",      bench_8f,        8, 0, 0, 0 },
    { "top-10",	      bench_top10,     6, 0, 0, 0 },
    { "container",    bench_container, -1, 0, 0, 0 },
    { "instance+4f",  bench_instance4, -1, 0, 0, 0 },
    { NULL,           NULL,         0, 0, 0, 0 }
};

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    fprintf(stderr, "PID %lu\n", (long unsigned) getpid());

    xo_set_program("test");

    g_xo = xo_create(XO_STYLE_TEXT, 0);
    xo_set_flags(g_xo, XOF_UTF8);

    for (int i = 1; argv[i]; i++) {
	const char *cp = argv[i];

        if (xo_streq(cp, "count")) {
	    opt_count = atoi(argv[++i]);

	} else if (xo_streq(cp, "discard")) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd > 0)
                xo_set_file_h(g_xo, fdopen(fd, "w"));

        } else if (xo_streq(cp, "html"))
            xo_set_style(g_xo, XO_STYLE_HTML);
	else if (xo_streq(cp, "info"))
	    xo_set_flags(NULL, XOF_INFO);
	else if (xo_streq(cp, "json"))
            xo_set_style(g_xo, XO_STYLE_JSON);
        else if (xo_streq(cp, "text"))
            xo_set_style(g_xo, XO_STYLE_TEXT);
        else if (xo_streq(cp, "xml")) {
            xo_set_style(g_xo, XO_STYLE_XML);
	} else
	    xo_err(1, "unknown option: '%s'", cp);
    }

    /* JSON/XML need open/close around everything */
    xo_open_container_h(g_xo, "benchmark");
    xo_open_container("benchmark");

    for (int i = 0; i < opt_count; i++) {
	for (bench_t *bp = benches; bp->b_label; bp++) {
	    uint64_t ns = time_fn(bp->b_fn, bp->b_nfields);

	    bp->b_ns_total += ns;
	    if (bp->b_ns_max < ns)
		bp->b_ns_max = ns;
	    if (bp->b_ns_min == 0 || bp->b_ns_min > ns)
		bp->b_ns_min = ns;
	}
    }

    xo_emit("total after {:count/%d} cycles (each {:reps/%d} * {:inner/%d}:\n",
	    opt_count, N_REPS, N_INNER);

    static const char hdr[] =
	"{T:/%-16s}  {T:/%-15s} {T:/%-15s} {T:/%-15s} {T:/%-15s}\n";
    xo_emit(hdr, "bench", "total", "ns/call", "min", "max");
    static const char dashes[] = "---------------";
    xo_emit(hdr, dashes, dashes, dashes, dashes, dashes);

    for (bench_t *bp = benches; bp->b_label; bp++) {
        xo_emit("{:label/%-16s}  {:total/%15s} {:per-call/%15s} "
		"{:min/%15s} {:max/%15s}\n",
		bp->b_label, format_ns(bp->b_ns_total),
		format_ns(bp->b_ns_total / opt_count),
		format_ns(bp->b_ns_min), format_ns(bp->b_ns_max));
    }

    xo_close_container_h(g_xo, "benchmark");
    xo_close_container("benchmark");
    xo_finish();
    xo_finish_h(g_xo);
    xo_destroy(g_xo);

    return 0;
}
