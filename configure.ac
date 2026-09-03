#
# $Id$
#
# See ./INSTALL for more info
#

#
# Release numbering: even numbered dot releases are official ones, and
# odd numbers are development ones.  The svn version of this file will
# only (ONLY!) ever (EVER!) contain odd numbers, so I'll always know if
# a particular user has the dist or svn release.
#

AC_PREREQ([2.69])
AC_INIT([libxo],[2.0.0],[phil@juniper.net])
AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability])

# Support silent build rules.  Requires at least automake-1.11.
# Disable with "configure --disable-silent-rules" or "make V=1"
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_INSTALL
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LN_S

# Must be after AC_PROG_AR
LT_INIT([dlopen shared])

AC_PATH_PROG(BASENAME, basename, /usr/bin/basename)
AC_PATH_PROG(BISON, bison, /usr/bin/bison)
AC_PATH_PROG(BASH, bash, /bin/bash)
AC_PATH_PROG(CAT, cat, /bin/cat)
AC_PATH_PROG(CHMOD, chmod, /bin/chmod)
AC_PATH_PROG(CP, cp, /bin/cp)
AC_PATH_PROG(DIFF, diff, /usr/bin/diff)
AC_PATH_PROG(MKDIR, mkdir, /bin/mkdir)
AC_PATH_PROG(MV, mv, /bin/mv)
AC_PATH_PROG(RM, rm, /bin/rm)
AC_PATH_PROG(SED, sed, /bin/sed)
AC_CHECK_PROGS(YACC, [byacc yacc], )

dnl The XPath grammar relies on byacc extensions (a reentrant parser
dnl exposing yystate and byacc-generated tables) that GNU bison does not
dnl provide, so require byacc instead of silently using an incompatible bison.
AS_IF([test -z "$YACC"],
  [AC_MSG_ERROR([byacc is required to build the XPath filter; please install byacc])])
