/*
 * Copyright (c) 2006-2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * xo_xparse.c -- lex xpath input into lexical tokens
 *
 * This file is derived from libslax/slaxlexer.c
 */

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/param.h>

#include <libxo/xo.h>
#include <libxo/xo_encoder.h>
#include <libxo/xo_buf.h>

#include "xo_xpath.tab.h"
#include "xo_xparse.h"

#define XD_BUF_FUDGE (BUFSIZ/8)
#define XD_BUF_INCR BUFSIZ

#define XO_MAX_CHAR	128	/* Size of our character tables */

#define XO_PATHS_DEF	32	/* Number of paths allocated by default */

static int xo_xparse_setup;	/* Have we initialized? */

xo_xparse_node_t xo_xparse_dead_node; /* Dead space when you just don't care */

/*
 * These are lookup tables for one and two character literal tokens.
 */
static short xo_single_wide[XO_MAX_CHAR];
static char xo_double_wide[XO_MAX_CHAR];
static char xo_triple_wide[XO_MAX_CHAR];

/*
 * Define all one character literal tokens, mapping the single
 * character to the xo_xpath.y token
 */
static int xo_single_wide_data[] = {
    L_AT, '@',
    L_CBRACE, '}',
    L_CBRACK, ']',
    L_COMMA, ',',
    L_COLON, ':',
    L_CPAREN, ')',
    L_DOT, '.',
    L_EOS, ';',
    L_EQUALS, '=',
    L_GRTR, '>',
    L_LESS, '<',
    L_MINUS, '-',
    L_NOT, '!',
    L_OBRACE, '{',
    L_OBRACK, '[',
    L_OPAREN, '(',
    L_PLUS, '+',
    L_QUESTION, '?',
    L_SLASH, '/',
    L_STAR, '*',
    L_UNDERSCORE, '_',
    L_VBAR, '|',
    0
};

/*
 * Define all two character literal tokens, mapping two contiguous
 * characters into a single xo_xpath.y token.  N.B.: These are not
 * "double-wide" as in multi-byte characters, but are two distinct
 * characters.  See also: xo_xparse_double_wide(), where this list is
 * unfortunately duplicated.
 */
static int xo_double_wide_data[] = {
    L_ASSIGN, ':', '=',
    L_DAMPER, '&', '&',
    L_DCOLON, ':', ':',
    L_DEQUALS, '=', '=',
    L_DOTDOT, '.', '.',
    L_DSLASH, '/', '/',
    L_DVBAR, '|', '|',
    L_GRTREQ, '>', '=',
    L_LESSEQ, '<', '=',
    L_NOTEQUALS, '!', '=',
    L_PLUSEQUALS, '+', '=',
    0
};

#if 0
/*
 * Define all three character literal tokens, mapping three contiguous
 * characters into a single xo_xpath.y token.
 */
static int xo_triple_wide_data[] = {
    L_DOTDOTDOT, '.', '.', '.',
    0
};
#endif

/*
 * Define all keyword tokens, mapping the keywords into the xo_xpath.y
 * token.  We also provide KMF_* flags to indicate what context the keyword
 * is reserved.  SLAX keywords are not reserved inside XPath expressions
 * and XPath keywords are not reserved in non-XPath contexts.
 */
typedef struct keyword_mapping_s {
    int km_ttype;		/* Token type (returned from yylex) */
    const char *km_string;	/* Token string */
    int km_flags;		/* Flags for this entry */
} keyword_mapping_t;

/* Flags for km_flags: */
#define KMF_NODE_TEST	(1<<0)	/* Node test */
#define KMF_SLAX_KW	(1<<1)	/* Keyword for slax */
#define KMF_XPATH_KW	(1<<2)	/* Keyword for xpath */
#define KMF_STMT_KW	(1<<3)	/* Fancy statement (slax keyword in xpath) */
#define KMF_JSON_KW	(1<<4)	/* JSON-only keywords */

static keyword_mapping_t keywordMap[] = {
    { K_AND, "and", KMF_XPATH_KW },
    { K_COMMENT, "comment", KMF_SLAX_KW | KMF_NODE_TEST },
    { K_DIV, "div", KMF_XPATH_KW },
    { K_ID, "id", KMF_NODE_TEST }, /* Not really, but... */
    { K_KEY, "key", KMF_SLAX_KW | KMF_NODE_TEST }, /* Not really, but... */
    { K_MOD, "mod", KMF_XPATH_KW },
    { K_NODE, "node", KMF_NODE_TEST },
    { K_OR, "or", KMF_XPATH_KW },
    { K_PROCESSING_INSTRUCTION, "processing-instruction",
      KMF_SLAX_KW | KMF_NODE_TEST }, /* Not a node test, but close enough */
    { K_TEXT, "text", KMF_NODE_TEST },
    { 0, NULL, 0 }
};


typedef struct xo_xparse_ttname_map_s {
    int st_ttype;		/* Token number */
    const char *st_name;	/* Fancy, human-readable value */
} xo_xparse_ttname_map_t;

