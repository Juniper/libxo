#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""The writer: dump() and dumps().

Values are first normalized into the reader's own shapes (Record,
RList, a plain list for a leaf-list, scalars), then written.  Layout,
quoting and the dense widening rules follow enc_rtoon.c, so that
``dumps(loads(text)) == text`` for what the encoder writes.
"""

import collections.abc
import datetime
import decimal
import enum
import math

from . import _bind, _lex
from ._errors import DumpError
from ._lex import ExactFloat, ExactInt
from ._tree import Record, RList

VERSION_LINE = "@version 1.0.0\n"


# ------------------------------------------------------------------
# Normalizing
# ------------------------------------------------------------------

def _is_mapping_like(value):
    return isinstance(value, collections.abc.Mapping) \
        or _bind.is_record(value)


def _norm(value, where, fi=None):
    """Return the value in the reader's shapes."""
    if fi is not None and fi.dump is not None:
        value = fi.dump(value)

    if value is None or isinstance(value, (bool, str, int, float,
                                           decimal.Decimal)):
        return value
    if _bind.is_record(value):
        return _norm_record(value, where)
    if isinstance(value, RList):
        return RList([_norm_instance(v, "%s[%d]" % (where, i))
                      for i, v in enumerate(value)],
                     keys_=value.keys_, dense=value.dense)
    if isinstance(value, collections.abc.Mapping):
        if fi is not None and _bind.wants_list(fi) \
                and all(_bind.is_record(v) for v in value.values()):
            return RList([_norm_record(v, "%s[%d]" % (where, i))
                          for i, v in enumerate(value.values())])
        return _norm_mapping(value, where)
    if isinstance(value, enum.Enum):
        return _norm(value.value, where)
    if isinstance(value, (datetime.datetime, datetime.date,
                          datetime.time)):
        return value.isoformat()
    if isinstance(value, (list, tuple)):
        if fi is not None and _bind.wants_list(fi) and not value:
            return RList()
        if value and all(_is_mapping_like(v) for v in value):
            return RList([_norm_instance(v, "%s[%d]" % (where, i))
                          for i, v in enumerate(value)])
        return _norm_leaves(value, where)
    if isinstance(value, (set, frozenset)):
        try:
            items = sorted(value)
        except TypeError:
            items = list(value)
        return _norm_leaves(items, where)
    dump = _bind.custom_dump_for(value)
    if dump is not None:
        return _norm(dump(value), where)
    raise DumpError("%s: cannot write a %s" % (where or "document",
                                              type(value).__name__))


def _norm_leaves(items, where):
    out = []
    for i, item in enumerate(items):
        val = _norm(item, "%s[%d]" % (where, i))
        if isinstance(val, (dict, list)):
            raise DumpError("%s[%d]: a list mixes containers and scalars"
                            % (where, i))
        out.append(val)
    return out


def _norm_instance(value, where):
    val = _norm(value, where)
    if not isinstance(val, Record):
        raise DumpError("%s: a list instance must be a container" % where)
    return val


def _norm_mapping(value, where):
    keys = getattr(value, "keys_", ())
    rec = Record(keys_=keys)
    for name, item in value.items():
        if not isinstance(name, str):
            raise DumpError("%s: field name %r is not a str"
                            % (where or "document", name))
        rec[name] = _norm(item, _join(where, name))
    return rec


def _norm_record(obj, where):
    keys = []
    rec = Record()
    for name, item, is_key, fi in _bind.dump_fields(obj):
        if item is None:
            continue
        rec[name] = _norm(item, _join(where, name), fi)
        if is_key:
            keys.append(name)
    rec.keys_ = tuple(keys)
    return rec


def _join(where, name):
    return where + "/" + name if where else name


# ------------------------------------------------------------------
# Writing
# ------------------------------------------------------------------

def _scalar_text(value, where, in_delimited=False, is_last=False):
    """The text of one scalar, quoted when it must be."""
    if isinstance(value, str):
        if not in_delimited and value == "-":
            # "name -" would read as an empty list (spec 4.1)
            return '"-"'
        return _lex.quote_value(value, in_delimited, is_last)
    if value is True:
        return "true"
    if value is False:
        return "false"
    if value is None:
        return "null"
    if isinstance(value, (ExactInt, ExactFloat)):
        return value.raw
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if math.isnan(value) or math.isinf(value):
            raise DumpError("%s: %r cannot be written" % (where, value))
        return repr(value)
    if isinstance(value, decimal.Decimal):
        text = str(value)
        if _lex.number_kind(text) is None:
            raise DumpError("%s: Decimal %s cannot be written"
                            % (where, text))
        return text
    raise DumpError("%s: cannot write a %s" % (where,
                                              type(value).__name__))


def _write_key(name):
    # A name starting with "#" or "@" would read as a comment or an
    # instruction at the start of a line
    if name[:1] in ("#", "@"):
        return '"' + _lex.escape(name) + '"'
    return _lex.quote_key(name)


def _leaf_text(values, where):
    out = []
    last = len(values) - 1
    for i, val in enumerate(values):
        out.append(_scalar_text(val, where, True, i == last))
    return ",".join(out) + ","


