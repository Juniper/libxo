/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2022, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, October 2022
 */

/**
 * libxo needs to ensure that it's not emitting invalid utf-8.  There are
 * two reasons for this: first, it's just good behavior, and second, the
 * Unicode TR-36 tags this as a security concern:
 *     https://unicode.org/reports/tr36/
 * So for good hygiene and the internet security, we'll ensure that only
 * valid UTF-8 strings are emitted by libxo.
 *
 * This implementation borrows from https://github.com/sheredom/utf8.h
 */

#ifndef INCLUDE_XO_UTF8_H
#define INCLUDE_XO_UTF8_H

#include <stdint.h>
#include <string.h>

typedef uint32_t xo_codepoint_t;

/**
 * The "length" bits in the first byte are not properly encoded
 */
#define XO_UTF8_ERR_BAD_LEN	((xo_codepoint_t) -1)

/**
 * The trailing bytes (non-first bytes) of the encoding are not
 * proper.  This is an interesting case, since one or more of the
 * trailing bytes may be an ASCII character, which TR-36 says we should
 * not skip.
 */
#define XO_UTF8_ERR_TRAILING	((xo_codepoint_t) -2)

/**
 * The UTF-8 representation is not the shortest form, which is
 * required for security reasons, so that filters can reliably use
 * regular expressions to block particular string content.  Using
 * non-shortest form representations allow evil-doers to circumvent
 * such rules.  See TR-36 for details.
 */
#define XO_UTF8_ERR_NON_SHORT	((xo_codepoint_t) -3)

/**
 * The UTF-8 representation lacks sufficient bytes and was assumably
 * truncated somewhere in transit.
 */
#define XO_UTF8_ERR_TRUNCATED	((xo_codepoint_t) -4)

/**
 * We're looking at a secondary byte (second or later in a series),
 * having missed the first byte.
 */
#define XO_UTF8_ERR_SECONDARY	((xo_codepoint_t) -5)

/**
 * The bytes decode to a surrogate (U+D800 thru U+DFFF).  Surrogates
 * are a UTF-16 mechanism and are not valid in UTF-8.
 */
#define XO_UTF8_ERR_SURROGATE	((xo_codepoint_t) -6)

/**
 * The bytes decode to a value above U+10FFFF, the last codepoint.
 */
#define XO_UTF8_ERR_RANGE	((xo_codepoint_t) -7)

static inline int
xo_utf8_iserror (xo_codepoint_t wc)
{
    return (wc > 0x10ffff);
}

/*
 * The string(3) search functions take a const string and return a
 * non-const pointer into it; this lets ours do the same without
 * tripping -Wcast-qual.
 */
static inline char *
xo_utf8_unconst (const char *cp)
{
    return (char *) (uintptr_t) cp;
}

/**
 * Return a text message describing the error in 'wc'
 */
const char *
xo_utf8_wchar_errmsg (xo_codepoint_t wc);

/*
 * strchrnul is a nice extension from glibc and FreeBSD (10), which we
 * want to support.  If the system doesn't provide it, we carry our own.
 * Note that xo_strchrnul is the 8-bit version; xo_ustrchrnul is the
 * wchar version.
 */
#ifdef HAVE_STRCHRNUL
#define xo_strchrnul strchrnul
#else /* HAVE_STRCHRNUL */
static inline char *
xo_strchrnul (char *str, int c)
{
    unsigned char ch = (unsigned char) c; /* Trim value */

    for ( ; *str && *str != ch; str++)
	continue;

    return (char *) str;
}
#endif /* HAVE_STRCHRNUL */


/**
 * Return non-zero if the byte is part of a UTF-8 character sequence;
 * returns zero if the byte is normal ASCII (7-bit data).
 */
static inline int
xo_is_utf8_byte (char ch)
{
    return (ch & 0x80);
}

/**
 * Return non-zero if the byte is the first byte of a multi-byte UTF-8
 * character, the one whose high bits give the character's length
 * (0b11xxxxxx).
 */
static inline int
xo_is_utf8_len_byte (char ch)
{
    return (ch & 0xc0) == 0xc0;
}

