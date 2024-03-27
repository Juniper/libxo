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
%token L_ASSIGN		    1  ":="
%token L_AT		    2  "@"
%token L_CBRACE		    3  "}"
%token L_COMMA		    4  ","
%token L_COLON		    5  ":"
%token L_DAMPER		    6  "&&"
%token L_DCOLON		    7  "::"
%token L_DEQUALS	    8  "=="
%token L_DOTDOT		    9  ".."
%token L_DOTDOTDOT	    10 "..."
%token L_DSLASH		    11 "//"
%token L_DVBAR		    12 "||"
%token L_EOS		    13 ";"
%token L_EQUALS		    14 "="
%token L_GRTR		    15 ">"
%token L_GRTREQ		    16 ">="
%token L_LESS		    17 "<"
%token L_LESSEQ		    18 "<="
%token L_MINUS		    19 "-"
%token L_NOT		    20 "!"
%token L_NOTEQUALS	    21 "!="
%token L_OBRACE		    22 "{"
%token L_OBRACK		    23 "["
%token L_OPAREN		    24 "("
%token L_PLUS		    25 "+"
%token L_PLUSEQUALS	    26 "+="
%token L_QUESTION	    27 "?"
%token L_SLASH		    28 "/"
%token L_STAR		    29 "*"
%token L_UNDERSCORE	    30 "_"
%token L_VBAR		    31 "|"

%token L_LAST		    32 /* Last literal token value */

/*
 * Keyword tokens
 */
%token K_COMMENT	    33 "comment"
%token K_ID		    34 "id"
%token K_KEY		    35 "key"
%token K_NODE		    36 "node"
%token K_PROCESSING_INSTRUCTION 37 "processing-instruction"
%token K_TEXT		    38 "text"


/*
 * Operator keyword tokens, which might be NCNames if they appear inside an
 * XPath expression
 */
%token M_OPERATOR_FIRST	    39 /* Magic marker: first operator keyword */
%token K_AND		    40 "and"
%token K_DIV		    41 "div"
%token K_MOD		    42 "mod"
%token K_OR		    43 "or"
%token M_OPERATOR_LAST	    44 /* Magic marker: last operator keyword */

/*
 * Literal tokens which _may_ preceed the multiplication operator
 */
%token M_MULTIPLICATION_TEST_LAST 45 /* Magic marker: highest token number */
%token L_ASTERISK	    46 "*"
%token L_CBRACK		    47 "]"
%token L_CPAREN		    48 ")"
%token L_DOT		    49 "."

/*
 * Token types: generic tokens (returned via ss_token)
 */
%token T_AXIS_NAME	    50 /* a built-in axis name */
%token T_BARE		    51 /* a bare word string (bare-word) */
%token T_FUNCTION_NAME	    52 /* a function name (bare-word) */
%token T_NUMBER		    53 /* a number (4) */
%token T_QUOTED		    54 /* a quoted string ("foo") */
%token T_VAR		    55 /* a variable name ($foo) */

/*
 * Magic tokens (used for special purposes).  M_* tokens are used to
 * trigger explicit behavior in the parser.  These tokens are never
 * really parsed, and the lexer will never return them, except for
 * M_ERROR.
 */
%token M_SEQUENCE	    56 /* A $x...$y sequence */
%token M_CONCAT		    57 /* underscore -> concat mapping */
%token M_TERNARY	    58 /* "?:" -> <xsl:choose> mapping */
%token M_TERNARY_END	    59 /* End of a M_TERNARY chain */
%token M_ERROR		    60 /* An error was detected in the lexer */
%token M_XPATH		    61 /* Building an XPath expression */
%token M_PARSE_FULL	    62 /* Parse a slax document */
%token M_PARSE_SLAX	    63 /* Parse a SLAX-style XPath expression */
%token M_PARSE_XPATH	    64 /* Parse an XPath expression */
%token M_PARSE_PARTIAL	    65 /* Parse partial SLAX contents */
%token M_JSON		    66 /* Parse a JSON document */

/*
 * 'Constructs' refer to high-level constructs, as we move from lexing
 * to semantics.  This is meant to make processing easier.
 */
%token C_ABSOLUTE	    67 /* An absolute path */
%token C_ATTRIBUTE	    68 /* Attribute axis ('@') */
%token C_DESCENDANT	    69 /* Absolute child ("//tag") */
%token C_ELEMENT	    70 /* Path element */
%token C_EXPR		    71 /* Parenthetical expresion (nested) */
%token C_INDEX		    72 /* Index value ('foo[4]') */
%token C_NOT		    73 /* Negation of path */
%token C_PATH		    74 /* Path of elements */
%token C_PREDICATE	    75 /* Node contains a predicate */
%token C_TEST		    76 /* Node test (e.g. node()) */
%token C_UNION		    77 /* Union of two paths */
%token C_INT64		    78 /* Signed 64-bit integer */
%token C_UINT64		    79 /* Unsigned 64-bit integer */
%token C_FLOAT		    80 /* Floating point number (double) */
%token C_STRING		    81 /* String value (const char *) */
%token C_BOOLEAN	    82 /* Boolean value */
/* Note: Add new names to xo_xparse_ttname_map[] in xo_xparse.c */

/*
 * Use a "%pure-parser" for reentracy
%define api.pure full
 */
%pure-parser

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
/* Log via our function */
#define YYFPRINTF(_fp, _fmt...) xo_xparse_yyprintf(xparse_data, _fmt)
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
		    xo_xparse_node_t *xnp = xo_xparse_node(xparse_data, $1);
		    if (xnp->xn_type == C_ELEMENT) {
			xnp = xo_xparse_node(xparse_data, id);
			xnp->xn_type = C_PATH;
			xo_xparse_node_set_contents(xparse_data, id, $1);
		    } else {
			id = $1;
		    }
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
		{
		    xo_xparse_node_set_contents(xparse_data, $1, $2);
		    $$ = xo_xparse_yyval(xparse_data, $1);
		}
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
xo_xparse_token_name (xo_xparse_token_t ttype)
{
    if (ttype >= YYNTOKENS)
	return "unknown";

    return yytname[YYTRANSLATE(ttype)];
}

const char *
xo_xparse_fancy_token_name (xo_xparse_token_t ttype)
{
    if (ttype >= YYNTOKENS)
	return "unknown";

    return xo_xparse_token_name_fancy[YYTRANSLATE(ttype)];
}

/*
 * Expose YYTRANSLATE outside the yacc file
 */
xo_xparse_token_t
xo_xparse_token_translate (xo_xparse_token_t ttype)
{
    return YYTRANSLATE(ttype);
}

#ifndef YYTERROR
#define YYTERROR YYSYMBOL_YYerror /* the new enum */
#endif /* YYTERROR */

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
