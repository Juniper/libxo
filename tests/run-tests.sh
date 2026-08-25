#!/bin/sh
# $Id$
#
# Copyright 2016-2024, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

GOODDIR=${SRCDIR}/saved
S2O="sed 1,/@@/d"
ECHO=/bin/echo
FILES_BASE=files.txt
FILES=out/$FILES_BASE

# Plugin test mode: plain (default), validate, or pass.
# validate/pass binaries are built by the Makefile into out/ and have
# the mode name as an infix: out/base.validate.test, out/base.pass.test.
# They are diffed against the same saved/ baselines as plain mode.
RUN_MODE=plain

# Extra arguments appended to every binary invocation (e.g. "file data.txt")
BIN_EXTRA_ARGS=

# trap trap_fail SIGABRT

trap_fail () {
    echo "SIGABRT receieved; exitting..."
    exit 1;
}

run () {
    cmd="$1"

    if [ "$DOC" = doc ]; then
        ${ECHO} "$cmd"
    else
        if [ ! -z ${TEST_VERBOSE} ]; then
            ${ECHO} "command: $cmd"
	fi
	# We need to eval to handle "&&" in commands
        eval $cmd
    fi
}

mecho() {
    if [ "$DOC" = doc ]; then
        ${ECHO} "# $1"
    else
        ${ECHO} "$1"
    fi
}

info () {
    mecho "$@"
}

set_fmt_option () {
    case $1 in
	fullpath*)
	    opt="--libxo @$1"
	    ;;
	*)
	    opt="--libxo:$1"
	    ;;
    esac
}

run_tests () {
    oname=$name.$ds.$fmt
    out=out/$oname
    mecho "... $test ... $fmt ... $name ... $ds ..."
    set_fmt_option
    run "$test $LIBXOPTS $opt $data input $input > $out.out 2> $out.err"

    echo "$oname.out" >> $FILES
    echo "$oname.err" >> $FILES

    run "diff -Nu ${SRCDIR}/saved/$oname.out out/$oname.out | ${S2O}"
    run "diff -Nu ${SRCDIR}/saved/$oname.err out/$oname.err | ${S2O}"
}

do_run_one_command () {
    local test=$1 ; shift
    local fmt=$1 ; shift
    local name=$1 ; shift
    local ds=$1 ; shift
    local input=$1 ; shift

    local oname=`echo "$name.$ds.$fmt" | sed -e 's@:@_@g' -e 's@,@_@g'`
    local out=out/$oname

    mecho "... $test ... $fmt ... $name ... $ds ..."

    set_fmt_option $fmt
    run "$test $LIBXOPTS $opt $* input $input > $out.out 2> $out.err"

    echo "$oname.out" >> $FILES
    echo "$oname.err" >> $FILES

    run "diff -Nu ${SRCDIR}/saved/$oname.out out/$oname.out | ${S2O}"
    run "diff -Nu ${SRCDIR}/saved/$oname.err out/$oname.err | ${S2O}"
}

do_run_one_input () {
    local test=$1 ; shift
    local fmt=$1 ; shift
    local input=$1 ; shift

    local name=`basename $input .in`
    local ds=1

    # echo "run_one_input: ${test}::${fmt}::${input} ..."

    do_run_one_command $test $fmt $name $ds $input

    run_data=`grep '^#run' $input`
    if [ ! -z "${run_data}" ]; then
        echo "${run_data}" | while read comment arguments ; do
	    ds=`expr $ds + 1`
	    do_run_one_command $test $fmt $name $ds $input $arguments
	done
    fi
}

do_run_one_test () {
    local test=$1 ; shift

    local base=`basename $test .test`

    for fmt in ${TEST_FORMATS:-T}; do
  	for input in `echo ${SRCDIR}/${base}*.in`; do
	    if [ -f $input ]; then
		do_run_one_input $test $fmt $input
	    fi
	done
    done
}

do_run_tests_new () {
    local tests="$@"

    mkdir -p out
    cp /dev/null $FILES

    for test in $tests; do
	do_run_one_test $test
    done
}

