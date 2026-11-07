/*
 * validate_05.c: test input for the xo_validate clang plugin, covering
 * the "B" (boolean) field role.
 *
 * "OK"   - no diagnostic expected
 * "WARN" - error or warning from the plugin expected
 */

#include <stdbool.h>
#include <stdint.h>

#include <libxo/xo.h>

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    int i_val = 1;
    unsigned u_val = 1;
    uint64_t u64 = 1;
    bool b_val = true;
    double d_val = 1.0;
    void *p_val = &i_val;
    const char *cs_val = "x";

    /*
     * With no format, a B field takes a string
     */

    /* OK: a string */
    xo_emit("{B:flag}\n", cs_val);

    /* OK: NULL is a false string */
    xo_emit("{B:flag}\n", (const char *) 0);

    /* WARN: an int given to a field with no format */
    xo_emit("{B:flag}\n", i_val);

    /* WARN: a bool given to a field with no format */
    xo_emit("{B:flag}\n", b_val);

    /* WARN: no argument at all */
    xo_emit("{B:flag}\n");

    /*
     * The conversion spec sets the type
     */

    /* OK: int */
    xo_emit("{B:flag/%d}\n", i_val);

    /* OK: bool promotes to int */
    xo_emit("{B:flag/%d}\n", b_val);

    /* OK: words and an unsigned */
    xo_emit("{B:flag/on+off%u}\n", u_val);

    /* OK: a 64-bit value */
    xo_emit("{B:flag/on+off%!64u}\n", u64);

    /* OK: a double */
    xo_emit("{B:flag/%f}\n", d_val);

    /* OK: a pointer */
    xo_emit("{B:flag/%p}\n", p_val);

    /* WARN: a string given for "%d" */
    xo_emit("{B:flag/on+off%d}\n", cs_val);

    /* WARN: an int given for "%p" */
    xo_emit("{B:flag/%p}\n", i_val);

    /* OK: words with no spec take a string */
    xo_emit("{B:flag/on+off}\n", cs_val);

    /* WARN: words with no spec given an int */
    xo_emit("{B:flag/on+off}\n", i_val);

    /*
     * A "*" boolean pair is a string argument, before the value
     */

    /* OK: pair, then value */
    xo_emit("{B:flag/*%d}\n", "yes+no", i_val);

    /* WARN: a NULL pair is a runtime failure */
    xo_emit("{B:flag/*%d}\n", NULL, i_val);

    /* WARN: the pair is missing */
    xo_emit("{B:flag/*%d}\n", i_val);

    /* WARN: the pair and value are swapped */
    xo_emit("{B:flag/*%d}\n", i_val, "yes+no");

    /*
     * Star ints come before the value
     */

    /* OK: width and precision, then the string */
    xo_emit("{B:flag/%*.*s}\n", 1, 2, cs_val);

    /* OK: "%@*@" int, then the value */
    xo_emit("{B:flag/%@*@d}\n", 1, i_val);

    /* OK: pair, star, value */
    xo_emit("{B:flag/*%*d}\n", "yes+no", 3, i_val);

    /* WARN: the star int is missing */
    xo_emit("{B:flag/%*d}\n", i_val);

    /*
     * The 'a' modifier takes the name first, then the value
     */

    /* OK: name, then value */
    xo_emit("{Ba:/%d}\n", "flag", i_val);

    /*
     * Encoding formats must match the display format's arguments;
     * the encoding format adds no arguments of its own
     */

    /* OK: matching formats */
    xo_emit("{B,full-boolean:flag/T+F%d/1+0%d}\n", i_val);

    /* OK: matching "*" pairs, one pair argument */
    xo_emit("{B,full-boolean:flag/*%d/*%d}\n", "yes+no", i_val);

    /* WARN: the encoding format takes a different type */
    xo_emit("{B,full-boolean:flag/T+F%d/1+0%p}\n", i_val);

    /* WARN: the encoding format has no boolean pair */
    xo_emit("{B,full-boolean:flag/T+F%d/%d}\n", i_val);

    /*
     * Parse errors
     */

    /* WARN: no '+' in the boolean pair */
    xo_emit("{B:flag/on%d}\n", i_val);

    /* WARN: more than one conversion spec */
    xo_emit("{B:flag/%d%d}\n", i_val, i_val);

    /* WARN: text after the conversion spec */
    xo_emit("{B:flag/%d units}\n", i_val);

    /* WARN: full-boolean on a value field */
    xo_emit("{V,full-boolean:flag}\n", cs_val);

    /* WARN: no name */
    xo_emit("{B:/%d}\n", i_val);

    /*
     * Lint warnings
     */

    /* WARN: humanize makes no sense here */
    xo_emit("{Bh:flag/%d}\n", i_val);

    /* WARN: the encoding format is ignored without full-boolean */
    xo_emit("{B:flag/T+F%d/1+0%d}\n", i_val);

    /* WARN: full-boolean with no boolean pair */
    xo_emit("{B,full-boolean:flag/%d}\n", i_val);

    /* WARN: an empty word */
    xo_emit("{B:flag/+off%d}\n", i_val);

    xo_finish();
    return 0;
}
