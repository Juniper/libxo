# Containers, lists, instances, and keys

The schema is designed in phase 2, from what the output means, not
from how the code happens to print it.  Converting printf by printf
and letting the structure fall out of the call graph gives a bad
schema.

## Calls

    xo_open_container("name");   ...   xo_close_container("name");
    xo_open_list("name");        ...   xo_close_list("name");
    xo_open_instance("name");    ...   xo_close_instance("name");

The list and its instances use the same name.  There are _h variants
taking a handle, and _d variants ("close whatever is open") which
should not be used in new code: always close by name, so the code says
what it closes.

## The top container

One per program, named "<thing>-information" for the information the
program reports, never for the program:

    df       storage-system-information
    ps       process-information
    w        uptime-information
    wc       wc is an exception in the old patch ("wc"); a new
             conversion would use something like "count-information"

Open it after xo_parse_args() and option handling, before the first
output.  Close it before xo_finish() on every normal exit.

## Lists and instances

A loop over things of the same kind is a list, and each iteration an
instance:

    xo_open_list("filesystem");
    for (i = 0; i < n; i++) {
        xo_open_instance("filesystem");
        xo_emit("{k:name/%-*s} ...", ...);
        xo_close_instance("filesystem");
    }
    xo_close_list("filesystem");

- The list name is a singular noun: "filesystem", not "filesystems";
  "process", not "process-list".
- The list opens before the loop and closes after it, even if the loop
  runs zero times.
- The instance opens and closes inside the body, on every path:
  "continue" needs a close first, or restructure so it doesn't.
- A list whose instances come from more than one loop (ls: files
  given on the command line, then directory contents) is still one
  list if the instances are the same kind of thing.

## Keys

Mark with {k:} the fields that identify an instance: pid, interface
name, path, mount point.  Emit them first in the instance.  Keys make
XPath filtering ("--libxo filter=...") cheap and correct, so choose
them even when the output has no obvious "ID" column.  A list may have
no natural key (lines of a log); then it has none.

## Leaf-lists

Repeated scalar values of one kind (argv words, flag names, group
names) are a leaf-list: {l:name} on each value.  Do not make a list of
instances with one field each.

## Containers

A section of output with a heading, or a group of related fields that
appear together, is a container:

    +-- interface-statistics
       +-- input
       |  +-- packets
       |  +-- errors
       +-- output
          +-- packets
          +-- errors

Use containers to group, not to mirror code.  A container per printf,
or per helper function, is wrong.

## Lists that span functions

ls opens the list in the caller and the printer opens each instance.
That is fine; write down in the phase-2 notes which function opens
and which closes, and check both paths.

## Nested and conditional lists

- Nested: w -d (processes under each user), ls -R (entries under each
  directory).  The inner list opens and closes inside the outer
  instance.
- Conditional: a list that only appears if some item exists (netstat
  sections).  Open it when the first item is seen, and close it only
  if it was opened; keep a flag, rather than opening an empty list
  that confuses readers of the text-oriented code.

## Text that interleaves instances

When the text prints something that belongs elsewhere (a header after
the rows, a grand total, a summary line between groups), do not bend
the schema to the print order.  Emit the item into a sibling
container (or the parent), which libxo allows at any point:

    +-- storage-system-information
       +-- filesystem* [name]
       +-- total?          only with -c

## Early exits

Every return or exit between an open and its close must close what is
open, or the encoded output is truncated or malformed.  List them in
phase 2.  xo_err(), xo_errx() and xo_errc() call xo_finish()
themselves; plain exit() does not.  xo_finish_atexit() (registered
once, early in main) handles the remaining exits, but closing
explicitly is still clearer.

## Anti-patterns

- A container per printf, or per helper function.
- A list with exactly one instance, used as a container.
- A container whose name repeats its parent ("process-information"
  containing "process-info").
- A name that changes with a flag (the same field called "blocks"
  under -k and "kilobytes" otherwise).  The name stays; the value or
  the units change.
- Structure decided by output order rather than meaning.
- Relying on the "dtrt" flag to absorb unbalanced opens and closes.
