# printf idioms and their xo_emit forms

Each entry shows the original and the conversion.  In every case the
TEXT output must be byte-for-byte identical; check with xo-check after
each batch of changes.  Names here are illustrations; the real names
come from the agreed schema.

## Plain values

    printf("%s\n", name);
    xo_emit("{:name/%s}\n", name);

    printf("%d %s\n", count, name);
    xo_emit("{:count/%d} {:name/%s}\n", count, name);

## Padded columns

A numeric width is removed for encoding automatically.

    printf("%-8s %5d %s\n", user, pid, cmd);
    xo_emit("{:user/%-8s} {:pid/%5d} {:command/%s}\n", user, pid, cmd);

## Width from a variable

A '*' width is removed for encoding too, so no encoding format is
needed (format-strings.md).

    printf("%-*s %*ld ", uw, user, sw, (long) size);
    xo_emit("{:user/%-*s} {:size/%*ld} ", uw, user, sw, (long) size);

## Sign flags

The default encoding keeps "%+d"'s sign, so XML and the encoders get
"+5"; add drop-plus so every style gets the number (pitfalls.md):

    printf("%+d\n", delta);
    xo_emit("{,drop-plus:delta/%+d}\n", delta);

## Headings

    printf("%-8s %5s %s\n", "USER", "PID", "COMMAND");
    xo_emit("{T:USER/%-8s} {T:PID/%5s} {T:COMMAND}\n");

    printf("USER       PID COMMAND\n");
    xo_emit("{T:USER       PID COMMAND}\n");

Titles are not encoded.  A heading whose text depends on a flag
(df -h vs -k) can take the text from an argument: "{T:/%s}".

## "label: value" lines

    printf("Total: %d\n", total);
    xo_emit("{Lwc:Total}{:total/%d}\n", total);

{Lwc:} appends ":" and " ".  If the original spacing is anything other
than exactly ": ", write the text literally instead:
"{L:Total}:  {:total/%d}\n", or "{L:Total:}  ...".

## "total N" lines

    printf("total %ld\n", blocks);
    xo_emit("{L:total} {:total-blocks/%ld}\n", blocks);

The number is data; the word is a label.

## Plurals

    printf("%d user%s\n", n, n == 1 ? "" : "s");
    xo_emit("{:users/%d} {N:user}{N:/%s}\n", n, n == 1 ? "" : "s");

Keep the original expression.  {Np:user,users} is for programs that
use gettext; do not introduce it otherwise.

## Separators and decoration

    printf("%s, %s\n", a, b);
    xo_emit("{:first/%s}{D:,} {:second/%s}\n", a, b);

Mark punctuation that decorates fields with {D:}: separators,
parentheses, the colon after a label ("{L:Free}{D::}").  The text is
the same, and HTML output gives it its own class, so CSS can style it
apart from the data (no italics, a lighter color).  Any punctuation is
allowed, literal or through a format ("{D:/%c}"); letters and digits
belong in {L:}, {N:} or {U:}.  Plain-text punctuation is not wrong,
just unstyled.

## Ternaries choosing between strings

    printf("%s\n", up ? "up" : "down");
    xo_emit("{:state/%s}\n", up ? "up" : "down");

If the value is a true boolean and the words match:

    xo_emit("{B:enabled/up+down%d}\n", up);

## Missing values ("-", "none", "*")

The text shows a placeholder; the encoding should not invent a value.

    if (p->addr)
        printf("%-15s ", p->addr);
    else
        printf("%-15s ", "-");

    if (p->addr)
        xo_emit("{:address/%-15s} ", p->addr);
    else
        xo_emit("{P:/%-15s} ", "-");

Use {P:} or {F:} for the placeholder, so the field is absent from the
encoding.  If absence must be visible, add an {e:} boolean.

## Percentages

    printf("%3d%%", pct);
    xo_emit("{:used-percent/%3d}{U:%}", pct);

or, keeping the percent sign as text:

    xo_emit("{:used-percent/%3d}%", pct);

Note the single '%': text outside fields is copied as is.

## Times

Show the formatted time; encode something a machine can read.

    strftime(buf, sizeof(buf), "%b %e %H:%M", tm);
    printf("%s ", buf);

    xo_emit("{d:/%s}{e:modify-time/%ld} ", buf, (long) when);

Pick one encoded form per program (epoch seconds, or an ISO 8601
string built with strftime("%FT%T%z")) and use it for every time
field.  Older conversions used xo_attr("value", ...) before the field;
an attribute appears only in XML and is lost in JSON, so do not use
it for new fields.  An existing conversion that ships it keeps it.

## Sizes shown humanized

{h:} replaces humanize_number() output padded with printf only when
the flags match (pitfalls.md).  Otherwise keep humanize_number for
display and encode the raw value:

    humanize_number(buf, 5, size, "", HN_AUTOSCALE, HN_NOSPACE);
    printf(" %*s", width, buf);

    humanize_number(buf, 5, size, "", HN_AUTOSCALE, HN_NOSPACE);
    xo_emit(" {d:/%*s}{e:size/%jd}", width, buf, (intmax_t) size);

## putchar, puts, fputs of fixed text

    putchar('\n');            xo_emit("\n");
    puts("done");             xo_emit("done\n");
    fputs(" -> ", stdout);    xo_emit(" -> ");

puts() adds a newline; fputs() does not.

## fputs or printf("%s") of a variable

    fputs(name, stdout);
    xo_emit("{:name/%s}", name);

Never pass a variable as the format: xo_emit(name) is a format-string
injection, and a '{' in the data breaks it.

## Wide characters

    wprintf(L"%ls\n", wname);
    xo_emit("{:name/%ls}\n", wname);

## Character-at-a-time output

Loops that print one character at a time (escaping a name, as ls -b
does) should build the string into a buffer and emit it once:

    xo_emit("{:name/%s}", escaped_buf);

If that would change the text in a corner case (truncation, invalid
multibyte sequences), say so with the warning block.

## Output to a FILE * chosen at run time

When a function writes to "out", which may be stdout or stderr (wc's
SIGINFO handler), use a handle:

    static xo_handle_t *xo_stderr;
    ...
    xo_stderr = xo_create_to_file(stderr, XO_STYLE_TEXT, 0);
    ...
    xop = (out == stderr) ? xo_stderr : NULL;
    xo_emit_h(xop, "{:lines/%7ju/%ju}", ...);
    ...
    if (xop != NULL)
        xo_flush_h(xop);

NULL means the default handle.  A handle on stderr, a terminal, or
a line-buffered stream flushes at each newline, as stdio does, so
the xo_flush_h() is not needed for stderr.  Keep one where the
original called fflush(), for a handle on a fully buffered file.

## Output that must not be converted

- Output to files the program writes as data (not its report).
- curses output.
- Debug output under a -d or -v flag that goes to stderr: leave it as
  fprintf(stderr, ...).  Not xo_message(): that writes to stdout
  (errors.md).
- Output from a child process (system, popen).

List them in the report.
