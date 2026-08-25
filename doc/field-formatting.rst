
.. index:: Field Formatting
.. _field-formatting:

Field Formatting
----------------

The field format is similar to the format string for printf(3).  Its
use varies based on the role of the field, but generally is used to
format the field's contents.

If the format string is not provided for a value field, it defaults to
"%s".

Note a field definition can contain zero or more printf-style
'directives', which are sequences that start with a '%' and end with
one of following characters: "diouxXDOUeEfFgGaAcCsSp".  Each directive
is matched by one or more arguments to the xo_emit function.

The format string has the form::

  '%' format-modifier* format-character

The format-modifier can be:

- a '#' character, indicating the output value should be prefixed
  with '0x', typically to indicate a base 16 (hex) value.
- a minus sign ('-'), indicating the output value should be padded on
  the right instead of the left.
- a leading zero ('0') indicating the output value should be padded on the
  left with zeroes instead of spaces (' ').
- one or more 'h' characters, indicating shorter input data.
- one or more 'l' characters, indicating longer input data.
- a 'z' character, indicating a 'size_t' argument.
- a 't' character, indicating a 'ptrdiff_t' argument.
- a ' ' character, indicating a space should be emitted before
  positive numbers.
- a '+' character, indicating sign should emitted before any number.
- a field-width indication as described under :ref:`field-widths`.
- a '!' character followed by the number of bits in the integer
  argument (8, 16, 32, or 64).  See `integer-size_` below.

Note that 'q', 'D', 'O', and 'U' are considered deprecated and will be
removed eventually.  They are supported for compatibility with
:manpage:`printf(3)` strings.

.. _integer-sizes:

Integer Sizes
~~~~~~~~~~~~~

Use the integer size indicator to convey the size of the argument in
bytes, easing the burden of the mismatch between the fixed size types
in <stdint.h> and the implementation-dependent sizes in `printf(3)`,
e.g "%lld", "%ld", and "%d".  Having defined a `uint64_t`, you can use
"%!64x" to print it without worrying about portability issues or
resorting to using casts, not to mention "PRIu64".

::

   uint64_t count, code;
   ...
   xo_emit("Count: {:count/%!64u}, Code {:code/%#.12!64x}\n",
            count, code);

The integer size indicator consists of a '!' character followed by
"64", "32", "16" or "8" to indicate the number of bits in the argument
type and can use "d", "u", or "x" as the format-character.  Using the
"8" and "16" values is optional, since these types are guaranteed to be the
same size of smaller than "int", which is the minimal size for
variadic arguments, but their presence allow a one-to-one matching
between the type name (uint16_t) and the format ("%!16d").

As a mnemonic, consider that '!' looks like an upside down "i" for
integer.

.. _field-widths:

Field Widths
~~~~~~~~~~~~

Field widths are included in the format modifier using an optional set
of up to three groups of one or more digits ('0' - '9'), separated by
a period ('.').

If any of the groups consists of a '*' instead of a decimal digit
string, the value is given by the next argument to the function.

The first group specifies a minimum field width, in columns.  If the
formatted value uses fewer columns, spaces will be added for padding
give the proper width.

The second group specifies the "precision" which is has differing
impact, depending on the type of the field.  For floating point
values, it represents the maximum number of significant digits after
the decimal point.  For integer values, it gives the minimum number of
digits to appear, using leading zeroes for padding.

For strings, the second group has traditionally served two purposes,
giving the number of columns to fill and the number of bytes of memory
to be referenced.  But with UTF-8 character encodings, a single column
can consume up to four bytes of data in a string.

For this reason, `libxo` supports a third group, allowing the second
to represent the maximum number of columns while the third represents
the maximum number of bytes to be inspected while processing.  This
allows multi-byte characters and columns to be handled distinctly.  If
the third group is used as both the bytes count and the number of
columns, using the limited number of bytes to fill the columns,
padding with spaces if the number of bytes is exhausted.

`libxo` will not dereference memory beyond the given number of bytes.

For the "data" style encodings (XML, JSON), the first group will be
ignored, since whitespace padding is not desirable in those encodings.
The second and third will not be ignored, since `libxo` must respect
floating point precision, leading zeros, and string lengths.

::

   /* 8 columns of output, padded with zeroes */
   xo_emit("[{:count/%.8d}]\\n", count);  /* "[00001234]" */

   /* 12 columns of output, the last 8 are padded with zeroes */
   xo_emit("[{:count/%12.8d}]\\n", count);  /* "[    00001234]" */

   /* 8 columns of output; up to the first 20 bytes of 'name' are inspected */
   xo_emit("[{:name/%.8.20s}]\\n", name);  /* "[goodname]" */

   /* Same, but using '*' and function arguments */
   xo_emit("[{:name/%.*.*s}]\\n", 8, 20, name);  /* "[goodname]" */

