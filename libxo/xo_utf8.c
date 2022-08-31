
/*
 * These next few function are make The Essential UTF-8 Ginsu Knife.
 * Identify an input and output character, and convert it.
 */
static uint8_t xo_utf8_data_bits[5] = { 0, 0x7f, 0x1f, 0x0f, 0x07 };
static uint8_t xo_utf8_len_bits[5]  = { 0, 0x00, 0xc0, 0xe0, 0xf0 };

ssize_t
xo_buf_utf8_len (xo_handle_t *xop, const char *buf, ssize_t bufsiz)
{
    unsigned b = (unsigned char) *buf;
    ssize_t len, i;

    len = xo_utf8_to_wc_len(buf);
    if (len < 0) {
        xo_failure(xop, "invalid UTF-8 data: %02hhx", b);
	return -1;
    }

    if (len > bufsiz) {
        xo_failure(xop, "invalid UTF-8 data (short): %02hhx (%d/%d)",
		   b, len, bufsiz);
	return -1;
    }

    for (i = 2; i < len; i++) {
	b = (unsigned char ) buf[i];
	if ((b & 0xc0) != 0x80) {
	    xo_failure(xop, "invalid UTF-8 data (byte %d): %x", i, b);
	    return -1;
	}
    }

    return len;
}

/*
 * Build a wide character from the input buffer; the number of
 * bits we pull off the first character is dependent on the length,
 * but we put 6 bits off all other bytes.
 */
wchar_t
xo_utf8_char (const char *buf, ssize_t len)
{
    /* Most common case: singleton byte */
    if (len == 1)
	return (unsigned char) buf[0];

    ssize_t i;
    wchar_t wc;
    const unsigned char *cp = (const unsigned char *) buf;

    wc = *cp & xo_utf8_data_bits[len];
    for (i = 1; i < len; i++) {
	wc <<= 6;		/* Low six bits have data */
	wc |= cp[i] & 0x3f;
	if ((cp[i] & 0xc0) != 0x80)
	    return (wchar_t) -1;
    }

    return wc;
}

/*
 * Determine the number of bytes needed to encode a wide character.
 */
ssize_t
xo_utf8_emit_len (wchar_t wc)
{
    ssize_t len;

    if ((wc & ((1 << 7) - 1)) == wc) /* Simple case */
	len = 1;
    else if ((wc & ((1 << 11) - 1)) == wc)
	len = 2;
    else if ((wc & ((1 << 16) - 1)) == wc)
	len = 3;
    else if ((wc & ((1 << 21) - 1)) == wc)
	len = 4;
    else
	len = -1;		/* Invalid */

    return len;
}

/*
 * Emit one wide character into the given buffer
 */
void
xo_utf8_emit_char (char *buf, ssize_t len, wchar_t wc)
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
    buf[0] &= xo_utf8_data_bits[len]; /* Clear out the length bits */
    buf[0] |= xo_utf8_len_bits[len]; /* Drop in new length bits */
}

#if 0
char *
xo_ustrstr (char *data, char *pat)
{
    return NULL;
}
#endif

char *
xo_ustrnchr (const char *data, size_t len, wchar_t ch)
{
    char *cp = (char *) data;

    if (ch < 0x80) {
	/*
	 * We're looking for an ASCII character in a UTF-8 string, so
	 * life is pretty easy.  We could skip bytes with the high bit set,
	 * but since we know 'ch' won't match, we just compare that.
	 */
	for ( ; len > 0; len--, cp++) {
	    if (*cp == ch)
		return (char *) cp;	/* Drop the const on return */
	}

    } else {
	/*
	 * More complex: we're looking for a wide character in a UTF-8
	 * string, we we'll need to extract each value and see what if
	 * it matches.
	 */
	while (len > 0) {
	    if (!(*cp & 0x80)) {
		/* Not a wide character, so we can skip it */
		cp += 1;
		len -= 1;
		continue;
	    }

	    ssize_t clen = xo_utf8_to_wc_len(cp);
	    wchar_t val = xo_utf8_char(cp, clen);
	    if (val == ch)
		return (char *) cp;	/* Drop the const on return */

	    cp += clen;
	    len -= clen;
	}
    }

    return NULL;
}

char *
xo_ustrnspn (char *data, size_t len, char *what)
{
    while


    return NULL;
}

char *
xo_ustrcnspn (char *data, size_t len, char *what)
{
    return NULL;
}