AS_IF([$YACC --version 2>/dev/null | grep -qi bison],
  [AC_MSG_ERROR([byacc is required to build the XPath filter; the detected yacc is GNU bison, which is not compatible. Please install byacc])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_CHECK_FUNCS([__flbf])
AC_CHECK_FUNCS([asprintf])
AC_CHECK_FUNCS([bzero])
AC_CHECK_FUNCS([ctime])
AC_CHECK_FUNCS([dlfunc])
AC_CHECK_FUNCS([fdopen])
AC_CHECK_FUNCS([flock])
AC_CHECK_FUNCS([getpass])
AC_CHECK_FUNCS([getprogname])
AC_CHECK_FUNCS([getrusage])
AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_FUNCS([memmove])
AC_CHECK_FUNCS([memrchr])
AC_CHECK_FUNCS([srand])
AC_CHECK_FUNCS([sranddev])
AC_CHECK_FUNCS([strchr])
AC_CHECK_FUNCS([strchrnul])
AC_CHECK_FUNCS([strcspn])
AC_CHECK_FUNCS([strerror])
AC_CHECK_FUNCS([strlcpy])
AC_CHECK_FUNCS([strspn])
AC_CHECK_FUNCS([sysctlbyname])
AC_CHECK_FUNCS([uexterr_gettext])

AC_CHECK_HEADERS([bsd/string.h])
AC_CHECK_HEADERS([ctype.h])
AC_CHECK_HEADERS([dlfcn.h])
AC_CHECK_HEADERS([dlfcn.h])
AC_CHECK_HEADERS([errno.h])
AC_CHECK_HEADERS([exterr.h])
AC_CHECK_HEADERS([langinfo.h])
AC_CHECK_HEADERS([monitor.h])
AC_CHECK_HEADERS([stdio.h])
AC_CHECK_HEADERS([stdio_ext.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([stdtime/tzfile.h])
AC_CHECK_HEADERS([string.h])
AC_CHECK_HEADERS([sys/exterrvar.h])
AC_CHECK_HEADERS([sys/param.h])
AC_CHECK_HEADERS([sys/queue.h], , [AC_MSG_ERROR([Could not find <sys/queue.h>])])
AC_CHECK_HEADERS([sys/sysctl.h])
AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([threads.h])
AC_CHECK_HEADERS([tzfile.h])
AC_CHECK_HEADERS([unistd.h ])

dnl
dnl Some packages need to be checked against version numbers so we
dnl define a function here for later use
dnl
AC_DEFUN([VERSION_TO_NUMBER],
[`$1 | sed -e 's/lib.* //' | awk 'BEGIN { FS = "."; } { printf "%d", ([$]1 * 1000 + [$]2) * 1000 + [$]3;}'`])

dnl humanize_number(3) is a great function, but it's not standard.
dnl Note Macosx has the function in libutil.a but doesn't ship the
dnl header file, so I'll need to carry my own implementation.  See:
dnl    https://devforums.apple.com/thread/271121
AC_CHECK_HEADERS([libutil.h])
AC_CHECK_LIB([util], [humanize_number],
     [HAVE_HUMANIZE_NUMBER=$ac_cv_header_libutil_h],
     [HAVE_HUMANIZE_NUMBER=no])

AC_MSG_CHECKING([humanize_number results])
AC_MSG_RESULT(:${HAVE_HUMANIZE_NUMBER}:${ac_cv_header_libutil_h}:)

if test "$HAVE_HUMANIZE_NUMBER" = "yes"; then
    AC_DEFINE([HAVE_HUMANIZE_NUMBER], [1], [humanize_number(3)])
fi

AM_CONDITIONAL([HAVE_HUMANIZE_NUMBER], [test "$HAVE_HUMANIZE_NUMBER" = "yes"])

AC_CHECK_LIB([bsd], [strlcpy],
     [HAVE_LIBBSD=yes],
     [HAVE_LIBBSD=no])
AM_CONDITIONAL([HAVE_LIBBSD], [test "$HAVE_LIBBSD" = "yes"])

AC_MSG_CHECKING([exterr results])

if test "$ac_cv_header_exterr_h" = "yes" -a "$ac_cv_func_uexterr_gettext" = "yes"; then
    HAVE_EXTERR=yes
    AC_DEFINE([HAVE_EXTERR], [1], [Have extended error infra: uexterr_gettext(2)])
fi

AC_MSG_RESULT(:${HAVE_EXTERR}:${ac_cv_header_exterr_h}:${ac_cv_func_uexterr_gettext}:)

AM_CONDITIONAL([HAVE_EXTERR], [test "$HAVE_EXTERR" = "yes"])

AC_ARG_WITH(llvm-config,
        [  --with-llvm-config=[PFX]        Specify location of llvm-config],
        [LLVM_CONFIG=$withval],
	[LLVM_CONFIG="no"])

AC_MSG_CHECKING([llvm-config])
if test ${LLVM_CONFIG} = "no"; then
    AC_MSG_RESULT([no llvm-config, so llvm-plugins are disabled])
elif ! test -x ${LLVM_CONFIG} ; then
    AC_MSG_FAILURE("could not find llvm-config tool")
else
    AC_DEFINE([HAVE_VALIDATE], [1], [generate plugin for llvm to validate libxo format strings])
    LLVM_BINDIR=`${LLVM_CONFIG} --bindir 2>/dev/null`
    LLVM_CLANG=${LLVM_BINDIR}/clang
    LLVM_CMAKE_DIR=`${LLVM_CONFIG} --cmakedir 2>/dev/null`
    CLANG_CMAKE_DIR=`dirname ${LLVM_CMAKE_DIR}`/clang
    CLANG_VERSION=`${LLVM_CONFIG} --version 2>/dev/null`
    CLANG_VERSION_NUMBER=VERSION_TO_NUMBER(echo $CLANG_VERSION)

    AC_DEFINE_UNQUOTED(CLANG_VERSION_NUMBER, [$CLANG_VERSION_NUMBER], [clang version as a number])

    AC_PATH_PROG([CMAKE_COMMAND], [cmake], [cmake])
    HAVE_VALIDATE=yes
fi

AM_CONDITIONAL([HAVE_VALIDATE], [test "${HAVE_VALIDATE}" = "yes"])
AC_SUBST(LLVM_CONFIG)
AC_SUBST(LLVM_BINDIR)
AC_SUBST(LLVM_CLANG)
AC_SUBST(LLVM_CMAKE_DIR)
AC_SUBST(CLANG_CMAKE_DIR)
AC_SUBST(CMAKE_COMMAND)
AC_SUBST(CLANG_VERSION)
AC_SUBST(CLANG_VERSION_NUMBER)

dnl On macOS the cmake plugin build must match the libxo build architecture.
dnl Derive this from host_cpu so the right -DCMAKE_OSX_ARCHITECTURES flag is
dnl baked into the Makefile at configure time.  This ensures the plugin is
dnl built correctly even when cmake itself runs under Rosetta (x86_64 emulation)
dnl at make time, which would otherwise cause cmake to auto-detect x86_64.
CMAKE_OSX_ARCHITECTURES=
case $host_cpu in
    arm64 | aarch64)
        CMAKE_OSX_ARCHITECTURES=arm64
        ;;
    x86_64)
        CMAKE_OSX_ARCHITECTURES=x86_64
        ;;
esac
AC_SUBST(CMAKE_OSX_ARCHITECTURES)

dnl macOS SDK path for the non-Apple clang used by the validate plugin.
dnl On darwin, auto-detected via xcrun; override with --with-sdk-path=PATH
dnl or suppress with --with-sdk-path=no.
AC_ARG_WITH(sdk-path,
        [  --with-sdk-path=[PATH]   Specify macOS SDK path for validate plugin (default: auto)],
        [SDK_PATH=$withval],
        [SDK_PATH=auto])

if test "${SDK_PATH}" = "auto"; then
    case $host_os in
        darwin*)
            AC_PATH_PROG([XCRUN], [xcrun], [none])
            if test "${XCRUN}" != "none"; then
                SDK_PATH=`${XCRUN} --show-sdk-path 2>/dev/null`
            else
                SDK_PATH=
            fi
            ;;
        *)
            SDK_PATH=
            ;;
    esac
