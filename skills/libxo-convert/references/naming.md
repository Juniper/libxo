# Naming fields and containers

Names are the part of a conversion users depend on most: scripts and
filters key on them, and once shipped they cannot change.  The goal
is that the same concept gets the same name in every program.

## Rules

- Lower case, ASCII letters, digits, and hyphens.  Nothing else
  (xolint rejects other characters).
- At least three characters.
- Nouns or noun phrases: "mounted-on", "process-id", "user".
- Full words, unless the abbreviation is the domain term: pid, tty,
  uid, gid, mtu, cpu, ttl.  Do not invent abbreviations.
- <object>-<attribute> order: "inode-count", not "count-inodes";
  "interface-name", not "name-of-interface".
- Standard suffixes and what they mean:
      -count     a number of things
      -percent   a percentage, 0 to 100
      -time      a point in time
      -size      a size in bytes, unless {U:} says otherwise
      -name      a human-readable name
      -id        a numeric or opaque identifier
- Units in the name only when they are not obvious and not given by
  {U:}: "size", not "size-in-bytes"; but "blocks" and "kilobytes" are
  different things.
- No names that only make sense next to a text column heading
  ("1k-blocks", "%iused"), and none derived from the C variable
  ("ki_pid", "sb_hiwat").
- A name never changes with a flag.  df -h and df -k produce the same
  field names; the displayed value or the units change.
- The top container is "<thing>-information" (hierarchy.md).
- List names are singular.

## Where names come from

1. The program's man page: column descriptions, flag descriptions.
   Use its vocabulary.
2. The lexicon (below): names other programs already use.
3. Domain standards where they exist: for interfaces, routes, and
   other networking data, the names used by IETF and OpenConfig YANG
   models are a good tiebreaker.

## The lexicon

libxo installs a lexicon of tags that installed programs already use.
The directory is ${datadir}/libxo/lexicon/ (for example
/usr/local/share/libxo/lexicon/); "xo-lexicon -h" prints the default.

    system.lex               "tag packages uses": packages is the
                             number of programs using the tag
    system.discouraged.lex   "tag packages uses [replacement]"
    ${pkg}.lex               per-package source files (do not read
                             these directly; they are merged)

How to use it in phase 2:

- For each concept, look for an existing tag in system.lex
  (grep '^tag ' system.lex, or search for the word).  Prefer tags with
  a high packages count: one large program must not outvote many
  small ones.
- Never choose a tag listed in system.discouraged.lex.  Use the
  replacement if one is given, and say so in the review package.
- Exception (the prime directive): a name the program being worked on
  already ships stays, even if it is discouraged.  Note it in the
  review; new fields next to it use the preferred names.
- After the schema is drafted, write a scratch C file containing only
  the xo_emit and xo_open_* calls you plan (or, after phase 4, use the
  real sources) and run:

      xo-lexicon check FILES...      # known / discouraged / new
      xo-lexicon check -n FILES...   # just the new tags

  The second form produces the new-words file.  Without xo-lexicon,
  compare the schema's names against system.lex by hand.

If no lexicon is installed, every name is new; say so in the review
package and choose names by the rules above.

## The new-words file

${MODULENAME}.new-words lists every container, list, and field name in the
schema that is not in system.lex: one tag per line, sorted, no
duplicates, nothing else.  It is a lexicon candidate list for the
user.  The definition of each word is in the description statement
of ${MODULENAME}.yang, so the file stays one word per line.

## What to put in the review package

- Each name that was hard to pick, the alternatives considered, and
  why this one won.
- Each discouraged tag avoided and the replacement used.
- Each shipped discouraged tag kept.
- Names where the text output's vocabulary and the lexicon disagree.
