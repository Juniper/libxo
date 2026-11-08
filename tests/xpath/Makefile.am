#
# $Id$
#
# Copyright 2014-2026, Juniper Networks, Inc.
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
    -I${top_builddir}/filter \
    -I${top_srcdir}/filter \
    ${WARNINGS}

# Ick: maintained by hand!
TEST_CASES = \
xpath_01.c

xpath_01_test_SOURCES = xpath_01.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )

noinst_PROGRAMS = ${TEST_CASES:.c=.test}

LDADD = \
    ${top_builddir}/libxo/libxo.la \
    ${top_builddir}/filter/libxo_filter.la

if HAVE_HUMANIZE_NUMBER
LDADD += -lutil
endif

if HAVE_LIBBSD
LDADD += -lbsd
endif

#
# Sadly, pathetically, this list must be maintained by hand.  We can't
# just say 'TEST_FILES=`sed "s:^:saved:" ${srcdir}/saved/files.txt`.
#
TEST_FILES= \
    xpath_01_01.1.fullpath_slax_pretty.out \
    xpath_01_01.1.fullpath_slax_pretty.err \
    xpath_01_02.1.fullpath_slax_pretty.out \
    xpath_01_02.1.fullpath_slax_pretty.err \
    xpath_01_03.1.fullpath_slax_pretty.out \
    xpath_01_03.1.fullpath_slax_pretty.err \
    xpath_01_04.1.fullpath_slax_pretty.out \
    xpath_01_04.1.fullpath_slax_pretty.err \
    xpath_01_05.1.fullpath_slax_pretty.out \
    xpath_01_05.1.fullpath_slax_pretty.err \
    xpath_01_06.1.fullpath_slax_pretty.out \
    xpath_01_06.1.fullpath_slax_pretty.err \
    xpath_01_07.1.fullpath_slax_pretty.out \
    xpath_01_07.1.fullpath_slax_pretty.err \
    xpath_01_08.1.fullpath_slax_pretty.out \
    xpath_01_08.1.fullpath_slax_pretty.err \
    xpath_01_09.1.fullpath_slax_pretty.out \
    xpath_01_09.1.fullpath_slax_pretty.err \
    xpath_01_10.1.fullpath_slax_pretty.out \
    xpath_01_10.1.fullpath_slax_pretty.err \
    xpath_01_11.1.fullpath_slax_pretty.out \
    xpath_01_11.1.fullpath_slax_pretty.err \
    xpath_01_12.1.fullpath_slax_pretty.out \
    xpath_01_12.1.fullpath_slax_pretty.err \
    xpath_01_13.1.fullpath_slax_pretty.out \
    xpath_01_13.1.fullpath_slax_pretty.err \
    xpath_01_14.1.fullpath_slax_pretty.out \
    xpath_01_14.1.fullpath_slax_pretty.err \
    xpath_01_15.1.fullpath_slax_pretty.out \
    xpath_01_15.1.fullpath_slax_pretty.err \
    xpath_01_16.1.fullpath_slax_pretty.out \
    xpath_01_16.1.fullpath_slax_pretty.err \
    xpath_01_17.1.fullpath_slax_pretty.out \
    xpath_01_17.1.fullpath_slax_pretty.err \
    xpath_01_18.1.fullpath_slax_pretty.out \
    xpath_01_18.1.fullpath_slax_pretty.err \
    xpath_01_19.1.fullpath_slax_pretty.out \
    xpath_01_19.1.fullpath_slax_pretty.err \
    xpath_01_20.1.fullpath_slax_pretty.out \
    xpath_01_20.1.fullpath_slax_pretty.err \
    xpath_01_21.1.fullpath_slax_pretty.out \
    xpath_01_21.1.fullpath_slax_pretty.err \
    xpath_01_22.1.fullpath_slax_pretty.out \
    xpath_01_22.1.fullpath_slax_pretty.err \
    xpath_01_23.1.fullpath_slax_pretty.out \
    xpath_01_23.1.fullpath_slax_pretty.err \
    xpath_01_24.1.fullpath_slax_pretty.out \
    xpath_01_24.1.fullpath_slax_pretty.err \
    xpath_01_25.1.fullpath_slax_pretty.out \
    xpath_01_25.1.fullpath_slax_pretty.err \
    xpath_01_26.1.fullpath_slax_pretty.out \
    xpath_01_26.1.fullpath_slax_pretty.err \
    xpath_01_27.1.fullpath_slax_pretty.out \
    xpath_01_27.1.fullpath_slax_pretty.err \
    xpath_01_01.1.fullpath.out \
    xpath_01_01.1.fullpath.err \
    xpath_01_02.1.fullpath.out \
    xpath_01_02.1.fullpath.err \
    xpath_01_03.1.fullpath.out \
    xpath_01_03.1.fullpath.err \
    xpath_01_04.1.fullpath.out \
    xpath_01_04.1.fullpath.err \
    xpath_01_05.1.fullpath.out \
    xpath_01_05.1.fullpath.err \
    xpath_01_06.1.fullpath.out \
    xpath_01_06.1.fullpath.err \
    xpath_01_07.1.fullpath.out \
    xpath_01_07.1.fullpath.err \
    xpath_01_08.1.fullpath.out \
    xpath_01_08.1.fullpath.err \
    xpath_01_09.1.fullpath.out \
    xpath_01_09.1.fullpath.err \
    xpath_01_10.1.fullpath.out \
    xpath_01_10.1.fullpath.err \
    xpath_01_11.1.fullpath.out \
    xpath_01_11.1.fullpath.err \
    xpath_01_12.1.fullpath.out \
    xpath_01_12.1.fullpath.err \
    xpath_01_13.1.fullpath.out \
    xpath_01_13.1.fullpath.err \
    xpath_01_14.1.fullpath.out \
    xpath_01_14.1.fullpath.err \
    xpath_01_15.1.fullpath.out \
    xpath_01_15.1.fullpath.err \
    xpath_01_16.1.fullpath.out \
    xpath_01_16.1.fullpath.err \
    xpath_01_17.1.fullpath.out \
    xpath_01_17.1.fullpath.err \
    xpath_01_18.1.fullpath.out \
    xpath_01_18.1.fullpath.err \
    xpath_01_19.1.fullpath.out \
    xpath_01_19.1.fullpath.err \
    xpath_01_20.1.fullpath.out \
    xpath_01_20.1.fullpath.err \
    xpath_01_21.1.fullpath.out \
    xpath_01_21.1.fullpath.err \
    xpath_01_22.1.fullpath.out \
    xpath_01_22.1.fullpath.err \
    xpath_01_23.1.fullpath.out \
    xpath_01_23.1.fullpath.err \
    xpath_01_24.1.fullpath.out \
    xpath_01_24.1.fullpath.err \
    xpath_01_25.1.fullpath.out \
    xpath_01_25.1.fullpath.err \
    xpath_01_26.1.fullpath.out \
    xpath_01_26.1.fullpath.err \
    xpath_01_27.1.fullpath.out \
    xpath_01_27.1.fullpath.err \
    xpath_01_01.1.XP.out \
    xpath_01_01.1.XP.err \
    xpath_01_02.1.XP.out \
    xpath_01_02.1.XP.err \
    xpath_01_03.1.XP.out \
    xpath_01_03.1.XP.err \
    xpath_01_04.1.XP.out \
    xpath_01_04.1.XP.err \
    xpath_01_05.1.XP.out \
    xpath_01_05.1.XP.err \
    xpath_01_06.1.XP.out \
    xpath_01_06.1.XP.err \
    xpath_01_07.1.XP.out \
    xpath_01_07.1.XP.err \
    xpath_01_08.1.XP.out \
    xpath_01_08.1.XP.err \
    xpath_01_09.1.XP.out \
    xpath_01_09.1.XP.err \
    xpath_01_10.1.XP.out \
    xpath_01_10.1.XP.err \
    xpath_01_11.1.XP.out \
    xpath_01_11.1.XP.err \
    xpath_01_12.1.XP.out \
    xpath_01_12.1.XP.err \
    xpath_01_13.1.XP.out \
    xpath_01_13.1.XP.err \
    xpath_01_14.1.XP.out \
    xpath_01_14.1.XP.err \
    xpath_01_15.1.XP.out \
    xpath_01_15.1.XP.err \
    xpath_01_16.1.XP.out \
    xpath_01_16.1.XP.err \
    xpath_01_17.1.XP.out \
    xpath_01_17.1.XP.err \
    xpath_01_18.1.XP.out \
    xpath_01_18.1.XP.err \
    xpath_01_19.1.XP.out \
    xpath_01_19.1.XP.err \
    xpath_01_20.1.XP.out \
    xpath_01_20.1.XP.err \
    xpath_01_21.1.XP.out \
    xpath_01_21.1.XP.err \
    xpath_01_22.1.XP.out \
    xpath_01_22.1.XP.err \
    xpath_01_23.1.XP.out \
    xpath_01_23.1.XP.err \
    xpath_01_24.1.XP.out \
    xpath_01_24.1.XP.err \
    xpath_01_25.1.XP.out \
    xpath_01_25.1.XP.err \
    xpath_01_26.1.XP.out \
    xpath_01_26.1.XP.err \
    xpath_01_27.1.XP.out \
    xpath_01_27.1.XP.err \
    xpath_01_01.1.JP.out \
    xpath_01_01.1.JP.err \
    xpath_01_02.1.JP.out \
    xpath_01_02.1.JP.err \
    xpath_01_03.1.JP.out \
    xpath_01_03.1.JP.err \
    xpath_01_04.1.JP.out \
    xpath_01_04.1.JP.err \
    xpath_01_05.1.JP.out \
    xpath_01_05.1.JP.err \
    xpath_01_06.1.JP.out \
    xpath_01_06.1.JP.err \
    xpath_01_07.1.JP.out \
    xpath_01_07.1.JP.err \
    xpath_01_08.1.JP.out \
    xpath_01_08.1.JP.err \
    xpath_01_09.1.JP.out \
    xpath_01_09.1.JP.err \
    xpath_01_10.1.JP.out \
    xpath_01_10.1.JP.err \
    xpath_01_11.1.JP.out \
    xpath_01_11.1.JP.err \
    xpath_01_12.1.JP.out \
    xpath_01_12.1.JP.err \
    xpath_01_13.1.JP.out \
    xpath_01_13.1.JP.err \
    xpath_01_14.1.JP.out \
    xpath_01_14.1.JP.err \
    xpath_01_15.1.JP.out \
    xpath_01_15.1.JP.err \
    xpath_01_16.1.JP.out \
    xpath_01_16.1.JP.err \
    xpath_01_17.1.JP.out \
    xpath_01_17.1.JP.err \
    xpath_01_18.1.JP.out \
    xpath_01_18.1.JP.err \
    xpath_01_19.1.JP.out \
    xpath_01_19.1.JP.err \
    xpath_01_20.1.JP.out \
    xpath_01_20.1.JP.err \
    xpath_01_21.1.JP.out \
    xpath_01_21.1.JP.err \
    xpath_01_22.1.JP.out \
    xpath_01_22.1.JP.err \
    xpath_01_23.1.JP.out \
    xpath_01_23.1.JP.err \
    xpath_01_24.1.JP.out \
    xpath_01_24.1.JP.err \
    xpath_01_25.1.JP.out \
    xpath_01_25.1.JP.err \
    xpath_01_26.1.JP.out \
    xpath_01_26.1.JP.err \
    xpath_01_27.1.JP.out \
    xpath_01_27.1.JP.err

