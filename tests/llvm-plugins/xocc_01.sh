#!/bin/sh
#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Tests for the xocc wrapper: which options it adds for compile-only,
# link-only, and one-step builds, where the libraries go, and how it
# sets LD_LIBRARY_PATH.  Invoked from the Makefile as:
#
#     sh xocc_01.sh <xocc> <source> <outdir> <cflags> <ldflags>
#
# The Makefile replaces the install paths in the output, so the
# baselines don't depend on the machine.
#

XOCC="$1"
SRC="$2"
OUT="$3"
CFLAGS_ARGS="$4"
LINK_ARGS="$5"

run () {
    echo "== $*"
    "$@"
    echo "rc=$?"
}

#
# "dry" shows the command line without running it
# ("-sdk" leaves out the macOS SDK flags, which vary by machine)
#
echo "### dry runs"

# Compile only: plugins, no libraries
run sh ${XOCC} full-sdk+dry -c foo.c -o foo.o

# Link only: no plugins, libraries after the inputs
run sh ${XOCC} full-sdk+dry foo.o bar.o -o foo
run sh ${XOCC} full-sdk+dry -Werror foo.o -o foo

# One step: plugins, then libraries after the inputs
run sh ${XOCC} full-sdk+dry foo.c -o foo

# Options that take an argument don't make it a source file
run sh ${XOCC} full-sdk+dry -include cfg.c foo.o -o foo
run sh ${XOCC} full-sdk+dry -o foo.c foo.o

# "-x c" makes a source of any file
run sh ${XOCC} full-sdk+dry -x c foo.src -o foo

# "errors" runs the validator, without errors-as-warnings
run sh ${XOCC} errors-sdk+dry -c foo.c

# "precompile" doesn't run the validator
run sh ${XOCC} precompile-sdk+dry -c foo.c

# "lint" alone is a syntax check, so nothing is linked
run sh ${XOCC} lint-sdk+dry foo.c

# Removing verbs
run sh ${XOCC} full-p-sdk+dry -c foo.c
run sh ${XOCC} full-ldflags-sdk+dry foo.o -o foo

#
# LD_LIBRARY_PATH: a compiler that reports it shows what xocc
# exports.  With nothing inherited there must be no trailing colon.
#
echo "### LD_LIBRARY_PATH"
cat > ${OUT}/xocc_01.env.sh <<'EOF'
#!/bin/sh
echo "LD_LIBRARY_PATH=[${LD_LIBRARY_PATH}]"
EOF
chmod +x ${OUT}/xocc_01.env.sh

echo "== unset"
env -u LD_LIBRARY_PATH XO_REAL_CC=${OUT}/xocc_01.env.sh \
    sh ${XOCC} validate -c foo.c
echo "== inherited"
env LD_LIBRARY_PATH=/inherited XO_REAL_CC=${OUT}/xocc_01.env.sh \
    sh ${XOCC} validate -c foo.c

#
# Real builds of the same program, in each mode
#
echo "### builds"

run sh ${XOCC} full ${CFLAGS_ARGS} -c ${SRC} -o ${OUT}/xocc_01.o
run sh ${XOCC} full -Werror ${OUT}/xocc_01.o ${LINK_ARGS} -o ${OUT}/xocc_01.ld
run ${OUT}/xocc_01.ld --libxo:T
run sh ${XOCC} full ${CFLAGS_ARGS} ${SRC} ${LINK_ARGS} -o ${OUT}/xocc_01.both
run ${OUT}/xocc_01.both --libxo:T

exit 0