do_run_tests () {
    mkdir -p out
    cp /dev/null $FILES

    for test in ${TESTS}; do
	base=`basename $test .test`

        for fmt in ${TEST_FORMATS:-T}; do
  	    for input in `echo ${SRCDIR}/${base}*.in`; do
		if [ -f $input ]; then
		    name=`basename $input .in`
		    ds=1
		    grep '^#run' $input | while read comment data ; do
			run_tests
			ds=`expr $ds + 1`
		    done
		fi
	    done
	done
    done
}

accept_file () {
    if ! cmp -s $*; then
        echo "... $1 ..."
        run "cp $*"
    fi
}

do_one_accept () {
    local test=$1 ; shift
    local fmt=$1 ; shift
    local name=$1; shift
    local ds=$1; shift
    local input=$1; shift

    local oname=`echo "$name.$ds.$fmt" | sed -e 's@:@_@g' -e 's@,@_@g'`

    accept_file out/$oname.out ${SRCDIR}/saved/$oname.out
    accept_file out/$oname.err ${SRCDIR}/saved/$oname.err
}

do_accept () {
    local base
    local fmt
    local input
    local ds
    local name

    mkdir -p ${SRCDIR}/saved

    for test in ${TESTS}; do
	base=`basename $test .test`
	base=`basename $base .c`

        for fmt in ${TEST_FORMATS:-T}; do
	    for input in `echo ${SRCDIR}/${base}*.in`; do
		if [ -f $input ]; then
		    name=`basename $input .in`
		    ds=1

		    do_one_accept $test $fmt $name $ds $input

		    grep '^#run' $input | while read comment data ; do
			ds=`expr $ds + 1`
			do_one_accept $test $fmt $name $ds $input
		    done
		fi
	    done
	done
    done

    accept_file $FILES ${SRCDIR}/saved/$FILES_BASE
}

# ---------------------------------------------------------------------------
# Binary-test support (core / utf8 style: no input files)
#
# Format specs in TEST_FORMATS and .fmts sidecar files use one of:
#   NAME            -> --libxo:WNAME   (e.g. T, XP, JP)
#   NAME=LIBXO_OPTS -> --libxo=LIBXO_OPTS   (e.g. E=warn,encoder=test)
#   fullpath:...    -> --libxo @fullpath:... (existing encoder-style)
#
# The token SRCDIR in LIBXO_OPTS is expanded to ${SRCDIR} at run time,
# which lets .fmts files reference files in the source tree
# (e.g. XPmap=warn,xml,pretty,map-file=SRCDIR/test_12.map).
#
# Plugin modes (-m validate, -m pass): the Makefile builds out/base.MODE.test
# and run-tests.sh invokes those; output goes to out/base.MODE.fmt.{out,err}
# but is always diffed against saved/base.fmt.{out,err} (plain baselines).
# ---------------------------------------------------------------------------

# Parse a single format spec; sets $fmt_name and $fmt_opt.
parse_fmt_spec () {
    local spec="$1"
    case $spec in
    fullpath*)
        fmt_name=$(printf '%s\n' "$spec" | sed 's@[,:]@_@g')
        fmt_opt="--libxo @$spec"
        ;;
    *=*)
        fmt_name="${spec%%=*}"
        local libxo_opts="${spec#*=}"
        libxo_opts=$(printf '%s\n' "$libxo_opts" | sed "s@SRCDIR@${SRCDIR}@g")
        fmt_opt="--libxo=$libxo_opts"
        ;;
    *)
        fmt_name="$spec"
        fmt_opt="--libxo:W$spec"
        ;;
    esac
}

# Run one binary test for one format spec.
do_run_bin_one () {
    local binary="$1"
    local base="$2"
    local spec="$3"

    parse_fmt_spec "$spec"

    # Output files get the mode as an infix so plain/validate/pass results
    # can coexist in out/ without overwriting each other.
    local out_base
    if [ "$RUN_MODE" = plain ]; then
        out_base="${base}.${fmt_name}"
    else
        out_base="${base}.${RUN_MODE}.${fmt_name}"
    fi
    local out="out/${out_base}"

    mecho "... $base ... ${RUN_MODE} ... $fmt_name ..."

    run "LC_ALL=en_US.UTF-8 ${CHECKER} ${binary} $LIBXOPTS $fmt_opt ${BIN_EXTRA_ARGS} > ${out}.out 2> ${out}.err"

    echo "$out_base.out" >> $FILES
    echo "$out_base.err" >> $FILES

    # Always diff against plain saved baselines (all modes should match).
    run "diff -Nu ${SRCDIR}/saved/${base}.${fmt_name}.out ${out}.out | ${S2O}"
    run "diff -Nu ${SRCDIR}/saved/${base}.${fmt_name}.err ${out}.err | ${S2O}"
}

