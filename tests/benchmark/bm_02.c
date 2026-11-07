/*
 * XPath parser micro-benchmark for libxo's filter library.
 *
 * Goal: measure the cost of xo_xparse_snap_stack, which makes the
 * parser's yylex() wrapper copy the parser state stack at every
 * token, so syntax errors can report table-independent "expected"
 * tokens.
 *
 * Strategy: parse every "?expr" line from the given input files
 * (such as tests/xpath/xpath_01_*.in) "iters" times with the snapshot
 * on, then again with it off, for five rounds, and report the
 * difference.
 *
 * Environment:
 *   GOOD_ONLY=1  drop the expressions that fail to parse, so the
 *                error-path simulation isn't included
 *   REUSE=1      reuse one xo_xparse_data_t per iteration, instead
 *                of creating one per parse, so the xd_stack buffer
 *                allocation isn't included
 *
 * Run: ./bm_02.test iters file ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/param.h>

#include "xo_config.h"
#include "xo.h"
#include "xo_buf.h"
#include "xo_xpath.tab.h"
#include "xo_xparse.h"

#define BM_ROUNDS 5		/* Number of on/off rounds */

static char **exprs;		/* Expressions to parse */
static int exprs_count;		/* Number of entries in exprs[] */
static int exprs_max;		/* Size of exprs[] */
static int errors_count;	/* Parse failures in the last run */

/*
 * Discard parse warnings; the error cases are expected to fail
 */
static void
bm_quiet_warn (void *data, const char *fmt, va_list vap)
{
    (void) data;
    (void) fmt;
    (void) vap;
}

static double
bm_now (void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static xo_xparse_data_t *
bm_create (void)
{
    xo_xparse_data_t *xdp = xo_xparse_create();

    if (xdp == NULL) {
	fprintf(stderr, "bm_02: out of memory\n");
	exit(1);
    }

    xo_xpath_set_warn_func(xdp, bm_quiet_warn, NULL);
    return xdp;
}

/*
 * Parse all expressions 'iters' times, with the snapshot on or off,
 * returning the elapsed time in seconds
 */
static double
bm_run (int snap, int iters, int reuse)
{
    xo_xparse_data_t *keep = NULL;

    xo_xparse_snap_stack = snap;
    errors_count = 0;

    double start = bm_now();

    for (int n = 0; n < iters; n++) {
	if (reuse) {
	    if (keep)
		xo_xparse_destroy(keep);
	    keep = bm_create();
	}

	for (int i = 0; i < exprs_count; i++) {
	    xo_xparse_data_t *xdp = reuse ? keep : bm_create();

	    if (xo_xparse_parse_string(NULL, xdp, exprs[i], M_PARSE_FULL))
		errors_count += 1;

	    if (!reuse)
		xo_xparse_destroy(xdp);
	}
    }

    double elapsed = bm_now() - start;

    if (keep)
	xo_xparse_destroy(keep);

    return elapsed;
}

/*
 * Read the "?expr" lines from an xpath_01 input file
 */
static void
bm_read_file (const char *filename)
{
    char line[BUFSIZ];
    FILE *fp = fopen(filename, "r");

    if (fp == NULL) {
	fprintf(stderr, "bm_02: %s: cannot open\n", filename);
	return;
    }

    while (fgets(line, sizeof(line), fp)) {
	if (line[0] != '?')
	    continue;

	line[strcspn(line, "\n")] = '\0';

	if (exprs_count >= exprs_max) {
	    exprs_max = exprs_max ? exprs_max * 2 : 256;
	    exprs = realloc(exprs, exprs_max * sizeof(*exprs));
	    if (exprs == NULL) {
		fprintf(stderr, "bm_02: out of memory\n");
		exit(1);
	    }
	}

	exprs[exprs_count++] = strdup(line + 1);
    }

    fclose(fp);
}

/*
 * Drop the expressions that fail to parse
 */
static void
bm_good_only (void)
{
    int i, j = 0;

    for (i = 0; i < exprs_count; i++) {
	xo_xparse_data_t *xdp = bm_create();

	if (xo_xparse_parse_string(NULL, xdp, exprs[i], M_PARSE_FULL) == 0)
	    exprs[j++] = exprs[i];
	else
	    free(exprs[i]);

	xo_xparse_destroy(xdp);
    }

    exprs_count = j;
}

int
main (int argc, char **argv)
{
    if (argc < 3) {
	fprintf(stderr, "usage: bm_02 iters file ...\n");
	return 1;
    }

    int iters = atoi(argv[1]);
    int reuse = (getenv("REUSE") != NULL);

    if (iters <= 0) {
	fprintf(stderr, "bm_02: invalid iteration count: %s\n", argv[1]);
	return 1;
    }

    for (int a = 2; a < argc; a++)
	bm_read_file(argv[a]);

    if (getenv("GOOD_ONLY"))
	bm_good_only();

    if (exprs_count == 0) {
	fprintf(stderr, "bm_02: no expressions found\n");
	return 1;
    }

    bm_run(1, 1, reuse);	/* Warm up */
    printf("%d expressions, %d iterations%s\n", exprs_count, iters,
	   reuse ? ", reused xparse data" : "");

    double on = 0, off = 0;

    for (int r = 0; r < BM_ROUNDS; r++) {
	double t_on = bm_run(1, iters, reuse);
	int e_on = errors_count;
	double t_off = bm_run(0, iters, reuse);

	printf("round %d: snap on %.3fs, off %.3fs (%+.1f%%), errors %d/%d\n",
	       r, t_on, t_off, (t_on - t_off) / t_off * 100,
	       e_on / iters, errors_count / iters);
	on += t_on;
	off += t_off;
    }

    printf("total: on %.3fs, off %.3fs, overhead %+.1f%%, %.0f ns/parse\n",
	   on, off, (on - off) / off * 100,
	   (on - off) / ((double) BM_ROUNDS * iters * exprs_count) * 1e9);

    return 0;
}
