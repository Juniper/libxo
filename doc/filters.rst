.. index:: filter

.. _filter:

Filtering Output
================

The `--libxo filter=path` option allows output to be filter using
XPath-like expressions that describe the set of node to emit or
suppress.

Filter expressions start with a union, which is part of the XPath
standard, but not a normal top-level rule.  `libxo` uses a union to
allow multiple paths to be expressed in a single filter option
("--libxo filter=one|two").  In addition, multiple paths can be
expressed in multiple filter options ("--libxo filter=one --libxo
filter=two").

`libxo` makes extensive use of the XPath expression language.
Many XPath constructs are not useful in filter expressions.  In
addition, many of the constructs are not fully implemented in `libxo`.
While the library will recognize the full XPath syntax, it will
display errors when unimplemented constructs are used.  These
constructs will be implemented over time, as time and requirements
allow.

Expressions are used to select nodes, specify conditions, and to
filter output content.  Expressions contain five constructs:

- Paths to elements

 - Select nodes based on node names

  - Example: chapter

 - Selects child nodes based on parent node names

  - Example: doc/chapter/section/paragraph

 - Selects child nodes under at any depth

  - Example: doc//paragraph

 - Selects parent nodes relative to the current node

  - Example: ../../chapter

 - Selects attributes using "@" followed by attribute name

  - Example: ../../chapter/@number

- Predicate tests

 - Selects nodes for which the expression in the square brackets
   evaluates to "true" (with boolean() type conversion)

  - Example chapter[@number == 1]

 - Can be applied to any path member

  - Example: chapter[@number == 1]/section[@number == 2]

 - Use a number to select the <n>th node from a node set

  - Example: chapter[1]

 - Multiple predicate tests can be specified (ANDed together)

  - Example: chapter[@number > 15][section/@number > 15]

- References to variables or parameters

 - Variable and parameter names start with "$"

  - Example: $this

 - Variables can hold expressions, node sets, etc

  - Example: $this/paragraph

- Literal strings

 - `libxo` accepts single or double quotes

  - Example: "test"

 - Character escaping is allowed

  - Example: '\tthat\'s good\n\tnow what?\n'

- Calls to functions

 - Function calls can be used in expressions or predicate tests

  - Example: chapter[count(section) > 15]

 - Allows calls to pre-defined or user-defined functions

  - Example: chapter[substring-before(title, "ne") == "O"]

`libxo` follows XPath syntax, with the following additions:

+ "&&" may be used in place of the "and" operator
+ "||" may be used in place of the "or" operator
+ "==" may be used in place of the "=" operator
+ "!" may be used in place of the "not()" operator
+ "_" is the concatenation operator ("x" _ "y" === concat("x", "y"))
+ "?:" is converted into choose/when/otherwise elements

The first four additions are meant to prevent programmers from
learning habits in `libxo` that will negatively affect their ability
to program in other languages.  The last two additions are for
convenience.

When referring to XPath expressions in this document, we
mean this extended syntax.

Strings are encoded using quotes (single or double) in a way that will
feel natural to C programmers.  The concatenation operator is
underscore ("_"), which is the new concatenation operator for Perl 6.
(The use of "+" or "." would have created ambiguities in the SLAX
language.)

Many elements of the expression syntax are characters that are
interpreted by various user shells (zsh, tcsh, bash, etc) and will
need to be properly escaped according to the rules of the specifics
shell.