xo_xparse_ttname_map_t xo_xparse_ttname_map[] = {
    { L_AT,			"attribute axis ('@')" },
    { L_CBRACE,			"close brace ('}')" },
    { L_OBRACK,			"close bracket (']')" },
    { L_COLON,			"colon (':')" },
    { L_COMMA,			"comma (',')" },
    { L_DAMPER,			"logical AND operator ('&&')" },
    { L_DCOLON,			"axis operator ('::')" },
    { L_DEQUALS,		"equality operator ('==')" },
    { L_DOTDOT,			"parent axis ('..')" },
    { L_DOTDOTDOT,		"sequence operator ('...')" },
    { L_DSLASH,			"descendant operator ('//')" },
    { L_DVBAR,			"logical OR operator ('||')" },
    { L_EOS,			"semi-colon (';')" },
    { L_EQUALS,			"equal sign ('=')" },
    { L_GRTR,			"greater-than operator ('>')" },
    { L_GRTREQ,			"greater-or-equals operator ('>=')" },
    { L_LESS,			"less-than operator ('<')" },
    { L_LESSEQ,			"less-or-equals operator ('<=')" },
    { L_MINUS,			"minus sign ('-')" },
    { L_NOT,			"not sign ('!')" },
    { L_NOTEQUALS,		"not-equals sign ('!=')" },
    { L_OBRACE,			"open brace ('{')" },
    { L_OBRACK,			"open bracket ('[')" },
    { L_OPAREN,			"open parentheses ('(')" },
    { L_PLUS,			"plus sign ('+')" },
    { L_PLUSEQUALS,		"increment assign operator ('+=')" },
    { L_SLASH,			"slash ('/')" },
    { L_STAR,			"star ('*')" },
    { L_UNDERSCORE,		"concatenation operator ('_')" },
    { L_VBAR,			"union operator ('|')" },
    { K_COMMENT,		"'comment'" },
    { K_ID,			"'id'" },
    { K_KEY,			"'key'" },
    { K_NODE,			"'node'" },
    { K_PROCESSING_INSTRUCTION,	"'processing-instruction'" },
    { K_TEXT,			"'text'" },
    { K_AND,			"'and'" },
    { K_DIV,			"'div'" },
    { K_MOD,			"'mod'" },
    { K_OR,			"'or'" },
    { L_ASTERISK,		"asterisk ('*')" },
    { L_CBRACK,			"close bracket (']')" },
    { L_CPAREN,			"close parentheses (')')" },
    { L_DOT,			"dot ('.')" },
    { T_AXIS_NAME,		"built-in axis name" },
    { T_BARE,			"bare word string" },
    { T_FUNCTION_NAME,		"function name" },
    { T_NUMBER,			"number" },
    { T_QUOTED,			"quoted string" },
    { T_VAR,			"variable name" },
    { C_ABSOLUTE,		"absolute path" },
    { C_ATTRIBUTE,		"attribute axis" },
    { C_DESCENDANT,		"descendant child ('one//two')" },
    { C_ELEMENT,		"path element" },
    { C_EXPR,			"parenthetical expresions" },
    { C_INDEX,			"index value ('foo[4]')" },
    { C_NOT,			"negation ('!tag')" },
    { C_PATH,			"path of element" },
    { C_PREDICATE,		"predicate ('[test]')" },
    { C_TEST,			"node test ('node()')" },
    { C_UNION,			"union of two paths ('one|two')" },
    { C_INT64,			"signed 64-bit integer" },
    { C_UINT64,			"unsigned 64-bit integer" },
    { C_FLOAT,			"floating point number (double)" },
    { C_STRING,			"string value (const char *)" },
    { C_BOOLEAN,		"boolean value" },
    { M_ERROR,                  "invalid xpath expression" },
    { 0, NULL }
};

/*
 * Set up the lexer's lookup tables
 */
static void
xo_xparse_setup_lexer (void)
{
    int i, ttype;

    xo_xparse_setup = 1;

    for (i = 0; xo_single_wide_data[i]; i += 2)
	xo_single_wide[xo_single_wide_data[i + 1]] = xo_single_wide_data[i];

    for (i = 0; xo_double_wide_data[i]; i += 3)
	xo_double_wide[xo_double_wide_data[i + 1]] = i + 2;

    /* There's only one triple wide, so optimize (for now) */
    xo_triple_wide['.'] = 1;

    for (i = 0; keywordMap[i].km_ttype; i++)
	xo_xparse_keyword_string[xo_xparse_token_translate(keywordMap[i].km_ttype)]
	    = keywordMap[i].km_string;

    for (i = 0; xo_xparse_ttname_map[i].st_ttype; i++) {
	ttype = xo_xparse_token_translate(xo_xparse_ttname_map[i].st_ttype);
	xo_xparse_token_name_fancy[ttype] =  xo_xparse_ttname_map[i].st_name;
    }
}

#if 0
/*
 * Does the given character end a token?
 */
static inline int
xo_xparse_is_final_char (int ch)
{
    return (ch == ';' || isspace(ch));
}
#endif

/*
 * Does the input buffer start with the given keyword?
 */
static int
xo_xparse_keyword_match (xo_xparse_data_t *xdp, const char *str)
{
    int len = strlen(str);
    int ch;

    if (xdp->xd_len - xdp->xd_start < len)
	return FALSE;

    if (memcmp(xdp->xd_buf + xdp->xd_start, str, len) != 0)
	return FALSE;

    ch = xdp->xd_buf[xdp->xd_start + len];

    if (xo_xparse_is_bare_char(ch))
	return FALSE;

    return TRUE;
}

/*
 * Return the token type for the two character token given by
 * ch1 and ch2.  Returns zero if there is none.
 */
