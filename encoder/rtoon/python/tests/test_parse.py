#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for rtoon._parse: events, structure and the strict checks."""

import io
import textwrap
import unittest

import rtoon
from rtoon import (END_CONTAINER, END_INSTANCE, END_LIST, FIELD,
                   LEAF_LIST, START_CONTAINER, START_INSTANCE, START_LIST,
                   VERSION, Event, ParseError)


def doc(text):
    return textwrap.dedent(text).lstrip("\n")


def events(text, **kwargs):
    return list(rtoon.iterparse(doc(text), **kwargs))


def kinds(text, **kwargs):
    return [(e.kind, e.name) for e in events(text, **kwargs)]


def F(name, value, key=False, raw=None):
    if raw is None:
        raw = rtoon._bind.raw_of(value)
    return Event(FIELD, 0, name=name, value=value, raw=raw, key=key)


class TestStructure(unittest.TestCase):
    def test_version(self):
        evs = events("@version 1.0.0\na 1\n")
        self.assertEqual(evs[0], Event(VERSION, 0, value="1.0.0"))
        self.assertEqual(evs[1], F("a", 1))

    def test_container_vs_list(self):
        self.assertEqual(kinds("""
            c
              x 1
            """), [(START_CONTAINER, "c"), (FIELD, "x"),
                   (END_CONTAINER, "c")])
        self.assertEqual(kinds("""
            l
              - x 1
            """), [(START_LIST, "l"), (START_INSTANCE, None),
                   (FIELD, "x"), (END_INSTANCE, None), (END_LIST, "l")])

    def test_lookahead_skips_comments(self):
        self.assertEqual(kinds("""
            l

              # a comment
              - x 1
            """)[0], (START_LIST, "l"))

    def test_empty_container(self):
        self.assertEqual(kinds("c\nd 1\n"),
                         [(START_CONTAINER, "c"), (END_CONTAINER, "c"),
                          (FIELD, "d")])
        self.assertEqual(kinds("c\n"),
                         [(START_CONTAINER, "c"), (END_CONTAINER, "c")])

    def test_empty_lists(self):
        want = [(START_LIST, "l"), (END_LIST, "l")]
        self.assertEqual(kinds("l\n  -\n"), want)
        self.assertEqual(kinds("l -\n"), want)

    def test_empty_instance(self):
        self.assertEqual(kinds('l\n  - ""\n'),
                         [(START_LIST, "l"), (START_INSTANCE, None),
                          (END_INSTANCE, None), (END_LIST, "l")])

    def test_instance_first_field_container(self):
        got = kinds("""
            l
              - via
                  x 1
                z 2
            """)
        self.assertEqual(got, [(START_LIST, "l"), (START_INSTANCE, None),
                               (START_CONTAINER, "via"), (FIELD, "x"),
                               (END_CONTAINER, "via"), (FIELD, "z"),
                               (END_INSTANCE, None), (END_LIST, "l")])

    def test_leaf_list_event(self):
        evs = events("tags red,\"\",3,\n")
        self.assertEqual(evs[0].kind, LEAF_LIST)
        self.assertEqual(evs[0].values, ["red", "", 3])
        self.assertEqual(evs[0].raws, ["red", "", "3"])

    def test_line_numbers(self):
        evs = events("""
            # comment
            a 1

            b
              c 2
            """)
        self.assertEqual([(e.kind, e.line) for e in evs],
                         [(FIELD, 2), (START_CONTAINER, 4), (FIELD, 5),
                          (END_CONTAINER, 4)])     # END: line of START

    def test_crlf_and_chunks(self):
        text = "a 1\r\nb\r\n  c x\r\n"
        want = events("a 1\nb\n  c x\n")
        self.assertEqual(list(rtoon.iterparse(text)), want)
        chunks = [text[i:i + 3] for i in range(0, len(text), 3)]
        self.assertEqual(list(rtoon.iterparse(chunks)), want)
        self.assertEqual(list(rtoon.iterparse(io.StringIO(text))), want)