/**
 * Return non-zero if the byte is a secondary byte, one of the bytes
 * that follow the first byte of a multi-byte UTF-8 character
 * (0b10xxxxxx).
 */
static inline int
xo_is_utf8_secondary_byte (char ch)
{
    return ((ch & 0xc0) == 0x80);
}

/**
 * Return the mask for data bits in the first byte of a UTF-8 character
 * of length 'len'.  These bits hold the high bits of the codepoint.
 * Use a 64-bit const to avoid branches or memory references.
 */
static inline int
xo_utf8_data_bits (int n)
{
    const uint64_t data_bits
	= (0x07 << 4)
	| (0x0f << 3)
	| (0x1f << 2)
	| (0x7f << 1);

    return (data_bits >> n) & 0xff;
}

/**
 * Return the mask for len bits in the first byte of a UTF-8 character
 * of length 'len'.  These bits are set to 1.
 */
static inline int
xo_utf8_len_bits (int len)
{
    const uint64_t len_bits
	= (0xf0 << 4)
	| (0xe0 << 3)
	| (0xc0 << 2)
	| (0x00 << 1);

    return (len_bits >> len) & 0xff;
}

/**
 * Inspect the first byte of a UTF-8 character representation and
 * return the number of bytes in the representation, as indicated by
 * the high-order bits.
 */
static inline int
xo_utf8_rlen (char ch)
{
    if ((ch & 0x80) == 0x0)   /* 0xxx xxxx */
        return 1;
    if ((ch & 0xe0) == 0xc0)  /* 110x xxxx */
        return 2;
    if ((ch & 0xf0) == 0xe0)  /* 1110 xxxx */
        return 3;
    if ((ch & 0xf8) == 0xf0)  /* 1111 0xxx */
        return 4;
    return -1;			/* Invalid non-utf8 byte */
}

/**
 * Inspect the first byte of a UTF-8 character representation and
 * return the number of bytes in the representation, as indicated by
 * the high-order bits.
 */
static inline int
xo_utf8_len (char ch)
{
    int ulen = xo_utf8_rlen(ch);
    return (ulen <= 0) ? 1 : ulen;
}

/**
 * Determine the number of bytes needed to encode a wide character.
 */
static inline ssize_t
xo_utf8_to_len (xo_codepoint_t wc)
{
    if ((wc & 0x7f) == wc) /* Simple case */
	return 1;
    if ((wc & 0x7ff) == wc)
	return 2;
    if ((wc & 0xffff) == wc)
	return 3;
    if ((wc & 0x1fffff) == wc)
	return 4;
    return -1;		/* Invalid input wchar */
}

/**
 * Emit one wide character into the given buffer.  'len' must be <= 4.
 */
static inline void
xo_utf8_to_bytes (char *buf, ssize_t len, xo_codepoint_t wc)
{
    ssize_t i = 0;

    if (len == 1) { /* Simple case */
	buf[0] = wc & 0x7f;
	return;
    }

    switch (len) {
    case 4:
	buf[i++] = (char)(0x80 | ((wc >> 18) & 0x07));
	/* fallthru */
    case 3:
	buf[i++] = (char)(0x80 | ((wc >> 12) & 0x3f));
	/* fallthru */
    case 2:
	buf[i++] = (char)(0x80 | ((wc >> 6) & 0x3f));
	buf[i++] = (char)(0x80 | (wc & 0x3f));
    }

    /* Finish off the first byte with the length bits */
    buf[0] &= xo_utf8_data_bits(len); /* Clear out the length bits */
    buf[0] |= xo_utf8_len_bits(len); /* Drop in new length bits */
}

/**
 * Emit one wide character into the given buffer
 */
static inline void
xo_utf8_emit_char (char *buf, ssize_t len, xo_codepoint_t wc)
{
    ssize_t i;

    if (len == 1) { /* Simple case */
	buf[0] = wc & 0x7f;
	return;
    }

    /* Start with the low bits and insert them, six bits at a time */
    for (i = len - 1; i >= 0; i--) {
	buf[i] = 0x80 | (wc & 0x3f);
	wc >>= 6;		/* Drop the low six bits */
    }

    /* Finish off the first byte with the length bits */
    buf[0] &= xo_utf8_data_bits(len); /* Clear out the length bits */
    buf[0] |= xo_utf8_len_bits(len); /* Drop in new length bits */
}