EXTRA_DIST = \
    ${TEST_CASES} \
    xpath_01_23.inc \
    ${addprefix saved/, ${TEST_FILES}}

# Filter for diff output; "make test S2O=cat" keeps the diff headers
S2O = ${SED} '1,/@@/d'

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

#TEST_TRACE = set -x ; 

TEST_JIG = \
      ${CHECKER} ./$$base.test --libxo$$xoopts ${TEST_OPTS} \
      > out/$$base.$$fmt.out 2> out/$$base.$$fmt.err ; \
 ${CHECK_DIFFS} ${srcdir}/saved/$$base.$$fmt out/$$base.$$fmt $$?

TEST_JIG2 = \
echo "... $$test ... $$fmt ..."; \
xoopts==warn,$$extra ; \
${TEST_JIG}; true;

TEST_FORMATS = fullpath:slax,pretty fullpath XP JP

RUN_TESTS = ${srcdir}/../run-tests.sh -d ${srcdir}

RUN_TESTS_CMD = ${BASH} ${RUN_TESTS} -T "${TEST_FORMATS}"

tests-run: ${bin_PROGRAMS}
	@${MKDIR} -p out
	@${RUN_TESTS_CMD} -l warn,filter-warn run ${TEST_CASES:.c=.test}

# Run each test under leaks(1) (macOS).  ./xpath_01.test is a libtool
# wrapper script, so we run the real binary from .libs, with the
# installed libraries.  The leaks report lands in the .out files, so
# expect diffs; read them for "leaks for" lines.
#LEAKS_CMD = DYLD_LIBRARY_PATH=${libdir} leaks --atExit --
LEAKS_CMD = ${LIBTOOL} --mode=execute leaks --atExit --

