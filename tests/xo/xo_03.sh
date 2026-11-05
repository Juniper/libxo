#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

# The encoder tests choose their own style, so they want xo alone
set -- ${XO}
XOBIN=$1

XOP="${XO} --warn"

# Option words that are accepted and ignored must not draw a warning
${XOP} --libxo retain 'retain {:option}\n' accepted
${XOP} --libxo no-retain 'no-retain {:option}\n' accepted
echo

# CBOR is binary, so show it as hex, one value to a line
hex () {
    od -An -tx1 | sed -e 's/  */ /g' -e 's/^ //' -e 's/ $//' -e '/^$/d'
}

XOC="${XOBIN} --libxo encoder=cbor"

# Integers on both sides of each change in the size of the encoding
for value in 0 23 24 255 256 65535 65536 2147483647; do
    echo "cbor integer ${value}:"
    ${XOC} '{:v/%d}' ${value} | hex
done

# Strings on both sides of the first change in the size of the length
for value in abcdefghijklmnopqrstuvw abcdefghijklmnopqrstuvwx ; do
    echo "cbor string ${value}:"
    ${XOC} '{:v}' ${value} | hex
done

# Content that only starts like a number stays a string
for value in 1.5 12abc ; do
    echo "cbor content ${value}:"
    ${XOC} '{:v/%s}' ${value} | hex
done

# The fullpath encoder escapes quotes, backslashes and control
# characters, and leaves UTF-8 alone
XOF="${XOBIN} --libxo @fullpath --wrap top/item"

${XOF} '{:tab}\n' "`printf 'left\tright'`"
${XOF} '{:newline}\n' "`printf 'line one\nline two'`"
${XOF} '{:quotes}\n' "say \"hi\" and 'bye'"
${XOF} '{:backslash}\n' 'c:\dir\file'
${XOF} '{:control}\n' "`printf 'soh\001and\177del'`"
${XOF} '{:accents}\n' "`printf 'caf\303\251 na\303\257ve'`"
