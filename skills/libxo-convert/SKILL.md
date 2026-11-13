---
name: libxo-convert
description: Convert a C program's output to libxo so it can emit text, XML, JSON, and HTML from one code path. Use when asked to "convert a program to libxo", "add --libxo support", "replace printf with xo_emit", "add JSON/XML output using libxo", or to extend or fix an existing libxo conversion (xo_emit, xo_open_container, xo_parse_args).
---

# Converting a program to libxo

This skill turns a C program's printf-style output into libxo calls:
xo_parse_args(), xo_emit(), xo_open_*/xo_close_*, xo_err()/xo_warn(),
the #include, and the link flags.  The mechanical parts are easy.  The
hard part is the data model: which containers, lists, keys, and names
the encoded output has.  The workflow below forces that design to be
written down and reviewed before code is edited.

Paths in this file are relative to this skill's directory.  The
scripts are plain shell; run them with "sh scripts/NAME" if they are
not executable.

## The prime directive: first, do no harm

Nothing that already ships may change.

- Text output (the program run without --libxo) must be byte-for-byte
  identical to the original: same characters, same spacing, same
  trailing whitespace, same bugs.  Not "equivalent", not "better
  aligned".  Identical.
- Standard error text and exit status must not change.
- If the program already has libxo output, that encoded output also
  ships: its names, nesting, keys, and value formats stay, even when a
  name is discouraged or the structure is not what you would choose
  today.  You may add fields and containers.  You may not rename,
  move, remove, or retype anything.

If you cannot preserve some output exactly, do not accept the
difference quietly.  Report it in this block, with these exact first
two lines, every time it happens: when you make the change, in the
phase-6 check output, and at the top of the final report.

    ****************************************************************
    *** WARNING: TEXT OUTPUT CHANGED                             ***
    ***   file:line   print.c:412                                ***
    ***   invocation: ls -lF /tmp                                ***
    ***   expected:   "drwxr-xr-x  2 root  wheel  ..."           ***
    ***   got:        "drwxr-xr-x 2 root  wheel  ..."            ***
    ***   reason:     ...                                        ***
    ****************************************************************

A conversion with any such warning is not finished, whatever else
passes.

## Hard rules

- Follow the phases in order.  Do not edit code before phase 3.
- Stop after phase 2 and ask the user to review the data model.  This
  is not optional, even for small programs, unless the user has told
  you in this conversation to skip the review.
- Literal format strings only.  Never build an xo_emit format with
  snprintf or string pasting; use the {a:} modifier, a '*' width, or
  separate calls (references/pitfalls.md lists the usual answers).
  If the code seems to need a built format anyway, libxo is missing a
  feature: stop, don't build it, and report the gap in phase 7 with
  the code that needs it.
- Every field name you emit is in the agreed schema.  A new name after
  phase 2 is a schema change: report it, do not slip it in.
- A '*' width needs no encoding format; the default drops it.  An
  explicit encoding format takes the same arguments as the display
  format, so it gives each '*' it doesn't use a "%@*@" (see
  references/format-strings.md).
- xolint (and "xocc validate+lint" when available) must report no
  errors or warnings; read each "info" note and fix it or say why it
  is expected.
- Never change a project's own test baselines or golden files to make
  a check pass.  Report differences instead.

## Phase 0: Baseline

1. Build the unmodified program.  The skill targets current libxo;
   check "libxo-config --version" against the libxo you build with.
2. Choose representative invocations: each major flag and output mode,
   no arguments, empty input, an error case (missing file, bad
   option).  Write them, one command line per line, to a cases file
   (see scripts/xo-check for the format).  When the program is not the
   first word of a line (a pipeline), mark where the libxo options go
   with "@XO@".
3. Record the baseline: "sh scripts/xo-check record CASES DIR".  This
   captures stdout, stderr, and exit status of each invocation, and
   runs each twice to find cases whose output varies (times, pids);
   make those deterministic if possible.  If the program already has
   libxo output, use "record -x", which also records its XP, JP, and
   fullpath output, so phase 6 can check nothing already shipped was
   lost.  DIR is a scratch directory, never the project's tests.
4. Run the program's existing tests, if it has any, and note the
   result.

## Phase 1: Inventory

Run "sh scripts/xo-inventory FILES..." over the program's sources.  It
lists, per function, the output calls (printf, fprintf to stdout and
stderr, puts, putchar, fputs, fwrite, and the wide forms), the
err/warn family, exit and return paths out of main, loops containing
output, and any existing xo_* calls.

Read the code around each entry.  Read the man page: it names the
columns and explains the flags, and it is the source for names and
descriptions.  For large programs, work one output section at a time.

## Phase 2: Design the data model (then stop)

Read references/hierarchy.md and references/naming.md first.

Write the schema in this notation (RFC 8340-like tree lines, with the
libxo format and the C source in place of the type):

    +-- process-information
       +-- process* [pid]                   one per output row
          +-- pid               %d          kp->ki_pid
          +-- user              %s          user_from_uid(ki_uid)
          +-- cpu-percent       %.1f        getpcpu(k); {U:percent}
          +-- wait-channel?     %s          only with -l
          +-- argument*         %s          leaf-list; -ww argv loop

- "name* [key ...]" is a list with its key leafs;
- "name*" on a leaf, with no key, is a leaf-list;
- "name?" is optional: present only under some flag or condition
  (the emit is inside an "if"); when unsure, say so in the note;