static int
xo_xparse_double_wide (xo_xparse_data_t *xdp UNUSED, uint8_t ch1, uint8_t ch2)
{
#define DOUBLE_WIDE(ch1, ch2) (((ch1) << 8) | (ch2))
    switch (DOUBLE_WIDE(ch1, ch2)) {
    case DOUBLE_WIDE(':', '='): return L_ASSIGN;
    case DOUBLE_WIDE('&', '&'): return L_DAMPER;
    case DOUBLE_WIDE(':', ':'): return L_DCOLON;
    case DOUBLE_WIDE('=', '='): return L_DEQUALS;
    case DOUBLE_WIDE('.', '.'): return L_DOTDOT;
    case DOUBLE_WIDE('/', '/'): return L_DSLASH;
    case DOUBLE_WIDE('|', '|'): return L_DVBAR;
    case DOUBLE_WIDE('>', '='): return L_GRTREQ;
    case DOUBLE_WIDE('<', '='): return L_LESSEQ;
    case DOUBLE_WIDE('!', '='): return L_NOTEQUALS;
    case DOUBLE_WIDE('+', '='): return L_PLUSEQUALS;
    }

    return 0;
}

/*
 * Return the token type for the triple character token given by
 * ch1, ch2 and ch3.  Returns zero if there is none.
 */
static int
xo_xparse_triple_wide (xo_xparse_data_t *xdp UNUSED, uint8_t ch1, uint8_t ch2, uint8_t ch3)
{
    if (ch1 == '.' && ch2 == '.' && ch3 == '.')
	return L_DOTDOTDOT;	/* Only one (for now) */

    return 0;
}

/*
 * Returns the token type for the keyword at the start of
 * the input buffer, or zero if there isn't one.
 *
 * Ignore XPath keywords if they are not allowed.  Same for SLAX keywords.
 * For node test tokens, we look ahead for the open paren before
 * returning the token type.
 *
 * (Should this use a hash or something?)
 */
static int
xo_xparse_keyword (xo_xparse_data_t *xdp)
{
    keyword_mapping_t *kmp;
    int ch;

    for (kmp = keywordMap; kmp->km_string; kmp++) {
	if (xo_xparse_keyword_match(xdp, kmp->km_string)) {
	    return kmp->km_ttype;

	    if (kmp->km_flags & KMF_NODE_TEST) {
		int look = xdp->xd_cur + strlen(kmp->km_string);

		for ( ; look < xdp->xd_len; look++) {
		    ch = xdp->xd_buf[look];
		    if (ch == '(')
			return kmp->km_ttype;
		    if (ch != ' ' && ch != '\t')
			break;
		}

		/* Didn't see the open paren, so it's not a node test */
	    }

	    return 0;
	}
    }

    return 0;
}

/*
 * We are working with simple strings, so a read means EOF.
 */
static int
xo_xparse_get_input (xo_xparse_data_t *xdp UNUSED, int final UNUSED)
{
    return TRUE;
}

/*
 * Move the current point by one character, getting more data if needed.
 */
static int
xo_xparse_move_cur (xo_xparse_data_t *xdp)
{
    XO_DBG(xdp->xd_xop, "xo_xplex: move:- %u/%u/%u",
	   xdp->xd_start, xdp->xd_cur, xdp->xd_len);

    int moved;

    if (xdp->xd_cur < xdp->xd_len) {
	xdp->xd_cur += 1;
	moved = TRUE;
    } else moved = FALSE;

    if (xdp->xd_cur == xdp->xd_len) {
       if (xo_xparse_get_input(xdp, 0)) {
           XO_DBG(xdp->xd_xop, "xo_xplex: move:! %u/%u/%u",
                   xdp->xd_start, xdp->xd_cur, xdp->xd_len);
	   if (moved)
	       xdp->xd_cur -= 1;
           return -1;
       }
    }

    XO_DBG(xdp->xd_xop, "xo_xplex: move:+ %u/%u/%u",
	   xdp->xd_start, xdp->xd_cur, xdp->xd_len);
    return 0;
}

static void
xo_xparse_warn_default (void *data, const char *fmt, va_list vap)
{
    xo_handle_t *xop = data;

    xo_warn_hcv(xop, -1, 0, fmt, vap);
}

static void
xo_xparse_warn (xo_xparse_data_t *xdp, const char *fmt, ...)
{
    xo_xpath_warn_func_t func = xdp->xd_warn_func ?: xo_xparse_warn_default;
    void *data = xdp->xd_warn_func ? xdp->xd_warn_data : xdp->xd_xop;
    va_list vap;

    va_start(vap, fmt);
    func(data, fmt, vap);
    va_end(vap);
}

static char *
xo_xparse_location (xo_xparse_data_t *xdp)
{
    static char xo_location[MAXPATHLEN + 64]; /* Path size plus little extra */

    if (xdp->xd_filename[0] == 0) {
	xo_location[0] = '\0';		/* We have no information */

    } else if (xdp->xd_line == 0) {
	snprintf(xo_location, sizeof(xo_location), "%s: ", xdp->xd_filename);

    } else {
	snprintf(xo_location, sizeof(xo_location), "%s:%u:%u ",
		 xdp->xd_filename, xdp->xd_line, xdp->xd_col);
    }

    return xo_location;
}

/**
 * Issue an error if the axis name is not valid
 *
 * @param xdp main xplex data structure
 * @param axis name of the axis to check
 */
void
xo_xparse_check_axis_name (xo_xparse_data_t *xdp, xo_xparse_node_id_t id)
{
    static const char *axis_names[] = {
	"ancestor",
	"ancestor-or-self",
	"attribute",
	"child",
	"descendant",
	"descendant-or-self",
	"following",
	"following-sibling",
	"namespace",
	"parent",
	"preceding",
	"preceding-sibling",
	"self",
	NULL
    };
    const char **namep;
    xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);

    if (xnp == NULL)
	return;
    
    const char *str = xo_xparse_str(xdp, xnp->xn_str);
    if (str == NULL)
	return;

    /*
     * Fix the token type correctly, since sometimes these are parsed
     * as T_BARE.
     */
    xnp->xn_type = T_AXIS_NAME;

    for (namep = axis_names; *namep; namep++) {
	if (xo_streq(*namep, str))
	    return;
    }

    xo_xparse_warn(xdp, "%sunknown axis name: '%s'",
		   xo_xparse_location(xdp), str);
}

