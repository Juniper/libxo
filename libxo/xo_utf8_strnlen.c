/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2022-2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, October 2022
 */

#include <limits.h>

#include "xo_config.h"
#include "xo.h"
#include "xo_utf8.h"

/**
 * UTF-8 version of strnlen(3): return the number of characters (not
 * bytes) in the first 'maxlen' bytes of the string.  Each invalid
 * byte counts as one character.
 */
size_t
xo_ustrnlen (const char *str, size_t maxlen)
{
    /* A count too large for ssize_t is no limit at all */
    return xo_utf8_buf_clen(str, (maxlen > SSIZE_MAX) ? -1 : (ssize_t) maxlen);
}
