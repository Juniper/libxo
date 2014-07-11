#
# Copyright 2011-2013, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if LIBXO_WARNINGS_HIGH
LIBXO_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

libxoincdir = ${includedir}/libxo

AM_CFLAGS = -I${top_srcdir} ${WARNINGS}

lib_LTLIBRARIES = libxo.la

libxoinc_HEADERS = \
     libxo.h

libxo_la_SOURCES = \
    libxo.c

man_MANS = libxo.3x
EXTRA_DIST = libxo.3x