xo_xparse_str_id_t
xo_xparse_str_new (xo_xparse_data_t *xdp, xo_xparse_token_t type)
{
    xo_off_t len = xdp->xd_cur - xdp->xd_start;
    const char *start = xdp->xd_buf + xdp->xd_start;
    xo_buffer_t *xbp = &xdp->xd_str_buf;

    /* If this is a quoted string, we want to trim the quotes */
    if (type == T_QUOTED && len >= 2) {
	start += 1;		/* Skip the leading quote */
	len -= 2;
    }

    xo_off_t cur = xbp->xb_curp - xbp->xb_bufp;
    char *newp = xo_buf_append_val(xbp, start, len + 1);
    newp[len] = '\0';

    xdp->xd_last_str = cur;

    return newp ? cur : 0;
}

void
xo_xparse_set_input (xo_xparse_data_t *xdp, const char *buf, xo_ssize_t len)
{
    if (xdp->xd_size < len + 1) {
	size_t size = (len + 1 + XD_BUF_FUDGE);
	size += XD_BUF_INCR - 1;
	size &= ~(XD_BUF_INCR - 1);

	xdp->xd_buf = xo_realloc(xdp->xd_buf, size);
	xdp->xd_size = xdp->xd_buf ? size : 0;
    }

    if (xdp->xd_buf) {
	xdp->xd_len = len;
	memcpy(xdp->xd_buf, buf, len);
	xdp->xd_buf[len] = '\0';
    } else {
	xdp->xd_len = 0;
    }

    xdp->xd_cur = 0;
    xdp->xd_start = 0;

    xdp->xd_errors = 0;
    xdp->xd_col = 0;
    xdp->xd_col_start = 0;
}

void
xo_xparse_dump_one_node (xo_xparse_data_t *xdp, xo_xparse_node_id_t id,
			 int indent, const char *title)
{
    if (id == 0)
	return;

    xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);

    const char *str = xo_xparse_str(xdp, xnp->xn_str);
    xo_xparse_node_t *next = xo_xparse_node(xdp, xnp->xn_next);
    xo_xparse_node_t *prev = xo_xparse_node(xdp, xnp->xn_prev);

    xo_dbg(xdp->xd_xop, "%*s%s%06ld: type %u (%s), str %ld [%s], "
	   "contents %ld, next %ld%s, prev %ld %s",
	   indent, "", title ?: "", id,
	   xnp->xn_type, xo_xparse_token_name(xnp->xn_type),
	   xnp->xn_str, str ?: "", xnp->xn_contents, 
	   xnp->xn_next, (next && next->xn_prev != id) ? " BAD" : "",
	   xnp->xn_prev, (prev && prev->xn_next != id) ? " BAD" : "");
}

static void
xo_xparse_dump_node (xo_xparse_data_t *xdp, xo_xparse_node_id_t id, int indent)
{
    xo_xparse_node_t *xnp;

    for ( ; id; id = xnp->xn_next) {
	xo_xparse_dump_one_node(xdp, id, indent, NULL);
	xnp = xo_xparse_node(xdp, id);
	if (xnp->xn_contents)
	    xo_xparse_dump_node(xdp, xnp->xn_contents, indent + 4);
    }
}

void
xo_xparse_dump (xo_xparse_data_t *xdp)
{
    if (!xo_isset_flags(xdp->xd_xop, XOF_DEBUG))
	return;

    uint32_t i;
    xo_xparse_node_id_t *pp = xdp->xd_paths;

    for (i = 0; i < xdp->xd_paths_cur; i++, pp++) {
	xo_dbg(xdp->xd_xop, "--- %u: %ld", i, *pp);
	xo_xparse_dump_node(xdp, *pp, 4);
    }
}

static int
xo_xparse_feature_warn_one_node (const char *tag, xo_xparse_data_t *xdp UNUSED,
				 const int *map, int len,
				 xo_xparse_node_id_t id UNUSED,
				 xo_xparse_node_t *xnp)
{
    xo_xparse_token_t type = xnp->xn_type;

    if ((int) type < len && map[type]) {
	xo_xparse_warn(xdp, "%s%sxpath feature is unsupported: %s",
		       tag ?: "", tag ? ": " : "",
		       xo_xparse_fancy_token_name(type));
	return 1;
    }

    return 0;
}

static int
xo_xparse_feature_warn_node (const char *tag, xo_xparse_data_t *xdp,
			     const int *map, int len,
			     xo_xparse_node_id_t id)
{
    int hit = 0;
    xo_xparse_node_t *xnp;

    for ( ; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(xdp, id);
	hit += xo_xparse_feature_warn_one_node(tag, xdp, map, len, id, xnp);
	if (xnp->xn_contents)
	    hit += xo_xparse_feature_warn_node(tag, xdp, map, len,
					       xnp->xn_contents);
    }

    return hit;
}

