.. index:: filter

.. _filter:

Filtering Output
================

The filter feature allows applications to select which portions of
structured output are emitted, based on XPath-like expressions.  Only
matching instances are written to the output stream; non-matching
instances are silently discarded.

Filters are useful when an application emits a large dataset (for
example, all network sockets) but only a subset is of interest (for
example, established TCP connections).  The filter can be applied
without modifying the application code.  Unlike traditional unix
tools, filters are aware of the fields and their context.

.. index:: Filter expressions
.. index:: expression syntax
.. _filter-expressions:

Expression Syntax
-----------------

Filters use an XPath-like expression language.
Expressions are used to select content hierarchies and specify conditions
for content to be selected.  Expressions contain five constructs:

- Paths to elements

  - Select nodes based on node names

    - Example: chapter
    - Selects all elements with the name `chapter`

  - Selects child nodes based on parent node names

    - Example: doc/chapter/section/paragraph
    - Selects all `paragraph` elements that are under a `section`
      element, which are themselves under a `chapter` element, which
      in turn must be under a `doc` element.

- Negate elements

  - Select nodes not to emit

    - Example: !remote
    - Select all node except `remote` and its descendants

- Wildcard elements

  - Select any nodes.  A "\*" matches any element.

    - Example: one/\*/three
    - Select any `three` elements that are under an element of any
      name, which in turn must be under a `one` element .

- Relative and absolute paths

  - A path with a leading slash is "anchored" at the root of the
    output tree, while one without matches anywhere in the output
    hierarchy.

    - Example: /one/two and one/two
    - The former matches any `two` elements under a root-level `one`
      element, while the latter matches any `two` elements under a
      `one` element anywhere in the tree.

- Predicate tests

  - Selects nodes for which the expression in the square brackets
    evaluates to "true" (with boolean() type conversion)

    - Example chapter[number == 1]
    - Selects all `chapter` elements which contain a `number` element with
      a value of 1.

  - Can refer to attributes using a leading at-sign ("@")

    - Example chapter[@number == 1]
    - Selects all `chapter` elements which have a `number` attribute with
      a value of 1.

  - Can perform math using addition ("+"), subtraction ("-"),
    multiplication ("*") , and division ("div") and comparisons using
    equals ("=" or "=="), greater than (">"), greater than or equal
    (">="), less than ("<"), less than or equal ("<=").  Comparisons
    allow type forcing when the types are unequal.

    - Example: item[cost * count >= 1500]
    - Selects any `item` element where the product of their `cost` and
      `count` elements are greater than or equal to 1500.

  - Can be applied to any path member

    - Example: chapter[@number == 1]/section[@number == 2]
    - Selects all `section` elements which have a `number` attribute with
      a value of 2 which are parented by a `chapter` element which
      have a `number` attribute with a value of 1.

  - Use a number to select the <n>th node from a node set, using an
    origin of 1 (not 0):

    - Example: chapter[1]
    - Selects the first `chapter` element

  - Multiple predicate tests can be specified (ANDed together)

    - Example: chapter[@number > 15][page-count > 10]
    - Selects all `chapter` elements with a `number` attribute with a
      value greater than 15 and contains a `page-count` element with
      value greater than 10.

- Each step of the path can zero or more predicates
    - Example: one[a > 4]/two[b < 3]/three[c == 2]/four[d == 1]

- Literal string

  - `libxo` accepts single or double quotes

    - Example: "test"

  - Character escaping is supported

    - Example: '\\tthat\\'s good\\n\\tnow what?\\n'

- Numbers

  - Integer values

    - Example: 5, 10, 20000

  - Floating point values

    - Example: 3.14, 4.3e50, 0.125

  - Hexadecimal numbers (base 16)

    - Example: 0x20, 0xABCD, 0xfefefefe

- Calls to functions

  - Function calls can be used in expressions or predicate tests

    - Example: chapter[number(section) > 15]

  - Allows calls to pre-defined or user-defined functions

    - Example: chapter[substring-before(title, "ne") == "O"]

`libxo` follows XPath syntax, with the following additions:

- "&&" may be used in place of the "and" operator.

  - Example: food[@fruit && @delicious]
  - Synonym: food[@fruit and @delicious]

- "||" may be used in place of the "or" operator.

  - Example: fish[tropical || colorful]
  - Synonym: fish[tropical or colorful]

- "==" may be used in place of the "=" operator.

  - Example: tree[height == 10]
  - Synonym: tree[height = 10]

