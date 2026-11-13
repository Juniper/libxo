# Example: ls (excerpts)

From FreeBSD's 2014 ls; the 2014 conversion is libxo's
patches/ls.patch.  The lessons: lists that open in one function and
fill in another, a conditional outer list, keys that are displayed
late, column counting, and a text change the 2014 patch made.

If the ls being converted already ships libxo output, it keeps its
names.  This example converts the pre-libxo source.

## Phase 2: schema (long format)

    +-- file-information
       +-- directory*                    when ls lists directories
          +-- path?             %s       fts_path; when its name prints
          +-- total-blocks?     %lu      "total N"; -l or -s
          +-- entry* [name]
             +-- name           %s       fts_name
             +-- inode?         %ju      -i
             +-- blocks?        %jd      -s
             +-- mode           %s       strmode() text, "drwxr-xr-x"
             +-- links          %u
             +-- user           %s
             +-- group          %s
             +-- size           %jd      bytes
             +-- modify-time?   %ld      default; -u/-U/-c pick another
             +-- access-time?   %ld      -u
             +-- birth-time?    %ld      -U
             +-- change-time?   %ld      -c
             +-- type?          %s       -F/-p: "directory", "link", ...
             +-- link-target?   %s       symlinks

Notes for the review:

- "directory" has no key: path is not printed (or emitted) when ls
  lists a single directory.  Ask whether to emit it encode-only
  always, so it can be a key.
- -u, -U and -c change which time is shown.  They are different times,
  so they have different names (like wc -c and -m), and the name tells
  the reader which one it is.
- The 2014 patch called the "total N" line "total-size" in one
  function and "total-blocks" in another.  It is a block count:
  "total-blocks".  "-size" means bytes (naming.md).
- The 2014 patch put the numeric mode in xo_attr("value"), which is
  lost in JSON; make it an {e:} field and name it in phase 2.

## Column counting

ls lays out -C output by counting what it printed:

    chcnt += printf("%*ju ", (int)inodefield, (uintmax_t)sp->st_ino);

The 2014 patch kept the counting:

    xo_set_flags(NULL, XOF_COLUMNS);    /* once, after xo_parse_args */
    ...
    chcnt += xo_emit("{:inode/%*ju/%@*@ju} ", (int)inodefield,
        (uintmax_t)sp->st_ino);

Without XOF_COLUMNS, xo_emit returned 0 here (checked), so the layout
breaks.  With it, xo_emit returns display columns, where printf
returns bytes: the same for ASCII, different for multibyte names.  If
the original counted bytes, a multibyte name changes the layout; put
that in a warning block, or count the bytes separately.

## Directory list, opened only when needed

    if (first) {
        first = 0;
        xo_open_list("directory");
    }
    xo_open_instance("directory");
    ...
    display(p, chp, options);       /* opens and closes the entry list */
    xo_close_instance("directory");
    ...
    if (!first)
        xo_close_list("directory");

This is the conditional-list pattern from hierarchy.md.  display()
opens and closes the "entry" list inside the directory instance; write
that down in the phase-2 notes.

## A key displayed late

The name is printed near the end of the line, after mode, size and
time, but a key must be the first field of the instance.  Emit it
encode-only first, and display-only where it prints:

    xo_open_instance("entry");
    xo_emit("{ke:name/%s}", name);
    ...
    xo_emit("{d:/%s}", name);

## A text change in the 2014 patch

Directory headings:

    (void)printname(p->fts_path);
    puts(":");

became

    (void)printname("path", p->fts_path);
    xo_emit(":");

puts() adds a newline; xo_emit(":") does not.  It must be
xo_emit(":\n").

The -F suffix ("{D:\\/}{e:type/directory}", "{D:/%c}") was right:
the suffix is decoration and the type is data (pitfalls.md).

## Formats built at run time

printname() and printsize() in the 2014 patch took the field name and
built the format with snprintf (pitfalls.md).  Pass a flag or use a
switch with literal formats instead:

    switch (which) {
    case T_ACCESS:
        xo_emit("{d:/%s}{e:access-time/%ld} ", buf, (long) when);
        break;
    ...
    }
