#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Regression tests for the xo_validate clang plugin.
# Requires LLVM/clang; only included in SUBDIRS when HAVE_VALIDATE is set.
#
# Targets:
#   tests / test  -- compile each tv_*.c with the plugin; diff against saved/
#   accept        -- overwrite saved/ with current output
#

LLVM_CLANG = @LLVM_CLANG@
LLVM_CMAKE_DIR = @LLVM_CMAKE_DIR@
CLANG_CMAKE_DIR = @CLANG_CMAKE_DIR@
SDK_PATH = @SDK_PATH@

VALIDATE_BUILD = ${top_builddir}/validate/plugin
VALIDATE_PLUGIN = ${VALIDATE_BUILD}/xo_validate.so
VALIDATE_PLUGIN_PATH = $(CURDIR)/${VALIDATE_PLUGIN}

INCLUDEDIR = ${prefix}/include

SYSROOT_FLAG = $(if ${SDK_PATH},-isysroot ${SDK_PATH},)

CLANG_CMD = ${LLVM_CLANG} \
    -fplugin=${VALIDATE_PLUGIN_PATH} \
    -I${INCLUDEDIR} \
    ${SYSROOT_FLAG} \
    -fsyntax-only

TEST_CASES = \
    validate_01

EXTRA_DIST = \
    ${addsuffix .c, ${TEST_CASES}} \
    ${addprefix saved/, ${addsuffix .out, ${TEST_CASES}}} \
    ${addprefix saved/, ${addsuffix .err, ${TEST_CASES}}}

all:

test tests: ${VALIDATE_PLUGIN}
	@${MKDIR} -p out
	-@(for tc in ${TEST_CASES}; do \
	    echo "... $$tc ..."; \
	    ( ${CLANG_CMD} ${srcdir}/$$tc.c \
	        > out/$$tc.out \
	        2> out/$$tc.err) ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.out out/$$tc.out ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$tc.err out/$$tc.err ; \
	done)

accept: ${VALIDATE_PLUGIN}
	@${MKDIR} -p out
	@${MKDIR} -p ${srcdir}/saved
	@(for tc in ${TEST_CASES}; do \
	    echo "... $$tc ..."; \
	    ${CP} out/$$tc.out ${srcdir}/saved/$$tc.out ; \
	    ${CP} out/$$tc.err ${srcdir}/saved/$$tc.err ; \
	done)

CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
