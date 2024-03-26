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

#include <libxo/xo.h>
#include <libxo/xo_encoder.h>
#include <libxo/xo_buf.h>

#define LIBXO_NEED_FILTER

#include "xo_xpath.tab.h"
#include "xo_xparse.h"
#include "xo_filter.h"

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

int
main (int argc, char **argv)
{
    int i;
    const char *input = NULL;
    int debug = 0;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (i = 1; argv[i]; i++) {
	if (xo_streq(argv[i], "debug"))
	    debug = 1;
	else if (xo_streq(argv[i], "input"))
	    input = argv[++i];
	else if (xo_streq(argv[i], "yydebug"))
	    xo_xpath_yydebug = 1;
    }

    if (debug)
	xo_set_flags(NULL, XOF_DEBUG);

    xo_xparse_node_t *xnp UNUSED;

    xo_filter_t *xfp = xo_filter_create(NULL);
    if (xfp == NULL)
	xo_errx(1, "allocation of filter failed");

    xo_handle_t *xop = NULL;	/* Use default output handle */
    xo_filter_data_set(xop, xfp);
    xo_xparse_data_t *xdp = xo_filter_data(xop, xfp);

    xo_xparse_init(xdp);

    FILE *in = input ? fopen(input, "r") : stdin;
    if (in == NULL)
	xo_err(1, "could not open file '%s'", input);

    char *cp, buf[BUFSIZ];
    char *field, *value;
    int rc;

    for (rc = 0;; rc = 0) {
	cp = fgets(buf, sizeof(buf), in);
	if (cp == NULL)
	    break;

	cp = trim(cp);
	fprintf(stderr, "main: input '%s'\n", cp ?: "");

	switch (*cp) {
	case '#':
	case ' ':
	case '\0':
	    continue;

	case '?':
	    cp = trim(cp + 1);

	    int xof_debug = xo_isset_flags(xop, XOF_DEBUG);
	    if (debug)
		xo_set_flags(xop, XOF_DEBUG);

	    xo_filter_add(xop, cp);

	    int bad_horse[] = { C_DESCENDANT, 0 };

	    xo_xpath_feature_warn("test", xdp, bad_horse, "+");

	    if (!debug && !xof_debug) {
		xo_set_flags(xop, XOF_DEBUG);
		xo_xparse_dump(xdp);
	    }

	    if (debug || !xof_debug)
		xo_clear_flags(xop, XOF_DEBUG);
	    break;

	case '+':
	    rc = xo_open_container_h(xop, trim(cp + 1));
	    break;

	case '-':
	    rc = xo_close_container_h(xop, trim(cp + 1));
	    break;

	case '<':
	    rc = xo_open_instance_h(xop, trim(cp + 1));
	    break;

	case '>':
	    rc = xo_close_instance_h(xop, trim(cp + 1));
	    break;

	case '=':		/* Non-key field */
	    field = cp + 1;
	    value = clean_token(field);
	    if (!*field || !*value)
		break;

	    fprintf(stderr, "main: field: '%s'='%s'\n", field, value);
	    rc = xo_emit_field_h(xop, "", field, "%s", value);
	    break;

	case '$':
	    field = cp + 1;
	    value = clean_token(field);
	    if (!*field || !*value)
		break;

	    fprintf(stderr, "main: key: '%s'='%s'\n", field, value);
	    rc = xo_emit_field_h(xop, "k", field, "%s", value);
	    break;

	case 'r':		/* Reset */
	    /* Out with the old */
	    xo_filter_destroy(xop, xfp);

	    /* In with the new */
	    xfp = xo_filter_create(NULL);
	    if (xfp == NULL)
		xo_errx(1, "allocation of filter failed");

	    xo_filter_data_set(xop, xfp);
	    xdp = xo_filter_data(xop, xfp);

	    xo_xparse_init(xdp);

	    rc = 0;
	    break;

	default:
	    fprintf(stderr, "main: filter: invalid line '%s'\n", cp);
	    
	}

	if (rc != 0)
	    fprintf(stderr, "main: filter: rc: %d\n", rc);

	fprintf(stderr, "main: status: %s\n",
		xo_filter_status_name(xo_filter_get_status(xop, xfp)));

    }


    xo_finish_h(xop);
    xo_xparse_clean(xdp);

    return 0;
}
