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
CMAKE_COMMAND = @CMAKE_COMMAND@
CMAKE_OSX_ARCHITECTURES = @CMAKE_OSX_ARCHITECTURES@

VALIDATE_NAME = xo_validate.so
VALIDATE_DIR = validate
VALIDATE_RELPATH = ${VALIDATE_DIR}/${VALIDATE_NAME}

VALIDATE_BUILD = ${top_builddir}/llvm-plugins/${VALIDATE_DIR}
VALIDATE_PLUGIN = ${VALIDATE_BUILD}/${VALIDATE_NAME}

PRECOMPILE_NAME = xo_precompile.so
PRECOMPILE_DIR = precompile
PRECOMPILE_RELPATH = ${PRECOMPILE_DIR}/${PRECOMPILE_NAME}

PRECOMPILE_BUILD = ${top_builddir}/llvm-plugins/${PRECOMPILE_DIR}
PRECOMPILE_PLUGIN = ${PRECOMPILE_BUILD}/${PRECOMPILE_NAME}

.PHONY: validate validate-clean precompile precompile-clean

if HAVE_VALIDATE

all: ${VALIDATE_PLUGIN} ${PRECOMPILE_PLUGIN}

${VALIDATE_PLUGIN}: ${VALIDATE_DIR}/CMakeCache.txt
	${MAKE} -C ${VALIDATE_DIR}

${VALIDATE_DIR}/CMakeCache.txt:
	${MKDIR} -p ${VALIDATE_DIR}
	${CMAKE_COMMAND} -S ${top_srcdir}/llvm-plugins/validate -B ${VALIDATE_DIR} \
	    -DLLVM_DIR="${LLVM_CMAKE_DIR}" \
	    -DClang_DIR="${CLANG_CMAKE_DIR}" \
	    -DCMAKE_OSX_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES}" \
	    -DCMAKE_INSTALL_PREFIX="${prefix}" \
	    -DXO_BUILDDIR="$(CURDIR)/${top_builddir}"

${PRECOMPILE_PLUGIN}: ${PRECOMPILE_DIR}/CMakeCache.txt
	${MAKE} -C ${PRECOMPILE_DIR}

${PRECOMPILE_DIR}/CMakeCache.txt:
	${MKDIR} -p ${PRECOMPILE_DIR}
	${CMAKE_COMMAND} -S ${top_srcdir}/llvm-plugins/precompile -B ${PRECOMPILE_DIR} \
	    -DLLVM_DIR="${LLVM_CMAKE_DIR}" \
	    -DCMAKE_OSX_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES}" \
	    -DCMAKE_INSTALL_PREFIX="${prefix}" \
	    -DXO_BUILDDIR="$(CURDIR)/${top_builddir}"

install-exec-local:
	${MAKE} -C ${VALIDATE_DIR} install
	${MAKE} -C ${PRECOMPILE_DIR} install

validate-clean:
	${RM} -fr ${VALIDATE_DIR}

precompile-clean:
	${RM} -fr ${PRECOMPILE_DIR}

CLEANDIRS = ${VALIDATE_DIR} ${PRECOMPILE_DIR}

clean-local:
	rm -rf ${CLEANDIRS}

else

all:

install-exec-local validate validate-clean precompile precompile-clean:

endif
