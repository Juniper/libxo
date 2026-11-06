#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Line-level lexing of RTOON: quoting, escapes, cells and scalars.

Every function here works on one line's content (the text after its
indentation).  Errors are raised as LexError with a 0-based position
within that content; the parser turns them into ParseError with the
real line and column.  Section numbers refer to rtoon-spec.md.
"""

import re

WS = " \t"

# Section 2.3's number grammar, with the leading-zero rule built in
_NUMBER_RE = re.compile(r"-?(?:0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?\Z")

_SIMPLE_ESCAPES = {'"': '"', "\\": "\\", "n": "\n", "r": "\r", "t": "\t"}

_HEX = "0123456789abcdefABCDEF"


class LexError(Exception):
    """An error at a position within one line's content."""

    def __init__(self, msg, pos=None):
        super().__init__(msg)
        self.msg = msg
        self.pos = pos


class ExactFloat(float):
    """A float that remembers the text it was read from.

    The reader makes one only when ``repr()`` of the float would not
    give back the text written (``1.50``, ``1e3``), so that the writer
    and ``str`` fields can reproduce the original exactly.
    """

    __slots__ = ("raw",)

    def __new__(cls, raw):
        obj = super().__new__(cls, raw)
        obj.raw = raw
        return obj

    def __reduce__(self):
        return (ExactFloat, (self.raw,))


class ExactInt(int):
    """An int that remembers the text it was read from (only ``-0``).

    (An int subclass cannot have ``__slots__``, so ``raw`` lives in the
    instance dict.)
    """

    def __new__(cls, raw):
        obj = super().__new__(cls, raw)
        obj.raw = raw
        return obj

    def __reduce__(self):
        return (ExactInt, (self.raw,))


def default_int(raw):
    """Default int conversion: plain int unless the text is not canonical."""
    val = int(raw)
    if str(val) != raw:
        return ExactInt(raw)
    return val


def default_float(raw):
    """Default float conversion: plain float unless repr() differs."""
    val = float(raw)
    if repr(val) != raw:
        return ExactFloat(raw)
    return val


def number_kind(text):
    """Return "int", "float" or None for an unquoted token's text."""
    m = _NUMBER_RE.match(text)
    if m is None:
        return None
    if m.group(1) is None and m.group(2) is None:
        return "int"
    return "float"


def is_typed_literal(text):
    """True if unquoted text would read as something other than a string."""
    return text in ("true", "false", "null") or number_kind(text) is not None


def scan_quoted(s, i):
    """Scan the quoted token starting at s[i] (a double quote).

    Returns (text, j), where text is the unescaped content and j the
    index just past the closing quote.
    """
    out = []
    j = i + 1
    n = len(s)
    start = j
    while True:
        if j >= n:
            raise LexError("unterminated quoted string", i)
        ch = s[j]
        if ch == '"':
            out.append(s[start:j])
            return "".join(out), j + 1
        if ch == "\\":
            out.append(s[start:j])
            if j + 1 >= n:
                raise LexError("unterminated quoted string", i)
            esc = s[j + 1]
            if esc in _SIMPLE_ESCAPES:
                out.append(_SIMPLE_ESCAPES[esc])
                j += 2
            elif esc == "u":
                hexs = s[j + 2:j + 6]
                if len(hexs) != 4 or any(c not in _HEX for c in hexs):
                    raise LexError("bad \\u escape", j)
                cp = int(hexs, 16)
                if 0xD800 <= cp <= 0xDFFF:
                    raise LexError("surrogate \\u escape not allowed", j)
                out.append(chr(cp))
                j += 6
            else:
                raise LexError("bad escape '\\%s'" % esc, j)
            start = j
            continue
        if ord(ch) < 0x20:
            raise LexError("raw control character inside quotes", j)
        j += 1


def skip_ws(s, i):
    n = len(s)
    while i < n and s[i] in WS:
        i += 1
    return i


