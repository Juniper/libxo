#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

XOP="${XO} --depth 1"

${XO} --open top

NF=
for i in one two three four; do
    ${XOP} ${NF} --wrap item 'Item {k:name} is {:value}\n' $i $i
    NF=--not-first
done

${XO} --close top