test-leaks: codesign.tstamp
	@${MKDIR} -p out
	@${RUN_TESTS_CMD} -l warn,filter-warn -c "${LEAKS_CMD}" \
	    run ${TEST_CASES:%.c=.libs/%.test}

# leaks(1) needs the get-task-allow entitlement, which the linker's
# ad-hoc signature lacks.  Re-sign only when a program is relinked.
GTA_PLIST = ${top_srcdir}/packaging/libxo.gta.plist

codesign.tstamp: ${noinst_PROGRAMS} ${GTA_PLIST}
	@for p in ${noinst_PROGRAMS}; do \
	    codesign -s - -f --entitlements ${GTA_PLIST} .libs/$$p \
	        || exit 1; \
	done
	@touch codesign.tstamp

codesign: codesign.tstamp

.PHONY: test-leaks codesign

#
# xpath_02 replays the xpath_01 inputs through the filter "walk" API
# (the API libpin uses) and prints fullpath-style lines.  "test-walk"
# diffs each backend's output against xpath_01's fullpath output, so
# run "make tests" first.  WALK_SKIP lists inputs with known, expected
# differences.
#
noinst_PROGRAMS += xpath_02.test
xpath_02_test_SOURCES = xpath_02.c
EXTRA_DIST += xpath_02.c

