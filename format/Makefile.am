#
# $Id$
#
# Copyright 2026, Juniper Networks, Inc.
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

enc_csvincdir = ${includedir}/libxo

AM_CFLAGS = \
    -I${top_srcdir}/libxo \
    -I${top_builddir}/libxo \
    ${WARNINGS}

LIBNAME = libxo_format
lib_LTLIBRARIES = libxo_format.la
#LIBS = \
#    -L${top_builddir}/libxo -lxo

#LDADD = ${top_builddir}/libxo/libxo.la

nodist_libxo_format_la_SOURCES = xo_format.c

xo_format.c: $(top_srcdir)/libxo/xo_format.c
	$(LN_S) $(top_srcdir)/libxo/xo_format.c xo_format.c

CLEANFILES = xo_format.c
