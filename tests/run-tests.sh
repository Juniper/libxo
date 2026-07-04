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

format_options["fullpath"]="@fullpath"

set_fmt_option () {
    case $1 in
	*)
	    if [ -z "${format_options[$1]}" ]; then
		opt="--libxo:$1";
	    else
		opt="--libxo ${format_options[$1]}";
            fi;;
    esac
}

run_tests () {
    oname=$name.$ds.$fmt
    out=out/$oname
    mecho "... $test ... $name ... $ds ..."
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

    local oname=$name.$ds.$fmt
    local out=out/$oname

    mecho "... $test ... $name ... $ds ..."

    set_fmt_option
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

    grep '^#run' $input | while read comment arguments ; do
	do_run_one_command $test $fmt $name $ds $input $arguments
	ds=`expr $ds + 1`
    done
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

accept_tests () {
    oname=$name.$ds.$fmt

    accept_file out/$oname.out ${SRCDIR}/saved/$oname.out
    accept_file out/$oname.err ${SRCDIR}/saved/$oname.err
}

do_accept () {
    mkdir -p ${SRCDIR}/saved

    for test in ${TESTS}; do
	base=`basename $test .test`
	base=`basename $base .c`

        for fmt in ${TEST_FORMATS:-T}; do
	    for input in `echo ${SRCDIR}/${base}*.in`; do
		if [ -f $input ]; then
		    name=`basename $input .in`
		    ds=1
		    grep '^#run' $input | while read comment data ; do
			accept_tests
			ds=`expr $ds + 1`
		    done
		fi
	    done
	done
    done

    accept_file $FILES ${SRCDIR}/saved/$FILES_BASE
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
    -T) TEST_FORMATS=$2; shift;;
    -v) S2O=cat;;
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

    accept)
        TESTS="$@"
        do_accept
    ;;

    accept-all)
        TESTS=`echo *test`
        do_accept
    ;;

    *)
        ${ECHO} "unknown verb: $verb" 1>&2
	;;
esac

exit 0