# Run all format specs for all listed test binaries.
do_run_bins () {
    mkdir -p out
    cp /dev/null $FILES

    local test_bin base binary fmts_file spec

    for test_bin in "$@"; do
        base=$(basename "$test_bin" .test)

        case $RUN_MODE in
        validate) binary="./${base}.validate-test" ;;
        pass)     binary="./${base}.pass-test" ;;
        *)        binary="./${test_bin}" ;;
        esac

        for spec in ${TEST_FORMATS}; do
            do_run_bin_one "$binary" "$base" "$spec"
        done

        # Per-test extra formats from sidecar file (NAME=OPTS lines, # comments ok)
        if [ ! -z "${EXTRA_FMTS}" ]; then
            fmts_file="${SRCDIR}/${base}.fmts"
            if [ -f "$fmts_file" ]; then
		while IFS= read -r spec; do
                    case $spec in '#'*|'') continue ;; esac
                    do_run_bin_one "$binary" "$base" "$spec"
		done < "$fmts_file"
            fi
	fi
    done
}

# Accept plain-mode output as new baselines.  Plugin-mode output is not
# accepted separately because it must match the plain baselines.
do_accept_bins () {
    mkdir -p "${SRCDIR}/saved"

    local test_bin base spec fmts_file

    for test_bin in "$@"; do
        base=$(basename "$test_bin" .test)

        for spec in ${TEST_FORMATS}; do
            parse_fmt_spec "$spec"
            local oname="${base}.${fmt_name}"
            accept_file "out/$oname.out" "${SRCDIR}/saved/$oname.out"
            accept_file "out/$oname.err" "${SRCDIR}/saved/$oname.err"
        done

        fmts_file="${SRCDIR}/${base}.fmts"
        if [ -f "$fmts_file" ]; then
            while IFS= read -r spec; do
                case $spec in '#'*|'') continue ;; esac
                parse_fmt_spec "$spec"
                local oname="${base}.${fmt_name}"
                accept_file "out/$oname.out" "${SRCDIR}/saved/$oname.out"
                accept_file "out/$oname.err" "${SRCDIR}/saved/$oname.err"
            done < "$fmts_file"
        fi
    done

    accept_file $FILES "${SRCDIR}/saved/$FILES_BASE"
}

#
# pa and xi tests do not work on linux yet
#
case `uname`-`basename $PWD` in
    Linux-pa|Linux-xi) exit 0;;
esac

while [ $# -gt 0 ]
do
    case "$1" in
    -d) SRCDIR=$2; shift;;
    -D) TEST_VERBOSE=1;;
    -n) DOC=doc;;
    -l) LIBXOPTS="$LIBXOPTS --libxo '$2'"; shift;;
    -m) RUN_MODE=$2; shift;;
    -a) BIN_EXTRA_ARGS=$2; shift;;
    -T) TEST_FORMATS=$2; shift;;
    -v) S2O=cat;;
    -X) EXTRA_FMTS=1;;
    -*) echo "unknown option" >&2; exit;;
    *) break;;
    esac
    shift
done

verb=$1
shift

case $verb in
    run-old)
        TESTS="$@"
        do_run_tests
    ;;

    run)
        do_run_tests_new $@
    ;;

    run-one)
	## run-one foo_01.test fullpath foo_01_11.in
        do_run_one_input $1 $2 ${SRCDIR}/$3
    ;;

    run-all)
        TESTS=`echo *test`
        do_run_tests
    ;;

    run-bin)
        do_run_bins "$@"
    ;;

    accept)
        TESTS="$@"
        do_accept
    ;;

    accept-all)
        TESTS=`echo *test`
        do_accept
    ;;

    accept-bin)
        do_accept_bins "$@"
    ;;

    *)
        ${ECHO} "unknown verb: $verb" 1>&2
	;;
esac

exit 0
