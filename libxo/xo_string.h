/*
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBXO_XO_STRING_H
#define LIBXO_XO_STRING_H

#ifdef HAVE_GCC

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#define MY_GNU_SOURCE
#endif /* _GNU_SOURCE */

#ifndef __USE_GNU
#define __USE_GNU 1
#define MY__USE_GNU
#endif /* __USE_GNU */

#include <string.h>

#ifdef MY_GNU_SOURCE
#undef _GNU_SOURCE
#endif /* MY_GNU_SOURCE */
#ifdef MY__USE_GNU
#undef ___USE_GNU
#endif /* MY__USE_GNU */

#ifdef HAVE_BSD_STRING_H
#include <bsd/string.h>
#endif /* HAVE_BSD_STRING_H */

#else /* HAVE_GCC */
#include <string.h>		/* So simple, eh? */
#endif /* HAVE_GCC */

#endif /* LIBXO_XO_STRING_H */