class _Writer:
    def __init__(self, dense):
        self.out = []
        self.dense = dense

    def line(self, depth, text):
        self.out.append("  " * depth + text + "\n")

    def fields(self, rec, depth, where):
        for name, val in rec.items():
            self.node("  " * depth, depth, name, val,
                      name in rec.keys_, _join(where, name))

    def node(self, prefix, depth, name, val, is_key, where):
        """One field, container or list.  ``prefix`` is the text the
        line starts with (indentation, plus "- " for an instance's
        first field) and ``depth`` the node's logical depth."""
        key = _write_key(name)
        if isinstance(val, RList):
            self.out.append(prefix + key + "\n")
            self.list(val, depth + 1, where)
        elif isinstance(val, dict):
            if is_key:
                raise DumpError("%s: a key field cannot be a container"
                                % where)
            self.out.append(prefix + key + "\n")
            self.fields(val, depth + 1, where)
        elif isinstance(val, list):
            sep = "=" if is_key else " "
            self.out.append(prefix + key + sep + _leaf_text(val, where)
                            + "\n")
        else:
            sep = "=" if is_key else " "
            self.out.append(prefix + key + sep + _scalar_text(val, where)
                            + "\n")

    def list(self, lst, depth, where):
        if not lst:
            self.line(depth, "-")
            return
        want = self.dense
        if want == "auto":
            want = lst.dense
        if want:
            flat = _flatten_all(lst)
            if flat is not None:
                self.dense_rows(flat, depth, where)
                return
        for i, inst in enumerate(lst):
            self.instance(inst, depth, "%s[%d]" % (where, i))

    def instance(self, rec, depth, where):
        if not rec:
            self.line(depth, '- ""')
            return
        first = True
        for name, val in rec.items():
            prefix = "  " * depth + "- " if first else "  " * (depth + 1)
            first = False
            self.node(prefix, depth + 1, name, val, name in rec.keys_,
                      _join(where, name))

    def dense_rows(self, flat, depth, where):
        """The encoder's widening: the columns come from the first row
        and a new fields-line is written whenever a row brings a name
        not seen before.  A key mark found later on an existing column
        shows up only on the next fields-line, as in enc_rtoon.c."""
        cols = []
        index = {}
        dirty = False
        for i, cells in enumerate(flat):
            for name, _val, is_key in cells:
                col = index.get(name)
                if col is not None:
                    if is_key:
                        cols[col][1] = True
                    continue
                index[name] = len(cols)
                cols.append([name, is_key])
                dirty = True
            if dirty:
                self.line(depth, "{" + ",".join(
                    _write_key(n) + ("=" if k else "") for n, k in cols)
                    + "}")
                dirty = False
            row = [None] * len(cols)
            for name, val, _key in cells:
                col = index[name]
                if row[col] is None:
                    row[col] = _scalar_text(val, "%s[%d]/%s"
                                            % (where, i, name), True)
            text = ",".join(c or "" for c in row)
            if text == "":
                # One empty column would make a blank line, which a
                # reader skips
                text = '""'
            self.line(depth, text)


def _flatten(rec, prefix, out):
    """Add rec's fields to ``out`` as (name, value, is_key) cells.
    Returns False if the record cannot be one dense row."""
    for name, val in rec.items():
        if "/" in name:
            return False
        if isinstance(val, dict):
            if not val or name in rec.keys_:
                return False
            if not _flatten(val, prefix + name + "/", out):
                return False
        elif isinstance(val, list):
            return False
        else:
            out.append((prefix + name, val, name in rec.keys_))
    return True


def _flatten_all(lst):
    """The cells of each instance, or None if the list must be sparse:
    an instance holds a list or leaf-list, the first instance has no
    fields, or a name has a slash of its own."""
    flat = []
    for inst in lst:
        cells = []
        if not _flatten(inst, "", cells):
            return None
        flat.append(cells)
    if not flat[0]:
        return None
    return flat


def dumps(obj, *, dense="auto", version=True, path=None):
    """Return ``obj`` as RTOON text.

    obj      a Record / RList tree from loads(), plain dicts and lists,
             or record-class instances (and lists or dicts of them)
    dense    False: every list sparse; True: every list dense where it
             can be; "auto": dense where ``RList.dense`` says so
    version  write the "@version 1.0.0" line
    path     slash-separated names to wrap ``obj`` in; the last one is
             the name of ``obj`` itself
    """
    if dense not in (True, False, "auto"):
        raise ValueError("dense must be True, False or 'auto'")
    from ._tree import split_path
    parts = split_path(path)
    where = "/".join(parts) if parts else ""
    val = _norm(obj, where)
    wr = _Writer(dense)
    if version:
        wr.out.append(VERSION_LINE)
    if parts is None:
        if not isinstance(val, dict):
            raise DumpError("a document must be a container (or give "
                            "path=)")
        wr.fields(val, 0, "")
    else:
        for depth, name in enumerate(parts[:-1]):
            wr.line(depth, _write_key(name))
        wr.node("  " * (len(parts) - 1), len(parts) - 1, parts[-1], val,
                False, where)
    # Like the encoder, write the version line only above some content
    if version and len(wr.out) == 1:
        return ""
    return "".join(wr.out)


def dump(obj, fp, **kwargs):
    """Write ``obj`` as RTOON to the text file ``fp``; see dumps()."""
    fp.write(dumps(obj, **kwargs))
