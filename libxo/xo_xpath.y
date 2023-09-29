%{
/*
 * Copyright (c) 2006-2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2023 (based on libslax's slaxparser.y)
 *
 * This is an implementation of XPath, suitable for filtering libxo nodes.
 * I'm hoping for something small and efficient.....
 */
%}

%{
%}

/*
 * Literal tokens which may _not_ preceed the multiplication operator
 */
%token L_ASSIGN		    ":="
%token L_AT		    "@"
%token L_CBRACE		    "}"
%token L_COMMA		    ","
%token L_COLON		    ":"
%token L_DAMPER		    "&&"
%token L_DCOLON		    "::"
%token L_DEQUALS	    "=="
%token L_DOTDOT		    ".."
%token L_DOTDOTDOT	    "..."
%token L_DSLASH		    "//"
%token L_DVBAR		    "||"
%token L_EOS		    ";"
%token L_EQUALS		    "="
%token L_GRTR		    ">"
%token L_GRTREQ		    ">="
%token L_LESS		    "<"
%token L_LESSEQ		    "<="
%token L_MINUS		    "-"
%token L_NOT		    "!"
%token L_NOTEQUALS	    "!="
%token L_OBRACE		    "{"
%token L_OBRACK		    "["
%token L_OPAREN		    "("
%token L_PLUS		    "+"
%token L_PLUSEQUALS	    "+="
%token L_QUESTION	    "?"
%token L_SLASH		    "/"
%token L_STAR		    "*"
%token L_UNDERSCORE	    "_"
%token L_VBAR		    "|"

%token L_LAST			/* Last literal token value */

/*
 * Keyword tokens
 */
%token K_APPEND			"append"
%token K_APPLY_IMPORTS		"apply-imports"
%token K_APPLY_TEMPLATES	"apply-templates"
%token K_ATTRIBUTE		"attribute"
%token K_ATTRIBUTE_SET		"attribute-set"
%token K_CALL			"call"
%token K_CASE_ORDER		"case-order"
%token K_CDATA_SECTION_ELEMENTS	"cdata-section-elements"
%token K_COMMENT		"comment"
%token K_COPY_NODE		"copy-node"
%token K_COPY_OF		"copy-of"
%token K_COUNT			"count"
%token K_DATA_TYPE		"data-type"
%token K_DECIMAL_FORMAT		"decimal-format"
%token K_DECIMAL_SEPARATOR	"decimal-separator"
%token K_DIE			"die"
%token K_DIGIT			"digit"
%token K_DOCTYPE_PUBLIC		"doctype-public"
%token K_DOCTYPE_SYSTEM		"doctype-system"
%token K_ELEMENT		"element"
%token K_ELSE			"else"
%token K_ENCODING		"encoding"
%token K_EXCLUDE		"exclude"
%token K_EXPR			"expr"
%token K_EXTENSION		"extension"
%token K_FALLBACK		"fallback"
%token K_FALSE			"false" /* JSON */
%token K_FOR			"for"
%token K_FORMAT			"format"
%token K_FOR_EACH		"for-each"
%token K_FROM			"from"
%token K_FUNCTION		"function"
%token K_GROUPING_SEPARATOR	"grouping-separator"
%token K_GROUPING_SIZE		"grouping-size"
%token K_ID			"id"
%token K_IF			"if"
%token K_IMPORT			"import"
%token K_INCLUDE		"include"
%token K_INDENT			"indent"
%token K_INFINITY		"infinity"
%token K_KEY			"key"
%token K_LANGUAGE		"language"
%token K_LETTER_VALUE		"letter-value"
%token K_LEVEL			"level"
%token K_MATCH			"match"
%token K_MAIN			"main"
%token K_MEDIA_TYPE		"media-type"
%token K_MESSAGE		"message"
%token K_MINUS_SIGN		"minus-sign"
%token K_MODE			"mode"
%token K_MVAR			"mvar"
%token K_NAN			"nan"
%token K_NODE			"node"
%token K_NS			"ns"
%token K_NS_ALIAS		"ns-alias"
%token K_NS_TEMPLATE		"ns-template"
%token K_NULL			"null" /* JSON */
%token K_NUMBER			"number"
%token K_OMIT_XML_DECLARATION	"omit-xml-declaration"
%token K_ORDER			"order"
%token K_OUTPUT_METHOD		"output-method"
%token K_PARAM			"param"
%token K_PATTERN_SEPARATOR	"pattern-separator"
%token K_PERCENT		"percent"
%token K_PER_MILLE		"per-mille"
%token K_PRESERVE_SPACE		"preserve-space"
%token K_PRIORITY		"priority"
%token K_PROCESSING_INSTRUCTION "processing-instruction"
%token K_RESULT			"result"
%token K_SET			"set"
%token K_SORT			"sort"
%token K_STANDALONE		"standalone"
%token K_STRIP_SPACE		"strip-space"
%token K_TEMPLATE		"template"
%token K_TERMINATE		"terminate"
%token K_TEXT			"text"
%token K_TRACE			"trace"
%token K_TRUE			"true" /* JSON */
%token K_UEXPR			"uexpr"
%token K_USE_ATTRIBUTE_SETS	"use-attribute-set"
%token K_VALUE			"value"
%token K_VAR			"var"
%token K_VERSION		"version"
%token K_WHILE			"while"
%token K_WITH			"with"
%token K_ZERO_DIGIT		"zero-digit"

