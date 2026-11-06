#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Generic tree building: RTOON events to Record / RList values."""

from ._errors import PathError
from . import _parse as P


class Record(dict):
    """A container, list instance or document, as a dict.

    ``keys_`` is a tuple of the names of the fields written as keys.
    ``line_`` is the line the record started on (None if unknown).
    """

    def __init__(self, *args, keys_=(), line_=None, **kwargs):
        super().__init__(*args, **kwargs)
        self.keys_ = tuple(keys_)
        self.line_ = line_

    def __repr__(self):
        return "Record(%s)" % dict.__repr__(self)

    def copy(self):
        return Record(self, keys_=self.keys_, line_=self.line_)


class RList(list):
    """A list of Records.

    ``keys_`` is the list's key fields (taken from its first instance)
    and ``dense`` is True when the list was written in dense form.
    """

    def __init__(self, *args, keys_=(), dense=False, line_=None):
        super().__init__(*args)
        self.keys_ = tuple(keys_)
        self.dense = dense
        self.line_ = line_

    def __repr__(self):
        return "RList(%s)" % list.__repr__(self)

    def copy(self):
        return RList(self, keys_=self.keys_, dense=self.dense,
                     line_=self.line_)


class _Builder:
    """Builds one value from the events of one node.

    Feed it events starting with the node's own START event (or a
    START_INSTANCE); ``done`` turns true when the node ends and
    ``result`` holds the value.  With ``lines`` set, each Record also
    gets ``lines_``, a dict from field name to line number, for
    error messages in the binding layer.
    """

    __slots__ = ("stack", "result", "done", "lines")

    def __init__(self, lines=False):
        self.stack = []
        self.result = None
        self.done = False
        self.lines = lines

    def _new_record(self, line):
        rec = Record(line_=line)
        if self.lines:
            rec.lines_ = {}
        return rec

    def _add(self, name, value, key, line):
        top = self.stack[-1]
        rec = top[0]
        if isinstance(rec, RList):
            rec.append(value)
            return
        rec[name] = value
        if key:
            top[1].append(name)
        if self.lines:
            rec.lines_[name] = line

    def feed(self, ev):
        kind = ev.kind
        if kind == P.FIELD:
            self._add(ev.name, ev.value, ev.key, ev.line)
        elif kind == P.LEAF_LIST:
            self._add(ev.name, list(ev.values), ev.key, ev.line)
        elif kind == P.START_CONTAINER or kind == P.START_INSTANCE:
            self.stack.append([self._new_record(ev.line), [], ev.name])
        elif kind == P.START_LIST:
            self.stack.append([RList(dense=ev.dense, line_=ev.line), None,
                               ev.name])
        elif kind == P.END_CONTAINER or kind == P.END_INSTANCE:
            rec, keys, name = self.stack.pop()
            if keys:
                rec.keys_ = tuple(dict.fromkeys(keys))
            self._finish(rec, name, ev.line)
        elif kind == P.END_LIST:
            lst, _, name = self.stack.pop()
            if lst:
                lst.keys_ = lst[0].keys_
            self._finish(lst, name, ev.line)

    def _finish(self, value, name, line):
        if not self.stack:
            self.result = value
            self.done = True
            return
        top = self.stack[-1][0]
        if isinstance(top, RList):
            top.append(value)
        else:
            self._add(name, value, False, line)


def build_document(events, lines=False):
    """Build the whole document as a Record."""
    bld = _Builder(lines)
    bld.stack.append([bld._new_record(1), [], None])
    for ev in events:
        if ev.kind == P.VERSION:
            continue
        bld.feed(ev)
    rec, keys, _ = bld.stack.pop()
    if keys:
        rec.keys_ = tuple(dict.fromkeys(keys))
    return rec


def split_path(path):
    if path is None:
        return None
    if isinstance(path, str):
        parts = [p for p in path.split("/") if p != ""]
    else:
        parts = list(path)
    if not parts:
        raise PathError("empty path")
    return parts


class _PathTracker:
    """Tracks the names of the open containers and lists."""

    def __init__(self, parts):
        self.parts = parts
        self.names = []

    def at(self):
        return self.names == self.parts


def find_path(events, path, lines=False):
    """Build the node at ``path``; the rest of the document is still
    read, so errors later on are reported.  The first match wins.
    A path may end at a field, in which case its value is returned."""
    parts = split_path(path)
    names = []
    bld = None
    found = False
    result = None
    for ev in events:
        if bld is not None:
            bld.feed(ev)
            if bld.done:
                result = bld.result
                found = True
                bld = None
                names.pop()
            continue
        kind = ev.kind
        if kind in (P.START_CONTAINER, P.START_LIST):
            names.append(ev.name)
            if not found and names == parts:
                bld = _Builder(lines)
                bld.feed(ev)
        elif kind in (P.END_CONTAINER, P.END_LIST):
            names.pop()
        elif kind in (P.FIELD, P.LEAF_LIST):
            if not found and names + [ev.name] == parts:
                found = True
                result = (ev.value if kind == P.FIELD
                          else list(ev.values))
    if not found:
        raise PathError("path '%s' not found" % "/".join(parts))
    return result


def iter_path(events, path, lines=False):
    """Yield each instance of every list at ``path``, as it ends."""
    parts = split_path(path)
    names = []
    depth_in = None         # len(names) when inside a matching list
    bld = None
    matched = False
    for ev in events:
        if bld is not None:
            bld.feed(ev)
            if bld.done:
                yield bld.result
                bld = None
            continue
        kind = ev.kind
        if kind in (P.START_CONTAINER, P.START_LIST):
            names.append(ev.name)
            if names == parts:
                if kind == P.START_CONTAINER:
                    raise PathError("path '%s' names a container, not a "
                                    "list" % "/".join(parts))
                matched = True
                depth_in = len(names)
        elif kind in (P.END_CONTAINER, P.END_LIST):
            if depth_in == len(names):
                depth_in = None
            names.pop()
        elif kind == P.START_INSTANCE and depth_in == len(names):
            bld = _Builder(lines)
            bld.feed(ev)
        elif kind in (P.FIELD, P.LEAF_LIST) and names + [ev.name] == parts:
            raise PathError("path '%s' names a field, not a list"
                            % "/".join(parts))
    if not matched:
        raise PathError("path '%s' not found" % "/".join(parts))