class TestDense(unittest.TestCase):
    SPARSE = """
        l
          - name=a
            n 1
            via
              x p
              y q
          - name=b
            n 2
            via
              x r
              y s
        """

    DENSE = """
        l
          {name=,n,via/x,via/y}
          a,1,p,q
          b,2,r,s
        """

    def test_same_events(self):
        sparse = events(self.SPARSE)
        dense = events(self.DENSE)
        self.assertTrue(dense[0].dense)
        self.assertFalse(sparse[0].dense)
        dense[0].dense = False
        self.assertEqual(sparse, dense)

    def test_grouping(self):
        got = kinds("""
            l
              {a,via/x,b,via/y,via/d/e}
              1,2,3,4,5
            """)
        self.assertEqual(got[2:-2],
                         [(FIELD, "a"), (START_CONTAINER, "via"),
                          (FIELD, "x"), (FIELD, "y"),
                          (START_CONTAINER, "d"), (FIELD, "e"),
                          (END_CONTAINER, "d"), (END_CONTAINER, "via"),
                          (FIELD, "b")])

    def test_widening_and_empty_cells(self):
        rows = rtoon.loads(doc("""
            l
              {a,b}
              1,
              {a,b,c}
              2,x,
            """))["l"]
        self.assertEqual(rows, [{"a": 1, "b": ""},
                                {"a": 2, "b": "x", "c": ""}])

    def test_short_row(self):
        rows = rtoon.loads("l\n  {a,b}\n  1\n")["l"]
        self.assertEqual(rows, [{"a": 1}])
        with self.assertRaises(ParseError):
            rtoon.loads("l\n  {a,b}\n  1\n", strict=True)

    def test_keys(self):
        rows = rtoon.loads("l\n  {a=,b}\n  1,2\n")["l"]
        self.assertEqual(rows[0].keys_, ("a",))
        self.assertEqual(rows.keys_, ("a",))


class TestErrors(unittest.TestCase):
    def error(self, text, line, **kwargs):
        with self.assertRaises(ParseError) as ctx:
            list(rtoon.iterparse(doc(text), **kwargs))
        self.assertEqual(ctx.exception.line, line, str(ctx.exception))
        return ctx.exception

    def test_both_modes(self):
        cases = [
            ("a\n\t  b 1\n", 2),                    # tab in indent
            ("a\n   b 1\n", 2),                     # odd indent
            ("a\n  b\n      c 1\n", 3),             # jumps two levels
            ("@frob 1\n", 1),                       # unknown instruction
            ("@version 2.0.0\n", 1),                # wrong major
            ('a "abc\n', 1),                        # unterminated
            ('a "\\q"\n', 1),                       # bad escape
            ('a "\\ud800"\n', 1),                   # surrogate
            ('a "x\x01"\n', 1),                     # raw control char
            ("l\n  {a,b}\n  1,2,3\n", 3),           # too many cells
            ("l\n  - a 1\n  -\n", 3),               # "-" after instances
        ]
        for text, line in cases:
            for strict in (False, True):
                with self.subTest(text=text, strict=strict):
                    self.error(text, line, strict=strict)

    def test_column(self):
        err = self.error('a 1\nb "x\n', 2)
        self.assertEqual(err.column, 3)
        self.assertEqual(err.text, 'b "x')

    def test_strict_only(self):
        cases = [
            ("a 1\na 2\n", 2),                       # repeated name
            ("t a,b\n", 1),                          # no trailing comma
            ("t a,,\n", 1),                          # empty last value
            ("l\n  - k=1\n  - k 2\n", 3),            # keys differ
            ("l\n  {a,b}\n  1,2\n  {a}\n  3\n", 4),  # narrowing
            ("l\n  {a,b}\n  1,2\n  {b,a,c}\n", 4),   # reordering
            ("a 1\n@version 1.0.0\n", 2),            # version not first
        ]
        for text, line in cases:
            with self.subTest(text=text):
                list(rtoon.iterparse(text))
                self.error(text, line, strict=True)

    def test_empty_instance_keys(self):
        text = 'l\n  - ""\n  - k=1\n'
        list(rtoon.iterparse(text, strict=True))

    def test_repeated_last_wins(self):
        self.assertEqual(rtoon.loads("a 1\na 2\n"), {"a": 2})


if __name__ == "__main__":
    unittest.main()