/*
 * Operator keyword tokens, which might be NCNames if they appear inside an
 * XPath expression
 */
%token M_OPERATOR_FIRST		/* Magic marker: first operator keyword */
%token K_AND			"and"
%token K_DIV			"div"
%token K_MOD			"mod"
%token K_OR			"or"
%token M_OPERATOR_LAST		/* Magic marker: last operator keyword */

/*
 * Literal tokens which _may_ preceed the multiplication operator
 */
%token M_MULTIPLICATION_TEST_LAST /* Magic marker: highest token number */
%token L_ASTERISK		"*"
%token L_CBRACK			"]"
%token L_CPAREN			")"
%token L_DOT			"."

/*
 * Token types: generic tokens (returned via ss_token)
 */
%token T_AXIS_NAME		/* a built-in axis name */
%token T_BARE			/* a bare word string (bare-word) */
%token T_FUNCTION_NAME		/* a function name (bare-word) */
%token T_NUMBER			/* a number (4) */
%token T_QUOTED			/* a quoted string ("foo") */
%token T_VAR			/* a variable name ($foo) */

/*
 * Magic tokens (used for special purposes).  M_* tokens are used to
 * trigger explicit behavior in the parser.  These tokens are never
 * really parsed, and the lexer will never return them, except for
 * M_ERROR.
 */
%token M_SEQUENCE		/* A $x...$y sequence */
%token M_CONCAT			/* underscore -> concat mapping */
%token M_TERNARY		/* "?:" -> <xsl:choose> mapping */
%token M_TERNARY_END		/* End of a M_TERNARY chain */
%token M_ERROR			/* An error was detected in the lexer */
%token M_XPATH			/* Building an XPath expression */
%token M_PARSE_FULL		/* Parse a slax document */
%token M_PARSE_SLAX		/* Parse a SLAX-style XPath expression */
%token M_PARSE_XPATH		/* Parse an XPath expression */
%token M_PARSE_PARTIAL		/* Parse partial SLAX contents */
%token M_JSON			/* Parse a JSON document */

/*
 * 'Constructs' refer to high-level constructs, as we move from lexing
 * to semantics.  This is meant to make processing easier.
 */
%token C_ABSOLUTE		/* An absolute path */
%token C_ATTRIBUTE		/* Attribute axis ('@') */
%token C_DESCENDANT		/* Absolute child ("//tag") */
%token C_ELEMENT		/* Path element */
%token C_EXPR			/* Parenthetical expresion (nested) */
%token C_INDEX			/* Index value ('foo[4]') */
%token C_NOT			/* Negation of path */
%token C_PATH			/* Path of elements */
%token C_PREDICATE		/* Node contains a predicate */
%token C_TEST			/* Node test (e.g. node()) */
%token C_UNION			/* Union of two paths */

/*
 * Use a "%pure-parser" for reentracy
 */
%define api.pure full