class Cell:
    """One scalar token: its text (unescaped), whether it was quoted,
    and its position within the line."""

    __slots__ = ("text", "quoted", "pos")

    def __init__(self, text, quoted, pos):
        self.text = text
        self.quoted = quoted
        self.pos = pos

    def __repr__(self):
        return "Cell(%r, %r, %r)" % (self.text, self.quoted, self.pos)


def split_cells(s, i=0):
    """Split s[i:] on unquoted commas.

    Returns a list of Cell; there is always at least one.  An unquoted
    cell's text is trimmed of spaces and tabs.  A quoted cell may have
    whitespace around it but nothing else.
    """
    cells = []
    n = len(s)
    while True:
        k = skip_ws(s, i)
        if k < n and s[k] == '"':
            text, j = scan_quoted(s, k)
            j = skip_ws(s, j)
            if j < n and s[j] != ",":
                raise LexError("unexpected text after quoted value", j)
            cells.append(Cell(text, True, k))
        else:
            j = k
            while j < n and s[j] != ",":
                if s[j] == '"':
                    raise LexError("unexpected quote in unquoted value", j)
                j += 1
            cells.append(Cell(s[k:j].rstrip(WS), False, k))
        if j >= n:
            return cells
        i = j + 1                       # past the comma


def has_unquoted_comma(s, i=0):
    """True if s[i:] holds a comma outside any quoted token."""
    n = len(s)
    while i < n:
        ch = s[i]
        if ch == '"':
            _, i = scan_quoted(s, i)
            continue
        if ch == ",":
            return True
        i += 1
    return False


class Field:
    """A field-line split per Section 2.1.

    kind is one of:
      "bare"   - a lone name: container or list open (or "name -")
      "value"  - a data-value or key-value; ``cells`` holds one Cell
      "leaf"   - a leaf-list; ``cells`` holds its values
    ``key`` is True for "name=value".  ``trailing`` is False for a
    leaf-list missing its trailing comma.
    """

    __slots__ = ("kind", "name", "key", "cells", "trailing", "vpos")

    def __init__(self, kind, name, key=False, cells=None, trailing=True,
                 vpos=None):
        self.kind = kind
        self.name = name
        self.key = key
        self.cells = cells
        self.trailing = trailing
        self.vpos = vpos


def scan_key(s):
    """Scan the key at the start of s.  Returns (name, j)."""
    n = len(s)
    if s and s[0] == '"':
        name, j = scan_quoted(s, 0)
        if j < n and s[j] not in WS and s[j] != "=":
            raise LexError("unexpected text after quoted name", j)
        return name, j
    j = 0
    while j < n and s[j] not in WS and s[j] != "=":
        if s[j] == '"':
            raise LexError("unexpected quote in name", j)
        j += 1
    return s[:j], j


def split_field(s):
    """Split one field-line (Section 2.1) into a Field."""
    name, j = scan_key(s)
    n = len(s)
    if j >= n:
        return Field("bare", name)

    if s[j] == "=":
        key = True
        vpos = skip_ws(s, j + 1)        # whitespace after "=" is tolerated
    else:
        key = False
        vpos = skip_ws(s, j)
        if vpos >= n:
            return Field("bare", name)

    if has_unquoted_comma(s, vpos):
        cells = split_cells(s, vpos)
        last = cells[-1]
        trailing = not last.quoted and last.text == ""
        if trailing:
            cells.pop()
            # "tags ," is a leaf-list with no values at all
            if len(cells) == 1 and not cells[0].quoted \
                    and cells[0].text == "":
                cells = []
        return Field("leaf", name, key, cells, trailing, vpos)

    cells = split_cells(s, vpos)
    return Field("value", name, key, cells, True, vpos)


