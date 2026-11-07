
.. index:: Field Roles
.. _field-roles:

Field Roles
~~~~~~~~~~~

Field roles are optional, and indicate the role and formatting of the
content.  The roles are listed below; only one role is permitted:

  === ============== =================================================
  R   Name           Description
  === ============== =================================================
  B   boolean        Field is a boolean value (true or false)
  C   color          Field has color and effect controls
  D   decoration     Field is non-text (e.g., colon, comma)
  E   error          Field is an error message
  F   format         Field for formatting text content
  G   (none)         Call gettext(3) on the format string
  L   label          Field is text that prefixes a value
  N   note           Field is text that follows a value
  P   padding        Field is spaces needed for vertical alignment
  T   title          Field is a title value for headings
  U   units          Field is the units for the previous value field
  V   value          Field is the name of field (the default)
  W   warning        Field is a warning message
  [   start-anchor   Begin a section of anchored variable-width text
  ]   stop-anchor    End a section of anchored variable-width text
  === ============== =================================================

::

    EXAMPLE:
        xo_emit("{L:Free}{D::}{P:   }{:free/%u} {U:Blocks}\\n",
                free_blocks);

When a role is not provided, the "*value*" role is used as the default.

Roles and modifiers can also use more verbose names, when preceded by
a comma::

    EXAMPLE:
        xo_emit("{,label:Free}{,decoration::}{,padding:   }"
                "{,value:free/%u} {,units:Blocks}\\n",
                free_blocks);

.. index:: Field Roles; Boolean
.. _boolean-role:

The Boolean Role ({B:})
+++++++++++++++++++++++

The boolean role emits a value that is either true or false.  The
argument is tested for "truth", and the field is rendered as one
of two words in the display styles (TEXT and HTML) and as a native
boolean in the encoding styles (XML, JSON, and encoders)::

    xo_emit("{B:enabled}\\n", str);                  /* true or false */
    xo_emit("{Bf:enabled}\\n", str);                 /* True or False */
    xo_emit("{B:enabled/%d}\\n", i32);               /* true or false */
    xo_emit("{B:enabled/%p}\\n", ptr);               /* true or false */
    xo_emit("{B:power/on+off}\\n", str);             /* on or off */
    xo_emit("{Bf:power/on+off%u}\\n", u32);          /* On or Off */
    xo_emit("{Bf:power/on+off%!64u}\\n", u64);       /* On or Off */
    xo_emit("{B:state/enabled+disabled%d}\\n", i32); /* enabled or disabled */
    xo_emit("{Bf:power/*%u}\\n", "on+off", u32);     /* On or Off */

The format has two parts, both optional, in this order:

- A *boolean pair*, giving the *true word*, a plus sign, and the
  *false word* ("on+off").  If the pair is a lone "*", it is taken
  from a "const char \*" argument, which comes before any other
  argument of the field.  The default pair is "true+false".

- A single conversion spec, which says how the argument is taken.
  The default is "%s".

The boolean pair is everything before the first "%".  It must have
exactly one "+".  A backslash escapes the next character, so "\\+",
"\\\*", "\\/", "\\}", and "\\\\" give a literal plus, star, slash,
close brace, and backslash, and "%%" gives a literal percent.
Remember that a backslash in a C string must itself be escaped::

    xo_emit("{B:mode/a\\\\+b+c%d}\\n", i32);         /* a+b or c */
    xo_emit("{B:rate/100%%+0%%%d}\\n", i32);         /* 100% or 0% */

There can be only one conversion spec, with nothing after it, and
it must take an argument (so "%m" is not allowed).  Flags, width,
precision, and length modifiers are parsed normally, and any
arguments they take (such as "%\*.\*s") are consumed, but the width
and precision are not applied to the word.  Use an anchor to line
up columns::

    xo_emit("{[:-8}{B:power/on+off%d}{]:} {:name}\\n", on, name);

The field name is required, as it is for the value role.

Truth
-----

The conversion spec decides how the argument is tested:

  ================================= ===================================
  Spec                              True when
  ================================= ===================================
  integers ("%d", "%u", "%c", ...)  The value is not zero
  floating point ("%f", "%g", ...)  The value is not zero (NaN is true)
  wide characters ("%lc")           The value is not zero
  strings ("%s", "%ls", ...)        The string is not NULL or empty
  pointers ("%p")                   The pointer is not NULL
  ================================= ===================================

