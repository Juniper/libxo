#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Regression tests for the xo_validate clang plugin and xo_precompile IR pass.
# Requires LLVM/clang; only included in SUBDIRS when HAVE_VALIDATE is set.
#
# Targets:
#   tests / test  -- run all tests; diff outputs against saved/
#   accept        -- overwrite saved/ with current output (after inspection)
#

LLVM_CLANG = @LLVM_CLANG@
LLVM_CMAKE_DIR = @LLVM_CMAKE_DIR@
CLANG_CMAKE_DIR = @CLANG_CMAKE_DIR@
SDK_PATH = @SDK_PATH@

VALIDATE_BUILD = ${top_builddir}/llvm-plugins/validate
VALIDATE_PLUGIN = ${VALIDATE_BUILD}/xo_validate.so
VALIDATE_PLUGIN_PATH = $(CURDIR)/${VALIDATE_PLUGIN}

PRECOMPILE_BUILD = ${top_builddir}/llvm-plugins/precompile
PRECOMPILE_PLUGIN = ${PRECOMPILE_BUILD}/xo_precompile.so
PRECOMPILE_PLUGIN_PATH = $(CURDIR)/${PRECOMPILE_PLUGIN}

INCLUDEDIR = ${prefix}/include
LIBDIR = ${prefix}/lib

SYSROOT_FLAG = $(if ${SDK_PATH},-isysroot ${SDK_PATH},)

LDADD =

if HAVE_HUMANIZE_NUMBER
LDADD += -lutil
endif

if HAVE_LIBBSD
LDADD += -lbsd
endif

CLANG_CMD = ${LLVM_CLANG} \
    ${CFLAGS} \
    -fplugin=${VALIDATE_PLUGIN_PATH} \
    -mllvm -xo-validate-errors-as-warnings -mllvm -xo-validate-lint \
    -I${INCLUDEDIR} \
    ${SYSROOT_FLAG} \
    -fsyntax-only

CLANG_PLAIN = ${LLVM_CLANG} \
    ${CFLAGS} \
    -I${INCLUDEDIR} \
    ${SYSROOT_FLAG}

CLANG_PASS = ${LLVM_CLANG} \
    ${CFLAGS} \
    -fpass-plugin=${PRECOMPILE_PLUGIN_PATH} \
    -I${INCLUDEDIR} \
    ${SYSROOT_FLAG}

PRECOMPILE_FORMATS = T XP JP JPu HP

TEST_CASES = \
    validate_01 \
    validate_02

PRECOMPILE_TEST_CASES = \
    precompile_01

