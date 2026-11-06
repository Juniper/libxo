#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for rtoon._lex: scalars, quoting, escapes, field splitting."""

import unittest

from rtoon import _lex
from rtoon._lex import Cell, LexError


def typed(text, quoted=False):
    return _lex.type_scalar(Cell(text, quoted, 0), _lex.default_int,
                            _lex.default_float)[0]


class TestScalars(unittest.TestCase):
    def test_numbers(self):
        for text, want in [("0", 0), ("42", 42), ("-7", -7),
                           ("0.5", 0.5), ("-0.5", -0.5), ("1e3", 1000.0),
                           ("2.5E-2", 0.025), ("0e1", 0.0),
                           ("3.14", 3.14), ("-1.5e+10", -1.5e10)]:
            with self.subTest(text=text):
                val = typed(text)
                self.assertEqual(val, want)
                self.assertIs(type(val) is bool, False)

    def test_int_vs_float(self):
        self.assertIsInstance(typed("42"), int)
        self.assertIsInstance(typed("4.0"), float)
        self.assertIsInstance(typed("4e0"), float)

    def test_leading_zeros_are_strings(self):
        for text in ("05", "0042", "-01", "00", "1.", ".5", "+1", "1e",
                     "0x10", "1_000", "--1"):
            with self.subTest(text=text):
                self.assertEqual(typed(text), text)

    def test_literals(self):
        self.assertIs(typed("true"), True)
        self.assertIs(typed("false"), False)
        self.assertIsNone(typed("null"))
        self.assertEqual(typed("True"), "True")
        self.assertEqual(typed("NULL"), "NULL")

    def test_quoted_stays_string(self):
        for text in ("true", "42", "null", "", "1e3"):
            with self.subTest(text=text):
                self.assertEqual(typed(text, True), text)

    def test_exact_text_kept(self):
        val = typed("1.50")
        self.assertEqual(val, 1.5)
        self.assertEqual(val.raw, "1.50")
        self.assertIs(type(typed("1.5")), float)
        self.assertEqual(typed("-0").raw, "-0")
        self.assertIs(type(typed("12")), int)


class TestQuoted(unittest.TestCase):
    def scan(self, s):
        return _lex.scan_quoted(s, 0)[0]

    def test_escapes(self):
        self.assertEqual(self.scan(r'"a\"b\\c\nd\re\tf"'),
                         'a"b\\c\nd\re\tf')
        self.assertEqual(self.scan(r'"\u0001"'), "\x01")
        self.assertEqual(self.scan(r'"éé"'), "éé")
        self.assertEqual(self.scan('""'), "")

    def test_end_index(self):
        self.assertEqual(_lex.scan_quoted('x "ab" y', 2), ("ab", 6))

    def test_errors(self):
        for s in ('"abc', '"ab\\', r'"\x41"', r'"\u12"', r'"\ud83d"',
                  '"a\x01b"', r'"\uDFFF"'):
            with self.subTest(s=s):
                with self.assertRaises(LexError):
                    self.scan(s)


