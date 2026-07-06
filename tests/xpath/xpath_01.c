/*
 * Copyright (c) 2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/param.h>

#include "xo_config.h"

#include <libxo/xo.h>
#include <libxo/xo_encoder.h>
#include <libxo/xo_buf.h>

#define LIBXO_NEED_FILTER

#include "xo_xpath.tab.h"
#include "xo_xparse.h"
#include "xo_filter.h"

static int opt_debug;
static int opt_quiet;
static const char *opt_filter;

static char *
trim (char *cp)
{
    while (isspace(*cp))
	cp += 1;

    char *ep = cp + strlen(cp);

    for (ep--; ep > cp && isspace(*ep); ep--)
	continue;
    if (++ep >= cp && (*ep == '\n' || *ep == '\r'))
	*ep = '\0';

    return cp;
}

static char *
clean_token (char *cp)
{
    while (*cp && !isspace(*cp))
	cp += 1;

    if (*cp == '\0')
	return cp;

    *cp++ = '\0';

    while (isspace(*cp))
	cp += 1;

    char *ep = cp + strlen(cp);

    for (ep--; ep > cp && isspace(*ep); ep--)
	continue;
    if (++ep >= cp && (*ep == '\n' || *ep == '\r'))
	*ep = '\0';

    return cp;
}

static void
do_add_filter (xo_handle_t *xop, xo_xparse_data_t *xdp, const char *filter)
{
    int xof_debug = xo_isset_flags(xop, XOF_DEBUG);
    if (opt_debug)
	xo_set_flags(xop, XOF_DEBUG);

    fprintf(stderr, "adding filter: '%s'\n", filter);
    int rc = xo_add_filter(xop, filter);
    fprintf(stderr, "added filter: %d\n", rc);

    /*
     * We really _should_ fail here, but it's really wonderful
     * to see the dumps of the internal data structures, so we
     * ignore the return code an continue on.
     */

    int bad_horse[] = { C_DESCENDANT, 0 };

    xo_xpath_feature_warn("test", xdp, bad_horse, "+");

    if (!opt_quiet && !opt_debug && !xof_debug) {
	xo_set_flags(xop, XOF_DEBUG);
	xo_xparse_dump(xdp);
    }

    if (opt_debug || !xof_debug)
	xo_clear_flags(xop, XOF_DEBUG);
}

