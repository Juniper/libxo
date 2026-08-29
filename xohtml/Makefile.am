#
# Copyright 2015, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

man_MANS = xohtml.1

INTERNAL_FILES = \
    xohtml.js \
    xohtml.css

EXTRA_DIST = \
    xohtml.1 \
    xohtml.sh.in \
    ${INTERNAL_FILES}

install-exec-hook:
	install xohtml.sh ${DESTDIR}${bindir}/xohtml
	mkdir -p ${DESTDIR}${XO_SHAREDIR}
	for file in ${INTERNAL_FILES}; do \
	    install ${srcdir}/$$file ${DESTDIR}${XO_SHAREDIR} ; done

uninstall-hook:
	for file in ${INTERNAL_FILES}; do \
	    rm ${DESTDIR}${XO_SHAREDIR}/$$file ; done
	rm -f ${DESTDIR}${bindir}/xohtml

install-data-hook:
	for file in ${man_MANS}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man1dir}/$$file ; done
