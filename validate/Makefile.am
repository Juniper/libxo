#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

# The validate plugin in cmake-based.  Enable using "./configure
# --with-llvm-config=PATH/llvm-config".  The macOS SDK path is
# detected at configure time via `xcrun`; override with
# "--with-sdk-path=PATH or suppress with --with-sdk-path=no.

LLVM_CLANG = @LLVM_CLANG@
LLVM_CMAKE_DIR = @LLVM_CMAKE_DIR@
CLANG_CMAKE_DIR = @CLANG_CMAKE_DIR@
SDK_PATH = @SDK_PATH@

VALIDATE_NAME = xo_validate.so
VALIDATE_DIR = plugin
VALIDATE_RELPATH = ${VALIDATE_DIR}/${VALIDATE_NAME}

VALIDATE_BUILD = ${top_builddir}/validate/${VALIDATE_DIR}
VALIDATE_PLUGIN = ${VALIDATE_BUILD}/${VALIDATE_NAME}

.PHONY: validate validate-clean accept-validate

if HAVE_VALIDATE

all: ${VALIDATE_PLUGIN}

${VALIDATE_PLUGIN}: ${VALIDATE_DIR}/CMakeCache.txt
	${MAKE} -C ${VALIDATE_DIR}

${VALIDATE_DIR}/CMakeCache.txt:
	${MKDIR} -p ${VALIDATE_DIR}
	cmake -S ${top_srcdir}/validate/plugin -B ${VALIDATE_DIR} \
	    -DLLVM_DIR="${LLVM_CMAKE_DIR}" \
	    -DClang_DIR="${CLANG_CMAKE_DIR}" \
	    -DXO_BUILDDIR="$(CURDIR)/${top_builddir}"

accept:
	${MAKE} -C ${top_builddir}/tests/validate accept

test tests:
	${MAKE} -C ${top_builddir}/tests/validate test

validate-clean:
	${RM} -fr ${VALIDATE_DIR}

CLEANDIRS = ${VALIDATE_DIR}

clean-local:
	rm -rf ${CLEANDIRS}

else

all:

validate validate-clean accept:
	@echo "validate plugin disabled. use --with-llvm-config=PATH"

endif
