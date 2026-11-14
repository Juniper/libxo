# Writing THING.yang

The YANG module is a formal copy of the phase-2 schema.  It gives the
user something precise to review, gives each name a definition (its
description), and can later validate the encoded output.  It is
written from the schema tree; nothing in it may disagree with the
tree.

## Module header

    module THING {
      yang-version 1.1;

      // TODO: namespace not assigned; supply one before publishing
      namespace "urn:TODO:THING";
      prefix THING;

      // TODO: organization and contact
      description
        "Output of THING(1), as produced by its libxo conversion.";

      revision YYYY-MM-DD {
        description "Initial version, from the libxo conversion.";
      }

      container THING-information {
        config false;
        ...
      }
    }

- The module name and prefix are THING: the top container name minus
  "-information".  If the prefix would clash or is long, ask.
- The namespace is the placeholder above.  Never invent a real one
  (no "urn:ietf:...", no domain names); the user supplies it.
- The revision date is today.
- "config false" on the top container: everything is state data.

## Nodes

    schema tree                        YANG
    +-- name          (container)      container name { ... }
    +-- name* [key]   (list)           list name { key "key"; ... }
    +-- name*         (leaf-list)      leaf-list name { type ...; }
    +-- name          (leaf)           leaf name { type ...; }
    +-- name?         (optional)       leaf name { ...; } and say when
                                       it is present in the description

A list with no natural key has no key statement; that is legal under
"config false".  Keys are listed in the order they are emitted.

Every node gets a description: what it means, in the man page's words
where possible, and the flag or condition it depends on ("Only with
-l.").  These descriptions are the definitions behind the new-words
file.

## Types

Take the type from the encoding format (or the display format if
there is none), and the C type of the argument:

    %d %i                    int32
    %ld %jd %lld %qd         int64
    %hd                      int16
    %hhd                     int8
    %u                       uint32
    %lu %ju %llu %zu %qu     uint64
    %hu                      uint16
    %x %o (and %lx, ...)     the unsigned type of the same size; the
                             display base does not change the value
    %f %e %g                 decimal64 { fraction-digits N; }, where N
                             is the precision ("%.1f" gives 1); if the
                             precision is not fixed, use string and
                             say why
    %s %ls %c                string
    {B:...}                  boolean
    {e:} with no format      string

Use the C type, not the format, when they disagree in width (an
int64_t printed with %d after a cast): the conversion should use the
wider format, and the module the wider type.

Do not invent ranges, patterns or enumerations unless the source makes
them certain.  A field whose value is one of a small fixed set of
strings ("up"/"down") may be an enumeration; list it as a question in
the review package.

## Units

A field with {U:...} gets a units statement with the same text:

    leaf used-percent {
      type uint32;
      units "percent";
      description "...";
    }

Do the same when the unit is implied by the name or the man page
("blocks of 1024 bytes").

## Times

Use the form the schema chose (printf-to-emit.md):

    leaf modify-time {
      type int64;
      units "seconds";
      description "Last modification time, seconds since the epoch.";
    }

or, for an ISO 8601 string, import ietf-yang-types and use
yang:date-and-time.  Do not import other modules.

## Checking the module

With pyang installed:

    pyang THING.yang              # syntax and semantics
    pyang -f tree THING.yang      # the tree, for the review package

The pyang tree must match the schema tree node for node.  pyang prints
"ro" for every node and a type column; that is expected.  If pyang is
not installed (or not runnable), say so in the review package rather
than skipping the module.

## Validating encoded output (optional)

libxo's output is close to, but not exactly, what YANG validators
expect:

- XML output has no namespace; a validator needs
  xmlns="urn:TODO:THING" on the top element.
- JSON output uses plain names; RFC 7951 JSON needs the top member to
  be module-qualified ("THING:THING-information").

So validation needs a small wrapper that adds these.  Do not change
the program's output to suit a validator.
