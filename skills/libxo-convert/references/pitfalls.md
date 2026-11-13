# Pitfalls

Mistakes found in the 2014 conversions in libxo's patches/ directory,
and libxo behaviors that surprise people.  Each was checked against
the current library; this skill targets current libxo only.

## Formats built at run time

The old ls and ps conversions built formats with snprintf:

    snprintf(fmt, sizeof(fmt), "{:%s/%%hs}", field);
    xo_emit(fmt, s);

xolint and xocc cannot check a format they cannot see, the field
names are invisible to the lexicon, and a '{' in the data is a
format-string injection.  xolint warns "format is not a literal; it
cannot be checked".

libxo is meant to do everything a built format did, so a conversion
never needs one.  Every built format in the old patches has a
literal answer:

    what was built                     use instead
    the field name (a function         "{a:/%s}": the name comes from
      parameter: ls, vmstat,             an argument
      netstat inetprint)
    the width ("%%%ds": ls,            a '*' width: "%*s", "%*ju"
      netstat show_stat)
    the "'" flag, chosen at run        two literal formats and an if
      time (ls f_thousands)
    the conversion ("lu" or "ju",      widen to uintmax_t and use
      netstat show_stat)                 "%ju"
    separators passed in (netstat      literal text; there are only a
      lsep, rsep)                        few choices
    "sec.usec" built in a buffer,      one field, two conversions:
      then "%s" (netstat bw_meter)       "{n:time/%s%lu.%06lu}", with
                                         "-" or "" for the sign

- If the choices are few, write each format literally and choose
  with an if or switch.
- If the field name really is data, use the argument modifier.  Use
  it sparingly; the lexicon cannot see those names either.

If none of these fits and the code seems to need snprintf to build a
format, libxo is missing a feature.  Stop, don't build the format,
and report it as a feature gap in the phase-7 report, with the code
that needs it.

The netstat bw_meter conversion also has a typo: the negative case
became "-%lu.06%lu" where "-%lu.%06lu" was meant, so -5.000123 prints
"-5.06123".  Check every "%06" moved from printf.

## Formats with no encoding format and a '*' width

    xo_emit("{:inodes-used/%*jd}", 8, (intmax_t) 1234);

This is right as written: the default encoding format drops a '*'
width as it drops a numeric one, so this encodes 1234.  No encoding
format is needed; "%@*@" in one is allowed but is never needed.

The same goes for an integer's precision, which printf uses as a
minimum digit count: "{:count/%.10d}" encodes 42.  An explicit
encoding format is used as written, so never give "%.10d" there; it
would encode 0000000042, which is invalid JSON.

Never write an encoding format with fewer '*'s than the display
format ("%*jd/%jd").  libxo repairs it, and warns with "warn", but it
is a bug; xolint and xocc both warn about it.

## Sign flags

"%+d" and "% d" keep their sign in the default encoding format, so
XML and the encoders get "+5" or " 5" (CBOR encodes a string); only
JSON drops the '+'.  Add the drop-plus modifier when the text has a
sign flag, so every encoding style gets the number:
"{,drop-plus:delta/%+d}".  It doesn't apply to a quoted ({q:}) field,
whose value is a string.

## "{U:%%}"

Field content is literal, so "{U:%%}" prints "%%" and sets
units="%%".  The old df conversion has this bug.  Write "{U:%}".
xolint reports "a percent sign in literal content is a literal, so %%
prints two".

The same applies to {T:}, {L:} and {D:}: "{T:%iused}" prints
"%iused".  The old df conversion wrote "{T:\%iused}", which is an
invalid C escape.  xolint reports "{T:%iused}" as "info: potential
missing slash"; here that is expected, since the '%' is text.

## "%%" outside a field

Text outside fields is copied as is, so printf("%d%%") becomes
"{:x/%d}%", not "{:x/%d}%%".  xolint reports a single "%" as "info:
a percent sign appearing in text is a literal"; there that is
expected.  For "%%" it says "..., so %% prints two"; that one is a
leftover from printf, so fix it.

## Humanized sizes

{h:} honors the field width, so "{h:size/%7ju}" prints "   118M"
(and "%-7ju" pads on the right).  {h:} can replace
humanize_number() plus a padded printf when the flags match:
{h:} puts no space before the suffix (HN_NOSPACE), so a call without
HN_NOSPACE needs "hn-space"; HN_DECIMAL is "hn-decimal",
HN_DIVISOR_1000 is "hn-1000", and the suffixes must be the same.
humanize_number() has more (HN_B, HN_GETSCALE, scale arguments) that
{h:} does not; check the text with xo-check either way.  When they
don't match, keep humanize_number() for the display and encode the
raw value:

    xo_emit(" {d:/%*s}{e:size/%jd}", width, buf, (intmax_t) size);

