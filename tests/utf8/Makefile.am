#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if LIBXO_WARNINGS_HIGH
LIBXO_WARNINGS = HIGH
endif
if HAVE_GCC
GCC_WARNINGS = yes
endif
include ${top_srcdir}/warnings.mk

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libxo \
    ${WARNINGS}

# Ick: maintained by hand!
TEST_CASES = \
utf8_01.c \
utf8_02.c \
utf8_03.c

utf8_01_test_SOURCES = utf8_01.c
utf8_02_test_SOURCES = utf8_02.c
utf8_03_test_SOURCES = utf8_03.c

base_tests = ${TEST_CASES:.c=.test}

noinst_PROGRAMS = ${base_tests}

if HAVE_VALIDATE
validate_tests = ${TEST_CASES:.c=.validate-test}
pass_tests = ${TEST_CASES:.c=.pass-test}
endif

LDADD = \
    ${top_builddir}/libxo/libxo.la

if HAVE_HUMANIZE_NUMBER
LDADD += -lutil
endif

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.c=.T.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.T.out}}

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------

RUN_TESTS_SH = ${srcdir}/../run-tests.sh
RUN_TESTS_CMD = ${BASH} ${RUN_TESTS_SH} -d ${srcdir}

TEST_FILE = ${srcdir}/data_01.txt
TEST_FORMATS = T

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	CHECKER='valgrind -q' ${MAKE} run-plain-tests

.PHONY: run-plain-tests run-validate-tests run-pass-tests

# These are substituted by configure; non-empty only when HAVE_VALIDATE is set.
LLVM_CLANG = @LLVM_CLANG@
SDK_PATH = @SDK_PATH@
INCLUDEDIR = ${prefix}/include
LIBDIR = ${prefix}/lib
SYSROOT_FLAG = $(if ${SDK_PATH},-isysroot ${SDK_PATH},)
VALIDATE_BUILD = ${top_builddir}/llvm-plugins/validate
PRECOMPILE_BUILD = ${top_builddir}/llvm-plugins/precompile
FORMAT_LIBDIR = ${top_builddir}/format/.libs
FORMAT_LA = ${top_builddir}/format/libxo_format.la
LIBXO_LIBDIR = ${top_builddir}/libxo/.libs
LIBXO_LA = ${top_builddir}/libxo/libxo.la

if HAVE_VALIDATE
PLUGIN_TESTS = run-validate-tests run-pass-tests
else
PLUGIN_TESTS =
endif

test tests: ${noinst_PROGRAMS} run-plain-tests ${PLUGIN_TESTS}

run-plain-tests:
	@${MKDIR} -p out
	${RUN_TESTS_CMD} -T "${TEST_FORMATS}" -a "file ${TEST_FILE}" \
	    run-bin ${TEST_CASES:.c=.test}

if HAVE_VALIDATE

all-local: $(validate_tests) $(pass_tests)

$(validate_tests): %.validate-test: $(srcdir)/%.c \
    $(VALIDATE_BUILD)/xo_validate.so \
    $(FORMAT_LA) $(LIBXO_LA)
	@${MKDIR} -p out
	@$(AM_V_GEN)DYLD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${DYLD_LIBRARY_PATH}" \
	    LD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${LD_LIBRARY_PATH}" \
	    ${LLVM_CLANG} ${CFLAGS} \
	    -fplugin=${VALIDATE_BUILD}/xo_validate.so \
	    -mllvm -xo-validate-errors-as-warnings \
	    -I${top_srcdir}/libxo -I${top_builddir}/libxo -I${INCLUDEDIR} ${SYSROOT_FLAG} \
	    -c $< -o $*.validate-test.o > out/$@.make.out 2> out/$@.make.err
	@$(LIBTOOL) $(AM_V_lt) --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=link \
	    ${LLVM_CLANG} ${CFLAGS} ${SYSROOT_FLAG} \
	    $*.validate-test.o ${LIBXO_LA} \
	    -o $@