%{

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <wchar.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <wctype.h>
#include <getopt.h>
#include <langinfo.h>

#include "xo_config.h"
#include "xo.h"
#include "xo_encoder.h"
#include "xo_buf.h"
#include "xo_xpath.tab.h"
#include "xo_xparse.h"

/*
 * This is a pure parser, allowing this library to link with other users
 * of yacc. To allow this, we pass our data structure (xpath_data_t)
 * to yyparse as the argument.
 */
#define yyparse xo_xpath_yyparse
#define YYPARSE_PARAM_TYPE xo_xparse_data_t *
#define YYPARSE_PARAM xparse_data
#define YYLEX_PARAM_TYPE xo_xparse_data_t *
#define YYLEX_PARAM xparse_data

#define YYSTYPE xo_xparse_node_id_t

#define YYERROR_VERBOSE

/*
 * With a "pure" parser, these are all local variables so we don't
 * need to have them #defined into "long" version (with the prefix),
 * so we nuke the #defines and use the real/old names.
 */
#undef yyerrflag
#undef yychar
#undef yyval
#undef yylval
#undef yynerrs
#undef yyloc
#undef yylloc

#undef yyerror
#define yyerror(_str) \
    xo_xpath_yyerror(xparse_data, _str, yystate)

#undef yylex
#define yylex(_lvalp, _param) \
    xo_xpath_yylex(_param, _lvalp)

/*
 * Even if we don't want debug printfs, we still need the arrays with
 * names for our own nefarious purposes.
 */
#undef YYDEBUG
#ifdef XO_YYDEBUG
#define YYDEBUG 1		/* Enable debug output */
#define YYFPRINTF fprintf	/* Log via our function */
#else /* XO_YYDEBUG */
#define YYDEBUG 1		/* Enable debug output */
#define YYFPRINTF xo_dont_bother /* Don't log via our function */

static inline void UNUSED
xo_dont_bother (FILE *fp UNUSED, const char *fmt UNUSED, ...)
{
    return;
}

#endif /* XO_YYDEBUG */

%}

/* -------------------------------------------------------------------
 */

%start start

%%

start :
        xpath_union_expression
		{
		    $$ = xo_xparse_yyval(xparse_data, $1);
		    xo_xparse_results(xparse_data, $1);
		    xparse_data->xd_errors = yynerrs;
		}
	;

q_name :
        T_BARE
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
        ;

xpath_value :
        xpath_expression
                { $$ = xo_xparse_yyval(xparse_data, $1); }
        ;

xpath_expression :
	xp_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpath_union_expression :
	xp_union_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

/* ---------------------------------------------------------------------- */

and_operator :
	K_AND
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| L_DAMPER
		{
		    xo_xparse_node_ok(xparse_data, $$)->xn_type = K_AND;
		}
	;

or_operator :
	K_OR
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| L_DVBAR
		{
		    xo_xparse_node_ok(xparse_data, $$)->xn_type = K_OR;
		}
	;

equals_operator :
	L_EQUALS
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| L_DEQUALS
		{
		    xo_xparse_node_ok(xparse_data, $$)->xn_type = L_EQUALS;
		}
	;

xp_location_path :
	xpc_relative_location_path
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_absolute_location_path
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xp_absolute_location_path :
	L_SLASH xp_relative_location_path_optional
		{
		    xo_xparse_node_set_type(xparse_data, $1, C_ABSOLUTE);
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}

	| xpc_abbreviated_absolute_location_path
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xp_expr :
	xp_ternary_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xp_ternary_expr :
	xp_or_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_or_expr L_QUESTION xp_or_expr L_COLON xp_ternary_expr
		{
		    xo_xparse_ternary_rewrite(xparse_data, &$$, &$1, &$2, &$3, &$4, &$5);
		}

	| xp_or_expr L_QUESTION L_COLON xp_ternary_expr
		{
		    xo_xparse_ternary_rewrite(xparse_data, &$$, &$1, &$2, NULL, &$3, &$4);
		}
	;

