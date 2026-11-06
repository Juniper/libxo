#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""The RTOON pull parser: lines in, events out.

The parser reads one meaningful line ahead, which is all it needs to
tell a container from a list (rtoon-spec.md Section 4).  Dense rows
are turned into the same events as sparse instances, and flattened
columns ("via/interface") into START_CONTAINER / END_CONTAINER around
their fields, so a consumer never needs to know which form was used.
"""

from ._errors import ParseError
from . import _lex
from ._lex import LexError

VERSION = "VERSION"
START_CONTAINER = "START_CONTAINER"
END_CONTAINER = "END_CONTAINER"
START_LIST = "START_LIST"
END_LIST = "END_LIST"
START_INSTANCE = "START_INSTANCE"
END_INSTANCE = "END_INSTANCE"
FIELD = "FIELD"
LEAF_LIST = "LEAF_LIST"

SUPPORTED_MAJOR = 1


class Event:
    """One parser event.

    ``kind`` is one of the module constants (START_LIST, FIELD, ...).
    ``line`` is the line it came from.  Other attributes depend on the
    kind; those that do not apply are None:

      name     START_*/END_* (not instances), FIELD, LEAF_LIST
      dense    START_LIST: True if the list is written in dense form
      value    VERSION (the version string), FIELD (the typed value)
      raw      FIELD: the exact text of the value
      key      FIELD, LEAF_LIST: True if written as a key
      values   LEAF_LIST: the typed values
      raws     LEAF_LIST: the exact text of each value
    """

    __slots__ = ("kind", "line", "name", "dense", "value", "raw", "key",
                 "values", "raws")

    def __init__(self, kind, line, name=None, dense=None, value=None,
                 raw=None, key=None, values=None, raws=None):
        self.kind = kind
        self.line = line
        self.name = name
        self.dense = dense
        self.value = value
        self.raw = raw
        self.key = key
        self.values = values
        self.raws = raws

    def _tuple(self):
        return (self.kind, self.name, self.dense, self.value, self.raw,
                self.key, self.values, self.raws)

    def __eq__(self, other):
        if not isinstance(other, Event):
            return NotImplemented
        return self._tuple() == other._tuple()

    def __hash__(self):
        return hash((self.kind, self.name))

    def __repr__(self):
        parts = [self.kind]
        for attr in ("name", "dense", "value", "raw", "key", "values",
                     "raws"):
            val = getattr(self, attr)
            if val is not None:
                parts.append("%s=%r" % (attr, val))
        parts.append("line=%r" % self.line)
        return "Event(" + ", ".join(parts) + ")"


class _Line:
    """A meaningful (non-blank, non-comment) line."""

    __slots__ = ("lineno", "depth", "col", "content", "text")

    def __init__(self, lineno, depth, col, content, text):
        self.lineno = lineno
        self.depth = depth
        self.col = col          # 0-based column where content starts
        self.content = content
        self.text = text


def _chunks_to_lines(source):
    """Yield complete lines (without newline) from a str or an iterable
    of str chunks of any size (a text file iterates as lines)."""
    if isinstance(source, str):
        source = (source,)
    pending = ""
    for chunk in source:
        if not isinstance(chunk, str):
            raise TypeError("RTOON input must be text, not %s"
                            % type(chunk).__name__)
        if pending:
            chunk = pending + chunk
        parts = chunk.split("\n")
        pending = parts.pop()
        yield from parts
    if pending:
        yield pending


class _Lines:
    """Meaningful lines with one line of lookahead.

    Blank and comment lines are dropped here; instructions are handled
    here too and reported through the ``versions`` list.
    """

    def __init__(self, source, strict):
        self._it = _chunks_to_lines(source)
        self._lineno = 0
        self._strict = strict
        self._seen_data = False
        self._seen_version = False
        self.pending_versions = []
        self.peek = None
        self.advance()

    def error(self, msg, line, pos=None):
        col = None if pos is None else line.col + pos + 1
        raise ParseError(msg, line.lineno, col, line.text)

    def advance(self):
        """Move to the next meaningful line; return the old one."""
        old = self.peek
        self.peek = self._read()
        return old

    def _read(self):
        for text in self._it:
            self._lineno += 1
            if text.endswith("\r"):
                text = text[:-1]
            n = len(text)
            i = 0
            while i < n and text[i] == " ":
                i += 1
            rest = text[i:]
            stripped = rest.rstrip(" \t")
            if stripped == "":
                continue
            if stripped[0] == "\t":
                raise ParseError("tab in indentation", self._lineno, i + 1,
                                 text)
            if stripped[0] == "#":
                continue
            if stripped[0] == "@":
                self._instruction(stripped, text, i)
                continue
            if i % 2:
                raise ParseError("indentation is not a multiple of two "
                                 "spaces", self._lineno, i + 1, text)
            self._seen_data = True
            return _Line(self._lineno, i // 2, i, stripped, text)
        return None

    def _instruction(self, s, text, col):
        line = _Line(self._lineno, 0, col, s, text)
        parts = s[1:].split(None, 1)
        word = parts[0] if parts else ""
        if word != "version":
            self.error("unknown instruction '@%s'" % word, line, 0)
        if len(parts) != 2:
            self.error("@version needs a version number", line, 0)
        value = parts[1].strip()
        major = value.split(".", 1)[0]
        if not major.isdigit():
            self.error("bad @version '%s'" % value, line, 0)
        if int(major) != SUPPORTED_MAJOR:
            self.error("unsupported RTOON version '%s'" % value, line, 0)
        if self._strict and (self._seen_data or self._seen_version):
            self.error("@version must be the first line", line, 0)
        self._seen_version = True
        self.pending_versions.append(Event(VERSION, self._lineno,
                                           value=value))


class _FlatGroup:
    """The nesting plan for one dense fields-line: flattened names are
    grouped by their prefix, wherever they appear in the line."""

    __slots__ = ("name", "items")

    def __init__(self, name):
        self.name = name
        self.items = []     # (index, leaf-name, is_key) or _FlatGroup


def _flat_plan(fields):
    root = _FlatGroup(None)
    groups = {}
    for idx, (name, is_key, _pos) in enumerate(fields):
        parts = name.split("/")
        if len(parts) == 1 or "" in parts:
            root.items.append((idx, name, is_key))
            continue
        node = root
        path = ()
        for part in parts[:-1]:
            path += (part,)
            sub = groups.get(path)
            if sub is None:
                sub = _FlatGroup(part)
                groups[path] = sub
                node.items.append(sub)
            node = sub
        node.items.append((idx, parts[-1], is_key))
    return root


class Parser:
    """Pull parser over RTOON text; iterate it to get events."""

    def __init__(self, source, strict=False, parse_int=None,
                 parse_float=None):
        self._strict = strict
        self._parse_int = parse_int or _lex.default_int
        self._parse_float = parse_float or _lex.default_float
        self._lines = _Lines(source, strict)

    def __iter__(self):
        return self._document()

    # -- helpers --------------------------------------------------------

    def _error(self, msg, line, pos=None):
        self._lines.error(msg, line, pos)

    def _lex(self, func, line, *args):
        try:
            return func(line.content, *args)
        except LexError as err:
            self._error(err.msg, line, err.pos)

    def _scalar(self, cell):
        return _lex.type_scalar(cell, self._parse_int, self._parse_float)

    def _versions(self):
        pend = self._lines.pending_versions
        while pend:
            yield pend.pop(0)

    def _next_line(self):
        """Consume the lookahead line, first yielding any instructions
        read on the way to it."""
        return self._lines.advance()

    # -- structure ------------------------------------------------------

    def _document(self):
        yield from self._versions()
        yield from self._fields(0, set())
        yield from self._versions()

    def _fields(self, depth, names):
        """Parse the field-lines at exactly ``depth``.  ``names`` tracks
        the names seen in this container, for the strict check."""
        lines = self._lines
        while True:
            yield from self._versions()
            line = lines.peek
            if line is None or line.depth < depth:
                return
            if line.depth > depth:
                self._error("unexpected indentation", line)
            lines.advance()
            yield from self._field(line, names)

    def _check_name(self, names, name, line):
        if self._strict:
            if name in names:
                self._error("repeated name '%s'" % name, line)
            names.add(name)

    def _field(self, line, names, keys=None):
        """Parse one field-line.  ``keys`` (sparse instances only)
        collects (name, is_key) for the strict key check."""
        content = line.content
        if content == "-" or content.startswith("- "):
            self._error("list instance outside a list", line)
        if content[0] == "{":
            self._error("fields-line outside a list", line)

        fld = self._lex(_lex.split_field, line)
        self._check_name(names, fld.name, line)

        if fld.kind == "bare":
            yield from self._node(fld.name, line)
            return

        if keys is not None:
            keys.append((fld.name, fld.key))

        if fld.kind == "leaf":
            if self._strict:
                if not fld.trailing:
                    self._error("leaf-list must end with a comma", line)
                if fld.cells and not fld.cells[-1].quoted \
                        and fld.cells[-1].text == "":
                    self._error("empty last leaf-list value must be "
                                "written \"\"", line, fld.cells[-1].pos)
            values = []
            raws = []
            for cell in fld.cells:
                val, raw = self._scalar(cell)
                values.append(val)
                raws.append(raw)
            yield Event(LEAF_LIST, line.lineno, name=fld.name, key=fld.key,
                        values=values, raws=raws)
            return

        cell = fld.cells[0]
        if not cell.quoted and not fld.key and cell.text == "-":
            # "name -": an empty list (Section 4.1)
            nxt = self._lines.peek
            if nxt is not None and nxt.depth > line.depth:
                self._error("content after an empty list", nxt)
            yield Event(START_LIST, line.lineno, name=fld.name, dense=False)
            yield Event(END_LIST, line.lineno, name=fld.name)
            return
        if self._strict and not cell.quoted and cell.text == "":
            self._error("empty value must be written \"\"", line, cell.pos)
        val, raw = self._scalar(cell)
        yield Event(FIELD, line.lineno, name=fld.name, value=val, raw=raw,
                    key=fld.key)

    def _node(self, name, line):
        """A bare name: decide container or list from the next line."""
        lines = self._lines
        depth = line.depth
        nxt = lines.peek
        if nxt is None or nxt.depth <= depth:
            yield Event(START_CONTAINER, line.lineno, name=name)
            yield Event(END_CONTAINER, line.lineno, name=name)
            return
        if nxt.depth > depth + 1:
            self._error("unexpected indentation", nxt)

        content = nxt.content
        if content == "-":
            lines.advance()
            after = lines.peek
            if after is not None and after.depth > depth:
                self._error("content after an empty-list marker", after)
            yield Event(START_LIST, line.lineno, name=name, dense=False)
            yield Event(END_LIST, nxt.lineno, name=name)
        elif content.startswith("- "):
            yield Event(START_LIST, line.lineno, name=name, dense=False)
            yield from self._sparse(depth + 1)
            yield Event(END_LIST, line.lineno, name=name)
        elif content[0] == "{":
            yield Event(START_LIST, line.lineno, name=name, dense=True)
            yield from self._dense(depth + 1)
            yield Event(END_LIST, line.lineno, name=name)
        else:
            yield Event(START_CONTAINER, line.lineno, name=name)
            yield from self._fields(depth + 1, set())
            yield Event(END_CONTAINER, line.lineno, name=name)

    def _sparse(self, depth):
        """Instances ("- ...") at ``depth``."""
        lines = self._lines
        list_keys = None
        while True:
            yield from self._versions()
            line = lines.peek
            if line is None or line.depth < depth:
                return
            if line.depth > depth:
                self._error("unexpected indentation", line)
            content = line.content
            if content == "-":
                self._error("empty-list marker in a list that has "
                            "instances", line)
            if not content.startswith("- "):
                self._error("expected a list instance ('- ')", line)
            lines.advance()

            yield Event(START_INSTANCE, line.lineno)
            rest = content[2:]
            names = set()
            keys = []
            if rest == '""':
                pass                    # an empty instance (Section 4.2)
            elif rest == "" or rest[0] in " \t":
                self._error("expected a field after '- '", line, 2)
            else:
                first = _Line(line.lineno, depth + 1, line.col + 2, rest,
                              line.text)
                yield from self._field(first, names, keys)
                yield from self._instance_fields(depth + 1, names, keys)
            # An empty instance has no fields to mark, so it says
            # nothing about the list's keys
            if self._strict and rest != '""':
                list_keys = self._check_keys(list_keys, keys, line)
            yield Event(END_INSTANCE, line.lineno)

    def _instance_fields(self, depth, names, keys):
        lines = self._lines
        while True:
            yield from self._versions()
            line = lines.peek
            if line is None or line.depth < depth:
                return
            if line.depth > depth:
                self._error("unexpected indentation", line)
            lines.advance()
            yield from self._field(line, names, keys)

    def _check_keys(self, list_keys, keys, line):
        """Strict: every instance marks the same key fields (4.2)."""
        mine = {name for name, is_key in keys if is_key}
        if list_keys is None:
            return mine
        if mine != list_keys:
            self._error("instance keys (%s) differ from the list's (%s)"
                        % (",".join(sorted(mine)),
                           ",".join(sorted(list_keys))), line)
        return list_keys

    def _dense(self, depth):
        """Fields-lines and rows at ``depth``."""
        lines = self._lines
        fields = None
        plan = None
        while True:
            yield from self._versions()
            line = lines.peek
            if line is None or line.depth < depth:
                return
            if line.depth > depth:
                self._error("unexpected indentation", line)
            lines.advance()
            content = line.content

            if content[0] == "{":
                new = self._lex(_lex.split_fields_line, line)
                if self._strict:
                    self._check_widen(fields, new, line)
                fields = new
                plan = _flat_plan(fields)
                continue

            if fields is None:
                self._error("row before any fields-line", line)
            cells = self._lex(_lex.split_cells, line)
            if len(cells) > len(fields):
                self._error("row has %d cells, fields-line has %d"
                            % (len(cells), len(fields)), line,
                            cells[len(fields)].pos)
            if self._strict and len(cells) != len(fields):
                self._error("row has %d cells, fields-line has %d"
                            % (len(cells), len(fields)), line)

            typed = []
            for cell in cells:
                typed.append(self._scalar(cell))
            yield Event(START_INSTANCE, line.lineno)
            yield from self._row(plan, typed, line.lineno)
            yield Event(END_INSTANCE, line.lineno)

    def _row(self, group, typed, lineno):
        for item in group.items:
            if isinstance(item, _FlatGroup):
                yield Event(START_CONTAINER, lineno, name=item.name)
                yield from self._row(item, typed, lineno)
                yield Event(END_CONTAINER, lineno, name=item.name)
                continue
            idx, name, is_key = item
            if idx >= len(typed):
                continue                # lenient: a short row
            val, raw = typed[idx]
            yield Event(FIELD, lineno, name=name, value=val, raw=raw,
                        key=is_key)

    def _check_widen(self, old, new, line):
        """Strict: a fields-line extends the one before it (6.2)."""
        seen = set()
        for name, _key, pos in new:
            if name in seen:
                self._error("repeated field name '%s'" % name, line, pos)
            seen.add(name)
        if old is None:
            return
        if len(new) < len(old):
            self._error("fields-line drops fields from the one before it",
                        line)
        for (oname, okey, _), (nname, nkey, pos) in zip(old, new):
            if oname != nname:
                self._error("fields-line reorders or renames '%s'" % oname,
                            line, pos)
            if okey != nkey:
                self._error("fields-line changes the key marking of '%s'"
                            % oname, line, pos)


def iterparse(source, *, strict=False, parse_int=None, parse_float=None):
    """Yield the events of an RTOON document.

    ``source`` is a str, a text file, or any iterable of str chunks
    (they need not be whole lines).  See ``Event`` for what each event
    carries.
    """
    if hasattr(source, "read") and not hasattr(source, "__iter__"):
        source = iter(lambda: source.read(65536), "")
    return iter(Parser(source, strict=strict, parse_int=parse_int,
                       parse_float=parse_float))
