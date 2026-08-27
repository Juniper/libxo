/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, April 2025
 */

#ifndef XO_DYLD_H
#define XO_DYLD_H

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#if !defined(HAVE_DLFUNC)
#define dlfunc(_p, _n)		dlsym(_p, _n)
#endif
#else /* HAVE_DLFCN_H */
#define dlopen(_n, _f)		NULL /* Fail */
#define dlsym(_p, _n)		NULL /* Fail */
#define dlfunc(_p, _n)		NULL /* Fail */
#endif /* HAVE_DLFCN_H */

typedef void (*xo_universal_func_t)(void);

/*
 * Return the encoder function for a specific shared library.  This is
 * really just a means of keeping the annoying gcc verbiage out of the
 * main code.  And that's only need because gcc breaks dlfunc's
 * promise that I can cast it's return value to a function: "The
 * precise return type of dlfunc() is unspecified; applications must
 * cast it to an appropriate function pointer type."
 */
static inline xo_universal_func_t
xo_dyld_func (void *dlp, const char *fname)
{
    xo_universal_func_t func;

#if defined(HAVE_GCC) && __GNUC__ > 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif /* HAVE_GCC */

    func = (xo_universal_func_t) dlfunc(dlp, fname);

#if defined(HAVE_GCC) && __GNUC__ > 8
#pragma GCC diagnostic pop	/* Restore previous setting */
#endif /* HAVE_GCC */

    return func;
}

static inline void *
xo_dyld_open (const char *path, const char *name, const char *ext)
{
    int plen = strlen(path);
    int nlen = strlen(name);
    int elen = strlen(ext);

    char buf[plen + nlen + elen + 3];
    char *bp = buf;

    memcpy(bp, path, plen);
    bp += plen;
    if (plen && bp[-1] != '/')
	*bp++ = '/';
    memcpy(bp, name, nlen);
    bp += nlen;
    if (*ext)
	*bp++ = '.';
    memcpy(bp, ext, elen + 1);
    
    void *dlp = dlopen(buf, RTLD_NOW);
    return dlp;
}

#endif /* XO_DYLD_H */