$(pass_tests): %.pass-test: $(srcdir)/%.c \
    $(PRECOMPILE_BUILD)/xo_precompile.so \
    $(FORMAT_LA) $(LIBXO_LA)
	@${MKDIR} -p out
	@$(AM_V_GEN)DYLD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${DYLD_LIBRARY_PATH}" \
	    LD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${LD_LIBRARY_PATH}" \
	    ${LLVM_CLANG} ${CFLAGS} \
	    -fpass-plugin=${PRECOMPILE_BUILD}/xo_precompile.so \
	    -I${top_srcdir}/libxo -I${top_builddir}/libxo -I${INCLUDEDIR} ${SYSROOT_FLAG} \
	    -c $< -o $*.pass-test.o > out/$@.make.out 2> out/$@.make.err
	@$(LIBTOOL) $(AM_V_lt) --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=link \
	    ${LLVM_CLANG} ${CFLAGS} ${SYSROOT_FLAG} \
	    $*.pass-test.o ${LIBXO_LA} \
	    -o $@

run-validate-tests: $(validate_tests)
	@${MKDIR} -p out
	-@for tc in ${TEST_CASES:.c=}; do \
	    diff -Nu ${srcdir}/saved/$$tc.validate-test.make.out \
	        out/$$tc.validate-test.make.out ; \
	    diff -Nu ${srcdir}/saved/$$tc.validate-test.make.err \
	        out/$$tc.validate-test.make.err ; \
	done
	@DYLD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${DYLD_LIBRARY_PATH}" \
	    LD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${LD_LIBRARY_PATH}" \
	    ${RUN_TESTS_CMD} -T "${TEST_FORMATS}" -m validate \
	    -a "file ${TEST_FILE}" run-bin ${TEST_CASES:.c=.test}

run-pass-tests: $(pass_tests)
	@${MKDIR} -p out
	-@for tc in ${TEST_CASES:.c=}; do \
	    diff -Nu ${srcdir}/saved/$$tc.pass-test.make.out \
	        out/$$tc.pass-test.make.out ; \
	    diff -Nu ${srcdir}/saved/$$tc.pass-test.make.err \
	        out/$$tc.pass-test.make.err ; \
	done
	@DYLD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${DYLD_LIBRARY_PATH}" \
	    LD_LIBRARY_PATH="${LIBXO_LIBDIR}:${FORMAT_LIBDIR}:${LD_LIBRARY_PATH}" \
	    ${RUN_TESTS_CMD} -T "${TEST_FORMATS}" -m pass \
	    -a "file ${TEST_FILE}" run-bin ${TEST_CASES:.c=.test}

else

run-validate-tests run-pass-tests:

endif

# Run a single test/format for quick iteration:
#   make one TEST_BIN=utf8_01.test TEST_FMT=T
one:
	${RUN_TESTS_CMD} -T "${TEST_FMT:-T}" -a "file ${TEST_FILE}" \
	    run-bin ${TEST_BIN:-utf8_01.test}

accept:
	@${MKDIR} -p ${srcdir}/saved
	${RUN_TESTS_CMD} -T "${TEST_FORMATS}" accept-bin ${TEST_CASES:.c=.test}
	-@test -z "${LLVM_CLANG}" || for tc in ${TEST_CASES:.c=}; do \
	    cp out/$$tc.validate-test.make.out \
	        ${srcdir}/saved/$$tc.validate-test.make.out 2>/dev/null ; \
	    cp out/$$tc.validate-test.make.err \
	        ${srcdir}/saved/$$tc.validate-test.make.err 2>/dev/null ; \
	    cp out/$$tc.pass-test.make.out \
	        ${srcdir}/saved/$$tc.pass-test.make.out 2>/dev/null ; \
	    cp out/$$tc.pass-test.make.err \
	        ${srcdir}/saved/$$tc.pass-test.make.err 2>/dev/null ; \
	done

.c.test:
	$(AM_V_CC)$(LTCOMPILE) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<

CLEANFILES = ${TEST_CASES:.c=.test} \
    ${TEST_CASES:.c=.validate-test} \
    ${TEST_CASES:.c=.validate-test.o} \
    ${TEST_CASES:.c=.validate-test.make.out} \
    ${TEST_CASES:.c=.validate-test.make.err} \
    ${TEST_CASES:.c=.pass-test} \
    ${TEST_CASES:.c=.pass-test.o} \
    ${TEST_CASES:.c=.pass-test.make.out} \
    ${TEST_CASES:.c=.pass-test.make.err}
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
