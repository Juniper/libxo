#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""The reading entry points: load(), loads() and iterload()."""

from . import _bind, _tree
from ._parse import iterparse


def _events(source, strict, parse_int, parse_float):
    if hasattr(source, "read") and not hasattr(source, "__iter__"):
        source = iter(lambda: source.read(65536), "")
    return iterparse(source, strict=strict, parse_int=parse_int,
                     parse_float=parse_float)


def loads(text, typ=None, *, path=None, strict=False, strict_types=False,
          parse_int=None, parse_float=None):
    """Read an RTOON document from a str.

    With no ``typ``, returns the document (or the node ``path`` names)
    as ``Record`` / ``RList`` / scalar values.  With ``typ``, converts
    it to that type; see the README for the supported types.

    path          slash-separated names of the node to return; the
                  first match wins.  It may end at a field.
    strict        enforce the writer rules too (see the README)
    strict_types  with ``typ``: no conversion of strings to numbers
                  or bools ("12" stays a str and does not fit int)
    parse_int     function to convert integer text (default int)
    parse_float   function to convert other numbers (default float)
    """
    events = _events(text, strict, parse_int, parse_float)
    lines = typ is not None
    if path is None:
        value = _tree.build_document(events, lines)
        prefix = ()
    else:
        value = _tree.find_path(events, path, lines)
        prefix = _tree.split_path(path)
    if typ is None:
        return value
    return _bind.bind(value, typ, strict=strict, strict_types=strict_types,
                      path=prefix)


def load(fp, typ=None, **kwargs):
    """Read an RTOON document from a text file; see ``loads()``."""
    return loads(fp, typ, **kwargs)


def iterload(fp, typ=None, *, path, strict=False, strict_types=False,
             parse_int=None, parse_float=None):
    """Yield each instance of the list(s) at ``path`` as it is read.

    ``fp`` is a str, a text file or an iterable of str chunks.  With
    ``typ`` (a record class, or ``list[T]`` meaning T), each instance
    is converted to it; with none, each is a ``Record``.  Instances of
    every list matching ``path`` are yielded, in document order.
    """
    events = _events(fp, strict, parse_int, parse_float)
    if typ is None:
        yield from _tree.iter_path(events, path, False)
        return
    item = _bind.item_type(typ)
    parts = _tree.split_path(path)
    for i, rec in enumerate(_tree.iter_path(events, path, True)):
        prefix = parts[:-1] + ["%s[%d]" % (parts[-1], i)]
        yield _bind.bind(rec, item, strict=strict,
                         strict_types=strict_types, path=prefix)
