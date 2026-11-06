#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for iterload() and iterparse() streaming."""

import dataclasses
import unittest

import rtoon
from rtoon import BindError, ParseError, PathError


@rtoon.record
@dataclasses.dataclass
class Row:
    id: int = rtoon.key()
    name: str = ""


def generate(count, dense, consumed=None):
    """Yield a long document a line at a time, noting how far the
    reader has pulled."""
    yield "@version 1.0.0\ntop\n  row\n"
    if dense:
        yield "    {id=,name}\n"
    for i in range(count):
        if consumed is not None:
            consumed.append(i)
        if dense:
            yield "    %d,n%d\n" % (i, i)
        else:
            yield "    - id=%d\n      name n%d\n" % (i, i)


class TestIterload(unittest.TestCase):
    def test_streams(self):
        for dense in (False, True):
            with self.subTest(dense=dense):
                consumed = []
                it = rtoon.iterload(generate(100000, dense, consumed), Row,
                                    path="top/row")
                first = next(it)
                self.assertEqual(first, Row(0, "n0"))
                # Only a line or two of lookahead has been read
                self.assertLess(len(consumed), 4)
                for _ in range(10):
                    next(it)
                self.assertLess(len(consumed), 15)
                it.close()

    def test_all(self):
        rows = list(rtoon.iterload(generate(50, True), path="top/row"))
        self.assertEqual(len(rows), 50)
        self.assertEqual(rows[49], {"id": 49, "name": "n49"})
        self.assertEqual(rows[0].keys_, ("id",))

    def test_list_type(self):
        rows = list(rtoon.iterload(generate(3, False), list[Row],
                                   path="top/row"))
        self.assertEqual(rows, [Row(0, "n0"), Row(1, "n1"), Row(2, "n2")])

    def test_every_matching_list(self):
        text = ("g\n  - name a\n    sub\n      - v 1\n      - v 2\n"
                "  - name b\n    sub\n      - v 3\n")
        got = [r["v"] for r in rtoon.iterload(text, path="g/sub")]
        self.assertEqual(got, [1, 2, 3])

    def test_bind_error_path(self):
        text = "top\n  row\n    - id=1\n    - id=x\n"
        with self.assertRaises(BindError) as ctx:
            list(rtoon.iterload(text, Row, path="top/row"))
        self.assertEqual(ctx.exception.path, "top/row[1]/id")
        self.assertEqual(ctx.exception.line, 4)

    def test_path_errors(self):
        text = "top\n  c\n    x 1\n  row\n    - id=1\n"
        for path in ("top/c", "top/c/x", "nope"):
            with self.subTest(path=path):
                with self.assertRaises(PathError):
                    list(rtoon.iterload(text, path=path))

    def test_late_parse_error(self):
        text = "top\n  row\n    - id=1\n    - id=\"2\n"
        it = rtoon.iterload(text, path="top/row")
        self.assertEqual(next(it), {"id": 1})
        with self.assertRaises(ParseError):
            next(it)


class TestIterparse(unittest.TestCase):
    def test_lazy(self):
        consumed = []
        evs = rtoon.iterparse(generate(100000, False, consumed))
        for _ in range(20):
            next(evs)
        self.assertLess(len(consumed), 10)


if __name__ == "__main__":
    unittest.main()