- "!" may be used in place of the "not()" operator.

  - Example: socket[!contains(path, "/var/run/")]
  - Synonym: socket[not(contains(path, "/var/run/"))]

- "_" is the concatenation operator: ("x" _ "y" === concat("x", "y"))

  - Example: thing[number("0x" _ hex-digits _ "00") > length]
  - Synonym: thing[number(concat("0x", hex-digits, "00")) > length]

- "?:" is converted into choose() and choose2() function calls.  Both
  the "condition ? if-true : if-false" and "condition ?: if-false"
  styles are supported.

  - Example: item[(user ?: login ?: owner) == "phil"]
  - Synonym: item[choose2(user, choose2(login, owner)) == "phil"]

The first four additions are meant to prevent programmers from
learning habits writing expression that will negatively affect their
ability to program in other languages.  It also keep users from
getting bitten when using familiar syntax.

The last two additions are for convenience and readability.
The colon-question is amazingly useful and hard to live without.

Strings are encoded using quotes (single or double) in a way that will
feel natural to C programmers.  The concatenation operator is
underscore ("_").  While this may seem an odd choice for the
concatenation operator, many of the familiar operators like "+" and
"." have other meanings in XPath expressions and cannot be used.

When referring to `filter expressions` or `path expressions` in this
document, we mean this extended syntax.

Enabling Filters
----------------

Filters can be enabled from the command line or from application code.

From the command line, pass one or more `filter=` options to
`--libxo`::

    netstat --libxo xml,pretty,filter='socket[tcp-state=="ESTABLISHED"]'

Multiple filter expressions are combined as a union (logical OR): an
instance matches if it satisfies any of the given expressions.  The
same effect can be achieved with a single `|`-separated expression or
with repeated `filter=` options::

    # Two expressions in a single option (union)
    my-app --libxo filter='socket[tcp-state=="ESTABLISHED"]|interface[name=="eth0"]'

    # Equivalently, two separate options
    my-app --libxo filter='socket[tcp-state=="ESTABLISHED"]' \
           --libxo filter='interface[name=="eth0"]'

The `filter-warn` option enables diagnostic messages on standard
error when filter predicates reference fields that are not present in
the output::

    my-app --libxo filter=item[color=='red'],filter-warn

Application code should likely never need it, but they can call
`xo_add_filter` to add a filter to a handle::

    xo_add_filter(xop, "socket[tcp-state==\"ESTABLISHED\"]");

See :ref:`xo_add_filter` for details.

Filter Expression Detail
------------------------

A filter path identifies which list to filter.  The path uses the same
element names as the `xo_open_list`/`xo_open_instance` calls in the
application.

The filter system is an optional component of libxo.  It is compiled
and linked into the library only when the `--enable-filters` option
is passed to `configure`.  When filters are not compiled in, the
`xo_add_filter` function is still present but returns an error.

Filtering is provided via a dynamically loaded library, reducing the
impact when filtering is not used.  If the filter library cannot be
loaded, an error message is emitted.

While content is being generated, `libxo` is applying any filters and
deciding what can be discarded completely, what can be emitted
immediately, and what must be buffered pending a final decision.  The
quicker this decision can be made, the better performance will be, and
filter expressions can be tailored to increase performance by allowing
that decision to be made as soon as possible.

For example the expression "/one/two/three" can discard any top-level element
that isn't `one`, which must then be rendered and buffered.  Any child
element of `one` that is not a `two` can be discard until a `two` is
seen, at which time `libxo` will similarly discard anything that's not
a `three`.

