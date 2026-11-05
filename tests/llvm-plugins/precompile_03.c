/*
 * precompile_03.c: regression test guarding against name-only dispatch in
 * the xo_precompile IR pass.
 *
 * This translation unit never includes libxo/xo.h, so "xo_emit" here is
 * purely a local, static function that happens to share a name with a
 * libxo emit function.  The pass must never rewrite a call to it: it should
 * only ever match a callee that is an external declaration (the real
 * libxo function), not a same-named local definition.  A regression here
 * changes the program's exit status under the pass, which the round-trip
 * exit-status check in the test Makefile catches.
 */

static long
xo_emit (const char *format, ...)
{
    (void) format;

    return 4242;
}

int
main (void)
{
    return xo_emit("local function") != 4242;
}