int
xo_xpath_feature_warn (const char *tag, xo_xparse_data_t *xdp,
		       const int *tokens, const char *bytes)
{
    if (xdp->xd_paths_cur == 0)	/* Parsing errors */
	return 0;

    int len = xo_xparse_num_tokens;
    int map[len];

    bzero(map, len * sizeof(map[0]));

    if (tokens) {
	for (int i = 0; tokens[i] && i < len; i++)
	    if (tokens[i])
		map[tokens[i]] = 1;
    }

    if (bytes) {
	for (int i = 0; bytes[i] && i < len; i++)
	    if (bytes[i]) {
		int num = bytes[i];
		    int val = xo_single_wide[num];
		if (val >= 0 && val <= len)
		    map[val] = 1;
	    }
    }

    uint32_t i;
    xo_xparse_node_id_t *pp = xdp->xd_paths;
    int rc = 0;

    for (i = 0; i < xdp->xd_paths_cur; i++, pp++) {
	rc += xo_xparse_feature_warn_node(tag, xdp, map, len, *pp);
    }

    return rc;
}

int
xo_xparse_ternary_rewrite (xo_xparse_data_t *xdp UNUSED,
			   xo_xparse_node_id_t *d0 UNUSED,
			   xo_xparse_node_id_t *d1 UNUSED,
			   xo_xparse_node_id_t *d2 UNUSED,
			   xo_xparse_node_id_t *d3 UNUSED,
			   xo_xparse_node_id_t *d4 UNUSED,
			   xo_xparse_str_id_t *d5 UNUSED)
{
    return 0;
}

int
xo_xparse_concat_rewrite (xo_xparse_data_t *xdp UNUSED,
			  xo_xparse_node_id_t *d0 UNUSED,
			  xo_xparse_node_id_t *d1 UNUSED,
			  xo_xparse_node_id_t *d2 UNUSED,
			  xo_xparse_node_id_t *d3 UNUSED)
{
    return 0;
}

int
xo_xparse_yyval (xo_xparse_data_t *xdp UNUSED, xo_xparse_node_id_t id)
{
    XO_DBG(xdp->xd_xop, "xo_xparse_yyval: $$ = %ld", id);

    return id;
}

/*
 * Return a new match struct, allocating a new one if needed
 */
static void
xo_xparse_result_add (xo_xparse_data_t *xdp, xo_xparse_node_id_t id)
{
    if (xdp->xd_paths_cur >= xdp->xd_paths_max) {
	uint32_t new_max = xdp->xd_paths_max + XO_PATHS_DEF;

	xo_xparse_node_id_t *pp;
	pp = xo_realloc(xdp->xd_paths, new_max * sizeof(*pp));
	if (pp == NULL)
	    return;

	xdp->xd_paths = pp;
	xdp->xd_paths_max = new_max;
    }

    xdp->xd_paths[xdp->xd_paths_cur++] = id;
}

/*
 * Add the final results of a parsing to the data.  The real work here
 * is rewriting patterns (like unions) into more easily handled forms.
 */
void
xo_xparse_results (xo_xparse_data_t *xdp, xo_xparse_node_id_t id)
{
    xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);
    xo_xparse_node_id_t next;

    if (xnp == NULL) {
	/* nothing; error? */

    } else if (xnp->xn_type == C_UNION) {
	for (id = xnp->xn_contents; id; id = next) {
	    xnp = xo_xparse_node(xdp, id);
	    xo_xparse_result_add(xdp, id);

	    /* Break off this node from the chain */
	    next = xnp->xn_next;
	    xnp->xn_next = xnp->xn_prev = 0;
	}

    } else {
	xo_xparse_result_add(xdp, id);
    }

    uint32_t deny_count = 0, abs_count = 0;
    xo_xparse_node_id_t i;
    xo_xparse_node_id_t *paths = xdp->xd_paths;

    /*
     * If all the expressions are NOTs or absolute paths, our life
     * gets easier.  So we whiffle thru them, counting the matches,
     * and then set the flags appropriately.
     *
     * all nots: !one | !two | !three
     * all abs:  /one | /two | /three
     */
    xo_xparse_node_id_t cur = xdp->xd_paths_cur;
    for (i = 0; i < cur; i++, paths++) {
	xnp = xo_xparse_node(xdp, *paths);
	if (xnp == NULL)
	    continue;

	if (xnp->xn_type == C_NOT)
	    deny_count += 1;

	if (xnp->xn_type == C_ABSOLUTE)
	    abs_count += 1;
    }

    const char *all_nots UNUSED;
    if (deny_count >= cur) {
	xdp->xd_flags |= XDF_ALL_NOTS;
	all_nots = ", all-nots: true";
    } else {
	xdp->xd_flags &= ~XDF_ALL_NOTS;
	all_nots = "";
    }

    const char *all_abs UNUSED;
    if (abs_count >= cur) {
	xdp->xd_flags |= XDF_ALL_ABS;
	all_abs = ", all-abs: true";
    } else {
	xdp->xd_flags &= ~XDF_ALL_ABS;
	all_abs = "";
    }

    XO_DBG(xdp->xd_xop, "xo: parse results: %u paths%s%s%s",
	   cur, all_nots, all_abs);
}

void
xo_xparse_node_set_next (xo_xparse_data_t *xdp, xo_xparse_node_id_t id,
			xo_xparse_node_id_t value)
{
    xo_xparse_node_id_t next = 0;

    if (id) {
	xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);

	next = id;
	while (xnp->xn_next != 0) {
	    next = xnp->xn_next;
	    xnp = xo_xparse_node(xdp, next);
	}
	xnp->xn_next = value;

	if (value) {
	    xo_xparse_node_t *last = xo_xparse_node(xdp, value);
	    last->xn_prev = next;
	}
    }

    XO_DBG(xdp->xd_xop, "xo_xparse_node_set_next: id %ld, next %ld, value %ld",
	   id, next, value);
}