A predicate like `socket[type == "tcp4"]' will buffer any `socket`
element, along with its complete hierarchy, until a `type` element
arrives, at which point a decision to discard or emit can be made
based on the contents of that `type` element.

A predicate using attributes is simpler, since the attribute will be
know when the element is emitted.

Filter expressions are a subset of XPath, with many features avoided
due to implementation cost and performance impact.

Among the unimplemented features are:

- ".." (parent); requires buffering parent siblings.
- node tests: comment(), text(), node(), and processing-instruction();
  not needed in this context.
- id() and key(); not needed in this context.
- "//" (descendant); requires additional buffering.
- nested predicates (predicates with predicates); complexity issues.
- predicate paths (deep paths in predicates); requires additional buffering.

Use of these features is detected and reported by `libxo` as errors.

Keys and Non-Keys in Predicates
+++++++++++++++++++++++++++++++

Keys are those fields using the "k" modifier and must appear before
non-key fields.  Since these appear first, buffering needs are
reduced, leading to better performance.  Predicates using non-key
fields will require more rendering and buffering.

In an instance closes while a predicate is still being evaluated due
to a with a missing field, that predicate will be evaluated with an
empty value for that field.

Example: filtering sockets by TCP state::

    $ netstat --libxo xml,pretty,filter='socket[tcp-state=="ESTABLISHED"]'
    <statistics>
      <socket>
        <protocol>tcp4</protocol>
        <receive-bytes-waiting>0</receive-bytes-waiting>
        <send-bytes-waiting>0</send-bytes-waiting>
        <local>
          <address>172.16.188.131</address>
          <port>ssh</port>
        </local>
        <remote>
          <address>172.16.188.1</address>
          <port>63510</port>
        </remote>
        <tcp-state>ESTABLISHED</tcp-state>
      </socket>
    </statistics>

The `socket` element is completely rendered into text and buffered
before the `tcp-state` field arrives and the predicate evaluates to
true.  Any `socket` elements that don't have a `tcp-state` will also be
fully rendered before `libxo` knows that the field is not present.

Sub-container Content
+++++++++++++++++++++

When an instance is matched, all of its content is emitted — including
fields inside nested containers (child containers opened with
`xo_open_container` inside the instance).  In the example above, the
`local` and `remote` containers and their fields are included in
the output for the matched socket.

But the limitation on predicate paths (deep paths in predicates) means
that you cannot use subelements of `local` or `remote` in a predicate.

Content in nested containers that appear before the predicate field is
buffered along with the top-level fields.  If the predicate resolves
true, the nested container content is committed as part of the
instance.  If the predicate resolves false, the entire instance —
including nested content — is discarded.

.. index:: Filter functions

Functions in Predicates
~~~~~~~~~~~~~~~~~~~~~~~

The following XPath functions are supported in filter predicates:

String functions:

========================== ==================================================
 Function                   Description
========================== ==================================================
 concat(s1, s2, ...)        Concatenates strings
 contains(str, sub)         True when *str* contains *sub* as a substring
 ends-with(str, suffix)     True when *str* ends with *suffix*
 normalize-space(str)       Strips leading/trailing space and collapses runs
 starts-with(str, prefix)   True when *str* begins with *prefix*
 string-length(str)         Returns the length of *str*
 substring(str, pos, len)   Returns a substring of *str*
 substring-after(str, s)    Returns the part of *str* after the first *s*
 substring-before(str, s)   Returns the part of *str* before the first *s*
 translate(str, from, to)   Character-by-character translation
========================== ==================================================

Numeric functions:

========================== ==================================================
 Function                   Description
========================== ==================================================
 ceiling(num)               Rounds up to the nearest integer
 floor(num)                 Rounds down to the nearest integer
 number(val)                Converts a value to a number
 round(num)                 Rounds to the nearest integer
 sum(node-set)              Returns the sum of a node set
========================== ==================================================

Boolean functions:

========================== ==================================================
 Function                   Description
========================== ==================================================
 boolean(val)               Converts a value to a boolean
 false()                    Always returns false
 not(expr)                  Negates the expression
 true()                     Always returns true
========================== ==================================================

Note that `true()` and `true` are annoyingly different.  The former
returns the boolean value true while the latter returns the value of
an element named `true`, which is likely non-existent.

Examples::

    # Items whose label starts with "eth"
    item[starts-with(label, "eth")]

    # Items whose price rounds up to more than 3
    item[ceiling(price) > 3]

    # Sockets whose path field does not contain "/var/run/"
    socket[!contains(path, "/var/run/")]

    # Items where the part of version after '-' equals "2"
    item[substring-after(version, "-") == "2"]

Regular expression functions:

========================== ==================================================
 Function                   Description
========================== ==================================================
 rematch(re, str, flags?)   Match a regular expression
========================== ==================================================

The rematch() function matches a `regular expression`_ (`regex`) and
returns either a boolean indicating the success of the match (the
default behavior) or the portion of the string that matches all or
part of the regex.  rematch() defaults to "enhanced" regex matching.
If the `flags` argument is not provided, it defaults to an empty
string.

.. _regular expression: https://en.wikipedia.org/wiki/Regular_expression

The `flags` string contains zero or more of the following flags:

======= ====================================================================
 Flag   Description
======= ====================================================================
  'b'    Use 'basic' REs (BRE) instead of the default 'extended' RE (ERE)
  'i'    Case-insensitive matching (REG_ICASE)
  'n'    Newline-sensitive REG_NEWLINE)
  '^'    Does not match at start of REG_NOTBOL)
  '$'    Does not match at end of string (REG_NOTEOL)
  's'    Return full match text as a string
  'm'    Return first capture group as string
  'mN'   Return capture group N as string (N: 0–9)
  'p'    REG_POSIX (platform-specific; ignored if unavailable)
======= ====================================================================

For example, the following expression would match for when the
`protocol` field is `tcp4`, but not for `tcp6`:

  socket[rematch("([a-z]+)([0-9]+)", protocol, "m2") == 4]

Multiple Predicates
~~~~~~~~~~~~~~~~~~~

Multiple predicates on the same node are ANDed: all must be true for
the instance to be selected::

    # Established TCP connections with pending data
    socket[tcp-state=="ESTABLISHED"][receive-bytes-waiting > 0]

    # Interfaces that are ethernet and administratively up
    interface[type=="ethernet"][state=="up"]

Shell Quoting
-------------

Filter expressions contain characters that many shells interpret
specially, including `[`, `]`, `"`, `'`, `=`, and `|`.
Quote the expression appropriately for the shell being used::

    # bash / zsh
    my-app --libxo "filter=socket[tcp-state==\"ESTABLISHED\"]"

    # tcsh
    my-app --libxo 'filter=socket[tcp-state=="ESTABLISHED"]'

