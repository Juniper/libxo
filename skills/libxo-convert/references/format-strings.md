# xo_emit format strings

Condensed from libxo's doc/format-strings.rst, field-roles.rst,
field-modifiers.rst, and field-formatting.rst.  When in doubt, those
are authoritative; the behaviors marked "checked" below were verified
against the library.

## Field syntax

    '{' [ role | modifier ]* [ ',' long-name ]* ':' [ content ]
        [ '/' field-format [ '/' encoding-format ] ] '}'

    xo_emit("{L:Free}{D::}{P:   }{:free/%u} {U:Blocks}\n", free_blocks);

- Text outside fields is copied to TEXT output and ignored by XML and
  JSON.
- For a value field, content is the name: "{:free/%u}".
- field-format is a printf format for display (TEXT, HTML).
- encoding-format is the printf format for XML, JSON, and encoders.
  It defaults to the field-format with a numeric minimum width removed
  ("%-10s" -> "%s", "%8.1f" -> "%.1f").  Before libxo 2.3.0 a '*'
  width is NOT removed; see "Star widths" below.  Without any format,
  both are "%s".

## Roles (at most one per field)

    V  value        the default; a named value encoded in XML/JSON
    B  boolean      true/false; displayed as a word, encoded as boolean
    T  title        heading text; content is literal, "/fmt" pads it
    L  label        text before a value ("Total" in "Total: 5")
    N  note         text after a value ("users" in "3 users")
    U  units        units of the previous value; an attribute in XML
    D  decoration   punctuation (":" "," "(" ")"); display only
    P  padding      spaces for alignment; display only
    F  format       display-only text that is not a value ("{F:/%s}")
    C  color        color and effects ("{C:bold}"); display only
    E  error        error message text
    W  warning      warning message text
    G  gettext      call gettext(3) on the format (domain in content)
    [  start-anchor begin variable-width text ("{[:%d}" or "{[:/%d}")
    ]  stop-anchor  end it ("{]:}")

Use T, L, N, D, P, U, F for everything printed that is not data.
A value field ({:name}) for anything that is.

## Modifiers

    a  argument     content (the name) is passed as a const char * arg
    c  colon        append ":" after a label
    d  display      only in display styles (TEXT, HTML)
    e  encoding     only in encoding styles (XML, JSON, encoders)
    f  first-cap    capitalize the first character
    g  gettext      call gettext on the rendered value
    h  humanize     display large numbers as 1.2K, 3M, ... (see pitfalls)
    i  int-group    locale thousands separators in display
    k  key          the field is a key of its list instance
    l  leaf-list    the field is one value of a leaf-list
    n  no-quotes    do not quote in JSON
    p  plural       gettext plural form ("{Np:user,users}")
    q  quotes       quote in JSON
    t  trim         trim leading and trailing whitespace
    w  white        append " " after a label

Long names follow a comma: "{L,white,colon:Total}" is "{Lwc:Total}".
Some modifiers exist only as long names: hn-space, hn-decimal,
hn-1000 (humanize options), units-attr, full-boolean, dense,
escape-private, escape-slash, escape-square.

## Escaping (checked)

- Literal braces outside a field: "{{" and "}}".
- A literal "/" or "}" inside a field's content: "\\/" or "\\}" in C
  source (one backslash for the compiler, one for libxo).
- '%' outside fields is NOT a printf directive: it is copied as is.
  "%%" prints two percent signs.  printf("%d%%\n", pct) becomes
  xo_emit("{:capacity/%d}%\n", pct).  This is the most common way to
  break text output.
- Title, label, note, and decoration content is literal text:
  "{T:%iused}" prints "%iused"; "{T:50%%}" prints "50%%".

## Titles and labels with widths (checked)

- "{T:USER/%-8s}" prints "USER    ": the content is the text and the
  format pads it.  No argument is consumed.
- "{T:/%-8s}" takes the text from the argument list.
- "{F:x=/%-5s}" likewise uses the content as the argument.  Always
  write F with a slash: "{F:/%s}".  "{F:%s}" does not do what it says.

## Display versus encoding

- {d:name/...} shows a value in TEXT/HTML only; {e:name/...} encodes a
  value in XML/JSON only.  Use the pair when what is shown and what is
  data differ: a humanized size shown, the byte count encoded; a
  formatted date shown, the epoch encoded.
- Two different names are needed when both forms are encoded.  A
  {d:} field may have no name: "{d:/%s}".
- "{:name/%s/%s}" (an explicit encoding format) is the right choice
  when only the format differs.

## Star widths (checked)

A '*' in a format takes an int argument.  The encoding format must
take the same arguments.  Since libxo 2.3.0 the default encoding
format drops a '*' width, as it drops a numeric one; before that it
kept it:

- "{:user/%-*s}" with (8, "phil") displays "phil    ".  Before 2.3.0
  it also encodes "phil    ", with the padding.
- "{:user/%-*s/%s}" is wrong: the encoding format takes one argument,
  the display format two.  Before 2.3.0 the encoder reads the width
  as a string and crashes; since 2.3.0 libxo repairs it and warns
  (with "warn").  xolint reports "encoding format uses different
  number of arguments"; xocc validate reports it too.
- "{:user/%-*s/%@*@s}" is right in every version: "%@*@" consumes
  and discards one int per '*' between the '@'s, then formats "%s".
  Use "%@**@s" for "%*.*s".

Numbers with star widths: before 2.3.0, "%-*d" with no encoding
format puts the padding in the JSON number ("12   "), which is
invalid JSON, and "{:count/%-*d/%@*@d}" encodes the number as a JSON
string.  Since 2.3.0 both give a number.  Write "%@*@", which is
correct with every version, and check the result with xo-check.

## Widths and UTF-8

"%3.10.6s": minimum 3 columns, at most 10 bytes read, at most 6
columns shown.  The third number exists because a precision in bytes
is not a width in columns for UTF-8 text.  Use it only when the
original code needed it; it does not change ASCII output.

## Other directives

- "%m" is supported (strerror(errno)).
- "%n" is not supported.
- "%hs" is a locale string, "%ls" a wide string; output is converted.
- "%'d" groups thousands; since 2.3.0 it is the same as {i:}.  See
  "Thousands separators" in pitfalls.md for older versions.

## Keys, leaf-lists, booleans

- {k:} marks the instance's key fields; emit keys first in the
  instance, before other fields.
- {l:} emits one value of a leaf-list; repeated calls build the list.
  It needs no xo_open_leaf_list unless the leaf-list spans calls that
  would otherwise be ambiguous.
- {B:} encodes a native boolean and displays "true"/"false", or two
  words given in the format ("{B:enabled/on+off%d}").  Use it only
  when the displayed words match the original text exactly.

## Gettext

{G:} and the g/p modifiers are for programs that translate output.
Do not add translation to a program that does not already translate;
it is out of scope for a conversion.
