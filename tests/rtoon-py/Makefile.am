#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

# Runs the unittest suite for the rtoon python package
# (encoder/rtoon/python).  The tests live with the package, but
# everything they write goes here in the build tree: bytecode goes
# to pycache/ via PYTHONPYCACHEPREFIX (so no __pycache__ appears in
# the source tree) and temporary files go to tmp/.

PY_SRCDIR = ${abs_top_srcdir}/encoder/rtoon/python
PY_CACHE = ${abs_builddir}/pycache
PY_TMP = ${abs_builddir}/tmp

PY_ENV = \
    PYTHONPATH=${PY_SRCDIR} \
    PYTHONPYCACHEPREFIX=${PY_CACHE} \
    RTOON_TEST_TMPDIR=${PY_TMP} \
    LIBXO_BUILD_DIR=${abs_top_builddir}

# Each test module runs on its own, with its output kept in
# out/<module>.log; the log is shown only when the module fails.
# "make tests PY_OPTS=-v" lists each test in the logs.
PY_OPTS =

tests-run:
	@${MKDIR_P} ${PY_TMP} out
	-@(for test in `cd ${PY_SRCDIR}/tests ; echo test_*.py` ; do \
	    base=`${BASENAME} $$test .py` ; \
	    echo "... $$base ..." ; \
	    env ${PY_ENV} ${PYTHON3} -m unittest discover ${PY_OPTS} \
	        -s ${PY_SRCDIR}/tests -t ${PY_SRCDIR}/tests -p $$test \
	        > out/$$base.log 2>&1 \
	    || { echo "*** $$base failed (out/$$base.log)" ; \
	         cat out/$$base.log ; \
	         ${REPORT_RECORD} out/$$base.log "unittest failed" ; } ; \
	done)

# Nothing to accept: the tests check their own results
accept:

CLEANFILES =

include ${top_srcdir}/tests/report.mk

clean-local:
	rm -rf ${PY_CACHE} ${PY_TMP} out
