# Errors, warnings, and other stderr output

In TEXT style (no --libxo), the xo_ forms below print exactly what the
originals print, to stderr, with the same "progname: " prefix, the
same strerror text, and the same exit status (checked).  In XML and
JSON styles they produce structured error or warning output instead.

## The err/warn family

    err(eval, fmt, ...)            xo_err(eval, fmt, ...)
    errx(eval, fmt, ...)           xo_errx(eval, fmt, ...)
    errc(eval, code, fmt, ...)     xo_errc(eval, code, fmt, ...)
    warn(fmt, ...)                 xo_warn(fmt, ...)
    warnx(fmt, ...)                xo_warnx(fmt, ...)
    warnc(code, fmt, ...)          xo_warn_c(code, fmt, ...)

Note the underscore in xo_warn_c; there is no xo_warnc.

The xo_err family calls xo_finish() before exiting, so open containers
are closed and the encoded output is complete.

The va_list forms have no one-to-one equivalents:

    vwarn(fmt, ap)                 xo_warn_hcv(NULL, errno, 0, fmt, ap)
    vwarnx(fmt, ap)                xo_warn_hcv(NULL, -1, 0, fmt, ap)
    vwarnc(code, fmt, ap)          xo_warn_hcv(NULL, code, 0, fmt, ap)
    verr(eval, fmt, ap)            xo_warn_hcv(NULL, errno, 0, fmt, ap);
                                   xo_finish(); exit(eval);
    verrx(eval, fmt, ap)           xo_warn_hcv(NULL, -1, 0, fmt, ap);
                                   xo_finish(); exit(eval);

Save errno in a local before any call that might change it.

## Structured warnings

xo_emit_warn(), xo_emit_warnx(), xo_emit_err(), xo_emit_errx(), and
the _c forms take an xo_emit format, so the parts of the message can
be fields:

    xo_emit_warn("{:filename/%s}", path);

Use them when the warning carries data a consumer would want (the
file that failed).  The text form is unchanged; check it.

## fprintf(stderr, ...)

Decide what the message is:

- An error or warning message with the program name prefixed by hand
  ("ls: foo: No such file"): it is a warn/warnx in disguise.  Convert
  to xo_warn/xo_warnx only if the text is identical, including the
  prefix; otherwise use xo_error().  xo_warn's prefix is the basename
  of argv[0], recorded by xo_parse_args() (or set by
  xo_set_program()), so xo_parse_args() must run first.  err(3) uses
  getprogname(), which is the same on BSD and glibc systems.
- A usage message: xo_error("usage: ...\n"), then xo_finish() if
  anything was opened, then the original exit.
- Any other message: xo_error(fmt, ...), which in TEXT style is
  vfprintf(stderr, fmt, ...), byte for byte.  xo_error does not add a
  newline; keep the original's.
- Debug or verbose output: leave it as fprintf(stderr, ...).

In XML and JSON styles, xo_error output goes into the encoded output
on stdout ({"error": {"message": ...}}), not to stderr.  That is the
intended behavior; the prime directive is about TEXT style.

## Not to be used for stderr

xo_message() writes to stdout (the handle) in TEXT style, not to
stderr.  Never use it to replace a write to stderr.

## Exit codes

Never change them.  "exit(1)" stays "exit(1)", even where
EXIT_FAILURE would be nicer.  Before each normal exit from main with
containers open, close them and call xo_finish().

## syslog

Convert syslog() to xo_syslog() only when the user asks; it changes
what is logged.

## Error paths in the middle of a list

A warning inside a loop (ls: cannot stat one file) is fine: xo_warn
does not disturb the open instances.  An xo_err inside a loop exits
with xo_finish(), which closes everything.  A plain "exit(1)" inside a
loop leaves the encoded output truncated: convert it, or close by
hand first, and list it in the phase-2 notes.