elif test "${SDK_PATH}" = "no" -o "${SDK_PATH}" = "none"; then
    SDK_PATH=
fi
AC_SUBST(SDK_PATH)

AC_ARG_ENABLE([gettext],
    [  --disable-gettext  Turn off support for gettext],
    [GETTEXT_ENABLE=$enableval],
    [GETTEXT_ENABLE=yes])

dnl Looking for gettext(), assumably in libintl
AC_ARG_WITH(gettext,
        [  --with-gettext=[PFX]           Specify location of gettext installation],
        [GETTEXT_PREFIX=$withval],
	[GETTEXT_PREFIX=/usr],
)

HAVE_GETTEXT=no

if test "$GETTEXT_ENABLE" != "no"; then

  dnl glibc provides gettext in libc; only link -lintl when a separate
  dnl libintl is required (e.g. macOS, *BSD), so detection also works on glibc.
  GETTEXT_INTL=-lintl
  AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
             [#include <stddef.h>]
             [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
                 [GETTEXT_INTL=])

  AC_MSG_CHECKING([gettext in ${GETTEXT_PREFIX}])

  _save_cflags="$CFLAGS"
  CFLAGS="$CFLAGS -I${GETTEXT_PREFIX}/include -L${GETTEXT_PREFIX}/lib -Werror ${GETTEXT_INTL}"
  AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
             [#include <stddef.h>]
             [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
             [HAVE_GETTEXT=yes],
             [HAVE_GETTEXT=no])
  CFLAGS="$_save_cflags"

  if test "$HAVE_GETTEXT" != "yes"; then
      GETTEXT_PREFIX=/opt/local
      AC_MSG_CHECKING([gettext in ${GETTEXT_PREFIX}])

      _save_cflags="$CFLAGS"
      CFLAGS="$CFLAGS -I${GETTEXT_PREFIX}/include -L${GETTEXT_PREFIX}/lib -Werror ${GETTEXT_INTL}"
      AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
             [#include <stddef.h>]
                 [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
                 [HAVE_GETTEXT=yes],
                 [HAVE_GETTEXT=no])
      CFLAGS="$_save_cflags"

      AC_MSG_RESULT([$HAVE_GETTEXT])
  fi

  if test "$HAVE_GETTEXT" != "yes"; then
      GETTEXT_PREFIX=/usr/local
      AC_MSG_CHECKING([gettext in ${GETTEXT_PREFIX}])

      _save_cflags="$CFLAGS"
      CFLAGS="$CFLAGS -I${GETTEXT_PREFIX}/include -L${GETTEXT_PREFIX}/lib -Werror ${GETTEXT_INTL}"
      AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <libintl.h>]
             [#include <stddef.h>]
                 [int main() {char *cp = dgettext(NULL, "xx"); return 0; }]])],
                 [HAVE_GETTEXT=yes],
                 [HAVE_GETTEXT=no])
      CFLAGS="$_save_cflags"

      AC_MSG_RESULT([$HAVE_GETTEXT])
  fi