/**
 * Return the codepoint for a UTF-8 character.  The 'len' is a value
 * returned by xo_utf8_len(), and the buf/bufsiz should be sufficient
 * for this length.  The 'on_err' value is what is returned when an
 * error is encountered.  If this value is zero, then a specific error
 * is returned, one of XO_UTF8_ERR_*.
 */
xo_codepoint_t
xo_utf8_codepoint (const char *buf, size_t bufsiz, int len,
		   xo_codepoint_t on_err);

/* --------- */

/**
 * Inspect a string (of length 'n' or less) to see if it's valid
 * UTF-8.  Returns either NULL indicating success, or a pointer to the
 * start of invalid character.
 */
char *
xo_utf8_nvalid (char *str, size_t len);

/**
 * Inspect a string to see if it's valid UTF-8.  Returns either NULL
 * indicating success, or a pointer to the start of invalid character.
 */
static inline char *
xo_utf8_valid (char *str)
{
    return xo_utf8_nvalid(str, strlen(str));
}

/**
 * Ensure that a string is valid UTF-8 by replacing any invalid bytes
 * with the replacement byte.  If the replacement character is NUL (0),
 * then replacement will terminate with the first replacement.  Returns
 * the number of times a replacement is made.
 */
int
xo_utf8_nmakevalid(char *str, size_t len, char replacement);

static inline int
xo_utf8_makevalid (char *str, char replacement)
{
    return xo_utf8_nmakevalid(str, strlen(str), replacement);
}

/**
 * Convert a codepoint to lower case.  This is amazingly complicated.
 */
xo_codepoint_t
xo_utf8_wtolower (xo_codepoint_t wc);

/**
 * Convert a codepoint to upper case.  This is amazingly complicated.
 */
xo_codepoint_t
xo_utf8_wtoupper (xo_codepoint_t wc);

/**
 * Return non-zero if the wide character (codepoint) is lowercase.
 */
static inline int
xo_utf8_wislower (xo_codepoint_t wc)
{
    return (wc != xo_utf8_wtoupper(wc));
}

/**
 * Return non-zero if the wide character (codepoint) is uppercase.
 */
static inline int
xo_utf8_wisupper (xo_codepoint_t wc)
{
    return (wc != xo_utf8_wtolower(wc));
}

/**
 * Return non-zero if the first UTF-8 character in 'str' is a lower
 * case codepoint.
 */
static inline int
xo_utf8_nislower (char *str, size_t len)
{
    if (len == 0)
	return 0;

    int ulen = xo_utf8_len(*str);
    xo_codepoint_t wc = xo_utf8_codepoint(str, len, ulen, ' ');
    return xo_utf8_wislower(wc);
}

/**
 * Return non-zero if the first UTF-8 character in 'str' is a lower
 * case codepoint.
 */
static inline int
xo_utf8_islower (char *str)
{
    return xo_utf8_nislower(str, strlen(str));
}

/**
 * Return non-zero if the first UTF-8 character in 'str' is an upper
 * case codepoint.
 */
static inline int
xo_utf8_nisupper (char *str, size_t len)
{
    if (str == NULL || len == 0)
	return 0;

    int ulen = xo_utf8_len(*str);
    xo_codepoint_t wc = xo_utf8_codepoint(str, len, ulen, ' ');
    return xo_utf8_wisupper(wc);
}

/**
 * Return non-zero if the next UTF-8 character an upper case codepoint
 */
static inline int
xo_utf8_isupper (char *str)
{
    return xo_utf8_nisupper(str, strlen(str));
}

/**
 * Return a pointer to the next codepoint in the input string.  At
 * most 'n' bytes will be inspected.   Returns NULL when a NUL byte
 * is encountered.
 */
