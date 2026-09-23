# Reading RTOON

RTOON is a compact, line-oriented text encoding of the same data model
JSON represents - objects, arrays, scalars. It is libxo's own format,
related to but not the same as TOON; do not apply TOON's rules where
they differ from what's below. Full formal spec:
`encoder/rtoon/rtoon-spec.md`. This file is the short version, meant to
be enough on its own to decode RTOON correctly.

Structure comes from indentation only (2 spaces per level, spaces
only, never tabs). Before anything else, check for these two
non-data line types, at any depth:

- A blank line, or a line whose first non-whitespace character is `#`,
  is a **comment** - ignore it completely; it carries no data and
  doesn't count when you're looking at "what's the next line under
  this parent."
- A line whose first non-whitespace character is `@` is an
  **instruction**, not data - a directive to the decoder, not part of
  the object/array structure. Only one is defined so far: a document
  normally starts with `@version 1.0.0`, a forward-compatibility marker
  (this format's own version) - just note it and move on; it doesn't
  affect how you decode anything else.

Every other line is one of six kinds. Read the kinds in
this order when classifying a line:

1. The line is exactly `-` and it's the only content under its parent
   -> that parent is an **empty list** (equivalent to `[]`).
2. The line starts with `- ` -> it's the start of a **list instance**
   (one array element). Everything after `- ` is itself a field-line
   (recurse into rules below). Following lines at the same indentation
   as that field belong to the same instance, until the next `- ` at
   that depth (next element) or a dedent (list ends).
3. The line's first non-blank character is `{` -> it's a **fields-line**
   for a dense (tabular) list: `{f1,f2,f3}` names the columns for every
   plain comma-row that follows, until the next fields-line or a
   dedent. A field-list only ever grows (later fields-lines add columns,
   never remove/rename/reorder) - treat each new one as replacing the
   column set for subsequent rows only, without touching values already
   decoded for earlier rows. A column name may end with a bare `=`
   (e.g. `{sku=,qty,price}`) - that marks the column as a key field,
   the same thing `=` means on a key-value line (rule 5 below); strip
   the trailing `=` to get the real column name and decode values in
   that column the same as any other field. A column name may also
   contain a `/`, e.g. `via/interface` - that means the encoder
   **flattened** a nested container (`via`) into this dense list's
   columns instead of dropping to sparse form. Every column name
   sharing the same `xxx/` prefix belongs to one reconstructed
   sub-object named `xxx`, keyed by the part after the slash; group them
   back together when building the decoded object. A flattened
   sub-object's own fields can't carry a key marker of their own (only
   the full `xxx/yyy` column name can end in `=`), so don't expect
   "which field of `via` was the key" to be recoverable.
4. Otherwise, if the line is under an active fields-line's scope
   (previous case) -> it's a **row**: split on commas, map cell *i* to
   field *i* of the current fields-line, in order. An empty spot
   between two commas (`a,,c`) means that field is an empty string for
   this record, not "missing" - "missing" only happens for a field that
   a *later* fields-line introduced, which earlier rows simply don't
   have a slot for at all. Type each non-empty, unquoted cell per
   "Scalar typing" below.
5. Otherwise, scan the line for an unquoted `=` before any unquoted
   whitespace. If found -> **key-value**: `name=value`. This is an
   ordinary object field, just one libxo happens to flag as an
   identifying/key field for the record it's in - decode its value the
   same as any other field; the `=` carries no meaning beyond "this one
   is a key," which you can use to know what identifies the record if
   asked, but doesn't change the shape of the decoded data.
6. Otherwise, if the line has unquoted whitespace after the first
   token, and everything after that whitespace ends in an unquoted
   trailing comma -> **leaf-list**: `name v1,v2,v3,` is an array of
   scalars named `name`. The trailing comma is mandatory and is what
   tells you this is a leaf-list rather than a data-value (rule 7) -
   `name v1,v2,v3,` is the array `["v1","v2","v3"]`-shaped, `name v1`
   (no trailing comma) is just the plain scalar `v1`. Even a
   single-element leaf-list keeps the trailing comma: `tags red,` is
   one element, `tags red` is a scalar. To decode: strip exactly one
   trailing comma, then split the remainder on commas the same way as a
   dense row (rule 4) - empty-between-commas means an empty string, no
   quoting needed there. The one exception is the *last* value: if it's
   meant to be an empty string, it must be spelled `""` (`tags
   red,"",`), since a bare empty slot right before the terminating
   comma would be indistinguishable from "no more elements." Type each
   non-empty, unquoted value per "Scalar typing" below.
7. Otherwise, if the line has unquoted whitespace after the first
   token -> **data-value**: `name value` - the rest of the line
   (trimmed) is a plain scalar. `value` spelled exactly `""` means the
   empty string, not literally two quote characters. Otherwise, if
   `value` is unquoted, type it per "Scalar typing" below.
8. Otherwise (the whole line is one token, and it isn't case 1) -> it
   opens a **container** or a **list**, and you can't tell which from
   this line alone. Look at what's indented under it: if that content
   matches rules 1-4 (a lone `-`, a `- ...` instance, or a `{...}`
   fields-line), it was a list; decode its children as array elements.
   Otherwise (an ordinary field-line, or nothing at all/immediate
   dedent) it was an object; decode its children as that object's
   properties, or decode it as `{}` if there's nothing under it at all.

Quoting: a value or key may be wrapped in `"..."`. Escapes: `\"` and
`\\` for the literal quote/backslash characters, `\n`/`\r`/`\t` for
LF/CR/HTAB, and `\uXXXX` (hex) for any other control character below
U+0020 - this is TOON's own Section 7.1 escape table, not JSON's (JSON
has no `\uXXXX`-only fallback rule the same way, and JSON permits a
few escapes, like `\/`, that never appear in rtoon output). A raw,
unescaped control byte never appears inside a quoted token.

`\uXXXX` also covers non-ASCII characters: any character in the Basic
Multilingual Plane (U+0080-U+D7FF, U+E000-U+FFFF) that rtoon quotes
comes out as `\uXXXX`, not literal UTF-8 - e.g. an accented letter
shows up as `\u00e9`, not as its raw UTF-8 bytes. The one exception is
a supplementary-plane character (above U+FFFF, e.g. most emoji) -
those have no `\uXXXX` form (a decoder must reject a surrogate-pair
escape standing in for one), so they appear as literal UTF-8 bytes
inside the quotes instead. When you see a quoted token, unescape it
per that table and use the result - the quoting itself carries no
meaning beyond "take this literally, don't apply the structural rules
above to its contents." A quoted token is always a string, no matter
what it looks like.

Scalar typing: this part is exact, not a heuristic. Every unquoted
value token (a data-value's value, a leaf-list value, a dense-row
cell) decodes to a typed value, the same "magic" TOON itself uses for
bare scalars:
- `true` or `false` (exact case) -> boolean,
- `null` (exact case) -> the null value (distinct from `""`, the empty
  string - don't conflate them),
- a token matching `-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?`, with a
  leading `0` in the integer part only legal when that part is the
  single digit `0` (so `0`/`0.5`/`-0.5` are numbers, `05`/`0042` are
  not) -> number,
- anything else unquoted -> string.

A string whose text would otherwise collide with one of these (an
actual product code `"42"`, a status string `"true"`) is always
quoted by the encoder for exactly this reason - if you see a bare `42`
or `true`, it really is the number or the boolean, never a
disguised string.

## Worked example

RTOON:

```
items
  {sku=,qty,price}
  A1,2,9.99
  {sku=,qty,price,cost}
  B2,1,14.5,0.37
note backorder possible
```

Decodes to:

```json
{
  "items": [
    { "sku": "A1", "qty": 2, "price": 9.99 },
    { "sku": "B2", "qty": 1, "price": 14.5, "cost": 0.37 }
  ],
  "note": "backorder possible"
}
```

(`items` is a container-or-list-open, rule 8: its first child is a
`{...}` fields-line, so it's a list, dense form; `sku`'s trailing `=`
in the fields-line marks it as the record's key, same meaning as `=`
on a key-value line. `note` is a data-value: plain key + rest-of-line
value.)

RTOON, same data with a nested field instead of a scalar `cost`:

```
items
  - sku=A1
    qty 2
    price 9.99
  - sku=B2
    qty 1
    price 14.5
    detail
      cost 0.37
      backordered true
note backorder possible
```

Decodes to the same shape, except `items[1]` gets a `detail` object
(`{ "cost": 0.37, "backordered": true }` - a native boolean, since
`backordered true` is unquoted) instead of a flat `cost` field -
`items` here is a list, sparse form (its first child is a `- ...`
line, rule 2), and `sku` is marked as this record's key via `=`, which
doesn't change how you decode it, only tells you it's what identifies
the record if you need to refer back to one.

RTOON, a dense list with a flattened nested field:

```
route-entry
  {destination=,protocol}
  10.1.2.3,discard
  {destination=,protocol,via/interface,via/source-address}
  10.2.2.3,local,igb0,10.10.2.3
  10.3.2.3,local,igb1,10.11.2.3
```

Decodes to:

```json
{
  "route-entry": [
    { "destination": "10.1.2.3", "protocol": "discard" },
    { "destination": "10.2.2.3", "protocol": "local",
      "via": { "interface": "igb0", "source-address": "10.10.2.3" } },
    { "destination": "10.3.2.3", "protocol": "local",
      "via": { "interface": "igb1", "source-address": "10.11.2.3" } }
  ]
}
```

(The second fields-line widens the first, adding `via/interface` and
`via/source-address` - two ordinary columns, per rule 3's fields-line
widening. The first record has no slot for either at all - it predates
the widening - so it gets no `via` key rather than an empty one. The
other two records' `via/*` columns are grouped back into a `via`
sub-object by their shared prefix.)

## Things that will NOT appear, so don't expect them

- No `[N]` length prefix on any list, ever - a list's length is just
  however many elements you actually decode before it ends.
- No colon after a container or list name.
- No separate type tag or sigil beyond quoting - the quote/no-quote
  choice on a value token IS the type signal (see "Scalar typing"
  above); there's no other schema information to look for.