fi

if test "$HAVE_GETTEXT" = "yes"; then
    AC_DEFINE([HAVE_GETTEXT], [1], [gettext(3)])
    GETTEXT_CFLAGS="-I${GETTEXT_PREFIX}/include"
    GETTEXT_LIBS="-L${GETTEXT_PREFIX}/lib ${GETTEXT_INTL}"
else
    GETTEXT_PREFIX=none
    GETTEXT_CFLAGS=
    GETTEXT_LIBS=
fi
AC_SUBST(GETTEXT_CFLAGS)
AC_SUBST(GETTEXT_LIBS)

GETTEXT_LIBDIR=${GETTEXT_PREFIX}/lib
AC_SUBST(GETTEXT_LIBDIR)
if test "$HAVE_GETTEXT" != "yes"; then
    GETTEXT_BINDIR=
elif test -x ${GETTEXT_PREFIX}/bin/msgfmt ; then
    GETTEXT_BINDIR=${GETTEXT_PREFIX}/bin
elif test -x ${GETTEXT_PREFIX}/local/bin/msgfmt ; then
    GETTEXT_BINDIR=${GETTEXT_PREFIX}/local/bin
else
    dnl gettext may live in libc (glibc); msgfmt can be elsewhere on PATH
    AC_PATH_PROG([MSGFMT], [msgfmt])
    AS_IF([test -n "$MSGFMT"], [GETTEXT_BINDIR=`AS_DIRNAME([$MSGFMT])`],
          [HAVE_GETTEXT=no])
fi

AC_MSG_CHECKING([final result for gettext])
AC_MSG_RESULT([$HAVE_GETTEXT])
AC_SUBST(GETTEXT_BINDIR)

AM_CONDITIONAL([HAVE_GETTEXT], [test "$HAVE_GETTEXT" = "yes"])

dnl Looking for how to do thread-local variables
AC_ARG_WITH(threads,
        [  --with-threads=[STYLE]           Specify style of thread-local support (none)],
        [THREAD_LOCAL=$withval],
	[THREAD_LOCAL=unknown],
)

AC_MSG_CHECKING([thread-locals are ${THREAD_LOCAL}])

if test "$THREAD_LOCAL" = "unknown"; then
     AC_LINK_IFELSE([AC_LANG_SOURCE([[]
           [__thread int foo; int main() { foo++; return foo; }]])],
           [THREAD_LOCAL=before],
           [THREAD_LOCAL=unknown])

    AC_MSG_RESULT([$THREAD_LOCAL])
fi

if test "$THREAD_LOCAL" = "unknown"; then
    AC_LINK_IFELSE([AC_LANG_SOURCE([[]
           [int __thread foo; int main() { foo++; return foo; }]])],
           [THREAD_LOCAL=after],
           [THREAD_LOCAL=unknown])
    AC_MSG_RESULT([$THREAD_LOCAL])
fi

if test "$THREAD_LOCAL" = "unknown"; then
    AC_LINK_IFELSE([AC_LANG_SOURCE([[]
           [__declspec(int) foo; int main() { foo++; return foo; }]])],
           [THREAD_LOCAL=declspec],
           [THREAD_LOCAL=unknown])
    AC_MSG_RESULT([$THREAD_LOCAL])
fi

if test "$THREAD_LOCAL" != "unknown"; then
    AC_DEFINE_UNQUOTED([HAVE_THREAD_LOCAL], 
              THREAD_LOCAL_${THREAD_LOCAL}, [thread-local setting])
fi

dnl Looking for libcrypto....
AC_CHECK_LIB([crypto], [MD5_Init])
AM_CONDITIONAL([HAVE_LIBCRYPTO], [test "$HAVE_LIBCRYPTO" != "no"])

