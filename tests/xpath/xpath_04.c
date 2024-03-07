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
    const char *output = "xpath.out";

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (i = 1; i < argc && argv[i] && argv[i][0] == '-'; i++) {
	if (xo_streq(argv[i], "--debug"))
	    xo_set_flags(NULL, XOF_DEBUG);
	else if (xo_streq(argv[i], "--input"))
	    input = argv[++i];
	else if (xo_streq(argv[i], "--output"))
	    output = argv[++i];
	else if (xo_streq(argv[i], "--yydebug"))
	    xo_xpath_yydebug = 1;
    }

    if (i == 0)
	return 2;

    xo_set_flags(NULL, XOF_DEBUG);

    xo_xparse_node_t *xnp UNUSED;

    FILE *fp = fopen(output, "w+");
    if (fp == NULL)
	xo_errx(1, "open failed");

    xo_handle_t *xop = xo_create_to_file(fp, XO_STYLE_XML, XOF_PRETTY);
    if (xop == NULL)
	xo_errx(1, "create failed");

    xo_filter_t *xfp = xo_filter_create(NULL);
    if (xfp == NULL)
	xo_errx(1, "allocation of filter failed");

    xo_xparse_data_t *xdp = xo_filter_data(xop, xfp);

    xo_xparse_init(xdp);
    strncpy(xdp->xd_filename, "test", sizeof(xdp->xd_filename));

    FILE *in = input ? fopen(input, "r") : stdin;
    if (in == NULL)
	xo_err(1, "could not open file '%s'", input);

    char *cp, buf[BUFSIZ];
    char *key, *value;
    int rc;

    for (;;) {
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
	    xo_xparse_set_input(xdp, cp, strlen(cp));
	    xo_xpath_yyparse(xdp);
	    xo_xparse_dump(xdp);
	    break;

	case '+':
	    rc = xo_filter_open_container(xop, xfp, trim(cp + 1));
	    break;

	case '-':
	    rc = xo_filter_close_container(xop, xfp, trim(cp + 1));
	    break;

	case '=':
	    fprintf(stderr, "main: allow: %s\n",
		    xo_filter_allow(xop, xfp) ? "true" : "false");
	    break;

	case '$':
	    key = cp + 1;
	    value = clean_token(key);
	    fprintf(stderr, "main: key: '%s'='%s'\n", key, value);
	    rc = xo_filter_key(xop, xfp, key, strlen(key), value, strlen(value));
	    break;

	case 'r':		/* Reset */
	    /* Out with the old */
	    xo_filter_destroy(xop, xfp);

	    /* In with the new */
	    xfp = xo_filter_create(NULL);
	    if (xfp == NULL)
		xo_errx(1, "allocation of filter failed");

	    xdp = xo_filter_data(xop, xfp);

	    xo_xparse_init(xdp);
	    strncpy(xdp->xd_filename, "test", sizeof(xdp->xd_filename));
	    rc = -1;
	    break;

	default:
	    fprintf(stderr, "main: filter: invalid line '%s'\n", cp);
	    
	}

	if (rc >= 0)
	    fprintf(stderr, "main: filter: allow: %s\n", rc ? "true" : "false");
    }


    xo_xparse_clean(xdp);

    return 0;
}