void
xo_xparse_node_set_contents (xo_xparse_data_t *xdp, xo_xparse_node_id_t id,
			xo_xparse_node_id_t value)
{
    xo_xparse_node_id_t next = 0;

    if (id) {
	xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);
	if (xnp->xn_contents == 0) {
	    xnp->xn_contents = value;
	} else {
	    next = xnp->xn_contents;
	    xnp = xo_xparse_node(xdp, xnp->xn_contents);
	    while (xnp->xn_next != 0) {
		next = xnp->xn_next;
		xnp = xo_xparse_node(xdp, next);
	    }
	    xnp->xn_next = value;

	    if (value) {
		xo_xparse_node_t *last = xo_xparse_node(xdp, value);
		last->xn_prev = next;
	    }
	}
    }
}

/**
 * This function is the core of the lexer.
 *
 * We inspect the input buffer, finding the first token and returning
 * it's token type.
 *
 * @param xdp main slax data structure
 * @return token type
 */
static int
xo_xparse_lexer (xo_xparse_data_t *xdp)
{
    uint8_t ch1, ch2, ch3;
    int look, rc;

    /* Skip leading whitespace */
    while (xdp->xd_cur < xdp->xd_len	
	   && isspace((int) xdp->xd_buf[xdp->xd_cur])) {
	if (xdp->xd_buf[xdp->xd_cur] == '\n') {
	    xdp->xd_line += 1;
	    xdp->xd_col_start = xdp->xd_cur;
	}

	xdp->xd_cur += 1;
    }

    xdp->xd_col = xdp->xd_cur - xdp->xd_col_start;
    xdp->xd_start = xdp->xd_cur; /* Mark the start of the token */

    /* We're only parsing a string, so no data mean EOF */
    if (xdp->xd_cur == xdp->xd_len)
	return -1;
	
    ch1 = xdp->xd_buf[xdp->xd_cur];
    ch2 = (xdp->xd_cur + 1 < xdp->xd_len) ? xdp->xd_buf[xdp->xd_cur + 1] : 0;
    ch3 = (xdp->xd_cur + 2 < xdp->xd_len) ? xdp->xd_buf[xdp->xd_cur + 2] : 0;

    if (ch1 < XO_MAX_CHAR) {
	if (xo_triple_wide[ch1]) {
	    rc = xo_xparse_triple_wide(xdp, ch1, ch2, ch3);
	    if (rc) {
		xdp->xd_cur += 3;
		return rc;
	    }
	}

	if (xo_double_wide[ch1]) {
	    rc = xo_xparse_double_wide(xdp, ch1, ch2);
	    if (rc) {
		xdp->xd_cur += 2;
		return rc;
	    }
	}

	if (xo_single_wide[ch1]) {
	    int lit1 = xo_single_wide[ch1];
	    xdp->xd_cur += 1;

	    if (lit1 == L_STAR) {
		/*
		 * If we see a "*", we have to think about if it's a
		 * L_STAR or an L_ASTERISK.  If we put them both into
		 * the same literal, we get a shift-reduce error since
		 * there's an ambiguity between "/ * /" and "foo * foo".
		 * Is it a node test or the multiplier operator?
		 * To discriminate, we look at the last token returned.
		 */
		if (xdp->xd_last > M_MULTIPLICATION_TEST_LAST)
		    return L_STAR; /* It's the multiplcation operator */
		else
		    return L_ASTERISK; /* It's a q_name (NCName) */
	    }

            if (ch1 == '.' && isdigit((int) ch2)) {
                /* continue */
            } else if (lit1 == L_UNDERSCORE) {
                /*
                 * Underscore is a valid first character for an element
                 * name, which is troubling, since it's also the concatenation
                 * operator in SLAX.  We look ahead to see if the next
                 * character is a valid character before making our
                 * decision.
                 */
		if (!xo_xparse_is_bare_char(ch2))
		    return lit1;
	    } else {
		return lit1;
	    }
	}

	if (ch1 == '\'' || ch1 == '"') {
	    /*
	     * Found a quoted string.  Scan for the end.  We may
	     * need to read some more, if the string is long.
	     */
	    if (xo_xparse_move_cur(xdp)) /* Move past the first quote */
		return -1;

	    for (;;) {
		if (xdp->xd_cur == xdp->xd_len)
		    if (xo_xparse_get_input(xdp, 0))
			return -1;

		if ((uint8_t) xdp->xd_buf[xdp->xd_cur] == ch1)
		    break;

#if 1
		int bump = (xdp->xd_buf[xdp->xd_cur] == '\\') ? 1 : 0;
#endif

		if (xo_xparse_move_cur(xdp))
		    return -1;

#if 1
		if (bump && xdp->xd_cur < xdp->xd_len)
		    xdp->xd_cur += bump;
#endif
	    }

	    if (xdp->xd_cur < xdp->xd_len)
		xdp->xd_cur += 1;	/* Move past the end quote */
	    return T_QUOTED;
	}

	if (ch1 == '$') {
	    /*
	     * Found a variable; scan for the end of it.
	     */
	    xdp->xd_cur += 1;
	    while (xdp->xd_cur < xdp->xd_len
		   && xo_xparse_is_var_char(xdp->xd_buf[xdp->xd_cur]))
		xdp->xd_cur += 1;
	    return T_VAR;
	}

	rc = xo_xparse_keyword(xdp);
	if (rc) {
	    xdp->xd_cur += strlen(xo_xparse_keyword_string[xo_xparse_token_translate(rc)]);
	    return rc;
	}

	if (isdigit(ch1) || (ch1 == '.' && isdigit(ch2))) {
	    int seen_e = FALSE;

	    for ( ; xdp->xd_cur < xdp->xd_len; xdp->xd_cur++) {
		int ch4 =  xdp->xd_buf[xdp->xd_cur];
		if (isdigit(ch4))
		    continue;
		if (ch4 == '.')
		    continue;
		if (ch4 == 'e' || ch4 == 'E') {
		    seen_e = TRUE;
		    continue;
		}
		if (seen_e && (ch4 == '+' || ch4 == '-'))
		    continue;
		break;		/* Otherwise, we're done */
	    }
	    return T_NUMBER;
	}
    }

    /*
     * Must have found a bare word or a function name, since they
     * are the only things left.  We scan forward for the next
     * character that doesn't fit in a T_BARE, but allow "foo:*"
     * as a special case.
     */
    for ( ; xdp->xd_cur < xdp->xd_len; xdp->xd_cur++) {
	if (xdp->xd_cur + 1 < xdp->xd_len && xdp->xd_buf[xdp->xd_cur] == ':'
		&& xdp->xd_buf[xdp->xd_cur + 1] == ':')
	    return T_AXIS_NAME;
	if (xo_xparse_is_bare_char(xdp->xd_buf[xdp->xd_cur]))
	    continue;
	if (xdp->xd_cur > xdp->xd_start && xdp->xd_buf[xdp->xd_cur] == '*'
		&& xdp->xd_buf[xdp->xd_cur - 1] == ':')
	    continue;
	break;
    }

    /*
     * It's a hack, but it's a standard-specified hack:
     * We need to see if this is a function name (T_FUNCTION_NAME)
     * or an NCName (q_name) (T_BARE).
     * So we look ahead for a '('.  If we find one, it's a function;
     * if not it's a q_name.
     */
    for (look = xdp->xd_cur; look < xdp->xd_len; look++) {
	ch1 = xdp->xd_buf[look];
	if (ch1 == '(')
	    return T_FUNCTION_NAME;
	if (!isspace(ch1))
	    break;
    }

    if (xdp->xd_cur == xdp->xd_start && xdp->xd_buf[xdp->xd_cur] == '#') {
	/*
	 * There's a special token "#default" that's used for
	 * namespace-alias.  It's an absurd hack, but we
	 * have to dummy it up as a T_BARE.
	 */
	static const char pdef[] = "#default";
	static const int plen = sizeof(pdef) - 1;
	if (xdp->xd_len - xdp->xd_cur > plen
	    && memcmp(xdp->xd_buf + xdp->xd_cur,
		      pdef, plen) == 0
	    && !xo_xparse_is_bare_char(xdp->xd_buf[xdp->xd_cur + plen])) {
	    xdp->xd_cur += sizeof(pdef) - 1;
	}
    }

    return T_BARE;
}

