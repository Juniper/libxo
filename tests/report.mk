#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Shared "make tests" support; see build/plans/report.rst.
#
# Each Makefile.am that includes this file puts its tests in a
# "tests-run" rule and gets "test"/"tests" from here.  The first
# "make tests" (the one without REPORT_FILE in its environment) is the
# top: it empties ${REPORT_FILE_DEFAULT}, runs the tests, and then
# shows the failures recorded there.  "make tests" always succeeds.
#
# Rules record failures with the run-tests.sh verbs:
#
#   ${CHECK_DIFFS} SAVED_BASE OUT_BASE [STATUS]
#                                  diff SAVED_BASE.{out,err} against
#                                  OUT_BASE.{out,err}; STATUS is the
#                                  test's exit status, to catch crashes
#   ${CHECK_DIFF} SAVED OUT        diff one file
#   ${REPORT_RECORD} PATH REASON   record any other failure
#
# "make accept" rules copy outputs to saved/ with these, which copy
# and name only the files that changed:
#
#   ${ACCEPT_FILE} OUT SAVED       copy one file
#   ${ACCEPT_FILES} OUT_BASE SAVED_BASE
#                                  copy OUT_BASE.{out,err}
#
# Include this file after CLEANFILES is set.

REPORT_FILE_DEFAULT = report.list
REPORT_SH = ${abs_top_srcdir}/tests/run-tests.sh
REPORT_CMD = ${BASH} ${REPORT_SH}

CHECK_DIFF = S2O="${S2O}" ${REPORT_CMD} check-diff
CHECK_DIFFS = S2O="${S2O}" ${REPORT_CMD} check-diffs
REPORT_RECORD = ${REPORT_CMD} record
ACCEPT_FILE = ${REPORT_CMD} accept-file
ACCEPT_FILES = ${REPORT_CMD} accept-files

CLEANFILES += ${REPORT_FILE_DEFAULT}

.PHONY: test tests tests-run

test tests:
	@if [ -n "$$REPORT_FILE" ] ; then \
	    ${MAKE} tests-run ; \
	else \
	    REPORT_FILE=`pwd`/${REPORT_FILE_DEFAULT} ; \
	    REPORT_PATH= ; \
	    export REPORT_FILE REPORT_PATH ; \
	    ${REPORT_CMD} report-start ; \
	    ${MAKE} tests-run || ${REPORT_RECORD} . "make tests failed" ; \
	    ${REPORT_CMD} report-finish ; \
	fi