xp_primary_expr :
	L_OPAREN xpc_expr L_CPAREN
		{
		    xo_xparse_node_set_type(xparse_data, $1, C_EXPR);
		    xo_xparse_node_set_contents(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}

	| xpc_variable_reference
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xpc_literal
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xpc_number
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xpc_function_call
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xp_union_expr :
	xp_not_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_union_expr L_VBAR xp_not_expr
		{
		    if (xo_xparse_node_type(xparse_data, $1) == C_UNION) {
			
			xo_xparse_node_set_next(xparse_data,
				xo_xparse_node_contents(xparse_data, $1),
						$3);
			$$ = xo_xparse_yyval(xparse_data, $1);
		    } else {
			xo_xparse_node_set_type(xparse_data, $2, C_UNION);
			xo_xparse_node_set_contents(xparse_data, $2, $1);
			xo_xparse_node_set_next(xparse_data, $1, $3);
			$$ = xo_xparse_yyval(xparse_data, $2);
		    }
		}
	;

xp_not_expr :
	xp_path_expr 
		{ 
		    xo_xparse_node_id_t id = xo_xparse_node_new(xparse_data);
		    xo_xparse_node_t *xnp = xo_xparse_node(xparse_data, id);
		    xnp->xn_type = C_PATH;
		    xo_xparse_node_set_contents(xparse_data, id, $1);
		    $$ = xo_xparse_yyval(xparse_data, id);
		}

	| L_NOT xp_path_expr
		{
		    xo_xparse_node_set_type(xparse_data, $1, C_NOT);
		    xo_xparse_node_set_contents(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xp_path_expr :
	xp_location_path
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_filter_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_filter_expr L_SLASH xpc_relative_location_path
		{
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    xo_xparse_node_set_next(xparse_data, $2, $3);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}

	| xp_filter_expr L_DSLASH xpc_relative_location_path
		{
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    xo_xparse_node_set_next(xparse_data, $2, $3);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xp_filter_expr :
	xp_primary_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_filter_expr xpc_predicate
		{
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xp_or_expr :
	xp_and_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_or_expr or_operator xp_and_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}
	;

xp_and_expr :
	xp_equality_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_and_expr and_operator xp_equality_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}
	;

xp_equality_expr :
	xp_relational_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_equality_expr equals_operator xp_relational_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_equality_expr L_NOTEQUALS xp_relational_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}
	;

xp_concative_expr :
	xp_additive_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_concative_expr L_UNDERSCORE xp_additive_expr
		{
		    xo_xparse_concat_rewrite(xparse_data, &$$, &$1, &$2, &$3);
		}
	;

xp_additive_expr :
	xp_multiplicative_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_additive_expr L_PLUS xp_multiplicative_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_additive_expr L_MINUS xp_multiplicative_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}
	;

xp_multiplicative_expr :
	xp_unary_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_multiplicative_expr L_STAR xp_unary_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_multiplicative_expr K_DIV xp_unary_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_multiplicative_expr K_MOD xp_unary_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}
	;

xp_unary_expr :
	xp_union_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| L_MINUS xp_unary_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

/* ------------------------------------------------------------------- */

xp_relative_location_path_optional :
	/* empty */
		{ $$ = xo_xparse_yyval(xparse_data, 0); }

	| xpc_relative_location_path
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_function_call :
	T_FUNCTION_NAME L_OPAREN xpc_argument_list_optional L_CPAREN
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_argument_list_optional :
	/* empty */
		{ $$ = xo_xparse_yyval(xparse_data, 0); }

	| xpc_argument_list
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_argument_list :
	xpc_argument
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xpc_argument_list L_COMMA xpc_argument
		{
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    xo_xparse_node_set_next(xparse_data, $2, $3);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xpc_argument :
	xpath_value
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	;

xpc_axis_specifier_optional :
	/* empty */
		{ $$ = xo_xparse_yyval(xparse_data, 0); }

	| T_AXIS_NAME L_DCOLON
		{
		    xo_xparse_check_axis_name(xparse_data, $1);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}

	| xpc_abbreviated_axis_specifier
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_predicate_list :
	/* empty */
		{ $$ = xo_xparse_yyval(xparse_data, 0); }

	| xpc_predicate_list xpc_predicate
		{
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1 ?: $2);
		}
	;