static inline char *
xo_utf8_nnext (char *str, size_t len)
{
    if (len == 0 || *str == '\0')
	return NULL;

    int ulen = xo_utf8_len(*str);
    xo_codepoint_t wc = xo_utf8_codepoint(str, len, ulen, 0);
    if (xo_utf8_iserror(wc))
	ulen = 1;		/* Invalid UTF-8 character */

    return str + ulen;
}

/**
 * Return a pointer to the next codepoint in the input string.
 * Returns NULL when a NUL byte is encountered.
 */
static inline char *
xo_utf8_next (char *str)
{
    return xo_utf8_nnext(str, strlen(str));
}

/**
 * Return the previous character in str that appears before cur, or
 * NULL when the start of the string is encountered.
 */
static inline char *
xo_utf8_prev (char *start, char *cur)
{
    char *cp;

    if (cur == NULL || start == NULL || cur <= start)
	return NULL;

    for (cp = cur - 1;; cp--) {
	if (!xo_is_utf8_byte(*cp)) /* ASCII */
	    return cp;		  /* The simple case */

	if (xo_is_utf8_len_byte(*cp))
	    return cp;		  /* Success */

	if (!xo_is_utf8_secondary_byte(*cp))
	    return cp;		  /* Invalid utf-8 character */

	if (cp == start)	  /* Hit the start of string */
	    return NULL;
    }
}

/**
 * Convert a string to lower case.
 */
void
xo_utf8_ntolower (char * restrict str, size_t len);

/**
 * Convert a string to lower case.
 */
static inline void
xo_utf8_tolower (char *str)
{
    return xo_utf8_ntolower(str, strlen(str));
}

/**
 * Convert a string to upper case.
 */
void
xo_utf8_ntoupper (char * str, size_t len);

/**
 * Convert a string to upper case.
 */
static inline void
xo_utf8_toupper (char *str)
{
    return xo_utf8_ntoupper(str, strlen(str));
}

/**
 * UTF-8 version of strncasecmp(3)
 */
int
xo_ustrncasecmp (const char *s1, size_t s1_len, const char *s2, size_t s2_len );

/**
 * UTF-8 version of strcasecmp(3)
 */
static inline int
xo_ustrcasecmp (const char *s1, const char *s2)
{
    return xo_ustrncasecmp(s1, strlen(s1), s2, strlen(s2));
}

/**
 * UTF-8 version of strncat(3) with strlcat(3) safety.  At most
 * 'count' bytes of 'append' are used and a character is never split.
 * Returns the length of the string it tried to create; 'dstsize' or
 * more means the result was truncated.
 */
size_t
xo_ustrlncat (char * restrict dst, const char * restrict append,
	     size_t dstsize, size_t count);

/**
 * UTF-8 version of strlcat(3)
 */
static inline size_t
xo_ustrlcat (char * restrict dst, const char * restrict append, size_t dstsize)
{
    return xo_ustrlncat(dst, append, dstsize, strlen(append));
}

/**
 * UTF-8 version of stpncpy(3).  Unlike stpncpy, the result is always
 * NUL terminated and is not padded, so 'dst' must have room for
 * 'len' + 1 bytes.  A character is never split.  Returns a pointer
 * to the NUL.
 */
char *
xo_ustpncpy (char * restrict dst, const char * restrict src, size_t len);

/**
 * UTF-8 version of stpcpy(3)
 */
static inline char *
xo_ustpcpy (char * restrict dst, const char * restrict src)
{
    return xo_ustpncpy(dst, src, strlen(src));
}

/**
 * UTF-8 version of strncpy(3).  Unlike strncpy, the result is always
 * NUL terminated and is not padded, so 'dst' must have room for
 * 'len' + 1 bytes.  A character is never split.
 */
char *
xo_ustrncpy (char * restrict dst, const char * restrict src, size_t len);

/**
 * UTF-8 version of strcpy(3)
 */
static inline char *
xo_ustrcpy (char * restrict dst, const char * restrict src)
{
    return xo_ustrncpy(dst, src, strlen(src));
}

/**
 * UTF-8 version of strchr(3)
 */
char *
xo_ustrchr_long (const char *str, xo_codepoint_t c);