Format Character
~~~~~~~~~~~~~~~~

The format character is described in the following table:

  ===== ================= ======================
   Ltr   Argument Type     Format
  ===== ================= ======================
   d     int               base 10 (decimal)
   i     int               base 10 (decimal)
   o     int               base 8 (octal)
   u     unsigned          base 10 (decimal)
   x     unsigned          base 16 (hex)
   X     unsigned long     base 16 (hex)
   D     long              base 10 (decimal)
   O     unsigned long     base 8 (octal)
   U     unsigned long     base 10 (decimal)
   e     double            [-]d.ddde+-dd
   E     double            [-]d.dddE+-dd
   f     double            [-]ddd.ddd
   F     double            [-]ddd.ddd
   g     double            as 'e' or 'f'
   G     double            as 'E' or 'F'
   a     double            [-]0xh.hhhp[+-]d
   A     double            [-]0Xh.hhhp[+-]d
   c     unsigned char     a character
   C     wint_t            a character
   s     char \*           a UTF-8 string
   S     wchar_t \*        a unicode/WCS string
   p     void \*           '%#lx'
  ===== ================= ======================

The 'h' and 'l' modifiers affect the size and treatment of the
argument:

  ===== ============= ====================
   Mod   d, i          o, u, x, X
  ===== ============= ====================
   hh    signed char   unsigned char
   h     short         unsigned short
   l     long          unsigned long
   ll    long long     unsigned long long
   j     intmax_t      uintmax_t
   t     ptrdiff_t     ptrdiff_t
   z     size_t        size_t
   q     quad_t        u_quad_t
  ===== ============= ====================

.. index:: UTF-8
.. index:: Locale

.. _utf-8:

UTF-8 and Locale Strings
~~~~~~~~~~~~~~~~~~~~~~~~

For strings, the 'h' and 'l' modifiers affect the interpretation of
the bytes pointed to argument.  The default '%s' string is a 'char \*'
pointer to a string encoded as UTF-8.  Since UTF-8 is compatible with
ASCII data, a normal 7-bit ASCII string can be used.  '%ls' expects a
'wchar_t \*' pointer to a wide-character string, encoded as a 32-bit
Unicode values.  '%hs' expects a 'char \*' pointer to a multi-byte
string encoded with the current locale, as given by the LC_CTYPE,
LANG, or LC_ALL environment varibles.  The first of this list of
variables is used and if none of the variables are set, the locale
defaults to "UTF-8".

libxo will convert these arguments as needed to either UTF-8 (for XML,
JSON, and HTML styles) or locale-based strings for display in text
style::

   xo_emit("All strings are utf-8 content {:tag/%ls}",
           L"except for wide strings");

  ======== ================== ===============================
   Format   Argument Type      Argument Contents
  ======== ================== ===============================
   %s       const char \*      UTF-8 string
   %S       const char \*      UTF-8 string (alias for '%ls')
   %ls      const wchar_t \*   Wide character UNICODE string
   %hs      const char *       locale-based string
  ======== ================== ===============================

.. admonition:: "Long", not "locale"

  The "*l*" in "%ls" is for "*long*", following the convention of "%ld".
  It is not "*locale*", a common mis-mnemonic.  "%S" is equivalent to
  "%ls".

For example, the following function is passed a locale-base name, a
hat size, and a time value.  The hat size is formatted in a UTF-8
(ASCII) string, and the time value is formatted into a wchar_t
string::

    void print_order (const char *name, int size,
                      struct tm *timep) {
        char buf[32];
        const char *size_val = "unknown";

	if (size > 0)
            snprintf(buf, sizeof(buf), "%d", size);
            size_val = buf;
        }

        wchar_t when[32];
        wcsftime(when, sizeof(when), L"%d%b%y", timep);

        xo_emit("The hat for {:name/%hs} is {:size/%s}.\\n",
                name, size_val);
        xo_emit("It was ordered on {:order-time/%ls}.\\n",
                when);
    }

It is important to note that xo_emit will perform the conversion
required to make appropriate output.  Text style output uses the
current locale (as described above), while XML, JSON, and HTML use
UTF-8.

UTF-8 and locale-encoded strings can use multiple bytes to encode one
column of data.  The traditional "precision'" (aka "max-width") value
for "%s" printf formatting becomes overloaded since it specifies both
the number of bytes that can be safely referenced and the maximum
number of columns to emit.  xo_emit uses the precision as the former,
and adds a third value for specifying the maximum number of columns.