xpc_predicate :
	L_OBRACK xpath_expression L_CBRACK
		{
		    xo_xparse_node_set_type(xparse_data, $1, C_PREDICATE);
		    xo_xparse_node_set_str(xparse_data, $1, 0);
		    if (xo_xparse_node_type(xparse_data, $2) == T_NUMBER)
			xo_xparse_node_set_type(xparse_data, $2, C_INDEX);
		    xo_xparse_node_set_contents(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xpc_relative_location_path :
	xpc_step
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xpc_relative_location_path L_SLASH xpc_step
		{
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}

	| xpc_abbreviated_relative_location_path
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_step :
	xpc_axis_specifier_optional xpc_node_test xpc_predicate_list
		{
		    if (xo_xparse_node_type(xparse_data, $1) == L_AT
			|| xo_xparse_node_is_attr_axis(xparse_data, $1)) {
			xo_xparse_node_set_type(xparse_data, $2, C_ATTRIBUTE);
		    } else {
			xo_xparse_node_set_contents(xparse_data, $2, $1);
		    }
		    xo_xparse_node_set_contents(xparse_data, $2, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xpc_abbreviated_step
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_abbreviated_absolute_location_path :
	L_DSLASH xpc_relative_location_path
		{
		    xo_xparse_node_set_type(xparse_data, $1, C_DESCENDANT);
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xpc_abbreviated_relative_location_path :
	xpc_relative_location_path L_DSLASH xpc_step
		{
		    xo_xparse_node_set_type(xparse_data, $2, C_DESCENDANT);
		    xo_xparse_node_set_next(xparse_data, $1, $2);
		    xo_xparse_node_set_next(xparse_data, $2, $3);
		    $$ = xo_xparse_yyval(xparse_data, $1 ?: $2);
		}
	;

xpc_literal :
	T_QUOTED
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_literal_optional :
	/* Empty */
		{ $$ = xo_xparse_yyval(xparse_data, 0); }

	| xpc_literal
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_number :
	T_NUMBER
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_variable_reference :
	T_VAR
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_node_test :
	xpc_name_test	
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xpc_node_type L_OPAREN L_CPAREN
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| K_PROCESSING_INSTRUCTION L_OPAREN xpc_literal_optional L_CPAREN
		{
		    xo_xparse_node_set_contents(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
	;

xpc_name_test :
	q_name
		{
		    xo_xparse_node_set_type(xparse_data, $1, C_ELEMENT);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}

	| L_ASTERISK /* L_STAR */
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_node_type :
	K_COMMENT
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| K_TEXT
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| K_NODE
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_abbreviated_step :
	L_DOT
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| L_DOTDOT
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

xpc_abbreviated_axis_specifier :
	L_AT
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

/*
 * This production gives a mechanism for slaxparser-xp.y to refer to
 * the "real" expr production, rather than the "lite" version.
 */
xpc_expr :
	xp_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }
	;

/*
 * There's another ambiguity between SLAX and attribute value templates
 * involving the use of ">" in an expression, like:
 *
 *      <problem child=one > two>;
 *
 * The conflict is between ">" as an operator in an XPath expression
 * and the closing character of a tag.  Fortunately, an expression
 * like this makes no sense in the attribute context, so we simply
 * block the use of relational operators inside "xpl" expressions.
 */

xp_relational_expr :
	xp_concative_expr
		{ $$ = xo_xparse_yyval(xparse_data, $1); }

	| xp_relational_expr L_LESS xp_concative_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_relational_expr L_GRTR xp_concative_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_relational_expr L_LESSEQ xp_concative_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}

	| xp_relational_expr L_GRTREQ xp_concative_expr
		{
		    xo_xparse_node_set_contents(xparse_data, $2, $1);
		    xo_xparse_node_set_next(xparse_data, $1, $3);
		    $$ = xo_xparse_yyval(xparse_data, $2);
		}
	;

%%

#ifndef XO_YYDEBUG
#undef yydebug
#define yydebug 0
#endif /* XO_YYDEBUG */

/*
 * These definitions need values and defines that are internal to
 * yacc, so they must be placed here.
 */

#ifndef YYNTOKENS
#ifdef YYMAXTOKEN
#define YYNTOKENS (YYMAXTOKEN+1)
#else
#error unknown value for YYNTOKENS
#endif /* YYMAXTOKEN */
#endif /* YYNTOKENS */

#ifndef YYLAST
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST ((sizeof(yytable) / sizeof(yytable[0])) - 1)
#endif /* YYLAST */

#define yytname xo_xpath_yyname
#define yypact xo_xpath_yydefred

const int xo_xparse_num_tokens = YYNTOKENS;
const char *xo_xparse_keyword_string[YYNTOKENS];
const char *xo_xparse_token_name_fancy[YYNTOKENS];

/*
 * Return a human-readable name for a given token type
 */
const char *
xo_xparse_token_name (int ttype)
{
    if (ttype < 0 || ttype >= YYNTOKENS)
	return "unknown";

    return yytname[YYTRANSLATE(ttype)];
}

const char *
xo_xparse_fancy_token_name (int ttype)
{
    if (ttype < 0 || ttype >= YYNTOKENS)
	return "unknown";

    return xo_xparse_token_name_fancy[YYTRANSLATE(ttype)];
}

/*
 * Expose YYTRANSLATE outside the yacc file
 */
int
xo_xparse_token_translate (int ttype)
{
    return YYTRANSLATE(ttype);
}

#ifndef YYTERROR
#define YYTERROR YYSYMBOL_YYerror /* the new enum */
#endif /* YYTERROR */

#if 1
/*
 * Return a better class of error message, if possible.  But it turns
 * out that this isn't possible in yacc.  bison adds a "lookahead
 * correction" that gives us information that we can use to find the
 * list of possibly-valid next tokens, which we use to build an
 * "expecting ..." message, but lacking that information, we just punt.
 *
 * The original code is in libslax/slaxparser.y, so maybe one day I'll
 * try to make it work.
 */
char *
xo_xparse_expecting_error (const char *token, int yystate UNUSED,
			   int yychar UNUSED)
{
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);

    SNPRINTF(cp, ep, "unexpected input");
    if (token)
	SNPRINTF(cp, ep, ": %s", token);

    return strdup(buf);
}
#endif
