#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

# Integer grouping.  Each case gives "grouping" explicitly, so the
# output does not depend on the locale.  Grouping only applies to the
# TEXT and HTML styles; the others should show plain digits.

XOD="${XO} --depth 1 --leading-xpath /top"
XON="${XOD} --not-first"
XOG="${XON} --libxo group,grouping=+3"

${XO} --open top

# The leftmost group is short with a repeating shape
${XOD} --libxo group,grouping=+3 '{:repeat/%d}\n' 1234567890

# Uneven shapes (en_IN style)
${XON} --libxo group,grouping=+3+2 '{:uneven/%d}\n' 1234567890

# "-1" stops grouping, leaving a long leftmost group that must not be
# grouped a second time (giving "1.234.567.890" or "1912.276.171")
${XON} --libxo group,grouping=.+3+-1 '{:stop/%d}\n' 1234567890
${XON} --libxo group,grouping=.+3+-1 '{:stop2/%d}\n' 1912276171

# A multi-character separator
${XON} --libxo "group,grouping=::+4" '{:wide/%u}\n' 123456789012

# Every decimal conversion is grouped; others are not
${XOG} '{:d/%d} {:i/%i} {:u/%u} {:ld/%ld} {:D/%D} {:U/%U}\n' \
    1234567 1234567 1234567 1234567 1234567 1234567
${XOG} '{:x/%x} {:o/%o} {:f/%.1f}\n' 1234567 1234567 1234567.5

# Negative values keep their sign in front; "--" keeps getopt from
# taking the value as an option
${XOG} -- '{:neg/%d}\n' -1234567

# Separators are added after width padding; leading zeros are not
# grouped at all
${XOG} '[{:right/%10d}] [{:left/%-10d}] [{:zero/%010d}]\n' \
    1234567 1234567 1234567

# Short values get no separator
${XOG} '{:short/%d} {:edge/%d}\n' 999 1000

# "{i:}" groups a single field without the "group" option
${XON} --libxo grouping=+3 '{i:one/%d} {:other/%d}\n' 1234567 1234567

${XO} --close top