In this example, the name field is printed with a minimum of 3 columns
and a maximum of 6.  Up to ten bytes of data at the location given by
'name' are in used in filling those columns::

    xo_emit("{:name/%3.10.6s}", name);

Characters Outside of Field Definitions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Characters in the format string that are not part of a field
definition are copied to the output for the TEXT style, and are
ignored for the JSON and XML styles.  For HTML, these characters are
placed in a <div> with class "text"::

  EXAMPLE:
      xo_emit("The hat is {:size/%s}.\\n", size_val);
  TEXT:
      The hat is extra small.
  XML:
      <size>extra small</size>
  JSON:
      "size": "extra small"
  HTML:
      <div class="text">The hat is </div>
      <div class="data" data-tag="size">extra small</div>
      <div class="text">.</div>

.. index:: errno

"%m" Is Supported
~~~~~~~~~~~~~~~~~

libxo supports the '%m' directive, which formats the error message
associated with the current value of "errno".  It is the equivalent
of "%s" with the argument strerror(errno)::

    xo_emit("{:filename} cannot be opened: {:error/%m}", filename);
    xo_emit("{:filename} cannot be opened: {:error/%s}",
            filename, strerror(errno));

"%n" Is Not Supported
~~~~~~~~~~~~~~~~~~~~~

libxo does not support the '%n' directive.  It's a bad idea and we
just don't do it.

The Encoding Format (eformat)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The "eformat" string is the format string used when encoding the field
for JSON and XML.  If not provided, it defaults to the primary format
with any minimum width removed.  If the primary is not given, both
default to "%s".

Content Strings
~~~~~~~~~~~~~~~

For padding and labels, the content string is considered the content,
unless a format is given.

.. index:: printf-like

Argument Validation
~~~~~~~~~~~~~~~~~~~

Many compilers and tool chains support validation of printf-like
arguments.  When the format string fails to match the argument list,
a warning is generated.  This is a valuable feature and while the
formatting strings for libxo differ considerably from printf, many of
these checks can still provide build-time protection against bugs.

libxo provide variants of functions that provide this ability, if the
"--enable-printflike" option is passed to the "configure" script.
These functions use the "_p" suffix, like "xo_emit_p()",
xo_emit_hp()", etc.

The following are features of libxo formatting strings that are
incompatible with printf-like testing:

- implicit formats, where "{:tag}" has an implicit "%s";
- the "max" parameter for strings, where "{:tag/%4.10.6s}" means up to
  ten bytes of data can be inspected to fill a minimum of 4 columns and
  a maximum of 6;
- percent signs in strings, where "{:filled}%" makes a single,
  trailing percent sign;
- the "l" and "h" modifiers for strings, where "{:tag/%hs}" means
  locale-based string and "{:tag/%ls}" means a wide character string;
- distinct encoding formats, where "{:tag/#%s/%s}" means the display
  styles (text and HTML) will use "#%s" where other styles use "%s";

If none of these features are in use by your code, then using the "_p"
variants might be wise:

  ================== ========================
   Function           printf-like Equivalent
  ================== ========================
   xo_emit_hv         xo_emit_hvp
   xo_emit_h          xo_emit_hp
   xo_emit            xo_emit_p
   xo_emit_warn_hcv   xo_emit_warn_hcvp
   xo_emit_warn_hc    xo_emit_warn_hcp
   xo_emit_warn_c     xo_emit_warn_cp
   xo_emit_warn       xo_emit_warn_p
   xo_emit_warnx      xo_emit_warnx_p
   xo_emit_err        xo_emit_err_p
   xo_emit_errx       xo_emit_errx_p
   xo_emit_errc       xo_emit_errc_p
  ================== ========================

Example
~~~~~~~

In this example, the value for the number of items in stock is emitted::

        xo_emit("{P:   }{Lwc:In stock}{:in-stock/%u}\\n",
                instock);

This call will generate the following output::

  TEXT:
       In stock: 144
  XML:
      <in-stock>144</in-stock>
  JSON:
      "in-stock": 144,
  HTML:
      <div class="line">
        <div class="padding">   </div>
        <div class="label">In stock</div>
        <div class="decoration">:</div>
        <div class="padding"> </div>
        <div class="data" data-tag="in-stock">144</div>
      </div>

Clearly HTML wins the verbosity award, and this output does
not include XOF_XPATH or XOF_INFO data, which would expand the
penultimate line to::

       <div class="data" data-tag="in-stock"
          data-xpath="/top/data/item/in-stock"
          data-type="number"
          data-help="Number of items in stock">144</div>