static inline char *
xo_ustrchr (const char *str, xo_codepoint_t c)
{
    if (c < 0x80)
	return strchr(str, c);
    return xo_ustrchr_long(str, c);
}

/**
 * UTF-8 version of strrchr(3)
 */
char *
xo_ustrrchr_long (const char *str, xo_codepoint_t c);

static inline char *
xo_ustrrchr (const char *str, xo_codepoint_t c)
{
    if (c < 0x80)
	return strrchr(str, c);
    return xo_ustrrchr_long(str, c);
}

/**
 * UTF-8 version of strchrnul(3)
 */
char *
xo_ustrchrnul_long (const char *str, xo_codepoint_t c);

static inline char *
xo_ustrchrnul (char *str, xo_codepoint_t c)
{
    if (c < 0x80)
	return xo_strchrnul(str, c);
    return xo_ustrchrnul_long(str, c);
}

/**
 * UTF-8 version of strspn(3).  Returns a count of bytes, not
 * characters, so the value can be used as an offset into the string.
 */
size_t
xo_ustrspn (const char *str, const char *charset);

/**
 * UTF-8 version of strcspn(3).  Returns a count of bytes, not
 * characters, so the value can be used as an offset into the string.
 */
size_t
xo_ustrcspn (const char *str, const char *charset);

/**
 * UTF-8 version of strndup(3).  At most 'len' bytes are duplicated,
 * less if the copy would end with part of a character.  The caller must free(3) the
 * result.
 */
char *
xo_ustrndup (const char *str, size_t len);

/**
 * UTF-8 version of strdup(3)
 */
static inline char *
xo_ustrdup (const char *str)
{
    return xo_ustrndup(str, strlen(str));
}

/**
 * Return the number of characters (not bytes) in a string, looking
 * at no more than 'len' bytes; a negative 'len' means the string's
 * NUL is the only limit.  Each invalid byte counts as one character.
 */
ssize_t
xo_utf8_buf_clen (const char *buf, ssize_t len);

/**
 * Return the number of characters (not bytes) in a string
 */
static inline ssize_t
xo_utf8_clen (const char *buf)
{
    return xo_utf8_buf_clen(buf, -1);
}

/**
 * UTF-8 version of strnlen(3): the number of characters (not bytes)
 * in the first 'maxlen' bytes of the string.
 */
size_t
xo_ustrnlen (const char *str, size_t maxlen);

/**
 * UTF-8 version of strlen(3): the number of characters (not bytes)
 */
static inline size_t
xo_ustrlen (const char *str)
{
    return xo_utf8_buf_clen(str, -1);
}

/**
 * UTF-8 version of strpbrk(3)
 */
char *
xo_ustrpbrk (const char *str, const char *charset);

/**
 * UTF-8 version of strsep(3).  Each character of 'delim' is a
 * delimiter and may be multi-byte.
 */
char *
xo_ustrsep (char **stringp, const char *delim);

/**
 * UTF-8 version of strncmp(3).  A character that 'len' lands inside
 * is compared whole, so up to three bytes past 'len' may be read
 * (never past a NUL).
 */
int
xo_ustrncmp (const char *s1, const char *s2, size_t len);

/**
 * UTF-8 version of strnstr(3)
 */
char *
xo_ustrnstr (const char *big, const char *little, size_t len);

/**
 * UTF-8 version of strstr(3)
 */
static inline char *
xo_ustrstr (const char *big, const char *little)
{
    return xo_ustrnstr(big, little, strlen(big));
}

/**
 * UTF-8 version of strcasestr(3)
 */
char *
xo_ustrcasestr (const char *big, const char *little);

/**
 * UTF-8 version of strlcpy(3).  'dstsize' is the size of 'dst'.
 * Returns the length of 'src'; 'dstsize' or more means the result was
 * truncated.  A character is never split.
 */
size_t
xo_ustrlcpy (char * restrict dst, const char * restrict src, size_t dstsize);

/**
 * Truncate a string at the given length while keeping the string
 * UTF-8 valid.
 */
size_t
xo_utrunc (char *str, size_t len);

#endif /* INCLUDE_XO_UTF8_H */