- a node with children and no marker is a container;
- no rw/ro column: all of this is state data.

With the tree, list:

- each name that was hard to pick, with the alternatives considered;
- lexicon results: run "xo-lexicon check" style lookups as described
  in references/naming.md; for each discouraged tag, its replacement;
  for an already-shipped name that is discouraged, note that it stays;
- text that is a value in disguise (a header with a count, "total N",
  "3 users,");
- text that is only formatting and needs no field;
- places where the text is a lossy rendering (humanized sizes,
  truncated names, "-" for missing) and the encoded form should carry
  the real value;
- every early exit or return between an open and its close.

Then write the YANG module, THING.yang, following references/yang.md,
where THING is the top container name minus "-information".  Run
"pyang -f tree THING.yang" if pyang is installed and check the result
matches your tree; a mismatch is your error, fix it before the user
sees it.

Then write the new-words file, THING.new-words: one tag per line,
sorted, no duplicates, listing every container, list, and field name
in the schema that is not in the installed lexicon.

Stop.  Present the review package (the schema tree with notes,
THING.yang and the pyang tree, THING.new-words) and ask the user to
review it.  Do not continue until the user agrees.  Changes go into
the schema and the module together; once agreed they are the contract
for phases 3 to 6.

## Phase 3: Mechanical edits

In this order, building and running "sh scripts/xo-check compare"
after each step; the text must still be identical:

1. "#include <libxo/xo.h>" and the link flags
   (references/build-glue.md).
2. In main, before getopt or any other use of argv:

        argc = xo_parse_args(argc, argv);
        if (argc < 0)
            exit(EXIT_FAILURE);

   The check is required; a program that ignores the result, or
   doesn't test it, has a bug.  Add xo_set_version("...") after it if
   the program has a version.
3. The err/warn family and other stderr output
   (references/errors.md).  Exit codes do not change.

## Phase 4: Emit conversion

Convert output calls with references/format-strings.md and
references/printf-to-emit.md, following the agreed schema.

- One printf usually becomes one xo_emit; do not merge or split calls
  unless needed to put fields into the right instance.
- Use the library's features when they produce identical text: {U:}
  for units, {T:} for headings, {L:}/{D:}/{P:}/{N:} for text around
  values, {e:}/{d:} when display and data differ, {k:} on keys, {l:}
  for leaf-lists.  {h:} and {i:} only after checking the text is
  unchanged (see references/pitfalls.md).
- "%" is an ordinary character outside a field and in literal field
  content; it is not an escape there.  Each printf "%%" outside a
  conversion becomes a single "%": printf("%d%%") is
  xo_emit("{:x/%d}%"), and a units "%%" is "{U:%}".  A "%%" left in
  text prints two percent signs.
- Keep argument expressions as they were; a value that needs a
  different encoded form gets its own {e:} field.
- Read references/pitfalls.md before starting; it lists the mistakes
  earlier conversions made.

## Phase 5: Hierarchy

Add xo_open_*/xo_close_* calls per the schema (references/hierarchy.md).

- Open the top container after xo_parse_args() and option handling,
  before the first output; close it, then call xo_finish(), before
  every normal exit from main (or use xo_finish_atexit()).
- Lists open before their loop and close after it.  Instances open and
  close inside the loop body, on every path, including "continue".
- Handle every early return or exit you listed in phase 2.  xo_err()
  and xo_errx() call xo_finish() themselves.
  xo_parse_args() also registers xo_finish_atexit(), so a missed
  exit() path still gives well-formed output; write the closes
  anyway, since the structure is part of the schema.
- Do not rely on the "dtrt" flag to hide an imbalance.

## Phase 6: Verify

Run "sh scripts/xo-check compare CASES DIR".  It runs each case with
no --libxo and compares stdout, stderr, and exit status with the
baseline (printing the warning block on any difference), then runs
XP, JP, H, and lint styles, checks that the XML and JSON are
well-formed and that libxo printed no warnings, prints the fullpath
encoder output, and compares existing XP/JP baselines when there are
any.

Also run xolint over the changed sources, and build once with
"xocc validate+lint" when xocc is available.

Compare the fullpath output with the agreed schema.  Any difference
is either a bug to fix or a schema change to report.

## Phase 7: Report

- At the top, every TEXT OUTPUT CHANGED block, in full.  If there are
  none, say so in one line.
- The schema as produced, redrawn in the phase-2 notation from the
  fullpath output.
- Naming decisions the user should look at, discouraged names kept,
  and the new-words file.
- The YANG module's TODOs (namespace, organization, contact).
- Anything left unconverted (curses output, output to files that is
  not program output, debug output) and why.
- Feature gaps: any place that seemed to need a built format, or
  another libxo feature that does not exist, with the code.
- The results of xolint, xocc, and the program's own tests.

## Reference files

- references/format-strings.md: field syntax, roles, modifiers,
  escaping, encoding formats
- references/printf-to-emit.md: printf idioms and their xo_emit forms
- references/hierarchy.md: containers, lists, instances, keys
- references/naming.md: naming rules and the lexicon
- references/errors.md: err/warn/stderr conversion
- references/build-glue.md: FreeBSD, plain make, autoconf, cmake, meson
- references/pitfalls.md: things earlier conversions got wrong
- references/yang.md: writing THING.yang from the schema
- examples/: worked conversions (before, schema, after)