def split_fields_line(s):
    """Parse a fields-line "{a,b=,c}" (Section 6.1).

    Returns a list of (name, is_key, pos).
    """
    n = len(s)
    if n < 2 or s[-1] != "}":
        raise LexError("fields-line must end with '}'", n)
    names = []
    i = 1
    if s[1:] == "}":
        return names
    while True:
        k = skip_ws(s, i)
        if k < n and s[k] == '"':
            name, j = scan_quoted(s, k)
            is_key = j < n and s[j] == "="
            if is_key:
                j += 1
            j = skip_ws(s, j)
        else:
            j = k
            while j < n and s[j] not in ",}":
                if s[j] == '"':
                    raise LexError("unexpected quote in field name", j)
                j += 1
            name = s[k:j].rstrip(WS)
            is_key = name.endswith("=")
            if is_key:
                name = name[:-1]
            if name == "":
                raise LexError("empty field name", k)
        if j >= n:
            raise LexError("fields-line must end with '}'", j)
        names.append((name, is_key, k))
        if s[j] == "}":
            if j != n - 1:
                raise LexError("unexpected text after '}'", j + 1)
            return names
        if s[j] != ",":
            raise LexError("expected ',' or '}'", j)
        i = j + 1


def type_scalar(cell, parse_int, parse_float):
    """Type one cell per Section 2.3.  Returns (value, raw)."""
    text = cell.text
    if cell.quoted:
        return text, text
    if text == "true":
        return True, text
    if text == "false":
        return False, text
    if text == "null":
        return None, text
    kind = number_kind(text)
    if kind == "int":
        return parse_int(text), text
    if kind == "float":
        return parse_float(text), text
    return text, text


# ------------------------------------------------------------------
# Writing: a port of enc_rtoon.c's quoting and escaping
# ------------------------------------------------------------------

def cp_must_escape(cp):
    """Port of rtoon_cp_must_escape()."""
    if cp < 0x80 or cp > 0xFFFF:
        return False
    return (cp <= 0xA0
            or cp == 0x1680
            or 0x2000 <= cp <= 0x200F
            or 0x2028 <= cp <= 0x202F
            or 0x205F <= cp <= 0x206F
            or cp == 0x3000 or cp == 0xFEFF
            or 0xFFF9 <= cp <= 0xFFFB
            or cp == 0xFFFE or cp == 0xFFFF)


def _has_escapable(s):
    return any(ord(c) >= 0x80 and cp_must_escape(ord(c)) for c in s)


def key_needs_quote(name):
    """Port of rtoon_key_needs_quote()."""
    if name == "" or name[0] in "{-":
        return True
    for ch in name:
        if ch in '=,"\\' or ord(ch) <= 0x20:
            return True
    return _has_escapable(name)


def value_needs_quote(value, in_delimited, is_string, is_last):
    """Port of rtoon_value_needs_quote()."""
    if value.startswith("{"):
        return True
    if value and (value[0] == " " or value[-1] == " "):
        return True
    for ch in value:
        if ch in ',"\\' or ord(ch) <= 0x1F:
            return True
    if _has_escapable(value):
        return True
    if not in_delimited and value == "":
        return True
    if in_delimited and is_last and value == "":
        return True
    if is_string and is_typed_literal(value):
        return True
    return False


_ESCAPES = {'"': '\\"', "\\": "\\\\", "\n": "\\n", "\r": "\\r",
            "\t": "\\t"}


def escape(s):
    """Port of rtoon_write_escaped()."""
    out = []
    for ch in s:
        esc = _ESCAPES.get(ch)
        if esc is not None:
            out.append(esc)
            continue
        cp = ord(ch)
        if cp <= 0x1F or cp_must_escape(cp):
            out.append("\\u%04x" % cp)
        else:
            out.append(ch)
    return "".join(out)


def quote_key(name):
    if key_needs_quote(name):
        return '"' + escape(name) + '"'
    return name


def quote_value(value, in_delimited=False, is_last=False):
    """Write a string value, quoted when it must be."""
    if value_needs_quote(value, in_delimited, True, is_last):
        return '"' + escape(value) + '"'
    return value
