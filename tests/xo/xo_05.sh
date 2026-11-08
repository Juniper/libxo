#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

# The "B" (boolean) field role.  xo(1) gets every argument as a
# string, so only an empty argument is false, whatever the conversion
# spec says: "0" is true, even for "%d".

XOD="${XO} --depth 1 --leading-xpath /top"
XON="${XOD} --not-first"

${XO} --open top

# The default spec is "%s": empty is false, anything else is true
${XOD} '{B:empty}{:empty-next/%d}\n' "" 1
${XON} '{B:full}{:full-next/%d}\n' "x" 2
${XON} '{B:zero-string}{:zero-string-next/%d}\n' "0" 3
${XON} '{B:false-string}{:false-string-next/%d}\n' "false" 4

# A "%d" spec does not change the rule: "0" is still true
${XON} '{B:zero-int/%d}{:zero-int-next/%d}\n' 0 5
${XON} '{B:empty-int/%d}{:empty-int-next/%d}\n' "" 6

# Words, with and without "f", and with escapes.  xo(1) handles
# backslash escapes in the format itself, so the libxo escape "\+"
# must be written as "\\+".
${XON} '{B:power/on+off%d}{:power-next/%d}\n' 0 7
${XON} '{Bf:power-off/on+off%d}{:power-off-next/%d}\n' "" 8
${XON} '{B:escaped/a\\+b+100%%%d}{:escaped-next/%d}\n' 1 9
${XON} '{B:escaped-off/a\\+b+100%%%d}{:escaped-off-next/%d}\n' "" 9

# A bad pair is reported, and the field shows true/false
${XON} '{B:bad/a+b+c%d}{:bad-next/%d}\n' 1 9

# A "*" pair takes its own argument, before the value
${XON} '{B:pair/*%d}{:pair-next/%d}\n' "yes+no" 1 10
${XON} '{B:pair-off/*%d}{:pair-off-next/%d}\n' "yes+no" "" 11
${XON} '{B:pair-empty/*%d}{:pair-empty-next/%d}\n' "" 1 12

# Star ints are consumed, but don't affect the value
${XON} '{B:stars/%*.*s}{:stars-next/%d}\n' 10 20 "" 13
${XON} '{B:at-star/%@*@d}{:at-star-next/%d}\n' 99 1 14

# full-boolean emits the words (or the encoding format's words) as data
${XON} '{B,full-boolean:raw/on+off%d}{:raw-next/%d}\n' 1 15
${XON} '{B,full-boolean:enc/T+F%d/1+0%d}{:enc-next/%d}\n' "" 16
${XON} '{Bn,full-boolean:enc-n/T+F%d/1+0%d}{:enc-n-next/%d}\n' 1 17

# "q" quotes the value in JSON
${XON} '{Bq:quoted}{:quoted-next/%d}\n' "x" 18

# Labels and alignment
${XON} '{Lwc:Enabled}{B:enabled/yes+no}\n' "x"
${XON} '{Lwc:Disabled}{B:disabled/yes+no}\n' ""

${XO} --close top
