/*
 * validate_04.cc: C++ test input for the xo_validate clang plugin.
 *
 * The plugin looks at every direct call, and C++ calls include
 * operators, conversions, and constructors, whose names are not
 * simple identifiers.  An assertion-enabled clang aborted when the
 * plugin asked for their names.  A release clang doesn't assert, so
 * this test also checks that xo_emit calls in C++ are still validated.
 *
 * "OK"   - no diagnostic expected
 * "WARN" - error or warning from the plugin expected
 */

#include <libxo/xo.h>

struct Value {
    int number;

    Value (int n) : number(n) { }

    operator int () const { return number; }
};

static int
operator+ (Value left, Value right)
{
    return left.number + right.number;
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    /* OK: an operator call, a constructor, and a conversion */
    int sum = Value(1) + Value(2);
    int conv = Value(3);

    /* OK */
    xo_emit("{:sum/%d} {:conv/%d}\n", sum, conv);

    /* WARN: format specifies type 'int' but the argument has type 'const char *' */
    xo_emit("{:sum/%d}\n", "three");

    /* WARN: expects 2 but 1 */
    xo_emit("{:sum/%d} {:conv/%d}\n", sum);

    return 0;
}
