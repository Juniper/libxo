#
# $Id$
#
# Copyright 2019, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

XOP="${XO} --warn"

# This is testing --wrap, --open, --close, --top-wrap, etc, so
# the output is not a single valid document

set -- 'The capital of {:state} is {:city}\n' 'North Carolina' Raleigh

${XOP} --top-wrap --open a/b/c "$@"
${XOP} --top-wrap --close a/b/c --not-first "$@"

${XOP} --top-wrap --wrap a/b/c "$@"

${XOP} --top-wrap --open a/b/c "$@"
${XOP} --depth 4 --not-first --wrap d/e/f "$@"
${XOP} --top-wrap --close a/b/c --not-first "$@"

${XOP} --wrap a/b/c "$@"

${XOP} --top-wrap --wrap a/b/c "$@"

${XOP} --top-wrap "test\n"

${XOP} --open answer
${XOP} "Answer:"
${XOP} --continuation "$@"
${XOP} --close answer

${XOP} --help
