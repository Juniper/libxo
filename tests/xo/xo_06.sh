#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

# The "a" (argument) modifier takes the field name from an argument,
# before the value.  xo(1) gets every argument as a string, so the
# name is the next argument on the command line.

XOD="${XO} --depth 1 --leading-xpath /top"
XON="${XOD} --not-first"

${XO} --open top

${XOD} '{a:/%s}\n' name value
${XON} '{a:/%d}{:count-next/%d}\n' count 42 1
${XON} '{La:} {:label-next/%d}\n' "Label" 2
${XON} '{ka:/%s} {a:/%u} {:next/%d}\n' key-name key-value other 7 3
${XON} '{a:/%*d}{:star-next/%d}\n' width 8 1234 4

${XO} --close top
