#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

AM_CFLAGS = -I${top_srcdir} -I${top_srcdir}/libxo

# Ick: maintained by hand!
TEST_CASES = \
xol_01.sh

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.sh=.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.err}}

S2O = | ${SED} '1,/@@/d'

all:

XO_LOGGER=../../xo-logger/xo-logger

TEST_ONE = \
      ${CHECKER} sh ${srcdir}/$$base.sh "${XO_LOGGER}" ${TEST_OPTS} \
      > out/$$base.out 2> out/$$base.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.out out/$$base.out ${S2O} ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.err out/$$base.err ${S2O}

test tests: ${bin_PROGRAMS}
	@${MKDIR} -p out
	-@(for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .sh` ; \
	    echo "... $$test ..."; \
	    ${TEST_ONE}; \
	    true; \
	done)

one:
	-@(test=${TEST_CASE}; ${TEST_ONE} ; true)

accept:
	-@(for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .sh` ; \
	    echo "... $$test ..."; \
	    ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	    ${CP} out/$$base.err ${srcdir}/saved/$$base.err ; \
	done)

CLEANFILES =
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
