# Build glue

Each source file that calls libxo needs:

    #include <libxo/xo.h>

after the system headers, and the program links with -lxo.

## FreeBSD (bsd.prog.mk)

libxo is in the FreeBSD base system.  Today's form:

    LIBADD+=	xo

added to the program's Makefile next to any other LIBADD entries.
The 2014 patches in libxo's patches/ directory used the older form,
which should not be copied:

    DPADD=	${LIBUTIL} ${LIBXO}          # old; do not use
    LDADD=	-lutil -lxo                  # old; do not use

They also added "${CARGS}" to CFLAGS; that was a local development
hook and must not be copied either.

Programs built in the rescue or crunch builds (RELEASE_CRUNCH) need
libxo there too; check for a ".if defined(RELEASE_CRUNCH)" block.

## Man page

FreeBSD's converted programs document the option.  With the user's
agreement, add to the man page:

- "Op Fl -libxo" in SYNOPSIS;
- a description of --libxo in the options list, in the form other
  converted programs use ("Generate output via libxo(3) in a
  selection of different human and machine readable formats.  See
  xo_options(7) for details on command line arguments.");
- "Xr libxo 3" and "Xr xo_options 7" in SEE ALSO.

## Plain Makefile

Use pkg-config or libxo-config:

    CFLAGS+=	`pkg-config --cflags libxo`
    LDLIBS+=	`pkg-config --libs libxo`

    CFLAGS+=	`libxo-config --cflags`
    LDLIBS+=	`libxo-config --libs`

## autoconf

    PKG_CHECK_MODULES([LIBXO], [libxo])

then add $(LIBXO_CFLAGS) and $(LIBXO_LIBS) to the program's flags in
Makefile.am.  If libxo is optional, wrap the calls in a
HAVE_LIBXO conditional only if the user asks; a conditional
conversion doubles the code paths and the testing.

## cmake

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBXO REQUIRED IMPORTED_TARGET libxo)
    target_link_libraries(prog PRIVATE PkgConfig::LIBXO)

## meson

    libxo_dep = dependency('libxo')
    executable('prog', sources, dependencies : [libxo_dep])

## Validation with xocc

xocc wraps the compiler and loads libxo's clang plugins, which check
xo_emit format strings against their arguments (as -Wformat does for
printf) and lint the field descriptors:

    xocc validate+lint+ldflags+sdk `libxo-config --cflags` -c foo.c

xocc does not add libxo's include path itself; pass the cflags.  The
"sdk" verb is needed on macOS.  For a whole build, use xocc as CC:

    make CC="xocc validate+lint+sdk"

xocc requires a clang that libxo was built against; if it is not
installed, rely on xolint:

    xolint foo.c bar.c
