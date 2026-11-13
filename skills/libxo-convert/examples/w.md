# Example: w and uptime (excerpts)

FreeBSD's w.c is also uptime(1).  The 2014 conversion is in libxo's
patches/w.patch.  These excerpts show the parts that teach something:
values printed as phrases, attributes, star widths and precisions, and
an early exit.

Note: FreeBSD has shipped a libxo w since 2015.  Converting that
program again would keep its shipped names (the prime directive);
this example converts the pre-libxo source.

## Phase 2: schema

    +-- uptime-information
       +-- time-of-day         %s     strftime %l:%M%p or %k:%M
       +-- uptime              %ld    seconds; display is "3 days, 4:05"
       +-- users               %d     nusers
       +-- load-average-1?     %.2f   absent if getloadavg fails
       +-- load-average-5?     %.2f
       +-- load-average-15?    %.2f
       +-- user* [user tty]?          w only, not uptime
          +-- user             %s     ut_user
          +-- tty              %s     ut_line minus "tty"/"cua"
          +-- from             %s     host as displayed
          +-- address?         %s     raw ut_host, when it differs
          +-- login-time       %ld    epoch seconds; display "9:41AM"
          +-- idle-time        %ld    seconds; display "2days", "1:05"
          +-- command          %s     ep->args, untruncated
          +-- process* [pid]?         only with -d
             +-- pid           %d
             +-- command       %s

Notes for the review:

- uptime, login-time and idle-time are shown as phrases ("3 days,",
  "2days", "-").  The phrases are display only; the data is seconds.
- "user [user tty]": a user can be logged in more than once, so the
  key is the pair.  Ask if the user prefers no key.
- The 2014 patch wrapped lists in containers ("user-table" holding
  "user-entry", "process-table" holding "process-entry").  A list is
  already the collection; the extra container and the "-entry" suffix
  add nothing (hierarchy.md).
- command is truncated to the terminal width in the text; the
  encoding carries the whole string.

## Uptime as a phrase

Before:

    (void)printf(" up");
    if (days > 0)
        (void)printf(" %d day%s,", days, days > 1 ? "s" : "");
    if (hrs > 0 && mins > 0)
        (void)printf(" %2d:%02d,", hrs, mins);
    else if (hrs > 0)
        (void)printf(" %d hr%s,", hrs, hrs > 1 ? "s" : "");
    ...

2014 patch:

    xo_emit(" up");
    xo_attr("seconds", "%lu", (unsigned long) tp.tv_sec);
    if (days > 0)
        xo_emit(" {:uptime/%d day%s},", days, days > 1 ? "s" : "");
    if (hrs > 0 && mins > 0)
        xo_emit(" {:uptime/%2d:%02d},", hrs, mins);

Three problems: "uptime" is emitted twice in one container (with days
and hours), which JSON consumers cannot handle; its value is a phrase
("3 day", " 4:05") that a program would have to parse; and the real
value is in an attribute, which JSON does not have.

After:

    xo_emit(" up{e:uptime/%ld}", (long) tp.tv_sec);
    if (days > 0)
        xo_emit(" {d:/%d} {d:/day%s},", days, days > 1 ? "s" : "");
    if (hrs > 0 && mins > 0)
        xo_emit(" {d:/%2d:%02d},", hrs, mins);
    else if (hrs > 0)
        xo_emit(" {d:/%d} {d:/hr%s},", hrs, hrs > 1 ? "s" : "");
    ...

    xo_emit(" {:users/%d} {N:/user%s}", nusers, nusers == 1 ? "" : "s");

The text is unchanged (" up 3 days,  4:05, 1 user").  pr_idle() and
pr_attime() follow the same pattern: one {e:idle-time} or
{e:login-time} with the seconds, and {d:} for each piece of the
phrase.

## Formats in an array

The 2014 patch:

    static const char *format[] = {
        " {:load-average-1/%.2f}",
        " {:load-average-5/%.2f}",
        " {:load-average-15/%.2f}",
    };
    ...
    xo_emit(format[i], avenrun[i]);

xolint cannot check these, and the lexicon cannot see the names.
Unroll the loop with literal formats:

    xo_emit(", load averages:");
    xo_emit(" {:load-average-1/%.2f}", avenrun[0]);
    if (use_comma)
        xo_emit(",");
    xo_emit(" {:load-average-5/%.2f}", avenrun[1]);
    if (use_comma)
        xo_emit(",");
    xo_emit(" {:load-average-15/%.2f}\n", avenrun[2]);

This keeps the text only because the original loop always runs three
times ("sizeof(avenrun) / sizeof(avenrun[0])"); check that kind of
claim against the source.

## Star widths and precisions

Before:

    (void)printf("%-*.*s %-*.*s %-*.*s ",
        W_DISPUSERSIZE, W_DISPUSERSIZE, ep->utmp.ut_user,
        W_DISPLINESIZE, W_DISPLINESIZE, line,
        W_DISPHOSTSIZE, W_DISPHOSTSIZE, *p ? p : "-");
    ...
    (void)printf("%.*s\n", argwidth - longidle - longattime, ep->args);

After (the 2014 patch got this right):

    xo_emit("{k:user/%-*.*s/%@**@s} {k:tty/%-*.*s/%@**@s} ",
        W_DISPUSERSIZE, W_DISPUSERSIZE, ep->utmp.ut_user,
        W_DISPLINESIZE, W_DISPLINESIZE, line);
    if (save_p != p)
        xo_emit("{e:address/%s}", save_p);
    xo_emit("{:from/%-*.*s/%@**@s} ",
        W_DISPHOSTSIZE, W_DISPHOSTSIZE, *p ? p : "-");
    ...
    xo_emit("{:command/%.*s/%@*@s}\n",
        argwidth - longidle - longattime, ep->args);

"%@**@s" consumes the width and precision arguments without using
them, so the encoded user is not padded or truncated.  For command,
"%@*@s" drops the precision: the text is truncated to the terminal,
the encoding is not.  The 2014 patch used xo_attr("address") for the
raw host; it is now an {e:} field, visible in JSON.

With -d, w prints each user's processes before the user's line, so
the nested process list would come before the keys.  Keys must be the
first fields in an instance.  Emit them at the top of the instance as
encode-only fields, and make the display forms display-only:

    xo_open_instance("user");
    xo_emit("{ek:user/%s}{ek:tty/%s}", ep->utmp.ut_user, line);
    if (dflag) {
        ... process list ...
    }
    xo_emit("{d:/%-*.*s} {d:/%-*.*s} ", ...same arguments as before...);

## Early exit for uptime

Before:

    if (header || wcmd == 0) {
        pr_header(&now, nusers);
        if (wcmd == 0) {
            (void)kvm_close(kd);
            exit(0);
        }
    }

The 2014 patch closed the container here but did not call
xo_finish(), so XML and JSON output could be cut short.  After:

        if (wcmd == 0) {
            xo_close_container("uptime-information");
            xo_finish();
            (void)kvm_close(kd);
            exit(0);
        }

## xo_parse_args

The 2014 patch called xo_parse_args() with no check.  Use the
standard form (SKILL.md, phase 3), before the "Are we w or uptime?"
test, which reads argv[0].
