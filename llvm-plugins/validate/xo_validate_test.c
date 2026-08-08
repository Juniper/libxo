/*
 * xo_validate_test.c: test input for the xo_validate clang plugin.
 *
 * Each test function is annotated with the expected diagnostic.
 * Run via:  make test-validate  (in the top build directory)
 */

#include <stdio.h>
#include <stdint.h>

#include <libxo/xo.h>

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	exit(1);

    /* Original five checks */

    /* test_syntax_ok: OK */
    xo_emit("{:name/%s}\n", "alice");

    /* test_syntax_missing_brace: WARN: missing closing */
    xo_emit("{:bad/\n", "oops");

    /* test_count_ok: OK */
    xo_emit("{:name/%s} {:age/%d}\n", "bob", 42);

    /* test_count_too_few: WARN: expects 2 but 1 */
    xo_emit("{:name/%s} {:age/%d}\n", "bob");

    /* test_count_too_many: WARN: expects 1 but 2 */
    xo_emit("{:name/%s}\n", "carol", 99);

    /* test_type_int_ok: OK */
    xo_emit("{:count/%d}\n", 5);

    /* test_type_int_string: WARN: type mismatch integer */
    xo_emit("{:count/%d}\n", "oops");

    /* test_type_string_ok: OK */
    xo_emit("{:name/%s}\n", "alice");

    /* test_type_string_int: WARN: type mismatch string */
    xo_emit("{:name/%s}\n", 42);

    /* Multiple roles (caught by xo_parse_format: "multiple types") */

    /* test_multiple_roles WARN: multiple field types */: 
    xo_emit("{LT:Max}\n");

    /* Value field name rules (all caught by xo_parse_format) */

    /* test_name_missing: WARN: value field must have a name */
    xo_emit("{:/%s}\n", "x");

    /* test_name_underscore: WARN: use hyphens not underscores */
    xo_emit("{:no_good/%s}\n", "x");

    /* test_name_uppercase: WARN: field name should be lower case */
    xo_emit("{:NAME/%s}\n", "x");

    /* test_name_leading_digit: WARN: field name cannot start with digit */
    xo_emit("{:9lives/%s}\n", "x");

    /* test_name_invalid_char: WARN: field name contains invalid character */
    xo_emit("{:a$b/%s}\n", "x");

    /* test_name_too_short: WARN: field name should be longer than 2 chars */
    xo_emit("{:ab/%s}\n", "x");

    /* test_name_ok: OK */
    xo_emit("{:good-name/%s}\n", "x");

    /* Anchor field checks (caught by xo_parse_format) */

    /* test_anchor_ok_static: OK: static width */
    xo_emit("{[:32}{:name/%s}{]:}\n", "x");

    /* test_anchor_ok_dynamic: OK: dynamic %d */
    xo_emit("{[:/%d}{:name/%s}{]:}\n", 32, "x");

    /* test_anchor_nonnumeric: WARN: anchor content must be decimal */
    xo_emit("{[:mumble}{:name/%s}{]:}\n", "x");

    /* test_anchor_wrong_format: WARN: anchor format must be %d */
    xo_emit("{[:/%s}{:name/%s}{]:}\n", "32", "x");

    /* test_anchor_both: WARN: anchor cannot have both */
    xo_emit("{[:32/%d}{:name/%s}{]:}\n", 32, "x");

    /* Humanize modifier requires format string */

    /* test_humanize_ok: OK */
    xo_emit("{h:size/%d}\n", 1024);

    /* test_humanize_no_format: WARN: humanize requires format string */
    xo_emit("{h:size}\n", 1024);

    /* Long field names (using comma-separated long-form roles) */

    /* test_long_role_ok: OK */
    xo_emit("{,value:good-name/%s}\n", "x");

    /* test_long_role_bad: WARN: unknown long name */
    xo_emit("{,humanization:size/%d}\n", 1024);

    /* XFF_ARGUMENT ('a' modifier): name from va_arg, then value */

    /* test_argument_modifier: OK: 2 args (name + value) */
    xo_emit("{a:/%s}\n", "tag-name", "value");

    return 0;
}
