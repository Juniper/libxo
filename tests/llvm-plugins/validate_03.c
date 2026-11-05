/*
 * validate_03.c: test input for the xo_validate clang plugin, covering
 * format extensions and modifiers that the plugin once misparsed.
 *
 * "OK"   - no diagnostic expected
 * "WARN" - error or warning from the plugin expected
 */

#include <stdint.h>

#include <libxo/xo.h>

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    uint16_t u16 = 42;
    uint32_t u32 = 42;
    uint64_t u64 = 42;
    unsigned u_val = 42;
    const char *cs_val = "x";

    /*
     * "%!NN" gives the argument's size in bits
     */

    /* OK: 64-bit value */
    xo_emit("{:bits/%!64u}\n", u64);

    /* OK: 32-bit value */
    xo_emit("{:bits/%!32x}\n", u32);

    /* OK: 16-bit value (promoted to int) */
    xo_emit("{:bits/%!16d}\n", u16);

    /* WARN: string given for a 64-bit integer */
    xo_emit("{:bits/%!64u}\n", "wrong");

    /* OK: with flags and precision */
    xo_emit("{:code/%#.12!64x}\n", u64);

    /*
     * "%@*@" consumes and discards one int argument before the value
     */

    /* OK: int, then the string */
    xo_emit("{:name/%@*@s}\n", 1, cs_val);

    /* WARN: the value is an int, not a string */
    xo_emit("{:name/%@*@s}\n", 1, 5);

    /* WARN: expects 2 but 1 */
    xo_emit("{:name/%@*@s}\n", cs_val);

    /* OK: two discarded ints */
    xo_emit("{:name/%@**@s}\n", 1, 2, cs_val);

    /*
     * The "i" (int-group) modifier
     */

    /* OK: no width, so no leading zero */
    xo_emit("{i:count/%u}\n", u_val);

    /* OK: width without zero fill */
    xo_emit("{i:count/%10u}\n", u_val);

    /* WARN: zero fill */
    xo_emit("{i:count/%010u}\n", u_val);

    /* WARN: not an integer */
    xo_emit("{i:count/%s}\n", cs_val);

    return 0;
}