The old df conversion used xo_attr("value", ...) with a prebuilt
format; the attribute is lost in JSON.  Don't.

## Thousands separators

{i:} and "%'" both use the locale's separator ("1,234,567" in en_US,
"1.234.567" in de_DE), and neither groups in the C locale.

"%'" and {i:} are one feature with two spellings, and behave as
printf's "%'" does.  They group integers (d, i, u) and the integer
part of floats, never hex, octal or zero-padded fields; the width
counts the separators ("%'12d" is 12 columns); and only TEXT and HTML
are grouped, so XML and JSON get the plain number.  Keep the
original's "%'" as it is; no encoding format is needed.

Check with xo-check in the locales the program runs in.

## Suffixes after names

ls -F appends "/", "*", "@", "=" or "%".  The old conversion used:

    xo_emit("{D:\\/}{e:type/directory}");
    xo_emit("{D:%}{e:type/whiteout}");
    xo_emit("{D:/%c}", c);

This is right: the suffix is decoration, and the type is data.  {D:}
takes any punctuation, literal or through a format ("{D:/%c}"), and
gives it its own class in HTML so CSS can style it apart from the
name.  Keep that form.  Letters and digits do not belong in {D:};
xolint warns about them.

Do not use {F:} for this: {F:} is for padding fields, not text.

## Malformed field descriptors

From the old netstat conversion:

    {q:measured-bytes%s}     missing '/': the name is "measured-bytes%s"
    {q:threshold-bytes%s}    same
    {:state:LISTEN}          ':' in a name
    {l:%u}                   no name
    {::type}                 empty modifiers and a ':' in the name

xolint catches all of these; run it after every batch.  The "warn"
option also reports a name that isn't a valid XML
name ("invalid field name: 'measured-bytes%s'"), and xo-check's
lint run sees that on stderr.

## Missing setup

- xo_set_version() after xo_parse_args() when the program has a
  version string; consumers use it to track changes.  None of the old
  patches set it.
- "if (argc < 0) exit(EXIT_FAILURE);" after xo_parse_args().  This
  is required; a program without it has a bug.  The old w conversion
  has no check.  xolint warns about an ignored or unchecked result,
  and the compiler warns about an ignored one.
- ${CARGS} in the Makefile (build-glue.md).

## Early exits

The old w conversion returns from main in places with containers open.
Every exit path needs the closes and xo_finish() (hierarchy.md).

xo_parse_args() registers xo_finish_atexit(), so exit() and a return
from main close what is open and finish every used handle; the XML
and JSON stay well formed.  Still write the closes and xo_finish()
where the original exits normally: the structure is part of the
schema, and _exit(), abort() and signals bypass atexit.

## Naming drift and typos

The old conversions used different names for one concept
("used-percent" in one place, "inodes-used-percent" next to it in
another; "used-blocks" vs "total-blocks") and shipped typos:

    adddress            input-histagram       send-byte-waiting
    ah-statictics       output-histagram      waitings
    esp-statictics      ipcomp-statictics

A program that already ships one of these keeps it (the prime
directive).  A new conversion never copies them.  They are in the
seed discouraged lexicon, with their replacements, so "xo-lexicon
check" reports them.

## puts() adds a newline

The old ls conversion turned puts(":") into xo_emit(":"), dropping
the newline.  puts(s) is xo_emit("...\n").

## Using xo_emit's return value

Code that counts output for layout ("chcnt += printf(...)", as ls -C
does) needs xo_set_flags(NULL, XOF_COLUMNS) once after
xo_parse_args().  With it, xo_emit returns display columns, not
bytes: the same as printf for ASCII, fewer for multibyte text.  In
XML and JSON styles the count is of the encoded value; it only
matters that the TEXT layout is unchanged.

Without XOF_COLUMNS, xo_emit returns the bytes written, as printf
does.  In XML and JSON that
includes the markup, so it is no use for layout.

## Handles and flushing

The old wc conversion sent SIGINFO output to a handle made with
xo_create_to_file(stderr, ...) and never flushed it.  A handle on
stderr, a terminal, or a line-buffered stream flushes at each
newline, as stdio does, and handles left open are finished at exit,
so that is now right.  A handle on a fully buffered file still
buffers; call xo_flush_h() where the original called fflush().

## What the old patches got right

- wc's handle for SIGINFO output to stderr (xo_create_to_file, then
  xo_emit_h); see printf-to-emit.md.
- ls's display suffix plus {e:type}.
