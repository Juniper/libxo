#
# Copyright 2014-2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

man_MANS = xolint.1 xo-lexicon.1

# The lexicon directory holds one ${pkg}.lex per package, merged by
# "xo-lexicon merge" into system.lex.  libxo installs a seed lexicon
# derived from the conversions in patches/.
XO_LEXICONDIR = ${XO_SHAREDIR}/lexicon

SEED_LEXICON = \
    lexicon/seed-df.lex \
    lexicon/seed-ls.lex \
    lexicon/seed-netstat.lex \
    lexicon/seed-netstat.discouraged.lex \
    lexicon/seed-ps.lex \
    lexicon/seed-vmstat.lex \
    lexicon/seed-w.lex \
    lexicon/seed-wc.lex

EXTRA_DIST = xolint.1 xolint.pl xo-lexicon.1 xo-lexicon.sh ${SEED_LEXICON}

CLEANFILES = xo-lexicon

all-local: xo-lexicon

xo-lexicon: ${srcdir}/xo-lexicon.sh Makefile
	sed -e 's;%%XO_LEXICONDIR%%;${XO_LEXICONDIR};' \
	    -e 's;%%XO_BINDIR%%;${bindir};' \
	    ${srcdir}/xo-lexicon.sh > $@.tmp
	chmod +x $@.tmp
	mv $@.tmp $@

install-exec-hook: xo-lexicon
	install ${srcdir}/xolint.pl ${DESTDIR}${bindir}/xolint
	install xo-lexicon ${DESTDIR}${bindir}/xo-lexicon

uninstall-hook:
	rm -f ${DESTDIR}${bindir}/xolint ${DESTDIR}${bindir}/xo-lexicon
	for file in ${SEED_LEXICON}; do \
	    rm -f ${DESTDIR}${XO_LEXICONDIR}/`basename $$file` ; done
	if [ -d ${DESTDIR}${XO_LEXICONDIR} ]; then \
	    sh ./xo-lexicon merge -D ${DESTDIR}${XO_LEXICONDIR} ; \
	    if [ -z "`ls ${DESTDIR}${XO_LEXICONDIR} \
	            | grep -v '^system\.'`" ]; then \
	        rm -f ${DESTDIR}${XO_LEXICONDIR}/system.lex \
	            ${DESTDIR}${XO_LEXICONDIR}/system.discouraged.lex ; \
	        rmdir ${DESTDIR}${XO_LEXICONDIR} ; \
	    fi ; \
	fi

install-data-hook: xo-lexicon
	for file in ${man_MANS}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man1dir}/$$file ; done
	${MKDIR_P} ${DESTDIR}${XO_LEXICONDIR}
	for file in ${SEED_LEXICON}; do \
	    install -m 644 ${srcdir}/$$file ${DESTDIR}${XO_LEXICONDIR} ; done
	sh ./xo-lexicon merge -D ${DESTDIR}${XO_LEXICONDIR}