Commas are particularly awkward since they are used for two purposes:
the separate libxo options (e.g. "warn,pretty") as well as separate
arguments in filter function arguments (e.g. "start-with(one, two)").
Option parsing happens first and the filter string is opaque to the
option parsing, so the commas any functions have to be escaped::

  my-app --libxo 'filter=food[substring(name\, 1\, 4) == "hush"]'

Unsupported Features
--------------------

While filter path expressions are based heavily on the XPath standard,
many XPath features are not supported, chiefly due to their
complexity, impact on steaming data processing, buffering costs, and
performance impacts.  This following list describes the unsupported
features::

    ================== ========================================================
    Unsupported        Details
    ================== ========================================================
    ..                 Alias for the parent axis, e.g. item[../count == 4]
    Node tests         comment(), text(), processing-instruction()
    Identities         id(), key()
    //                 Deep ancestor, e.g. top//deep or //deep
    Variables          XPath variables, e.g. $one
    axis names         Any of the axis names
    Nested predicates  Predicates within predicates, e.g. one[two[three]]
    Predicate paths    Multi-member paths inside predicates, e.g. one[two/three]
    ================== ========================================================

While no axis names are supported, `libxo` does support the "@" alias
for the attribute axis.

When an unsupported feature is parsed in a filter expression, `libxo`
will give an error message::

    % my-app --libxo:XP,filter-warn,filter='item[..//node()]'
    my-app: filter expression feature is unsupported: parent axis ('..')
    my-app: filter expression feature is unsupported: descendant child (e.g. 'one//two')
    my-app: filter expression feature is unsupported: 'node()'
    my-app: could not add the requested filter

.. _filter-warn:

Filter Flags (XOF\_FILTER\_WARN)
--------------------------------

  =================== =========================================================
   Flag                Description
  =================== =========================================================
   XOF_FILTER_WARN     Emit diagnostic warnings for runtime filter errors
  =================== =========================================================

The `XOF_FILTER_WARN` flag enables diagnostic output to standard error
when runtime issues are encountered while processing filters against
incoming data.  The volume of output will depend on the filter
expressions and input data, but might be useful in debugging issues
with filter expressions.  It corresponds to the `filter-warn`
command-line option::

    my-app --libxo filter='socket[number(tcp-state)==2]',filter-warn
    my-app: invalid number value: 'tcp-state'

This message will be repeated on each conversion error.

The "fdr" Encoder
-----------------

Filters are complex expressions, and the software that implements them
is also complex.  Despite the efforts of the author, it is possible
that expressions could be used that were not considered, tested, or
implemented.  In many cases, the setup required to support the failing
situation may not be something that can be shared to allow debugging.
To allow such debugging, an encoder named "fdr" (after the aviation
industry's "flight data recorder") allows the recording of the specific
output tags being passed to the filter software, making reproducing
problems trivial.  In addition, these failing cases can be added to
the test suite to ensure the quality of future releases.

To use the FDR, just add `--libxo @fdr` to your command line and
redirect output to a file suitable for submission.  As normal, all
`--libxo` options must appear before any other options or arguments.

Please be extremely careful to ensure no secret or sensitive data
appears in this output before submitting it as a bug attachment.
