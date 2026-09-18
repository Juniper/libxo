# RTOON wire format specification (draft)

RTOON ("R"-TOON) is libxo's own line-oriented, indentation-based text
format for cheaply representing the same data model libxo already emits
as XML/JSON/HTML/text, aimed at LLM-consumption use cases where token
count is the thing being optimized. It resembles the TOON format
(toon-format/spec) in spirit - indentation instead of braces, a tabular
form for uniform arrays - but is a deliberately non-conformant dialect,
not an implementation of TOON. See `build/toon-plan.md` Section 5.0 for why:
the streaming-length use case this format exists for (toon-format/spec#15)
was of no interest to TOON's upstream owner, so there is no compatibility
constraint holding this design back from diverging where libxo's
streaming/filtering constraints call for it.

This document specifies the wire format itself, independent of libxo's
internals. `build/rtoon-plan.md` covers how `encoder/rtoon/enc_rtoon.c`
implements this against libxo's encoder API. `encoder/rtoon/rtoon-skill.md`
is the compact reference meant to accompany RTOON output to an LLM
decoding it.

Status: reflects everything settled in design discussion as of
2026-09-15. Sections marked "Open" are explicitly not settled.

## 1. Overview

A document is a sequence of lines. Structure is conveyed by indentation
(spaces only - tabs are forbidden in indentation, same restriction TOON
itself imposes, so a decoder can reject a tab-indented document outright
rather than guess a tab width). Indentation increases in fixed steps of
**two spaces per level**. Two spaces is not an arbitrary style choice
here - see Section 3's list-instance rule for why that specific width makes the
grammar work cleanly.

There is exactly one delimiter, comma. (Real TOON lets an array header
select tab or pipe instead; RTOON does not carry that feature. Nothing
in libxo's use case has asked for it - see Section 9 if that changes.)

Before any of that, two line categories are recognized first, ahead of
everything else in this section (Section 1.1):

- a line that is blank, or whose first non-whitespace character is `#`,
  carries no data and is ignored entirely;
- a line whose first non-whitespace character is `@` is an
  **instruction**, not data - a directive to the decoder, distinct from
  the six data-line kinds below.

Every other line, once its leading indentation is stripped, is one of
six kinds:

1. **container** - a bare name, children (if any) follow indented.
2. **data-value** - `name value`, a plain scalar leaf.
3. **key-value** - `name=value`, a scalar leaf explicitly marked as a
   key field.
4. **list** - a bare name whose children are list-instances or a dense
   table (structurally identical to a container's opening line; see Section 4
   for how a decoder tells them apart).
5. **list-instance** - `- ` followed by a field-line, one per array
   element, YAML-block-sequence style.
6. **leaf-list** - `name v1,v2,v3`, a comma-separated run of scalars for
   one field.

A **dense list** is not a seventh kind - it's what a list's body looks
like when the encoder chooses the tabular form (Section 6) instead of
list-instance lines, for arrays of records that stay uniform (or
monotonically widening) in shape.

### 1.1 Comments, blank lines, and instructions

These are checked before the six data-line kinds above, at any
indentation, in any context (inside a container, a list, mid-instance -
anywhere a line can appear):

- **Comment**: a line whose first non-whitespace character is `#`. The
  entire line is ignored - it is not data, not an instruction, and
  contributes nothing to structure (in particular, it does not count as
  content for the purposes of Section 3/4's "is this container/list
  empty" lookahead).
- **Blank line**: a line containing only whitespace (or nothing at all).
  Also ignored, same as a comment.
- **Instruction**: a line whose first non-whitespace character is `@`.
  This is not data - it is a directive telling the decoder how to
  process what follows, distinct from all six data-line kinds. The
  general instruction mechanism is intentionally left underspecified
  here: future work will add instructions such as "replace", "merge",
  "activate", "deactivate", "delete", and others, for use cases beyond a
  single self-contained document. This spec defines exactly one
  instruction today (Section 1.2, `@version`); a decoder encountering an
  instruction it doesn't recognize SHOULD treat it as an error rather
  than silently ignoring it, since instructions (unlike comments) are
  meant to change how the rest of the document is read.

### 1.2 The `@version` instruction

Every RTOON document is expected to begin with a version-declaration
instruction, before any other content:

```
@version 1.0.0
```

This is a forward-compatibility escape hatch, not a feature negotiation
- it exists so that a future, incompatible revision of this format can
be unambiguously distinguished from what's specified here, by giving a
decoder something concrete to check before it commits to parsing the
rest of the document under these rules. This spec's own version is
`1.0.0`; nothing about version negotiation, compatibility ranges, or
what a decoder should do on a mismatch beyond "notice it" is specified
yet.

## 2. Lexical rules

### 2.1 Key/value split

For any line, scan left to right from the first non-whitespace
character:

- If an unquoted `=` is found before any unquoted whitespace, the line
  is **key-value**: the key is everything before the `=`, the value is
  everything after it, both trimmed of any surrounding whitespace.
  Canonical (encoder-emitted) form has no surrounding whitespace at all
  (`name=value`); a decoder MUST tolerate whitespace on either side of
  `=` for hand-edited input, but an encoder MUST NOT emit it - every
  space costs a token for no structural benefit (each byte of
  whitespace typically survives as its own token or fuses into the
  following word's leading-space token; it never comes for free), so
  padding around `=` is pure waste in the one context (a generated
  document, not a hand-edited one) where RTOON is actually used.
- Otherwise, if unquoted whitespace is found, the line is either
  **data-value** or **leaf-list**: the key is the first token; the rest
  of the line is the value. If that rest contains an unquoted
  comma, it's leaf-list (Section 5); otherwise data-value (Section 3.2).
- Otherwise (no unquoted `=`, no unquoted whitespace - the whole line
  is one token) the line is a **container** or **list** open, or one of
  the empty-list/list-instance markers in Sections 4 and 5 that also
  consist of a single token (`-`, or a `{...}` fields-line).

### 2.2 Quoting

Quoting uses double quotes. Escaping matches TOON's own Section 7.1
table exactly, not just its `\"` / `\\` rows: `\"` and `\\` are the
structural escapes, LF/CR/HTAB get the short mnemonic escapes `\n`,
`\r`, `\t`, and every other C0 control character (U+0000-U+001F) gets
a `\uXXXX` escape (lowercase hex). No control byte may ever appear
literally inside a quoted token. This is a hard requirement, not a
style choice: rtoon is line-oriented (indentation and line boundaries
carry structure), so a raw, unescaped LF or CR inside a value would
split the output mid-token and corrupt the surrounding line -
something a purely comma/quote-oriented escape set doesn't protect
against. No escape syntax beyond TOON's own is introduced.

rtoon also makes a policy choice where TOON leaves the encoder free to
pick: every non-ASCII Basic Multilingual Plane codepoint (U+0080
through U+D7FF and U+E000 through U+FFFF) is `\uXXXX`-escaped rather
than written out as literal UTF-8, even though TOON's table only says
an encoder MAY do this (its default is SHOULD emit literal UTF-8).
rtoon always takes the `\uXXXX` option here, so a quoted token's bytes
are all plain ASCII except for the one case that has no `\uXXXX` form
at all: a supplementary-plane codepoint (U+10000-U+10FFFF, encoded as
4 UTF-8 bytes) is written out as literal UTF-8, unescaped, because a
decoder MUST reject a surrogate-pair `\uXXXX` escape standing in for
one (Section 7.1's table again). This choice keeps the encoder's
output byte-for-byte predictable without a UTF-8-aware tokenizer for
the overwhelming majority of non-ASCII input, at the cost of not being
purely ASCII for the rare supplementary-plane character.

**Keys** (the first token of a line, or a field name inside a `{...}`
fields-line) MUST be quoted if any of:
- it contains an unquoted `=` that is not a trailing key-marker (see
  Section 6.1's field-name grammar) - i.e. any unquoted `=` not
  immediately followed by the field-list's `,` or `}`,
- it starts with `{` (would be read as a fields-line),
- it starts with `-` (would be read as a list-instance/empty-list
  marker),
- it contains unquoted whitespace (including any C0 control character,
  U+0000-U+001F, treated as whitespace for this purpose - it needs
  quoting whether or not it happens to satisfy `isspace()`) or an
  unquoted comma,
- it contains an unquoted `"` or `\`,
- it contains any non-ASCII byte (>= U+0080), since that byte is
  subject to the `\uXXXX`/literal-UTF-8 policy above and escaping only
  happens inside quotes,
- it is empty.

**Values** (a data-value's or key-value's value; a leaf-list or dense
row cell) MUST be quoted if any of:
- it starts with `{` (kept uniform with the key rule, even though only
  a row's first cell is actually at risk of fields-line confusion -
  see Section 6.3),
- (leaf-list / dense row cell only) it contains the active delimiter
  (`,`) unquoted,
- it contains an unquoted `"`, `\`, any C0 control character
  (U+0000-U+001F) - including LF/CR/HTAB - or any non-ASCII byte
  (>= U+0080), since any of those is subject to the escaping/`\uXXXX`
  policy above and escaping only happens inside quotes,
- (data-value / key-value only) it is empty - an empty scalar value
  MUST be spelled `""`, never bare nothing. This is the one quoting
  rule that exists purely for structural disambiguation rather than
  content: a data-value/key-value line with nothing after the
  separator would otherwise be indistinguishable from a container/list
  open with an accidentally-attached separator. Dense-row cells don't
  need this, because an empty cell there is already unambiguous by comma
  position (`a,,c` - see Section 6.4) - there's no bare-token-vs-nothing
  to confuse.
- (leaf-list only) it is empty **and** it occupies the last value
  position before the list's mandatory trailing comma (Section 5) - MUST
  be spelled `""`. An empty cell anywhere else in a leaf-list needs no
  quoting, same as a dense-row cell, since comma position alone
  disambiguates it there.
- it is a *string* value whose text, left unquoted, would be read as a
  typed literal under Section 2.3's rules - i.e. it case-sensitively
  equals `true`, `false`, or `null`, or matches Section 2.3's number
  grammar. A string that merely fails the number grammar (e.g. `0042`,
  disqualified by its leading zero) already decodes as a string
  unambiguously and needs no quoting on this account.

Nothing about "-" or "=" needs quoting in a *value* position - those
markers are only structurally meaningful at the start of a line (the
key position), never inside an already-delimited value.

### 2.3 Scalar typing

An unquoted value token is never "just a string" - it carries a type,
the same "magic" real TOON's own decoder applies to bare scalars
(matching TOON's SPEC.md Section 4, not merely inspired by it). In
order:

- the token `true` or `false` (exact case) -> boolean,
- the token `null` (exact case) -> the null value,
- otherwise, if the token matches the number grammar
  `-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?` **and** does not have a
  forbidden leading zero (a `0` at the start of the integer part is
  only legal when that integer part is the single digit `0` - so `0`,
  `0.5`, `-0.5`, `0e1` are numbers, but `05`, `0042`, `-01` are not)
  -> number,
- otherwise -> string.

A *quoted* token is always a string, regardless of what its content
looks like: `"true"`, `"42"`, `"null"` decode to the strings `true`,
`42`, `null`, never to the typed values. Quoting is precisely the
mechanism for spelling a string that would otherwise collide with a
typed literal - see Section 2.2's matching quoting-trigger bullet.

**Numeric normalization** (TOON SPEC.md Section 3's `-0` -> `0` and
NaN/Infinity -> `null` rules) is not something the encoder does: an
`XO_OP_CONTENT` numeric value arrives already formatted by libxo's own
core number-formatting layer (the same layer every other encoder -
CSV, CBOR, FDR - trusts as-is), and rtoon writes it through unchanged.
If libxo's own formatting ever needs to guarantee `-0`/NaN/Infinity
normalization, that's a libxo-wide concern to fix once upstream of all
encoders, not something duplicated per-encoder here.

```
count 42        -> number 42
code "42"       -> string "42"
active true     -> boolean true
label "true"    -> string "true"
mid null        -> the null value
mid ""          -> the empty string
```

The empty-string sentinel `""` and the unquoted `null` token are
unrelated: `""` is the empty string, `null` is the null value. An
encoder emitting an actual null MUST write the bare token `null`, never
`""`; a decoder MUST NOT treat them as the same value.

**No grouping.** RTOON is an encoding format, not a display format - an
encoder MUST render numbers in plain canonical form, with no
thousands-grouping separators and no locale-specific digit grouping
(the kind of humanized rendering libxo's text/HTML display styles
apply is out of scope here, the same way JSON/CBOR/CSV never honor it
either). A grouping separator inside a number token would also just be
a literal comma, which the number grammar doesn't accept anyway - so
this is stated as a rule on the encoder, not a new lexical allowance.

## 3. Container

```
container1
  container2
    container3
```

A bare name with nothing else on the line. Children, if any, are every
subsequent line at exactly one indent level deeper, until indentation
returns to the container's own level or shallower (or EOF).

An **empty container** is a bare name line followed immediately by a
dedent (or EOF) - no marker, no special case. This is the default,
unmarked reading of a childless bare-name line; see Section 4 for why that's
safe.

## 4. List (sparse form)

```
list
  - name Phil
    job owner
  - name Jim
    job worker
```

Lexically, a list's opening line is identical to a container's - a bare
name, nothing else. A decoder tells them apart the same way it decides
"empty or not" for a container: by looking at the first line at depth+1
that isn't itself a nested empty-list marker's continuation:

- nothing at depth+1 (dedent/EOF) -> the name opened an **empty
  container**.
- depth+1 content is exactly `-` alone -> the name opened an **empty
  list** (Section 4.1).
- depth+1 content starts with `- ` followed by more -> the name opened a
  **non-empty list**, sparse form, and that line is the first instance
  (Section 4.2).
- depth+1 content starts with `{` -> the name opened a **non-empty
  list**, dense form (Section 6).
- depth+1 content is an ordinary field-line (container/data-value/
  key-value/leaf-list) -> the name opened a **container** with that
  content as its first child.

This is a one-line lookahead, not a backtracking parse - for a
character-buffered decoder it's cheap (buffer one line), and for an LLM
reading the whole prefix at once it isn't a real cost at all; the model
already has the next line in view when it interprets the current one.

### 4.1 Empty list - two accepted spellings

Because `XO_OP_OPEN_LIST` fires before an encoder necessarily knows
whether any instances will follow, the encoder gets to choose whichever
spelling matches what it already knows at the point it writes each
line, rather than being forced to buffer the "list" line until
`XO_OP_CLOSE_LIST` resolves the question:

```
list -
```
or
```
list
  -
```

A decoder MUST treat both as identical: an empty list. This mirrors
libxo's own `XOIF_TOP_EMITTED`-style deferred-decision handling for
JSON's leading commas - don't make the writer track state it doesn't
need to, resolve the question at whichever event actually knows the
answer.

A lone `-` (nothing following, ignoring trailing whitespace) at a
list's child depth, when it is the *only* content the list body ever
has, is always the empty-list marker - never a one-instance list whose
instance happens to carry no fields. (An instance with zero fields
isn't a case libxo's own emit calls produce in practice - every
instance opens with at least a key. Treated as **open** below.)

### 4.2 List-instance

```
- key-name=key-value
  key1-name=key2-value
  data value
  more-data more-value
- key-name=second-instance-key-value
  key1-name=second-instance-key2-value
  data value-for-instance2
  more-data more-value-for-instance2
```

`- ` (dash, one space) marks the start of an instance. What follows on
that same line is the instance's first field, parsed exactly like any
other field-line (data-value, key-value, container-open, or leaf-list)
after the `- ` prefix is stripped. Subsequent lines at the same
indentation as that first field (i.e., depth+1 relative to the list,
the same column the content after `- ` started at) belong to the same
instance, until either another `- ` line appears at that depth (next
instance) or indentation decreases (list ends).

This is exactly why the fixed 2-space indent step matters: `- ` is also
two characters, so it occupies exactly one indent level's width, and
the instance's fields - first line and continuations alike - land at a
single, consistent depth with no special-case column tracking needed
beyond ordinary depth arithmetic.

Because continuation lines are ordinary field-lines, an instance field
that is itself a container or a nested list is just another line at
depth+1 followed by its own children at depth+2 - no flattening, no
CSV-style row-accumulate-and-flush needed for the sparse form (contrast
this with the dense form, Section 6.5, where nesting is a real problem):

```
list
  - name Phil
    address
      city Springfield
      zip 00000
```

An instance that closes without ever emitting a single field (an empty
instance) is written as `- ""` - a dash followed by a single empty
string - rather than a bare `-`:

```
list
  - ""
  - name second
```

`- ""` reads unambiguously as "an instance whose content is one empty
string," the same way any other single-value instance would look
(`- somevalue`); a bare `-` would instead read as a truncated or
malformed line. libxo's own `xo_emit()` calls never produce an empty
instance in practice - every instance carries at least a key - but the
encoding stays well-formed for one either way.

## 5. Leaf-list

```
tags red,green,blue,
```
or, for a single-element leaf-list:
```
tags red,
```

`name`, whitespace, then a comma-separated run of values, terminated by
a single **mandatory, unquoted trailing comma**. The trailing comma is
what distinguishes a leaf-list from a data-value line (Section 3.2):
`tags red` (no trailing comma) is a plain scalar; `tags red,` (trailing
comma) is a one-element array. This is why the trailing comma is
required even when there's only one value - dropping it for the
single-element case is exactly the ambiguity this rule exists to avoid.

The trailing comma is a pure terminator - it does not itself introduce
an extra (empty) element. Decode a leaf-list by stripping exactly one
trailing comma from the end of the line's value portion, then splitting
the remainder on commas exactly as before. Values follow the
value-quoting rules of Section 2.2. An empty cell between two commas (or
at the start) needs no quoting - position alone identifies it: `a,,c,`
means the middle value is empty, the same convention CSV and TOON's own
tabular rows already use.

**Exception - a genuinely empty last value.** Because the trailing
comma is a terminator, not an element separator, a value that is
actually meant to be an empty string cannot be left bare when it would
land in the *last* position: `tags red,,` would be visually
indistinguishable from "just the terminator, nothing more" (is that one
value `red` plus a trailing empty element, or `red` plus stray
whitespace before the terminator?). To avoid that, a leaf-list's last
value, when it is the empty string, MUST be spelled quoted: `tags
red,"",`. Every other position's empty cell (`a,,c,`) still needs no
quoting - only the position immediately before the terminating comma is
special.

## 6. List (dense form)

```
list
  {k=,f1,f2,f3}
  A,4,5,6
  B,7,8,9
  C,1,2,3
  {k=,f1,f2,f3,f4}
  D,a,b,c,d
  E,e,f,g,h
```

Used instead of Section 4's sparse list-instance form when an array's records
share a uniform (or monotonically widening) set of scalar fields - the
tabular form real TOON uses, adapted for streaming.

### 6.1 Fields-line

A body line whose first non-whitespace character is an unquoted `{` is
always a **fields-line**, declaring the ordered field names for every
row until the next fields-line: `{f1,f2,f3}`. This is sound precisely
because Section 2.2 already forces any real row whose first cell would
otherwise start with `{` to be quoted - so an unquoted leading `{` can
never legitimately be a row.

There is no `[N]` or `[-]` count anywhere in this form (an earlier
draft of this design carried a `key[-]:` bracket/colon header; it's
superseded - once dash and brace already distinguish list-from-container
per Section 4, and rows are self-terminating at end-of-line, the bracket buys
nothing and costs a token on every list). A list is exactly as long as
however many row lines appear before the next fields-line's scope ends
by dedent.

**Key marker.** Any field name in a fields-line MAY end with a single
unquoted `=` immediately before its closing delimiter (the `,` or the
`}`) to mark that field as a key, mirroring Section 4.2's `name=value`
marker for the sparse form: `{k=,f1,f2,f3}` marks `k` as the key;
`{k1=,k2=,f1,f2}` marks a composite key `k1`+`k2`. This is unambiguous
without any new quoting exception: Section 2.2 already requires any
field name containing a literal, non-trailing `=` to be quoted, so the
only way an unquoted `=` can appear in a field-name token at all is as
this trailing marker. A field name with no trailing `=` is an ordinary
(non-key) field, exactly as before this section was added.

### 6.2 Widening

Each fields-line's field set MUST be a superset of the previous one's
within the same list - appended only, never reordered, renamed, or
removed. The encoder only widens when it discovers a genuinely new
field name on some instance; a strict decoder SHOULD reject a
narrowing/reordering fields-line (cheap to check: track the running
maximum field list seen). Key markers are part of a field's identity
for this purpose: once a field has been marked with a trailing `=` in
some fields-line, every later fields-line in the same list that
re-lists that field MUST mark it too - the decoder has no memory of key
status beyond whatever the currently-governing fields-line says, so an
encoder that dropped the marker on a widened re-declaration would
silently un-key that field for every row from that point on.

### 6.3 Rows

One line per record, comma-separated cells, ordered to match the field
names of the fields-line currently in scope (the most recent one seen).
A row's cell count MUST equal that fields-line's field count - not the
list's overall union count.

### 6.4 Absent vs. present-but-empty

A field introduced only by a *later* fields-line has no slot at all on
an earlier row - full stop, not even an empty cell. A field that *is*
part of the row's own governing fields-line but wasn't supplied for
that record decodes to an explicit empty string via the bare-comma
convention of Section 5 (`a,,c` -> middle field is `""`).

### 6.5 Nesting

A dense row's cells are plain scalars; a nested container or list field
can't be put directly into one. Three options, in order of preference:

- **Flatten it** (Section 6.6) when the nested field is itself a
  container of scalars - the preferred fix, at the cost of losing the
  flattened sub-object's own key marking.
- **Fall back to a quoted, escaped sub-blob** - a self-contained nested
  RTOON fragment written as a quoted string cell - when flattening
  isn't applicable (e.g. the nested field is itself a list, whose
  cardinality varies row to row and so can't become a fixed set of
  columns).
- **Don't choose dense mode for that array in the first place** - fall
  back to Section 4's sparse form.

Since dense rows are emitted as they close and can't be retroactively
rewritten once a later record turns out to need nesting, **this spec
does not attempt a mid-stream downgrade from dense to sparse.** Unlike
an earlier draft of this design, the choice of dense vs. sparse is not
something the encoder infers by inspecting field shapes ahead of time -
static analysis can't settle it without buffering, which streaming
RTOON output does not do. Instead it's an explicit, upfront signal the
developer gives at the API level (the `dense` field modifier, or the
`XOF_DENSE` flag to `xo_open_list_hf()` - see `build/rtoon-plan.md`
Section 4); the encoder commits to dense or sparse mode as soon as that
signal is seen (or defaults to sparse if it never is), never by
inspecting the data itself.

### 6.6 Flattening

A dense list's records may include a field that, in the source data, is
itself a container of plain scalars. Rather than force that whole array
to sparse form (Section 6.5), the encoder MAY **flatten** that nested
container into several ordinary columns, one per leaf field of the
nested container, named with the parent field's name, a `/` separator,
and the nested field's own name: `via/interface`, `via/source-address`.
These flattened names are otherwise ordinary field names, subject to
every rule already stated for fields-lines (Section 6.1's quoting/key-
marker rules, Section 6.2's widening rule) - `/` needs no new quoting
exception, since it isn't otherwise meaningful in a field name.

Example - given this XML-shaped data (a `route-entry` list whose records
sometimes carry a nested `via` container):

```xml
<route-entry>
  <route-entry><destination>10.1.2.3</destination><protocol>discard</protocol></route-entry>
  <route-entry>
    <destination>10.2.2.3</destination>
    <protocol>local</protocol>
    <via><interface>igb0</interface><source-address>10.10.2.3</source-address></via>
  </route-entry>
  <route-entry>
    <destination>10.3.2.3</destination>
    <protocol>local</protocol>
    <via><interface>igb1</interface><source-address>10.11.2.3</source-address></via>
  </route-entry>
</route-entry>
```

as RTOON, dense form with `via` flattened instead of forcing sparse:

```
route-entry
  {destination=,protocol}
  10.1.2.3,discard
  {destination=,protocol,via/interface,via/source-address}
  10.2.2.3,local,igb0,10.10.2.3
  10.3.2.3,local,igb1,10.11.2.3
```

This is an ordinary application of Section 6.2's widening rule: the
second fields-line adds two new columns the first record's row simply
has no slot for (Section 6.4's absent-vs-empty distinction), exactly as
if those had been two unrelated new scalar fields rather than a nested
container's contents. A decoder reconstructs the nested object by
grouping every field name sharing a `/`-prefix (`via/`) back into a
sub-object (`via`) keyed by each name's remaining suffix
(`interface`, `source-address`).

**Caveat**: flattening does not preserve key marking (Section 6.1)
*within* the flattened sub-object - a trailing `=` can mark
`via/interface` itself as a key of the outer record, but there is no
way to say "`interface` was a key of `via`," since `via` no longer
exists as a distinct record once flattened. This is an accepted loss,
not an open problem: a flattened sub-object was, by definition, chosen
for arrays where gaining dense-mode eligibility matters more than
preserving that inner structure's own key-ness.

Flattening only applies to a nested field that is itself a container of
scalars. A nested field that is itself a list is not flattenable this
way (its element count varies row to row, so it can't become a fixed
set of columns) - see Section 6.5's other two options for that case.

## 7. Line classification (decoder summary)

For a body line at some depth, in order:

1. If the line is blank, or its first non-whitespace character is `#`
   -> ignore it entirely (Section 1.1); it does not affect depth
   tracking or "is this container/list empty" lookahead.
2. If the line's first non-whitespace character is `@` -> instruction
   (Section 1.1), not data; process per Section 1.2 (`@version`) or
   error on an unrecognized instruction.
3. Strip indentation, compute depth. Reject tabs in indentation.
   Depth must be <= (previous meaningful depth + 1); anything deeper is
   a structural error.
4. If in a dense list's scope and the line's first unquoted character
   is `{` -> fields-line (Section 6.1); split its contents on commas to
   get field names, stripping and noting any trailing unquoted `=` as
   that field's key marker.
5. If in a dense list's scope and not a fields-line -> row (Section 6.3),
   governed by the nearest preceding fields-line; type each cell per
   Section 2.3.
6. If the line is exactly `-` (nothing else) -> empty-list marker
   (Section 4.1) if this is the list's only content, else an error (open case
   from Section 4.1).
7. If the line starts with `- ` -> list-instance (Section 4.2); recurse into
   the remainder as a field-line.
8. Otherwise apply Section 2.1's key/value split: key-value, data-value,
   leaf-list, or a bare single token (container-or-list-open, resolved
   per Section 4 by peeking at the next deeper line). Type any
   resulting data-value/key-value value or leaf-list cell per
   Section 2.3.

## 8. Worked example

```json
{
  "items": [
    { "sku": "A1", "qty": 2, "price": 9.99 },
    { "sku": "B2", "qty": 1, "price": 14.5, "cost": 0.37 }
  ],
  "note": "backorder possible"
}
```

as RTOON (dense list, widened field set, trailing scalar):

```
items
  {sku=,qty,price}
  A1,2,9.99
  {sku=,qty,price,cost}
  B2,1,14.5,0.37
note backorder possible
```

Same data, forced to sparse form (e.g. because `cost` were itself a
nested container rather than a scalar):

```
items
  - sku=A1
    qty 2
    price 9.99
  - sku=B2
    qty 1
    price 14.5
    cost 0.37
note backorder possible
```

`qty` and `price` decode as numbers (Section 2.3) because they're
unquoted and match the number grammar; `sku`'s values decode as
strings because `A1`/`B2` don't. If a `sku` value happened to be the
string `"42"` (a product code that looks numeric), the encoder would
have to quote it - `sku="42"` or, in dense form, a quoted cell - to
keep it from decoding as the number 42.

## 9. Deliberate deviations from TOON, summarized

- No colon anywhere (container/list-open, key-value, data-value all
  drop it - see `build/toon-plan.md`'s design-conversation history for
  why each is safe).
- No `[N]`/`[-]` bracket/count on lists at all (superseded by Section 6.1's
  brace-and-dash-only disambiguation).
- `=` as a dedicated marker for key fields, in both the sparse form
  (`name=value`, Section 4.2) and the dense form (a trailing `=` on a
  field name in a fields-line, Section 6.1) - real TOON has no key
  concept at all; this is additive, not a deviation from something TOON
  does differently.
- Dual spelling for empty lists (Section 4.1) - real TOON always knows its
  array length upfront (`[0]`), so it has no equivalent problem to
  solve.
- Single delimiter (comma only) - real TOON's tab/pipe delimiter
  selection is dropped as unneeded (Section 9 below can reopen this).
- Multi-header (widening) field lists within one list body (Section 6.2) -
  real TOON requires one fixed field list per array; this is the
  feature that makes the tabular form usable under streaming/filtering
  at all.
- Mandatory trailing comma on a leaf-list (Section 5) - real TOON's
  tabular rows are already fixed-width by header, so it has no
  equivalent one-value-vs-scalar ambiguity to resolve.
- Comments (`#`), blank-line skipping, and `@`-instructions including
  `@version` (Section 1.1/1.2) - real TOON has no equivalent of any of
  these.
- Flattening a nested container into path-named columns (Section 6.6) -
  real TOON's tabular eligibility requires uniform, purely-scalar
  objects; it has no mechanism for absorbing a nested object's fields
  into the parent row at all.
- Always `\uXXXX`-escaping non-ASCII BMP codepoints (Section 2.2) -
  real TOON's Section 7.1 table leaves this as an encoder's own choice
  (SHOULD emit literal UTF-8, MAY emit `\uXXXX`); rtoon commits to the
  `\uXXXX` option everywhere it applies, rather than leaving it
  unspecified per-encoder.

Scalar typing (Section 2.3 - unquoted `true`/`false`/`null`/numbers
decode as their typed values, quoting is what forces a string reading)
is **not** in this list - it's carried over from real TOON's own
decode rules essentially verbatim, not a deviation.

## 10. Open items

- ~~Section 5's leaf-list-of-one collapse~~ RESOLVED: fixed by Section 5's
  mandatory trailing comma, not accepted as an ambiguity - `tags red` and
  `tags red,` are now distinct and unambiguous.
- ~~Section 6.5's dense/sparse commitment policy~~ RESOLVED: not an
  encoder-side static-analysis decision at all (static analysis can't
  settle it without buffering). Dense-vs-sparse is an explicit, upfront
  signal the developer gives at the API level - see Section 6.5 and
  `build/rtoon-plan.md` Section 4.
- ~~Section 4.1's degenerate "instance with zero fields" case~~ RESOLVED:
  every list instance has at least one field, always - not merely
  assumed, but taken as a hard invariant this spec relies on.
- ~~Whether a document-level way to force sparse-vs-dense is worth
  adding~~ RESOLVED: superseded by the per-list, per-developer `dense`
  field modifier / `XOF_DENSE` open-list flag (Section 6.5) - no separate
  document- or command-line-level option is needed or planned.
- Whether comma-only (no tab/pipe alternate delimiter) ever becomes a
  problem for values that are themselves comma-heavy (e.g. large
  numbers with thousands separators) - still open; no evidence yet that
  it does, and Section 2.3's no-grouping rule for numbers removes the
  most obvious case.
- Unicode/wide-character quoting edge cases: RESOLVED to follow TOON's
  rules - not merely assumed pending a concrete counterexample, but
  formally adopted verbatim from whatever TOON's own Section 7 settles.