static void
do_work (xo_handle_t *xop, xo_filter_t *xfp, xo_xparse_data_t *xdp, FILE *in)
{
    char *cp, buf[BUFSIZ];
    char *field, *value;
    int rc;
    int done = FALSE;

    for (rc = 0; !done; rc = 0) {
	cp = fgets(buf, sizeof(buf), in);
	if (cp == NULL)
	    break;

	cp = trim(cp);
	if (!opt_quiet)
	    fprintf(stderr, "main: input '%s'\n", cp ?: "");

	switch (*cp) {
	case '#':
	case ' ':
	case '\0':
	    continue;

	case '?':
	    if (opt_filter)
		break;		/* Ignore if there's a command-line filter */

	    cp = trim(cp + 1);

	    do_add_filter(xop, xdp, cp);
	    break;

	case '+':
	    rc = xo_open_container_h(xop, trim(cp + 1));
	    break;

	case '-':
	    rc = xo_close_container_h(xop, trim(cp + 1));
	    break;

	case '{':
	    rc = xo_open_list_h(xop, trim(cp + 1));
	    break;

	case '}':
	    rc = xo_close_list_h(xop, trim(cp + 1));
	    break;

	case '<':
	    rc = xo_open_instance_h(xop, trim(cp + 1));
	    break;

	case '>':
	    rc = xo_close_instance_h(xop, trim(cp + 1));
	    break;

	case '[':
	    field = trim(cp + 1);
	    value = clean_token(field);
	    if (!*field || !*value)
		break;

	    if (!opt_quiet)
		fprintf(stderr, "main: field: '%s'='%s'\n", field, value);

	    rc = xo_emit_field_h(xop, "", field, "%s", value);
	    break;

	case '=':		/* Non-key field */
	    field = trim(cp + 1);
	    value = clean_token(field);
	    if (!*field || !*value)
		break;

	    if (!opt_quiet)
		fprintf(stderr, "main: field: '%s'='%s'\n", field, value);

	    rc = xo_emit_field_h(xop, "", field, "%s", value);
	    break;

	case '$':
	    field = trim(cp + 1);
	    value = clean_token(field);
	    if (!*field || !*value)
		break;

	    if (!opt_quiet)
		fprintf(stderr, "main: key: '%s'='%s'\n", field, value);

	    rc = xo_emit_field_h(xop, "k", field, "%s", value);
	    break;

	case '@':
	    field = trim(cp + 1);
	    value = clean_token(field);
	    if (!*field || !*value)
		break;

	    if (!opt_quiet)
		fprintf(stderr, "main: attr: '%s'='%s'\n", field, value);

	    rc = xo_attr_h(xop, field, "%s", value);
	    break;

	case 'i':		/* Include */
	    field = trim(cp + 1);
	    value = clean_token(field);

	    if (value == NULL) {
		fprintf(stderr, "main: include: missing filename\n");
		continue;
	    }

	    fprintf(stderr, "main: include: '%s'\n", value);
	    FILE *newp = fopen(value, "r");
	    if (newp == NULL) {
		fprintf(stderr, "main: include: could not open file '%s'\n",
			value);
		continue;
	    }

	    do_work(xop, xfp, xdp, newp);

	    fclose(newp);
	    break;

	case 'r':		/* Reset */
	    if (opt_filter)
		break;		/* Ignore if there's a command-line filter */

	    /* Out with the old */
	    xo_filter_destroy(xop, xfp);

	    /* In with the new */
	    xfp = xo_filter_create(NULL);
	    if (xfp == NULL)
		xo_errx(1, "allocation of filter failed");

	    xo_set_filter_data(xop, xfp);
	    xdp = xo_filter_xparse_data(xop, xfp);

	    xo_xparse_init(xdp);

	    rc = 0;
	    break;

	case 'q':
	    done = TRUE;
	    break;

	default:
	    fprintf(stderr, "main: filter: invalid line '%s'\n", cp);
	    
	}

	if (!opt_quiet && rc != 0)
	    fprintf(stderr, "main: filter: rc: %d\n", rc);

	if (!opt_quiet)
	    fprintf(stderr, "main: status: %s\n",
		    xo_filter_status_name(xo_filter_get_status(xop, xfp)));

    }
}

int
main (int argc, char **argv)
{
    int i;
    const char *opt_input = NULL;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (i = 1; argv[i]; i++) {
	if (xo_streq(argv[i], "debug"))
	    opt_debug = 1;
	else if (xo_streq(argv[i], "filter"))
	    opt_filter = argv[++i];
	else if (xo_streq(argv[i], "input"))
	    opt_input = argv[++i];
	else if (xo_streq(argv[i], "quiet"))
	    opt_quiet = 1;
	else if (xo_streq(argv[i], "yydebug"))
	    xo_xpath_yydebug = 1;
    }

    if (opt_debug)
	xo_set_flags(NULL, XOF_DEBUG);

    xo_xparse_node_t *xnp UNUSED;

    xo_filter_setup_test();

    xo_filter_t *xfp = xo_filter_create(NULL);
    if (xfp == NULL)
	xo_errx(1, "allocation of filter failed");

    xo_handle_t *xop = NULL;	/* Use default output handle */
    xo_set_filter_data(xop, xfp);
    xo_xparse_data_t *xdp = xo_filter_xparse_data(xop, xfp);

    xo_xparse_init(xdp);

    FILE *in = opt_input ? fopen(opt_input, "r") : stdin;
    if (in == NULL)
	xo_err(1, "could not open file '%s'", opt_input);

    if (opt_filter)
	do_add_filter(xop, xdp, opt_filter);

    do_work(xop, xfp, xdp, in);

    xo_finish_h(xop);
    xo_xparse_clean(xdp);

    return 0;
}