/**
 * Lexer function called from bison's yyparse()
 *
 * We lexically analyze the input and return the token type to
 * bison's yyparse function, which we've renamed to xo_xpath_parse.
 *
 * @param xdp main xpath data structure
 * @param yylvalp pointer to bison's lval (stack value)
 * @return token type
 */
int
xo_xpath_yylex (xo_xparse_data_t *xdp, xo_xparse_node_id_t *yylvalp)
{
    int rc, look;

    if (!xo_xparse_setup)
	xo_xparse_setup_lexer();

    xo_xparse_node_id_t id = xo_xparse_node_new(xdp);
    xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);
    *yylvalp = id;

    bzero(xnp, sizeof(*xnp));

    /*
     * If we've saved a token type into xd_ttype, then we return
     * it as if we'd just lexed it.
     */
    if (xdp->xd_ttype) {
	rc = xdp->xd_ttype;
	xdp->xd_ttype = 0;

	xnp->xn_type = rc;

	return rc;
    }

#if 0
    /* Add the record flags to the current set */
    xdp->xd_flags |= xdp->xd_flags_next;
    xdp->xd_flags_next = 0;
#endif

    /*
     * Discard the previous token by moving the start pointer
     * past it.
     */
    xdp->xd_start = xdp->xd_cur;

    rc = xo_xparse_lexer(xdp);
    if (M_OPERATOR_FIRST < rc && rc < M_OPERATOR_LAST
	&& xdp->xd_last < M_MULTIPLICATION_TEST_LAST)
	rc = T_BARE;

    /*
     * It's a hack, but it's a standard-specified hack: We need to see
     * if this is a function name (T_FUNCTION_NAME) or an NCName
     * (q_name) (T_BARE).  So we look ahead for a '('.  If we find
     * one, it's a function; if not it's an T_BARE;
     */
    if (rc == T_BARE /* && xdp->xd_parse != M_JSON*/) {
	for (look = xdp->xd_cur; look < xdp->xd_len; look++) {
	    unsigned char ch = xdp->xd_buf[look];
	    if (ch == '(') {
		rc = T_FUNCTION_NAME;
		break;
	    }

	    if (!isspace((int) ch))
		break;
	}
    }

    /*
     * Save the token type in xd_last so we can do these same
     * hacks next time thru
     */
    xdp->xd_last = rc;

    if (rc > 0 && xdp->xd_start == xdp->xd_cur) {
	XO_DBG(xdp->xd_xop, "%sxpath: found a zero length token: %d/%s",
	       xo_xparse_location(xdp), rc, xo_xparse_token_name(rc));

	xdp->xd_last = rc = M_ERROR;

	/*
	 * We're attempting to return a reasonable token type, but
	 * with a zero length token.  Something it very busted with
	 * the input.  We can't just sit here, but, well, there are
	 * no good options.  We're going to move the current point
	 * forward in the hope that we'll see good input eventually.
	 */
	if (xdp->xd_cur < xdp->xd_len)
	    xdp->xd_cur += 1;
    }

    xnp->xn_type = rc;
    if (rc > 0)
	xnp->xn_str = xo_xparse_str_new(xdp, rc);

    XO_DBG(xdp->xd_xop, "xo_xplex: lex: '%.*s' -> %d/%s %s",
	   xdp->xd_cur - xdp->xd_start,
	   xdp->xd_buf + xdp->xd_start,
	   rc, (rc > 0) ? xo_xparse_token_name(rc) : "",
	   xnp->xn_str ? xo_xparse_str(xdp, xnp->xn_str) : "");

    xo_xparse_dump_one_node(xdp, id, 0, "yylex:: ");

    return rc;
}

