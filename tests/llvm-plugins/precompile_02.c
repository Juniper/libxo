/*
 * precompile_02.c: regression test for the xo_precompile IR pass and the
 * "_p" convenience wrappers (xo_emit_p, xo_emit_hp, xo_emit_fp, xo_emit_hp,
 * xo_emit_hvp, xo_emit_hvfp).
 *
 * Those wrappers are static inline in xo.h and their "cached" counterparts
 * (xo_emit_cached_p and friends) are never emitted as real libxo symbols, so
 * the pass must leave calls to them alone rather than rewriting them to a
 * target that can't link.  This file exercises every "_p" form so the pass
 * builds this file at all; a regression here shows up as a link failure in
 * the pass-compiled build, not as a diff.
 *
 * This file is compiled twice by the test Makefile, the same as
 * precompile_01.c, and both binaries must produce identical output (and
 * exit status) for every output format.
 */

#include <libxo/xo.h>
#include <stdarg.h>

static void
emit_hvfp (xo_handle_t *xop, xo_emit_flags_t flags, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_hvfp(xop, flags, fmt, vap);
    va_end(vap);
}

static void
emit_hvp (xo_handle_t *xop, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_hvp(xop, fmt, vap);
    va_end(vap);
}

static void
emit_fields_p (void)
{
    xo_emit_p("{:name/%s}\n", "alice");
    xo_emit_fp(0, "{:count/%d}\n", 42);
    xo_emit_hp(NULL, "{:first/%s} {:last/%s}\n", "john", "doe");
    xo_emit_hfp(NULL, 0, "{e:id/%d}{:label/%s}\n", 7, "item");
    emit_hvfp(NULL, 0, "{[:/%d}{:value/%s}{]:}\n", 20, "padded");
    emit_hvp(NULL, "{:extra/%s}\n", "xtra");
}

int
main (int argc, char *argv[])
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    xo_open_container("data");
    emit_fields_p();
    xo_close_container("data");
    xo_finish();
    return 0;
}
