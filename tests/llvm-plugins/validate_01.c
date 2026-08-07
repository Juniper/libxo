#include <libxo/xo.h>

int
main (int argc, char **argv)
{
    char name[] = "validate_01.test";  /* test trimming of xo_program */
    argv[0] = name;
    
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("top");

    /* valid format, no diagnostic */
    xo_emit("{:name/%s}\n", "alice");

    /* unclosed field */
    xo_emit("{:bad/", "oops");

    /* fewer varargs than fields */
    xo_emit("{:name/%s} {:age/%d}\n", "bob");

    /* more varargs than fields */
    xo_emit("{:name/%s}\n", "carol", 99);

    /* %d field gets string arg */
    xo_emit("{:count/%d}\n", "oops");

    /* %s field gets int arg */
    xo_emit("{:name/%s}\n", 42);

    /* multiple role characters */
    xo_emit("{LT:Max}\n");

    /* empty field name */
    xo_emit("{:/%s}\n", "x");

    /* underscore in field name */
    xo_emit("{:no_good/%s}\n", "x");

    /* uppercase in field name */
    xo_emit("{:NAME/%s}\n", "x");

    /* name starting with digit */
    xo_emit("{:9lives/%s}\n", "x");

    /* invalid character in field name */
    xo_emit("{:a$b/%s}\n", "x");

    /* field name too short */
    xo_emit("{:ab/%s}\n", "x");

    /* non-numeric anchor content */
    xo_emit("{[:mumble}{:name/%s}{]:}\n", "x");

    /* anchor with non-%d format */
    xo_emit("{[:/%s}{:name/%s}{]:}\n", "32", "x");

    /* anchor with both content and format */
    xo_emit("{[:32/%d}{:name/%s}{]:}\n", 32, "x");

    /* humanize modifier without format */
    xo_emit("{h:size}\n", 1024);

    /* role name too long */
    xo_emit("{,humanization:size/%d}\n", 1024);

    /* wrong argument types */
    xo_emit("{:name/%d}\n", "foo");
    xo_emit("{:count/%s}\n", 37);
    xo_emit("{:default}\n", 39);
    xo_emit("{:name/%d}, {:name/%s}\n", "foo", 37);
    xo_emit("{:value/%d.%u}, {:name/%.*s}\n", "foo", -4.537, "green", 13579);

    uint8_t u8 = 8;
    uint32_t u32 = 32;
    uint64_t u64 = 64;
    xo_emit("{:types/%c-%ld-%lld}\n", u64, u32, u8);

    xo_close_container("top");

    xo_finish();

    exit(0);
}