class TestSplitField(unittest.TestCase):
    def test_bare(self):
        fld = _lex.split_field("name")
        self.assertEqual((fld.kind, fld.name), ("bare", "name"))
        self.assertEqual(_lex.split_field("name   ").kind, "bare")

    def test_data_value(self):
        fld = _lex.split_field("name some value  ")
        self.assertEqual(fld.kind, "value")
        self.assertFalse(fld.key)
        self.assertEqual(fld.cells[0].text, "some value")

    def test_key_value(self):
        fld = _lex.split_field("name=k1")
        self.assertTrue(fld.key)
        self.assertEqual(fld.cells[0].text, "k1")

    def test_space_after_equals(self):
        fld = _lex.split_field("name=  k1")
        self.assertTrue(fld.key)
        self.assertEqual(fld.cells[0].text, "k1")

    def test_value_starting_with_equals(self):
        fld = _lex.split_field("op =x")
        self.assertFalse(fld.key)
        self.assertEqual(fld.cells[0].text, "=x")

    def test_quoted_keys(self):
        for s, name in [('"a=b" 1', "a=b"), ('"a,b" 1', "a,b"),
                        ('"a b" 1', "a b"), ('"{x" 1', "{x"),
                        ('"-x" 1', "-x"), ('"k"=v', "k")]:
            with self.subTest(s=s):
                self.assertEqual(_lex.split_field(s).name, name)

    def test_quoted_value(self):
        fld = _lex.split_field('msg "a, b"')
        self.assertEqual(fld.kind, "value")
        self.assertTrue(fld.cells[0].quoted)
        self.assertEqual(fld.cells[0].text, "a, b")

    def test_text_after_quoted_value(self):
        with self.assertRaises(LexError):
            _lex.split_field('msg "a" b')

    def leaf(self, s):
        fld = _lex.split_field(s)
        self.assertEqual(fld.kind, "leaf")
        return [(c.text, c.quoted) for c in fld.cells], fld.trailing

    def test_leaf_lists(self):
        self.assertEqual(self.leaf("c red,"), ([("red", False)], True))
        self.assertEqual(self.leaf("c a,,c,"),
                         ([("a", False), ("", False), ("c", False)], True))
        self.assertEqual(self.leaf('c red,"",'),
                         ([("red", False), ("", True)], True))
        self.assertEqual(self.leaf('c "x,y",z,'),
                         ([("x,y", True), ("z", False)], True))
        self.assertEqual(self.leaf("c ,"), ([], True))
        self.assertEqual(self.leaf("c a,b"),
                         ([("a", False), ("b", False)], False))

    def test_leaf_list_key(self):
        fld = _lex.split_field("c=a,b,")
        self.assertEqual(fld.kind, "leaf")
        self.assertTrue(fld.key)


class TestFieldsLine(unittest.TestCase):
    def test_plain(self):
        got = _lex.split_fields_line("{a=,b,via/c}")
        self.assertEqual([(n, k) for n, k, _ in got],
                         [("a", True), ("b", False), ("via/c", False)])

    def test_quoted(self):
        got = _lex.split_fields_line('{"a b"=,"c,d"}')
        self.assertEqual([(n, k) for n, k, _ in got],
                         [("a b", True), ("c,d", False)])

    def test_empty(self):
        self.assertEqual(_lex.split_fields_line("{}"), [])

    def test_errors(self):
        for s in ("{a,b", "{a,,b}", "{a}x", '{a"b}'):
            with self.subTest(s=s):
                with self.assertRaises(LexError):
                    _lex.split_fields_line(s)


class TestWriting(unittest.TestCase):
    def test_value_quoting(self):
        q = _lex.quote_value
        self.assertEqual(q("plain"), "plain")
        self.assertEqual(q(""), '""')
        self.assertEqual(q("42"), '"42"')
        self.assertEqual(q("true"), '"true"')
        self.assertEqual(q("a,b"), '"a,b"')
        self.assertEqual(q(" x"), '" x"')
        self.assertEqual(q("x "), '"x "')
        self.assertEqual(q("{x"), '"{x"')
        self.assertEqual(q('a"b'), '"a\\"b"')
        self.assertEqual(q("a\nb"), '"a\\nb"')
        self.assertEqual(q("\x01"), '"\\u0001"')
        self.assertEqual(q(" "), '"\\u00a0"')
        self.assertEqual(q("café"), "café")

    def test_delimited(self):
        q = _lex.quote_value
        self.assertEqual(q("", in_delimited=True), "")
        self.assertEqual(q("", in_delimited=True, is_last=True), '""')

    def test_key_quoting(self):
        q = _lex.quote_key
        self.assertEqual(q("name"), "name")
        for name in ("", "a b", "a=b", "a,b", "-x", "{x", 'a"b'):
            with self.subTest(name=name):
                self.assertTrue(q(name).startswith('"'))

    def test_round_trip(self):
        for s in ("", "x", "a,b", " x ", "a\"b\\c", "\t\n\r\x01\x1f",
                  " ﻿", "\U0001f600", "42", "{"):
            with self.subTest(s=s):
                text = _lex.quote_value(s)
                if text.startswith('"'):
                    self.assertEqual(_lex.scan_quoted(text, 0)[0], s)
                else:
                    self.assertEqual(text, s)


if __name__ == "__main__":
    unittest.main()