/*
 * Return a better class of error message
 * @returns freshly allocated string containing error message
 */
static char *
xo_xparse_syntax_error (xo_xparse_data_t *xdp UNUSED, const char *token,
		       int yystate, int yychar)
{
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);

    /*
     * If yystate is 1, then we're in our initial state and have
     * not seen any valid input.  We handle this state specifically
     * to give better error messages.
     */
    if (yystate == 1) {
	if (yychar == -1)
	    return strdup("unexpected end-of-file found (empty input)");

	if (yychar == L_LESS)
	    return strdup("unexpected '<'; file may be XML/XSLT");

	SNPRINTF(cp, ep, "missing 'version' statement");
	if (token)
	    SNPRINTF(cp, ep, "; %s is not legal", token);

    } else if (yychar == -1) {
	SNPRINTF(cp, ep, "unexpected end-of-expression");

    } else {
	char *msg = xo_xparse_expecting_error(token, yystate, yychar);
	if (msg)
	    return msg;

	SNPRINTF(cp, ep, "unexpected input");
	if (token)
	    SNPRINTF(cp, ep, ": %s", token);
    }

    return strdup(buf);
}

/**
 * Callback from yacc when an error is detected.
 *
 * @param xdp main slax data structure
 * @param str error message
 * @param value stack entry from bison's lexical stack
 * @return zero
 */
int
xo_xpath_yyerror (xo_xparse_data_t *xdp, const char *str, int yystate)
{
#ifdef HAVE_BISON
    static const char leader[] = "syntax error, unexpected";
#else /* HAVE_BISON */
    static const char leader[] = "syntax error";
#endif /* HAVE_BISON */
    
    static const char leader2[] = "error recovery ignores input";
    const char *token;
    char buf[BUFSIZ * 4];

    if (strncmp(str, leader2, sizeof(leader2) - 1) != 0)
	xdp->xd_errors += 1;

    token = xo_xparse_token_name_fancy[xo_xparse_token_translate(xdp->xd_last)];

    buf[0] = '\0';

    /*
     * Two possibilities: generic "syntax error" or some
     * specific error.  If the message has a generic
     * prefix, use our logic instead.  This avoids tossing
     * token names (K_VERSION) at the user.
     */
    if (strncmp(str, leader, sizeof(leader) - 1) == 0) {
	char *msg = xo_xparse_syntax_error(xdp, token, yystate, xdp->xd_last);

	if (msg) {
	    xo_xparse_warn(xdp, "%sfilter expression error: %s",
			   xo_xparse_location(xdp), msg, buf);
	    xo_free(msg);
	    return 0;
	}
    }

    xo_xparse_warn(xdp, "%sfilter expression error: %s%s%s%s%s",
		   xo_xparse_location(xdp), str,
		   token ? " before " : "", token, token ? ": " : "", buf);

    return 0;
}

void
xo_xparse_yyprintf(xo_xparse_data_t *xdp, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_dbg_v(xdp->xd_xop, fmt, vap);
    va_end(vap);
}

void
xo_xparse_init (xo_xparse_data_t *xdp)
{
    bzero(xdp, sizeof(*xdp));
    xdp->xd_line = 1;

    xo_buf_append_val(&xdp->xd_str_buf, "@EOF", 1);
}

xo_xparse_data_t *
xo_xparse_create (void)
{
    xo_xparse_data_t *xdp = xo_realloc(NULL, sizeof(*xdp));
    if (xdp)
	xo_xparse_init(xdp);

    return xdp;
}

void
xo_xparse_clean (xo_xparse_data_t *xdp)
{
    if (xdp) {
	xo_buf_cleanup(&xdp->xd_node_buf);
	xo_buf_cleanup(&xdp->xd_str_buf);
	xo_free(xdp->xd_buf);
    }
}

void
xo_xparse_destroy (xo_xparse_data_t *xdp)
{
    if (xdp) {
	xo_xparse_clean(xdp);
	xo_free(xdp);
    }
}

/**
 * Parse the input XPath strings into internal form that we can use.
 */
int
xo_xparse_parse_string (xo_handle_t *xop, xo_xparse_data_t *xdp,
			const char *input)
{
    /* Use our string as the input buffer */
    xo_xparse_set_input(xdp, input, strlen(input));

    int save_yydebug = xo_xpath_yydebug;
    if (xo_isset_flags(xop, XOF_DEBUG))
	xo_xpath_yydebug = 1;

    xdp->xd_xop = xop;		/* Temporarily record the handle */

    /* This is the main parsing function, built from xo_xpath.y */
    int rc = xo_xpath_yyparse(xdp);

    xdp->xd_xop = NULL;		/* Reset the handle */

    xo_xpath_yydebug = save_yydebug; /* Restore */

    xo_xparse_dump(xdp);

    return rc;
}
