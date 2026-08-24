#
# $Id$
#
# Copyright 2025, Juniper Networks, Inc.
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

LIBNAME = libxo_filter
pkglib_LTLIBRARIES = libxo_filter.la
#LIBS = \
#    -L${top_builddir}/libxo -lxo

#LDADD = ${top_builddir}/libxo/libxo.la

libxo_filter_la_SOURCES = \
    xo_filter.c \
    xo_xparse.c \
    xo_xpath.c

noinst_HEADERS = \
     xo_filter.h \
     xo_filter_ops.h \
     xo_xparse.h

man7_files = xo_filter.7

man_MANS = ${man7_files}

pkglibdir = ${XO_FILTERDIR}

UGLY_NAME = xo_filter.lib

YACC_OPTS = -P -p xo_xpath_yy -d -v -s -b xo_xpath
SED_OPTS=\
-e '/extern int YYPARSE_DECL();/d' \
-e 's/yydebug = yyn - .0./do {} while(0)/' \
-e '/typedef int YYINT;/d' \
-e '/int yyparse (void .YYPARSE_PARAM);/d' \
-e 's/yyparse (void .YYPARSE_PARAM)/xo_xpath_yyparse (xo_xparse_data_t *xparse_data)/'

xo_xpath.c xo_xpath.tab.h: xo_xpath.y
	@echo "  YACC     xo_xpath.y ..."
	@${YACC} ${YACC_OPTS} -o xo_xpath.tab.c ${srcdir}/xo_xpath.y
	@echo "  GEN      xo_xpath.c ..."
	@${SED} ${SED_OPTS} < xo_xpath.tab.c > xo_xpath.c

CLEANFILES = \
    xo_xpath.c \
    xo_xpath.tab.c \
    xo_xpath.tab.c.orig \
    xo_xpath.tab.h \
    xo_xpath.output

xo_xparse.o xo_filter.o xo_xparse.lo xo_filter.lo: xo_xpath.tab.h

EXTRA_DIST = \
    xo_xpath.y

install-exec-hook:
	@DLNAME=`sh -c '. ./libxo_filter.la ; echo $$dlname'` ; \
		if [ x"$$DLNAME" = x ]; \
                    then DLNAME=${LIBNAME}.${XO_LIBEXT}; fi ; \
		if [ "$(build_os)" = "cygwin" ]; \
		    then DLNAME="../bin/$$DLNAME"; fi ; \
		echo Install link $$DLNAME "->" ${UGLY_NAME} "..." ; \
		mkdir -p ${DESTDIR}${XO_FILTERDIR} ; \
		cd ${DESTDIR}${XO_FILTERDIR} \
		&& chmod +w . \
		&& rm -f ${UGLY_NAME} \
		&& ${LN_S} $$DLNAME ${UGLY_NAME}

xpath: all
	${COMPILE} -DTEST_XPATH \
		.libs/libxo.a -L/opt/local/lib -lintl \
		-o xpath ${srcdir}/xo_xparse.c

filter: all
	${COMPILE} -DTEST_FILTER \
		.libs/libxo.a -L/opt/local/lib -lintl \
		-o filter ${srcdir}/xo_filter.c


OPS_H = ${top_srcdir}/filter/xo_filter_ops.h

build_ops build_xo_filter_ops.h:
	[ -f ${OPS_H} ] && mv ${OPS_H} ${OPS_H}~ ; true
	sh ${top_srcdir}/bin/xo_filter_ops.sh > ${OPS_H}