AC_CHECK_MEMBER([struct sockaddr_un.sun_len],
    [HAVE_SUN_LEN=yes ;
        AC_DEFINE([HAVE_SUN_LEN], [1], [Have struct sockaddr_un.sun_len])],
    [HAS_SUN_LEN=no], [[#include <sys/un.h>]])

AC_CHECK_DECLS([__isthreaded], [], [], [#include <stdio.h>])
HAVE_ISTHREADED=${ac_cv_have_decl___isthreaded}

AC_MSG_CHECKING([whether to build with warnings])
AC_ARG_ENABLE([warnings],
    [  --enable-warnings    Turn on compiler warnings],
    [LIBXO_WARNINGS=$enableval],
    [LIBXO_WARNINGS=no])
AC_MSG_RESULT([$LIBXO_WARNINGS])
AM_CONDITIONAL([LIBXO_WARNINGS_HIGH], [test "$LIBXO_WARNINGS" != "no"])

AC_MSG_CHECKING([whether to build with debugging])
AC_ARG_ENABLE([debug],
    [  --enable-debug    Turn on debugging],
    [LIBXO_DEBUG=yes; AC_DEFINE([LIBXO_DEBUG], [1], [Enable debugging])],
    [LIBXO_DEBUG=no])
AC_MSG_RESULT([$LIBXO_DEBUG])
AM_CONDITIONAL([LIBXO_DEBUG], [test "$LIBXO_DEBUG" != "no"])

AC_MSG_CHECKING([whether to use int return codes])
AC_ARG_ENABLE([int-return-codes],
    [  --enable-int-return-codes    Use int return codes (instead of ssize_t)],
    [USE_INT_RETURN_CODES=yes; AC_DEFINE([USE_INT_RETURN_CODES], [1], [Use int return codes])],
    [USE_INT_RETURN_CODES=no])
AC_MSG_RESULT([$USE_INT_RETURN_CODES])

AC_MSG_CHECKING([whether to build with text-only rendering])
AC_ARG_ENABLE([text-only],
    [  --enable-text-only    Turn on text-only rendering],
    [LIBXO_TEXT_ONLY=yes; AC_DEFINE([LIBXO_TEXT_ONLY], [1], [Enable text-only rendering])],
    [LIBXO_TEXT_ONLY=no])
AC_MSG_RESULT([$LIBXO_TEXT_ONLY])
AM_CONDITIONAL([LIBXO_TEXT_ONLY], [test "$LIBXO_TEXT_ONLY" != "no"])

AC_MSG_CHECKING([whether to build with local wcwidth implementation])
AC_ARG_ENABLE([wcwidth],
    [  --disable-wcwidth        Disable local wcwidth implementation],
    [LIBXO_WCWIDTH=$enableval],
    [LIBXO_WCWIDTH=yes])
AC_MSG_RESULT([$LIBXO_WCWIDTH])
if test "${LIBXO_WCWIDTH}" != "no"; then
  AC_DEFINE([LIBXO_WCWIDTH], [1], [Enable local wcwidth implementation])
fi

AC_CHECK_LIB([m], [lrint])
AM_CONDITIONAL([HAVE_LIBM], [test "$HAVE_LIBM" != "no"])

AC_MSG_CHECKING([compiler for gcc])
HAVE_GCC=no
if test "${CC}" != ""; then
    HAVE_GCC=`${CC} --version 2>&1 | grep -i GCC`
    if test "${HAVE_GCC}" != ""; then
        HAVE_GCC=yes
	AC_DEFINE([HAVE_GCC], [1], [Using real gcc])
    else
        HAVE_GCC=no
    fi
fi
AC_MSG_RESULT([$HAVE_GCC])
AM_CONDITIONAL([HAVE_GCC], [test "$HAVE_GCC" = "yes"])
AC_SUBST(HAVE_GCC)

AC_MSG_CHECKING([compiler for clang])
HAVE_CLANG=no
if test "${CC}" != ""; then
    HAVE_CLANG=`${CC} cc --help 2>&1 | head -1 | grep -i clang`
    if test "${HAVE_CLANG}" != ""; then
        HAVE_CLANG=yes
    else
        HAVE_CLANG=no
    fi
fi
AC_MSG_RESULT([$HAVE_CLANG])
AM_CONDITIONAL([HAVE_CLANG], [test "$HAVE_CLANG" = "yes"])

AC_MSG_CHECKING([whether to build with printflike])
AC_ARG_ENABLE([printflike],
    [  --enable-printflike    Enable use of GCC __printflike attribute],
    [HAVE_PRINTFLIKE=yes;
           AC_DEFINE([HAVE_PRINTFLIKE], [1], [Support printflike])],
    [HAVE_PRINTFLIKE=no])
AC_MSG_RESULT([$HAVE_PRINTFLIKE])
AM_CONDITIONAL([HAVE_PRINTFLIKE], [test "$HAVE_PRINTFLIKE" != ""])

AC_MSG_CHECKING([whether to build with LIBXO_OPTIONS])
AC_ARG_ENABLE([libxo-options],
    [  --disable-libxo-options  Turn off support for LIBXO_OPTIONS],
    [LIBXO_OPTS=$enableval],
    [LIBXO_OPTS=yes])
AC_MSG_RESULT([$LIBXO_OPTS])
AM_CONDITIONAL([NO_LIBXO_OPTIONS], [test "$LIBXO_OPTS" != "yes"])

AC_MSG_CHECKING([whether to support filters])
AC_ARG_ENABLE([filters],
    [  --enable-filters      Support xpath-like filters],
    [LIBXO_NEED_FILTERS=$enableval; AC_DEFINE([LIBXO_NEED_FILTERS], [1], [Enable xpath-like filters])],
    [LIBXO_NEED_FILTERS=no])
AC_MSG_RESULT([$LIBXO_NEED_FILTERS])
AM_CONDITIONAL([LIBXO_NEED_FILTERS], [test "$LIBXO_NEED_FILTERS" != "no"])
AC_SUBST(LIBXO_NEED_FILTERS)

case $host_os in
     darwin*)
        LIBTOOL=glibtool
	XO_LIBEXT=dylib
	;;
     Linux*|linux*)
        CFLAGS="-D_GNU_SOURCE $CFLAGS"
	LDFLAGS=-ldl
	XO_LIBEXT=so
	;;
     cygwin*|CYGWIN*)
	LDFLAGS=-no-undefined
	XO_LIBEXT=ddl
	;;
esac

case $prefix in
     NONE)
	prefix=/usr/local
	;;
esac

XO_LIBS=-lxo
XO_SRCDIR=${srcdir}
XO_LIBDIR=${libdir}
XO_BINDIR=${bindir}
XO_INCLUDEDIR=${includedir}
XO_CFLAGS="${CFLAGS}"

AC_SUBST(XO_LIBS)
AC_SUBST(XO_SRCDIR)
AC_SUBST(XO_BINDIR)
AC_SUBST(XO_INCLUDEDIR)
AC_SUBST(XO_LIBEXT)
AC_SUBST(XO_CFLAGS)

AC_SUBST(XO_LIBDIR)
XO_LIBDIR=`echo $XO_LIBDIR | sed "s;\\${exec_prefix};$prefix;"`

AC_ARG_WITH(encoder-dir,
        [  --with-encoder-dir=[DIR]           Specify location of encoder libraries],
        [XO_ENCODERDIR=$withval],
        [XO_ENCODERDIR=$libdir/libxo/encoder]
)
AC_SUBST(XO_ENCODERDIR)

AC_ARG_WITH(filter-dir,
        [  --with-filter-dir=[DIR]           Specify location of filter libraries],
        [XO_FILTERDIR=$withval],
        [XO_FILTERDIR=$libdir/libxo]
)
AC_SUBST(XO_FILTERDIR)

AC_ARG_WITH(share-dir,
        [  --with-share-dir=[DIR]           Specify location of shared files],
        [XO_SHAREDIR=$withval],
        [XO_SHAREDIR=$datarootdir/libxo]
)
XO_SHAREDIR=`echo $XO_SHAREDIR | sed "s;\\${prefix};$prefix;"`
AC_SUBST(XO_SHAREDIR)

XO_LLVMDIR=`echo $XO_LIBDIR | sed "s;\\${exec_prefix};$prefix;"`/libxo
AC_SUBST(XO_LLVMDIR)

dnl for the spec file
RELDATE=`date +'%Y-%m-%d%n'`
AC_SUBST(RELDATE)

AC_MSG_RESULT(Using configure dir $ac_abs_confdir)

if test -d $ac_abs_confdir/.git ; then
  extra=`git branch | awk '/\*/ { print $2 }'`
  if test "$extra" != "" -a "$extra" != "master"
  then
      LIBXO_VERSION_EXTRA="-git-$extra"
  fi
fi

LIBXO_VERSION=$PACKAGE_VERSION
LIBXO_VERSION_NUMBER=VERSION_TO_NUMBER(echo $PACKAGE_VERSION)
AC_SUBST(LIBXO_VERSION)
AC_SUBST(LIBXO_VERSION_NUMBER)
AC_SUBST(LIBXO_VERSION_EXTRA)

AC_DEFINE_UNQUOTED(LIBXO_VERSION, ["$LIBXO_VERSION"],
    [Version number as dotted value])
AC_DEFINE_UNQUOTED(LIBXO_VERSION_NUMBER, [$LIBXO_VERSION_NUMBER],
    [Version number as a number])
AC_DEFINE_UNQUOTED(LIBXO_VERSION_STRING, ["$LIBXO_VERSION_NUMBER"],
    [Version number as string])
AC_DEFINE_UNQUOTED(LIBXO_VERSION_EXTRA, ["$LIBXO_VERSION_EXTRA"],
    [Version number extra information])

AC_CONFIG_HEADERS([libxo/xo_config.h])

AC_CONFIG_FILES([
     Makefile
     libxo-config.sh
     xohtml/xohtml.sh
     libxo/Makefile
     filter/Makefile
     format/Makefile
     libxo/add.man
     bin/Makefile
     encoder/Makefile
     encoder/cbor/Makefile
     encoder/csv/Makefile
     encoder/fdr/Makefile
     encoder/fullpath/Makefile
     encoder/test/Makefile
     llvm-plugins/Makefile
     llvm-plugins/xocc.sh
     xo/Makefile
     xolint/Makefile
     xohtml/Makefile
     xopo/Makefile
     packaging/libxo.pc
     doc/Makefile
     doc/top-link.html
     tests/Makefile
     tests/benchmark/Makefile
     tests/core/Makefile
     tests/gettext/Makefile
     tests/xo/Makefile
     tests/xpath/Makefile
     tests/utf8/Makefile
     tests/llvm-plugins/Makefile
     packaging/libxo.spec
     packaging/libxo.rb.base
])
AC_OUTPUT

AC_MSG_NOTICE([summary of build options:

  libxo version:    ${VERSION} ${LIBXO_VERSION_EXTRA}
  host type:        ${host} / ${host_os}
  install prefix:   ${prefix}
  srcdir:           ${XO_SRCDIR}
  libdir:           ${XO_LIBDIR}
  bindir:           ${XO_BINDIR}
  includedir:       ${XO_INCLUDEDIR}
  share dir:        ${XO_SHAREDIR}
  extensions dir:   ${XO_ENCODERDIR}
  filter dir:       ${XO_FILTERDIR}

  compiler:         ${CC} (gcc: ${HAVE_GCC:-no}) (clang: ${HAVE_CLANG:-no})
  compiler flags:   ${CFLAGS}
  library types:    Shared=${enable_shared}, Static=${enable_static}

  warnings:         ${LIBXO_WARNINGS:-no}
  debug:            ${LIBXO_DEBUG:-no}
  printf-like:      ${HAVE_PRINTFLIKE:-no}
  libxo-options:    ${LIBXO_OPTS:-no}
  text-only:        ${LIBXO_TEXT_ONLY:-no}
  gettext:          ${HAVE_GETTEXT:-no} (${GETTEXT_PREFIX})
  isthreaded:       ${HAVE_ISTHREADED:-no}
  thread-local:     ${THREAD_LOCAL:-no}
  local wcwidth:    ${LIBXO_WCWIDTH:-no}
  have exterr:      ${HAVE_EXTERR:-no}
  yacc:             ${YACC}
  filters:          ${LIBXO_NEED_FILTERS}
  clang version:    ${CLANG_VERSION:-none}:${CLANG_VERSION_NUMBER:-none}
  validate:         ${LLVM_CONFIG}:${LLVM_BINDIR:-none}:${LLVM_CMAKE_DIR:-none}
  sdk path:         ${SDK_PATH:-none}
])