A string is true whatever it says, so "0" and "false" are true.  A
"bool" (from <stdbool.h>) is promoted to an int, so use "%d" for it;
passing a bool to "{B:name}" (which defaults to "%s") is undefined
behavior.

Output Styles
-------------

In TEXT, the chosen word is emitted.  In HTML, the word is emitted
with "data-type" and "data-value" attributes; "data-value" is always
"true" or "false", whatever word is displayed::

    <div class="data" data-tag="power" data-type="boolean"
         data-value="true">On</div>

The encoding styles use the native form: "true" and "false" are
bare words in JSON, and text in XML.  Encoders are passed the value
"true" or "false" with the XFF_BOOLEAN flag set (see
:ref:`encoders`)::

    "power": true
    <power>true</power>

The "q" (quote) modifier makes JSON quote the value ("true").  The
"e", "d", "k", and "l" modifiers work as they do for the value role.
The "h" (humanize) and "i" (int-group) modifiers are ignored.

The "f" (first-cap) modifier capitalizes the first letter of the
chosen word.  The "g" (gettext) modifier translates the boolean pair
as a whole, so the translator sees "enabled+disabled" (or
"true+false" when there's no pair) and returns a pair, such as
"activada+desactivada".  The translation is done before "f" is
applied, so "enabled" can become "activada" and then "Activada".

The translated pair uses the same syntax as the format: exactly one
"+", with a backslash escaping the next character.  A translation
that isn't a valid pair is a runtime failure, and the untranslated
pair is used instead.  A "*" pair is translated as given, just as a
"{g:}" field's value is, so those msgids must be added to the
catalog by hand.  Encoding words are never translated.  See
:ref:`xopo` for how the pairs are extracted for translators.

The full-boolean Modifier
-------------------------

The "full-boolean" modifier, which has only the long name, makes the
encoding styles emit a word instead of the native form.  The word is
taken from the encoding format's boolean pair, if there is an
encoding format, or else from the display format's pair, without
gettext or first-cap::

    xo_emit("{B,full-boolean:answer/yup+nope}\\n", str);
    xo_emit("{B,full-boolean:found/T+F%p/1+0%p}\\n", ptr);

The first gives "yup" or "nope" in every style.  The second
displays "T" or "F", and encodes "1" or "0".  In JSON, the words
are quoted unless the "n" (no-quotes) modifier is used.  The
"data-value" attribute in HTML is still "true" or "false".

An encoding format must have both a boolean pair and a conversion
spec, and must take the same arguments as the display format: the
same "\*" pair (or none), the same "\*" widths, and the same
argument type.  Without "full-boolean", the encoding format is
checked but not used.  With "full-boolean" and no boolean pair, the
words are "true" and "false", emitted as strings.

.. index:: Field Roles; Color
.. _color-role:

The Color Role ({C:})
+++++++++++++++++++++

Colors and effects control how text values are displayed; they are
used for display styles (TEXT and HTML)::

    xo_emit("{C:bold}{:value}{C:no-bold}\\n", value);

Colors and effects remain in effect until modified by other "C"-role
fields::

    xo_emit("{C:bold}{C:inverse}both{C:no-bold}only inverse\\n");

If the content is empty, the "*reset*" action is performed::

    xo_emit("{C:both,underline}{:value}{C:}\\n", value);

The content should be a comma-separated list of zero or more colors or
display effects::

    xo_emit("{C:bold,inverse}Ugly{C:no-bold,no-inverse}\\n");

The color content can be either static, when placed directly within
the field descriptor, or a printf-style format descriptor can be used,
if preceded by a slash ("/"):

   xo_emit("{C:/%s%s}{:value}{C:}", need_bold ? "bold" : "",
           need_underline ? "underline" : "", value);

Color names are prefixed with either "fg-" or "bg-" to change the
foreground and background colors, respectively::

    xo_emit("{C:/fg-%s,bg-%s}{Lwc:Cost}{:cost/%u}{C:reset}\\n",
            fg_color, bg_color, cost);

The following table lists the supported effects:

  =============== =================================================
   Name           Description
  =============== =================================================
   bg-XXXXX       Change background color
   bold           Start bold text effect
   fg-XXXXX       Change foreground color
   inverse        Start inverse (aka reverse) text effect
   no-bold        Stop bold text effect
   no-inverse     Stop inverse (aka reverse) text effect
   no-underline   Stop underline text effect
   normal         Reset effects (only)
   reset          Reset colors and effects (restore defaults)
   underline      Start underline text effect
  =============== =================================================

The following color names are supported:

  ========= ============================================
   Name      Description
  ========= ============================================
   black
   blue
   cyan
   default   Default color for foreground or background
   green
   magenta
   red
   white
   yellow
  ========= ============================================

When using colors, the developer should remember that users will
change the foreground and background colors of terminal session
according to their own tastes, so assuming that "blue" looks nice is
never safe, and is a constant annoyance to your dear author.  In
addition, a significant percentage of users (1 in 12) will be color
blind.  Depending on color to convey critical information is not a
good idea.  Color should enhance output, but should not be used as the
sole means of encoding information.

.. index:: Field Roles; Decoration
.. _decoration-role:

The Decoration Role ({D:})
++++++++++++++++++++++++++

Decorations are typically punctuation marks such as colons,
semi-colons, and commas used to decorate the text and make it simpler
for human readers.  By marking these distinctly, HTML usage scenarios
can use CSS to direct their display parameters::

    xo_emit("{D:((}{:name}{D:))}\\n", name);

.. index:: Field Roles; format-role
.. _format-role:

The Format Role ({F:})
+++++++++++++++++++++++

The format role is used to format text data, and should be used for
text strings that are not values that should encoding in XML or JSON.
The format role is often used as an alternative to encode-only
("{e:}") fields.  The use of display-only fields "{d:}" inspired the
creation of the format role as a first-class construct in libxo.

The format field can be used with a printf-style format descriptor, if
preceded by a slash ("/").  It can also be used to static content,
when placed directly within the field descriptor, though this is
rarely used::

    xo_emit("{F:/(incomplete)}{en:incomplete/true}");
    xo_emit("{F:/%s}", nname);
    xo_emit("{F:static-content}");

Format fields have an extra convenience feature; if both content and
format are specified, instead of looking to the argument list for a
value, the content is used, allowing a mixture of format and content
within the field descriptor, though overlaps the `label-role`_::

    xo_emit("{F:name is /%20s}{:count/%d}\\n", count);

Since the incoming argument is a string, the format must be "%s" or
something suitable.

.. index:: Field Roles; Gettext
.. _gettext-role:

The Gettext Role ({G:})
+++++++++++++++++++++++

libxo supports internationalization (i18n) through its use of
gettext(3).  Use the "{G:}" role to request that the remaining part of
the format string, following the "{G:}" field, be handled using
gettext().

Since gettext() uses the string as the key into the message catalog,
libxo uses a simplified version of the format string that removes
unimportant field formatting and modifiers, stopping minor formatting
changes from impacting the expensive translation process.  A developer
change such as changing "/%06d" to "/%08d" should not force hand
inspection of all .po files.

The simplified version can be generated for a single message using the
"`xopo -s $text`" command, or an entire .pot can be translated using
the "`xopo -f $input -o $output`" command.

   xo_emit("{G:}Invalid token\\n");

The {G:} role allows a domain name to be set.  gettext calls will
continue to use that domain name until the current format string
processing is complete, enabling a library function to emit strings
using it's own catalog.  The domain name can be either static as the
content of the field, or a format can be used to get the domain name
from the arguments.

   xo_emit("{G:libc}Service unavailable in restricted mode\\n");

When a format is used, it consumes arguments like any other field,
so "{G:/%s}" takes one argument and "{G:/%s_%s}" takes two.  The
formatted value becomes the domain name and is not emitted as
output.  If the field has content, the format is ignored.

   xo_emit("{G:/%s}Service unavailable\\n", domain);

Each {G:} field discards any previously set domain name.  A {G:}
field with neither content nor a format ("{G:}") resets the domain,
so subsequent gettext calls use the default domain, as set by
textdomain(3).  This form does not consume an argument; there is
no default "%s" format for the {G:} role.

   xo_emit("{G:libc}Permission denied: {G:}{g:reason}\\n", reason);

See :ref:`i18n` for additional details.

.. index:: Field Roles; Label
.. _label-role:

The Label Role ({L:})
+++++++++++++++++++++

Labels are text that appears before a value::

    xo_emit("{Lwc:Cost}{:cost/%u}\\n", cost);

If a label needs to include a slash, it must be escaped using two
backslashes, one for the C compiler and one for libxo::

    xo_emit("{Lc:Low\\/warn level}{:level/%s}\\n", level);

.. index:: Field Roles; Note
.. _note-role:

The Note Role ({N:})
++++++++++++++++++++

Notes are text that appears after a value::

    xo_emit("{:cost/%u} {N:per year}\\n", cost);

.. index:: Field Roles; Padding
.. _padding-role:

The Padding Role ({P:})
+++++++++++++++++++++++

Padding represents whitespace used before and between fields.

The padding content can be either static, when placed directly within
the field descriptor, or a printf-style format descriptor can be used,
if preceded by a slash ("/")::

    xo_emit("{P:        }{Lwc:Cost}{:cost/%u}\\n", cost);
    xo_emit("{P:/%30s}{Lwc:Cost}{:cost/%u}\\n", "", cost);

.. index:: Field Roles; Title
.. _title-role:

The Title Role ({T:})
+++++++++++++++++++++

Title are heading or column headers that are meant to be displayed to
the user.  The title can be either static, when placed directly within
the field descriptor, or a printf-style format descriptor, if preceded
by a slash ("/")::

    xo_emit("{T:Interface Statistics}\\n");
    xo_emit("{T:/%20.20s}{T:/%6.6s}\\n", "Item Name", "Cost");

Title fields have an extra convenience feature; if both content and
format are specified, instead of looking to the argument list for a
value, the content is used, allowing a mixture of format and content
within the field descriptor::

    xo_emit("{T:Name/%20s}{T:Count/%6s}\\n");

Since the incoming argument is a string, the format must be "%s" or
something suitable.

.. index:: Field Roles; Units
.. index:: XOF_UNITS
.. _units-role:

The Units Role ({U:})
+++++++++++++++++++++

Units are the dimension by which values are measured, such as degrees,
miles, bytes, and decibels.  The units field carries this information
for the previous value field::

    xo_emit("{Lwc:Distance}{:distance/%u}{Uw:miles}\\n", miles);

Note that the sense of the 'w' modifier is reversed for units;
a blank is added before the contents, rather than after it.

When the XOF_UNITS flag is set, units are rendered in XML as the
"units" attribute::

    <distance units="miles">50</distance>

Units can also be rendered in HTML as the "data-units" attribute::

    <div class="data" data-tag="distance" data-units="miles"
         data-xpath="/top/data/distance">50</div>

.. index:: Field Roles; Value
.. _value-role:

The Value Role ({V:} and {:})
+++++++++++++++++++++++++++++

The value role is used to represent the a data value that is
interesting for the non-display output styles (XML and JSON).  Value
is the default role; if no other role designation is given, the field
is a value.  The field name must appear within the field descriptor,
followed by one or two format descriptors.  The first format
descriptor is used for display styles (TEXT and HTML), while the
second one is used for encoding styles (XML and JSON).  If no second
format is given, the encoding format defaults to the first format,
with any minimum width removed.  If no first format is given, both
format descriptors default to "%s"::

    xo_emit("{:length/%02u}x{:width/%02u}x{:height/%02u}\\n",
            length, width, height);
    xo_emit("{:author} wrote \"{:poem}\" in {:year/%4d}\\n,
            author, poem, year);

.. index:: Field Roles; Anchor
.. _anchor-role:

The Anchor Roles ({[:} and {]:})
++++++++++++++++++++++++++++++++

The anchor roles allow a set of strings by be padded as a group,
but still be visible to xo_emit as distinct fields.  Either the start
or stop anchor can give a field width and it can be either directly in
the descriptor or passed as an argument.  Any fields between the start
and stop anchor are padded to meet the minimum width given.

To give a width directly, encode it as the content of the anchor tag::

    xo_emit("({[:10}{:min/%d}/{:max/%d}{]:})\\n", min, max);

To pass a width as an argument, use ``%d`` or ``%u`` as the format,
which must appear after the "/".  The width may be on either the start
or stop anchor::

    xo_emit("({[:/%d}{:min/%d}/{:max/%d}{]:})\\n", width, min, max);
    xo_emit("({[:}{:min/%d}/{:max/%d}{]:/%d})\\n", min, max, width);

If the width is negative, padding will be added on the right, suitable
for left justification.  Otherwise the padding will be added to the
left of the fields between the start and stop anchors, suitable for
right justification.  If the width is zero, nothing happens.  If the
number of columns of output between the start and stop anchors is less
than the absolute value of the given width, nothing happens.

.. index:: XOF_WARN

Widths over 8k are considered probable errors and not supported.  If
XOF_WARN is set, a warning will be generated.
