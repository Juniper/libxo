#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if LIBXO_WARNINGS_HIGH
LIBXO_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

AM_CFLAGS = -I${top_srcdir} -I${top_srcdir}/libxo ${WARNINGS}

TEST_CASES = \
test_01.c \
test_02.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )

bin_PROGRAMS = ${TEST_CASES:.c=.bin}

test_01_bin_SOURCES = test-01.c
test_02_bin_SOURCES = test-02.c

LDADD = \
    ${top_builddir}/libxo/libxo.la

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.c=.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.out}}

S2O = | ${SED} '1,/@@/d'

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

#TEST_TRACE = set -x ; 

TEST_ONE = \
 ${CHECKER} $$base.bin ${TEST_OPTS} > out/$$base.out 2> out/$$base.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.out out/$$base.out ${S2O} ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.err out/$$base.err ${S2O}

test tests: ${bin_PROGRAMS}
	@${MKDIR} -p out
	-@ ${TEST_TRACE} (for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .c` ; \
	    echo "... $$test ..."; \
	    ${TEST_ONE}; \
            true; \
	done)

one:
	-@(test=${TEST_CASE}; data=${TEST_DATA}; ${TEST_ONE} ; true)

accept:
	-@(for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .c` ; \
	    echo "... $$test ..."; \
	    ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	    ${CP} out/$$base.err ${srcdir}/saved/$$base.err ; \
	done)

.c.bin:
	$(AM_V_CC)$(LTCOMPILE) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<

CLEANFILES = ${TEST_CASES:.c=.bin}
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
