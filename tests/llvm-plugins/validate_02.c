/*
 * xo_validate_test.c: test input for the xo_validate clang plugin.
 *
 * Each test is annotated with the expected diagnostic.
 * Run via:  make test-validate  (in the top build directory)
 *
 * "OK"   — no diagnostic expected
 * "WARN" — error or warning from the plugin expected (exact text in comment)
 *
 * Platform notes (LP64 / macOS ARM):
 *   sizeof(int) = 4, sizeof(long) = sizeof(long long) = 8
 *   size_t = unsigned long,  ptrdiff_t = long,  intmax_t = long
 *   uint64_t = unsigned long long,  int64_t = long long  (NOT unsigned long /
 *   long -- but IS unsigned long / long on FreeBSD and Linux)
 *   long double == double in size (64-bit) but are distinct types.
 *   Integer matching is by bit width, sign ignored: any 64-bit integer
 *   kind matches any other 64-bit integer kind of the same signedness
 *   category, so uint64_t/int64_t vs %lu/%ld warn or don't warn the
 *   same way regardless of which builtin kind they alias on this
 *   platform.  (clang's own -Wformat, checking fprintf() below, does
 *   not make this concession -- its output for the u64/i64 cases is
 *   expected to differ from ours and from platform to platform.)
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include <libxo/xo.h>

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	exit(1);

    FILE *dev_null = fopen("/dev/null", "w+");
    if (dev_null)
	xo_err(1, "failed to open dev/null");

    /*
     *  A field width or precision, or both, may be indicated by an
     * asterisk ‘*’ or an asterisk followed by one or more decimal
     * digits and a ‘$’ instead of a digit string.  In this case, an
     * int argument supplies the field width or precision.
     */
    int width = 5;
    xo_emit("{:width/%*.*s}\n", width, width, "x");

    /*
     * Syntax and count
     */

    /* OK */
    xo_emit("{:name/%s}\n", "alice");

    /* WARN: missing closing '}' */
    xo_emit("{:bad/\n", "oops");

    /* OK: two fields, two args */
    xo_emit("{:name/%s} {:age/%d}\n", "bob", 42);

    /* WARN: expects 2 but 1 */
    xo_emit("{:name/%s} {:age/%d}\n", "bob");

    /* WARN: expects 1 but 2 */
    xo_emit("{:name/%s}\n", "carol", 99);

    /*
     * Basic type checks (existing)
     */

    /* OK */
    xo_emit("{:count/%d}\n", 5);

    /* WARN: format specifies type 'int' but the argument has type 'char *' */
    xo_emit("{:count/%d}\n", "oops");

    /* OK */
    xo_emit("{:name/%s}\n", "alice");

    /* WARN: format specifies type 'char *' but the argument has type 'int' */
    xo_emit("{:name/%s}\n", 42);

    /*
     * Name and role checks
     */

    /* WARN: multiple field types */
    xo_emit("{LT:Max}\n");

    /* WARN: value field must have a name */
    xo_emit("{:/%s}\n", "x");

    /* WARN: use hyphens not underscores */
    xo_emit("{:no_good/%s}\n", "x");

    /* WARN: field name should be lower case */
    xo_emit("{:NAME/%s}\n", "x");

    /* WARN: field name cannot start with digit */
    xo_emit("{:9lives/%s}\n", "x");

    /* WARN: field name should be longer than 2 chars */
    xo_emit("{:ab/%s}\n", "x");

    /* OK */
    xo_emit("{:good-name/%s}\n", "x");

    /*
     * Anchor fields
     */

    /* OK: static width */
    xo_emit("{[:32}{:name/%s}{]:}\n", "x");

    /* OK: dynamic %d */
    xo_emit("{[:/%d}{:name/%s}{]:}\n", 32, "x");

    /* WARN: anchor content must be decimal */
    xo_emit("{[:mumble}{:name/%s}{]:}\n", "x");

    /* WARN: anchor cannot have both */
    xo_emit("{[:32/%d}{:name/%s}{]:}\n", 32, "x");

    /*
     * Humanize modifier
     */

    /* OK */
    xo_emit("{h:size/%d}\n", 1024);

    /* WARN: humanize requires format string */
    xo_emit("{h:size}\n", 1024);

    /*
     * XFF_ARGUMENT ('a' modifier)
     */

    /* OK: 2 args (name + value) */
    xo_emit("{a:/%s}\n", "tag-name", "value");

    /*
     * Integer length modifiers
     */

    int       i_val  = 42;
    long      l_val  = 42L;
    long long ll_val = 42LL;

    unsigned int       u_val  = 42U;
    unsigned long      ul_val = 42UL;
    unsigned long long ull_val = 42ULL;

    /* OK: %d with int */
    xo_emit("{:val/%d}\n", i_val);
    fprintf(dev_null, "{:val/%d}\n", i_val);

    /* WARN: format specifies type 'int' but argument has type 'long' */
    xo_emit("{:val/%d}\n", l_val);
    fprintf(dev_null, "{:val/%d}\n", l_val);

    /* OK: %ld with long */
    xo_emit("{:val/%ld}\n", l_val);
    fprintf(dev_null, "{:val/%ld}\n", l_val);

    /* WARN: format specifies type 'long' but argument has type 'int' */
    xo_emit("{:val/%ld}\n", i_val);
    fprintf(dev_null, "{:val/%ld}\n", i_val);

    /* OK: %lld with long long */
    xo_emit("{:val/%lld}\n", ll_val);
    fprintf(dev_null, "{:val/%lld}\n", ll_val);

    /* OK: %u with unsigned int */
    xo_emit("{:val/%u}\n", u_val);
    fprintf(dev_null, "{:val/%u}\n", u_val);

    /* OK: %u with int — same integer kind, sign differs; clang allows this */
    xo_emit("{:val/%u}\n", i_val);
    fprintf(dev_null, "{:val/%u}\n", i_val);

    /* OK: integer constant "1" — same kind, sign differs */
    xo_emit("{:val/%u}\n", 1);
    fprintf(dev_null, "{:val/%u}\n", 1);

    /* OK: %d with unsigned int — same integer kind, sign differs; clang allows this */
    xo_emit("{:val/%d}\n", u_val);
    fprintf(dev_null, "{:val/%d}\n", u_val);

    /* OK: integer constant "42U" — same kind, sign differs */
    xo_emit("{:val/%d}\n", 42U);
    fprintf(dev_null, "{:val/%d}\n", 42U);

    /* OK: %lu with unsigned long */
    xo_emit("{:val/%lu}\n", ul_val);
    fprintf(dev_null, "{:val/%lu}\n", ul_val);

    /* WARN: size — format specifies 'unsigned long' but argument is 'unsigned int' */
    xo_emit("{:val/%lu}\n", u_val);
    fprintf(dev_null, "{:val/%lu}\n", u_val);

    /* ok: %llu with unsigned long long */
    xo_emit("{:val/%llu}\n", ull_val);
    fprintf(dev_null, "{:val/%llu}\n", ull_val);

    /* WARN: size — format specifies 'unsigned long long' but argument is 'unsigned int' */
    xo_emit("{:val/%llu}\n", u_val);
    fprintf(dev_null, "{:val/%llu}\n", u_val);

    /* OK: %x with unsigned int */
    xo_emit("{:val/%x}\n", u_val);
    fprintf(dev_null, "{:val/%x}\n", u_val);

    /* OK: %x with int — same integer kind, sign differs; clang allows this */
    xo_emit("{:val/%x}\n", i_val);
    fprintf(dev_null, "{:val/%x}\n", i_val);

    /* OK: %lx with unsigned long */
    xo_emit("{:val/%lx}\n", ul_val);
    fprintf(dev_null, "{:val/%lx}\n", ul_val);

    /*
     * size_t, ptrdiff_t, intmax_t
     */

    size_t    sz_val = 42;
    ptrdiff_t pd_val = 42;
    intmax_t  jd_val = 42;
    uintmax_t ju_val = 42;

    /* OK: %zu with size_t */
    xo_emit("{:val/%zu}\n", sz_val);
    fprintf(dev_null, "{:val/%zu}\n", sz_val);

    /* WARN: format specifies 'unsigned long' (size_t) but argument is 'int' */
    xo_emit("{:val/%zu}\n", i_val);
    fprintf(dev_null, "{:val/%zu}\n", i_val);

    /* OK: %zu with unsigned long (same canonical type as size_t on LP64) */
    xo_emit("{:val/%zu}\n", ul_val);
    fprintf(dev_null, "{:val/%zu}\n", ul_val);

    /* OK: %td with ptrdiff_t */
    xo_emit("{:val/%td}\n", pd_val);
    fprintf(dev_null, "{:val/%td}\n", pd_val);

    /* WARN: format specifies ptrdiff_t (long) but argument is int */
    xo_emit("{:val/%td}\n", i_val);
    fprintf(dev_null, "{:val/%td}\n", i_val);

    /* OK: %jd with intmax_t */
    xo_emit("{:val/%jd}\n", jd_val);
    fprintf(dev_null, "{:val/%jd}\n", jd_val);

    /* OK: %ju with intmax_t — uintmax_t/intmax_t are same kind; clang allows this */
    xo_emit("{:val/%ju}\n", jd_val);
    fprintf(dev_null, "{:val/%ju}\n", jd_val);

    /* OK: %ju with uintmax_t */
    xo_emit("{:val/%ju}\n", ju_val);
    fprintf(dev_null, "{:val/%ju}\n", ju_val);

    /*
     * Floating-point
     */

    double      d_val  = 3.14;
    float       f_val  = 3.14f;
    long double ld_val = 3.14L;

    /* OK: %g with double */
    xo_emit("{:val/%g}\n", d_val);
    fprintf(dev_null, "{:val/%g}\n", d_val);

    /* OK: %g with float literal (float promotes to double in varargs) */
    xo_emit("{:val/%g}\n", f_val);
    fprintf(dev_null, "{:val/%g}\n", f_val);

    /* WARN: format specifies 'double' but argument has type 'int' */
    xo_emit("{:val/%g}\n", i_val);
    fprintf(dev_null, "{:val/%g}\n", i_val);

    /* OK: %Lg with long double */
    xo_emit("{:val/%Lg}\n", ld_val);
    fprintf(dev_null, "{:val/%Lg}\n", ld_val);

    /* WARN: %Lg expects long double, not double (distinct types even if same size) */
    xo_emit("{:val/%Lg}\n", d_val);
    fprintf(dev_null, "{:val/%Lg}\n", d_val);

    /* OK: %e with double */
    xo_emit("{:val/%e}\n", d_val);
    fprintf(dev_null, "{:val/%e}\n", d_val);

    /* OK: %f with double */
    xo_emit("{:val/%f}\n", d_val);
    fprintf(dev_null, "{:val/%f}\n", d_val);

    /* WARN: %f expects double but argument is int */
    xo_emit("{:val/%f}\n", i_val);
    fprintf(dev_null, "{:val/%f}\n", i_val);

    /*
     * Pointer and string
     */

    const char   *cs_val = "hello";
    char         *ms_val = NULL;
    int          *ip_val = &i_val;
    void         *vp_val = NULL;
    unsigned char ubuf[4] = {0};

    /* OK: %s with const char * */
    xo_emit("{:val/%s}\n", cs_val);
    fprintf(dev_null, "{:val/%s}\n", cs_val);

    /* OK: %s with char * */
    xo_emit("{:val/%s}\n", ms_val);
    fprintf(dev_null, "{:val/%s}\n", ms_val);

    /* OK: %s with unsigned char * (isCharType() includes unsigned char) */
    xo_emit("{:val/%s}\n", ubuf);
    fprintf(dev_null, "{:val/%s}\n", ubuf);

    /* WARN: %s expects char * but argument is int * (not a char pointer) */
    xo_emit("{:val/%s}\n", ip_val);
    fprintf(dev_null, "{:val/%s}\n", ip_val);

    /* WARN: %s expects char * but argument is int */
    xo_emit("{:val/%s}\n", i_val);
    fprintf(dev_null, "{:val/%s}\n", i_val);

    /* OK: %p with void * */
    xo_emit("{:val/%p}\n", vp_val);
    fprintf(dev_null, "{:val/%p}\n", vp_val);

    /* OK: %p accepts any pointer */
    xo_emit("{:val/%p}\n", cs_val);
    fprintf(dev_null, "{:val/%p}\n", cs_val);

    xo_emit("{:val/%p}\n", ip_val);
    fprintf(dev_null, "{:val/%p}\n", ip_val);

    /* WARN: %p expects a pointer but argument is int */
    xo_emit("{:val/%p}\n", i_val);
    fprintf(dev_null, "{:val/%p}\n", i_val);

    /*
     * Width arguments (%*s, %.*s) — width must be int
     */

    /* OK: %*s with int width and string value */
    xo_emit("{:val/%*s}\n", 10, cs_val);
    fprintf(dev_null, "{:val/%*s}\n", 10, cs_val);

    /* OK: %.*s with int precision and string */
    xo_emit("{:val/%.*s}\n", 20, cs_val);
    fprintf(dev_null, "{:val/%.*s}\n", 20, cs_val);

    /* OK: three-group width */
    xo_emit("{:val/%.*.*s}\n", 8, 20, cs_val);
    fprintf(dev_null, "{:val/%*.*s}\n", 8, 20, cs_val);

    /* WARN: width arg must be int but long provided */
    xo_emit("{:val/%*s}\n", l_val, cs_val);
    fprintf(dev_null, "{:val/%*s}\n", l_val, cs_val);

    /* WARN: precision arg must be int but long provided */
    xo_emit("{:val/%.*s}\n", l_val, cs_val);
    fprintf(dev_null, "{:val/%.*s}\n", l_val, cs_val);

    /*
     * stdint.h fixed-width types
     */

    uint32_t u32 = 42;
    int32_t  i32 = 42;
    uint64_t u64 = 42;
    int64_t  i64 = 42;

    /* OK: uint32_t = unsigned int on LP64; %u matches */
    xo_emit("{:val/%u}\n", u32);
    fprintf(dev_null, "{:val/%u}\n", u32);

    /* OK: int32_t = int on LP64; %d matches */
    xo_emit("{:val/%d}\n", i32);
    fprintf(dev_null, "{:val/%d}\n", i32);

    /* OK: uint32_t = unsigned int, %d with unsigned int — same kind */
    xo_emit("{:val/%d}\n", u32);
    fprintf(dev_null, "{:val/%d}\n", u32);

    /* OK: int32_t = int, %u with int — same kind */
    xo_emit("{:val/%u}\n", i32);
    fprintf(dev_null, "{:val/%u}\n", i32);

    /*
     * OK (portable): %lu wants a 64-bit unsigned value and u64 is
     * 64-bit unsigned, regardless of whether uint64_t aliases
     * "unsigned long" (FreeBSD/Linux) or "unsigned long long" (macOS).
     * No fprintf() pair: clang's own -Wformat checks against the real
     * platform typedef and disagrees between those two platforms,
     * which is expected and outside our control.
     */
    xo_emit("{:val/%lu}\n", u64);

    /* OK (portable): same reasoning, int64_t vs %ld */
    xo_emit("{:val/%ld}\n", i64);

    /* WARN: size — uint64_t (64-bit) vs %u (32-bit unsigned) */
    xo_emit("{:val/%u}\n", u64);
    fprintf(dev_null, "{:val/%u}\n", u64);

    /* WARN: size — int64_t (64-bit) vs %d (32-bit signed) */
    xo_emit("{:val/%d}\n", i64);
    fprintf(dev_null, "{:val/%d}\n", i64);

    /*
     * libxo-specific field roles with type checks
     */

    /* OK: encode-only field, value from va_arg */
    xo_emit("{e:count/%d}\n", i_val);
    fprintf(dev_null, "{e:count/%d}\n", i_val);

    /* WARN: encode-only but %d given a string */
    xo_emit("{e:count/%d}\n", cs_val);
    fprintf(dev_null, "{e:count/%d}\n", cs_val);

    /* OK: display-only field */
    xo_emit("{d:label/%s}\n", cs_val);
    fprintf(dev_null, "{d:label/%s}\n", cs_val);

    /* WARN: display-only, %s given an int */
    xo_emit("{d:label/%s}\n", i_val);
    fprintf(dev_null, "{d:label/%s}\n", i_val);

    /* OK: title field */
    xo_emit("{t:header/%s}\n", cs_val);
    fprintf(dev_null, "{t:header/%s}\n", cs_val);

    /* OK: note field */
    xo_emit("{N:/%s}\n", cs_val);
    fprintf(dev_null, "{N:/%s}\n", cs_val);

    /* OK: humanize with unsigned long */
    xo_emit("{h:size/%lu}\n", ul_val);
    fprintf(dev_null, "{h:size/%lu}\n", ul_val);

    /* WARN: humanize with int where %lu expects unsigned long */
    xo_emit("{h:size/%lu}\n", i_val);
    fprintf(dev_null, "{h:size/%lu}\n", i_val);

    /* OK: color field with format */
    xo_emit("{C:/fg-%s}", cs_val);
    fprintf(dev_null, "{C:/fg-%s}", cs_val);

    /* WARN: color field with %s but given int */
    xo_emit("{C:/fg-%s}", i_val);
    fprintf(dev_null, "{C:/fg-%s}", i_val);

    /*
     * Complex multi-field with correct types
     */

    /* OK: ls-style output — one string and two width+value pairs */
    xo_emit("{t:mode/%s} {t:links/%*u} {t:user/%-*s}\n",
	    cs_val, (int) 8, u_val, (int) 12, cs_val);
    fprintf(dev_null, "{t:mode/%s} {t:links/%*u} {t:user/%-*s}\n",
	    cs_val, (int) 8, u_val, (int) 12, cs_val);

    /* WARN: link count uses %*u but width arg is long not int */
    xo_emit("{t:mode/%s} {t:links/%*u} {t:user/%-*s}\n",
	    cs_val, l_val, u_val, (int) 12, cs_val);
    fprintf(dev_null, "{t:mode/%s} {t:links/%*u} {t:user/%-*s}\n",
	    cs_val, l_val, u_val, (int) 12, cs_val);

    /* OK: mixed signed/unsigned with correct specifiers */
    xo_emit("{:pid/%d} {:uid/%u} {:size/%lu} {:name/%s}\n",
	    i_val, u_val, ul_val, cs_val);
    fprintf(dev_null, "{:pid/%d} {:uid/%u} {:size/%lu} {:name/%s}\n",
	    i_val, u_val, ul_val, cs_val);

    /* WARN: pid field uses %ld but i_val is int (32-bit) */
    xo_emit("{:pid/%ld} {:uid/%u} {:size/%lu} {:name/%s}\n",
	    i_val, u_val, ul_val, cs_val);
    fprintf(dev_null, "{:pid/%ld} {:uid/%u} {:size/%lu} {:name/%s}\n",
	    i_val, u_val, ul_val, cs_val);

    /* OK: hex output with correct types */
    xo_emit("{:flags/%08x} {:addr/%lx} {:bytes/%zu}\n",
	    u_val, ul_val, sz_val);
    fprintf(dev_null, "{:flags/%08x} {:addr/%lx} {:bytes/%zu}\n",
	    u_val, ul_val, sz_val);

    /* OK: %08x with int — same integer kind, sign differs; clang allows this */
    xo_emit("{:flags/%08x} {:addr/%lx} {:bytes/%zu}\n",
	    i_val, ul_val, sz_val);
    fprintf(dev_null, "{:flags/%08x} {:addr/%lx} {:bytes/%zu}\n",
	    i_val, ul_val, sz_val);

    /*
     * Display/encoding format consistency (checked in shim, not plugin).
     * These test the arg-count/type split between display and encoding
     */

    /* OK: display and encoding both %d */
    xo_emit("{:val/%d/%d}\n", i_val);

    /* WARN: encoding has two args, display has one */
    xo_emit("{:val/%d/%d%d}\n", i_val);

    /* WARN: encoding uses %s but display uses %d (different types) */
    xo_emit("{:val/%d/%s}\n", i_val);

    /*
     * Long-form role names
     */

    /* OK */
    xo_emit("{,value:good-name/%s}\n", "x");

    /* WARN: unknown long name */
    xo_emit("{,humanization:size/%d}\n", 1024);

    xo_emit("{:missing-encoding/%d/}\n", 1024);

    xo_emit("{d:/this should be %s}\n", "text");
    xo_emit("{F:/this should be %s}\n", "text");

    fclose(dev_null);

    return 0;
}
