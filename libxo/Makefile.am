#
# Copyright 2014, Juniper Networks, Inc.
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

libxoincdir = ${includedir}/libxo

AM_CFLAGS = \
    -I${top_srcdir} \
    ${WARNINGS} \
    ${GETTEXT_CFLAGS}

AM_CFLAGS += \
    -DXO_ENCODERDIR=\"${XO_ENCODERDIR}\" \
    -DXO_SHAREDIR=\"${XO_SHAREDIR}\"

lib_LTLIBRARIES = libxo.la

LIBS = \
    ${GETTEXT_LIBS}

libxoinc_HEADERS = \
     xo.h \
     xo_encoder.h

noinst_HEADERS = \
     xo_buf.h \
     xo_explicit.h \
     xo_humanize.h \
     xo_wcwidth.h

libxo_la_SOURCES = \
    libxo.c \
    xo_encoder.c \
    xo_syslog.c \
    xo_tolower.c \
    xo_toupper.c \
    xo_utf8.c

if LIBXO_NEED_FILTERS
libxo_la_SOURCES += \
    xo_filter.c \
    xo_xpath.c \
    xo_xparse.c
endif

YACC_OPTS = -P -p xo_xpath_yy -d -v -s -b xo_xpath
SED_OPTS=\
-e '/extern int YYPARSE_DECL();/d' \
-e 's/yydebug = yyn - .0./do {} while(0)/' \
-e '/typedef int YYINT;/d'

xo_xpath.tab.c.orig xo_xpath.tab.h: xo_xpath.y
	@echo "  YACC     xo_xpath.y ..."
	@${YACC} ${YACC_OPTS} -o xo_xpath.tab.c.orig ${srcdir}/xo_xpath.y

xo_xpath.c: xo_xpath.tab.c.orig
	@echo "  GEN      xo_xpath.c ..."
	@${SED} ${SED_OPTS} < xo_xpath.tab.c.orig > xo_xpath.c

man3_files = \
    libxo.3 \
    xo_attr.3 \
    xo_create.3 \
    xo_emit.3 \
    xo_emit_err.3 \
    xo_err.3 \
    xo_error.3 \
    xo_finish.3 \
    xo_flush.3 \
    xo_message.3 \
    xo_no_setlocale.3 \
    xo_open_container.3 \
    xo_open_list.3 \
    xo_open_marker.3 \
    xo_parse_args.3 \
    xo_set_allocator.3 \
    xo_set_flags.3 \
    xo_set_info.3 \
    xo_set_options.3 \
    xo_set_style.3 \
    xo_set_syslog_enterprise_id.3 \
    xo_set_version.3 \
    xo_set_writer.3 \
    xo_syslog.3

man5_files = \
    xo_format.5

man7_files = \
    libxo.7 \
    xo_csv.7 \
    xo_options.7

man_MANS = ${man3_files} ${man5_files} ${man7_files}

EXTRA_DIST = \
    ${man_MANS}

CLEANFILES = xo_xpath.c xo_xpath.tab.c.orig xo_xpath.tab.h

xo_xparse.o xo_filter.o xo_xparse.lo xo_filter.lo: xo_xpath.tab.h

call-graph:
	${RM} libxo.o
	${MAKE} CC="clang -Xclang -analyze -Xclang \
            -analyzer-checker=debug.ViewCallGraph" libxo.o

install-data-hook:
	for file in ${man3_files}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man3dir}/$$file ; done
	for file in ${man5_files}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man5dir}/$$file ; done
	for file in ${man7_files}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man7dir}/$$file ; done

xpath: all
	${COMPILE} -DTEST_XPATH \
		.libs/libxo.a -L/opt/local/lib -lintl \
		-o xpath ${srcdir}/xo_xparse.c

filter: all
	${COMPILE} -DTEST_FILTER \
		.libs/libxo.a -L/opt/local/lib -lintl \
		-o filter ${srcdir}/xo_filter.c

var:
	@echo ${${VAR}}