WALK_BACKENDS = tree stream

# Inputs to leave out of test-walk, e.g. "WALK_SKIP = xpath_01_23"
WALK_SKIP =

TEST_NUM = *

test-walk: xpath_02.test
	@${MKDIR} -p out
	-@(for f in ${srcdir}/xpath_01_${TEST_NUM}.in ; do \
	    base=`${BASENAME} $$f .in` ; \
	    case " ${WALK_SKIP} " in *" $$base "*) continue ;; esac ; \
	    for be in ${WALK_BACKENDS} ; do \
		echo "... $$base ... $$be ..." ; \
		./xpath_02.test input $$f quiet $$be \
		    > out/$$base.walk.$$be.out 2> out/$$base.walk.$$be.err ; \
		${DIFF} -u out/$$base.1.fullpath.out \
		    out/$$base.walk.$$be.out ; \
	    done ; \
	done ; true)

accept:
	@${MKDIR} -p ${srcdir}/saved
	@${RUN_TESTS_CMD} accept ${TEST_CASES}

.c.test:
	$(AM_V_CC)$(LTCOMPILE) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<

CLEANFILES = ${TEST_CASES:.c=.test} codesign.tstamp

include ${top_srcdir}/tests/report.mk
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}

echo:
	echo ${${VAR}}

##
## (for c in 8 9; do DYLD_LIBRARY_PATH=/Users/phil/work/root/xo/lib leaks --atExit -- tests/xpath/.libs/xpath_01.test --libxo:XP input ../tests/xpath/xpath_01_22.in case $c quiet 2>&1 ; done)  > /tmp/2
##
