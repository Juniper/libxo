/*
 * xo_validate_test.c: test input for the xo_validate clang plugin.
 *
 * Each test function is annotated with the expected diagnostic.
 * Run via:  make test-validate  (in the top build directory)
 */

#include <libxo/xo.h>

/* ------------------------------------------------------------------ */
/* Original five checks                                                */
/* ------------------------------------------------------------------ */

void test_syntax_ok(void) {
    xo_emit("{:name/%s}\n", "alice");           /* OK */
}
void test_syntax_missing_brace(void) {
    xo_emit("{:bad/\n", "oops");                /* WARN: missing closing } */
}

void test_count_ok(void) {
    xo_emit("{:name/%s} {:age/%d}\n", "bob", 42);   /* OK */
}
void test_count_too_few(void) {
    xo_emit("{:name/%s} {:age/%d}\n", "bob");   /* WARN: expects 2 but 1 */
}
void test_count_too_many(void) {
    xo_emit("{:name/%s}\n", "carol", 99);       /* WARN: expects 1 but 2 */
}

void test_type_int_ok(void) {
    xo_emit("{:count/%d}\n", 5);                /* OK */
}
void test_type_int_string(void) {
    xo_emit("{:count/%d}\n", "oops");           /* WARN: type mismatch integer */
}
void test_type_string_ok(void) {
    xo_emit("{:name/%s}\n", "alice");           /* OK */
}
void test_type_string_int(void) {
    xo_emit("{:name/%s}\n", 42);                /* WARN: type mismatch string */
}

/* ------------------------------------------------------------------ */
/* Multiple roles (caught by xo_parse_format: "multiple types")       */
/* ------------------------------------------------------------------ */

void test_multiple_roles(void) {
    xo_emit("{LT:Max}\n");                      /* WARN: multiple field types */
}

/* ------------------------------------------------------------------ */
/* Value field name rules (all caught by xo_parse_format)             */
/* ------------------------------------------------------------------ */

void test_name_missing(void) {
    xo_emit("{:/%s}\n", "x");                   /* WARN: value field must have a name */
}
void test_name_underscore(void) {
    xo_emit("{:no_good/%s}\n", "x");            /* WARN: use hyphens not underscores */
}
void test_name_uppercase(void) {
    xo_emit("{:NAME/%s}\n", "x");               /* WARN: field name should be lower case */
}
void test_name_leading_digit(void) {
    xo_emit("{:9lives/%s}\n", "x");             /* WARN: field name cannot start with digit */
}
void test_name_invalid_char(void) {
    xo_emit("{:a$b/%s}\n", "x");               /* WARN: field name contains invalid character */
}
void test_name_too_short(void) {
    xo_emit("{:ab/%s}\n", "x");                /* WARN: field name should be longer than 2 chars */
}
void test_name_ok(void) {
    xo_emit("{:good-name/%s}\n", "x");          /* OK */
}

/* ------------------------------------------------------------------ */
/* Anchor field checks (caught by xo_parse_format)                    */
/* ------------------------------------------------------------------ */

void test_anchor_ok_static(void) {
    xo_emit("{[:32}{:name/%s}{]:}\n", "x");     /* OK: static width */
}
void test_anchor_ok_dynamic(void) {
    xo_emit("{[:/%d}{:name/%s}{]:}\n", 32, "x"); /* OK: dynamic %d */
}
void test_anchor_nonnumeric(void) {
    xo_emit("{[:mumble}{:name/%s}{]:}\n", "x"); /* WARN: anchor content must be decimal */
}
void test_anchor_wrong_format(void) {
    xo_emit("{[:/%s}{:name/%s}{]:}\n", "32", "x"); /* WARN: anchor format must be %d */
}
void test_anchor_both(void) {
    xo_emit("{[:32/%d}{:name/%s}{]:}\n", 32, "x"); /* WARN: anchor cannot have both */
}

/* ------------------------------------------------------------------ */
/* Humanize modifier requires format string                            */
/* ------------------------------------------------------------------ */

void test_humanize_ok(void) {
    xo_emit("{h:size/%d}\n", 1024);             /* OK */
}
void test_humanize_no_format(void) {
    xo_emit("{h:size}\n", 1024);               /* WARN: humanize requires format string */
}

/* ------------------------------------------------------------------ */
/* Long field names (using comma-separated long-form roles)           */
/* ------------------------------------------------------------------ */

void test_long_role_ok(void) {
    xo_emit("{,value:good-name/%s}\n", "x");    /* OK */
}
void test_long_role_bad(void) {
    xo_emit("{,humanization:size/%d}\n", 1024); /* WARN: unknown long name */
}

/* ------------------------------------------------------------------ */
/* XFF_ARGUMENT ('a' modifier): name from va_arg, then value          */
/* ------------------------------------------------------------------ */

void test_argument_modifier(void) {
    xo_emit("{a:/%s}\n", "tag-name", "value");  /* OK: 2 args (name + value) */
}