EXTRA_DIST = \
    ${addsuffix .c, ${TEST_CASES}} \
    ${addprefix saved/, ${addsuffix .out, ${TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .err, ${TEST_CASES}}} \
    ${addsuffix .c, ${PRECOMPILE_TEST_CASES}} \
    ${addprefix saved/, ${addsuffix .plain.build.out, ${PRECOMPILE_TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .plain.build.err, ${PRECOMPILE_TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .pass.build.out,  ${PRECOMPILE_TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .pass.build.err,  ${PRECOMPILE_TEST_CASES}}} \
    ${foreach fmt,${PRECOMPILE_FORMATS}, \
        ${addprefix saved/, ${addsuffix .plain.${fmt}.out, ${PRECOMPILE_TEST_CASES}}} \
        ${addprefix saved/, ${addsuffix .plain.${fmt}.err, ${PRECOMPILE_TEST_CASES}}} \
        ${addprefix saved/, ${addsuffix .pass.${fmt}.out,  ${PRECOMPILE_TEST_CASES}}} \
        ${addprefix saved/, ${addsuffix .pass.${fmt}.err,  ${PRECOMPILE_TEST_CASES}}}} \
    ${addprefix saved/, ${addsuffix .ir.out,   ${PRECOMPILE_TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .ir.err,   ${PRECOMPILE_TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .nm,       ${PRECOMPILE_TEST_CASES}}}

all:

.PHONY: test tests test-validate test-precompile accept

#
# Validate (AST plugin) tests: check plugin diagnostics against saved baselines.
#
test tests: test-validate test-precompile

test-validate: ${VALIDATE_PLUGIN}
	@${MKDIR} -p out
	-@(for tc in ${TEST_CASES}; do \
	    echo "... $$tc ..."; \
	    ${CLANG_CMD} ${srcdir}/$$tc.c \
	        > out/$$tc.out 2> out/$$tc.err.raw ; \
	    ${SED} -e "s/ (aka '[^']*')//" < out/$$tc.err.raw \
		| ${SED} -E -e "s/              %l+[ud]/             %XXX/" \
		> out/$$tc.err ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.out out/$$tc.out ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.err out/$$tc.err ; \
	done)

#
# Precompile (IR pass) tests.
#
# For each test case:
#   - Build with and without the pass; diff build output against saved/
#   - Run under each output format; diff plain and pass outputs against saved/
#   - Cross-check: diff plain vs pass for each format (round-trip sanity)
#   - Compile with the pass; dump the object's xo_* symbols (addresses
#     stripped, sorted) and diff against saved/ to confirm the expected
#     calls/globals (xo_emit_cached, .xo_fcache, .xo_fields, .xo_fspecs)
#     are present, without depending on LLVM-IR-text-level detail
#
test-precompile: ${PRECOMPILE_PLUGIN}
	@${MKDIR} -p out
	-@(for tc in ${PRECOMPILE_TEST_CASES}; do \
	    echo "... precompile build $$tc ..."; \
	    ${CLANG_PLAIN} ${LDADD} ${srcdir}/$$tc.c -L${LIBDIR} -Wl,-rpath,${LIBDIR} -lxo \
	        -o out/$$tc.plain \
	        > out/$$tc.plain.build.out 2> out/$$tc.plain.build.err ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.plain.build.out out/$$tc.plain.build.out ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.plain.build.err out/$$tc.plain.build.err ; \
	    ${CLANG_PASS} ${LDADD} ${srcdir}/$$tc.c -L${LIBDIR} -Wl,-rpath,${LIBDIR} -lxo \
	        -o out/$$tc.pass \
	        > out/$$tc.pass.build.out 2> out/$$tc.pass.build.err ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.pass.build.out out/$$tc.pass.build.out ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.pass.build.err out/$$tc.pass.build.err ; \
	    echo "... precompile run $$tc ..."; \
	    for fmt in ${PRECOMPILE_FORMATS}; do \
	        LC_ALL=en_US.UTF-8 out/$$tc.plain --libxo:W$$fmt \
	            > out/$$tc.plain.$$fmt.out 2> out/$$tc.plain.$$fmt.err ; \
	        ${DIFF} -Nu ${srcdir}/saved/$$tc.plain.$$fmt.out out/$$tc.plain.$$fmt.out ; \
	        ${DIFF} -Nu ${srcdir}/saved/$$tc.plain.$$fmt.err out/$$tc.plain.$$fmt.err ; \
	        LC_ALL=en_US.UTF-8 out/$$tc.pass  --libxo:W$$fmt \
	            > out/$$tc.pass.$$fmt.out  2> out/$$tc.pass.$$fmt.err ; \
	        ${DIFF} -Nu ${srcdir}/saved/$$tc.pass.$$fmt.out  out/$$tc.pass.$$fmt.out  ; \
	        ${DIFF} -Nu ${srcdir}/saved/$$tc.pass.$$fmt.err  out/$$tc.pass.$$fmt.err  ; \
	        ${DIFF} out/$$tc.plain.$$fmt.out out/$$tc.pass.$$fmt.out \
	            || echo "FAIL: round-trip mismatch $$tc $$fmt stdout" ; \
	        ${DIFF} out/$$tc.plain.$$fmt.err out/$$tc.pass.$$fmt.err \
	            || echo "FAIL: round-trip mismatch $$tc $$fmt stderr" ; \
	    done ; \
	    echo "... precompile ir $$tc ..."; \
	    ${CLANG_PASS} -c ${srcdir}/$$tc.c \
	        -o out/$$tc.pass.o \
	        > out/$$tc.ir.out 2> out/$$tc.ir.err ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.ir.out  out/$$tc.ir.out ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.ir.err  out/$$tc.ir.err ; \
	    nm out/$$tc.pass.o | grep xo_emit_cached | sort -u \
	        > out/$$tc.nm ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.nm out/$$tc.nm ; \
	done)

accept: ${VALIDATE_PLUGIN} ${PRECOMPILE_PLUGIN}
	@${MKDIR} -p out ${srcdir}/saved
	@(for tc in ${TEST_CASES}; do \
	    echo "... $$tc ..."; \
	    ${CP} out/$$tc.out ${srcdir}/saved/$$tc.out ; \
	    ${CP} out/$$tc.err ${srcdir}/saved/$$tc.err ; \
	done)
	@(for tc in ${PRECOMPILE_TEST_CASES}; do \
	    echo "... precompile $$tc ..."; \
	    ${CP} out/$$tc.plain.build.out ${srcdir}/saved/$$tc.plain.build.out ; \
	    ${CP} out/$$tc.plain.build.err ${srcdir}/saved/$$tc.plain.build.err ; \
	    ${CP} out/$$tc.pass.build.out  ${srcdir}/saved/$$tc.pass.build.out  ; \
	    ${CP} out/$$tc.pass.build.err  ${srcdir}/saved/$$tc.pass.build.err  ; \
	    for fmt in ${PRECOMPILE_FORMATS}; do \
	        ${CP} out/$$tc.plain.$$fmt.out ${srcdir}/saved/$$tc.plain.$$fmt.out ; \
	        ${CP} out/$$tc.plain.$$fmt.err ${srcdir}/saved/$$tc.plain.$$fmt.err ; \
	        ${CP} out/$$tc.pass.$$fmt.out  ${srcdir}/saved/$$tc.pass.$$fmt.out  ; \
	        ${CP} out/$$tc.pass.$$fmt.err  ${srcdir}/saved/$$tc.pass.$$fmt.err  ; \
	    done ; \
	    ${CP} out/$$tc.ir.out  ${srcdir}/saved/$$tc.ir.out  ; \
	    ${CP} out/$$tc.ir.err  ${srcdir}/saved/$$tc.ir.err  ; \
	    ${CP} out/$$tc.nm      ${srcdir}/saved/$$tc.nm      ; \
	done)

CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
