#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for dumps() / dump()."""

import dataclasses
import datetime
import decimal
import enum
import io
import os
import textwrap
import unittest

import rtoon
from rtoon import DumpError, Record, RList

DATA = os.path.join(os.path.dirname(__file__), "data")


def data(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as fp:
        return fp.read()


def doc(text):
    return "@version 1.0.0\n" + textwrap.dedent(text).lstrip("\n")


@rtoon.record
@dataclasses.dataclass
class Item:
    sku: str = rtoon.key()
    name: str = ""
    sold: int = 0
    in_stock: int = 0
    note: str | None = None


@rtoon.record
@dataclasses.dataclass
class Group:
    name: str = rtoon.key()
    items: list[Item] = dataclasses.field(default_factory=list)
    by_sku: dict[str, Item] = dataclasses.field(default_factory=dict)


class Color(enum.Enum):
    RED = "red"


class TestRoundTrip(unittest.TestCase):
    TEXTS = [
        data("inventory.rtoon"),
        data("interfaces.rtoon"),
        doc("""
            a 1
            b "42"
            c ""
            d "a, b"
            e " x"
            f 1.50
            g -0
            h null
            i "true"
            j "-"
            "k k" v
            l=key
            m "line\\none"
            n "\\u00a0"
            """),
        doc("""
            l
              {a=,v/x,b}
              1,2,3
              5,,
              {a=,v/x,b,v/y}
              6,7,8,9
              {a=,v/x,b,v/y,c}
              10,11,12,13,14
            e
              -
            """),
        doc("""
            l
              - via
                  x 1
                z 2
              - ""
              - t a,"",
            """),
    ]

    def test_dumps_loads(self):
        for text in self.TEXTS:
            with self.subTest(text=text):
                self.assertEqual(rtoon.dumps(rtoon.loads(text)), text)

    def test_loads_dumps(self):
        values = [
            {"a": 1, "b": "x", "c": [1, "2", ""], "d": {"e": None}},
            {"l": [{"x": 1}, {"x": 2, "y": {"z": True}}], "e": {}},
            {"f": 1.25, "s": "", "t": [], "u": " pad "},
        ]
        for val in values:
            with self.subTest(val=val):
                self.assertEqual(rtoon.loads(rtoon.dumps(val)), val)


class TestLayout(unittest.TestCase):
    def test_plain(self):
        got = rtoon.dumps({"top": {"n": 1, "tags": ["a", "b"],
                                   "l": [{"k": "v"}], "e": []}})
        self.assertEqual(got, doc("""
            top
              n 1
              tags a,b,
              l
                - k v
              e ,
            """))

    def test_dense_option(self):
        val = {"l": [{"a": 1, "b": {"c": 2}}, {"a": 3, "b": {"c": 4}}]}
        self.assertEqual(rtoon.dumps(val, dense=True), doc("""
            l
              {a,b/c}
              1,2
              3,4
            """))
        sparse = rtoon.dumps(val)
        self.assertEqual(rtoon.dumps(rtoon.loads(sparse), dense=True),
                         rtoon.dumps(val, dense=True))
        dense = rtoon.loads(rtoon.dumps(val, dense=True))
        self.assertEqual(rtoon.dumps(dense, dense=False), sparse)

    def test_cannot_be_dense(self):
        val = {"l": [{"a": 1, "t": ["x", "y"]}]}
        self.assertEqual(rtoon.dumps(val, dense=True),
                         rtoon.dumps(val, dense=False))
        val = {"l": [{}, {"a": 1}]}
        self.assertEqual(rtoon.dumps(val, dense=True),
                         rtoon.dumps(val, dense=False))

    def test_widening_key_mark(self):
        rows = RList([Record({"a": 1}), Record({"a": 2, "b": 3},
                                               keys_=("a",))], dense=True)
        self.assertEqual(rtoon.dumps({"l": rows}), doc("""
            l
              {a}
              1
              {a=,b}
              2,3
            """))

    def test_one_empty_column(self):
        rows = RList([Record({"a": "x"}), Record({"a": ""})], dense=True)
        text = rtoon.dumps({"l": rows})
        self.assertEqual(text, doc("""
            l
              {a}
              x
              ""
            """))
        self.assertEqual(rtoon.loads(text)["l"][1], {"a": ""})

    def test_path(self):
        items = rtoon.loads(data("inventory.rtoon"), path="top/data/item")
        self.assertEqual(rtoon.dumps(items, path="top/data/item"),
                         data("inventory.rtoon"))
        self.assertEqual(rtoon.dumps(3, path="a/b", version=False),
                         "a\n  b 3\n")

    def test_version(self):
        self.assertEqual(rtoon.dumps({"a": 1}, version=False), "a 1\n")
        self.assertEqual(rtoon.dumps({}), "")

    def test_scalars(self):
        got = rtoon.dumps({"d": decimal.Decimal("1.10"), "c": Color.RED,
                           "t": datetime.date(2026, 11, 5),
                           "s": {"b", "a"}, "q": ("x",)},
                          version=False)
        self.assertEqual(got, "d 1.10\nc red\nt 2026-11-05\ns a,b,\n"
                              "q x,\n")

    def test_quoting_names(self):
        got = rtoon.dumps({"#x": 1, "@y": 2, "a b": 3}, version=False)
        self.assertEqual(rtoon.loads(got), {"#x": 1, "@y": 2, "a b": 3})

    def test_dump_file(self):
        fp = io.StringIO()
        rtoon.dump({"a": 1}, fp)
        self.assertEqual(fp.getvalue(), doc("a 1\n"))


class TestRecords(unittest.TestCase):
    def test_record_objects(self):
        grp = Group("g1", [Item("a", "gum", 3, note="hi"), Item("b")],
                    {"c": Item("c", "x")})
        text = rtoon.dumps({"group": grp})
        self.assertEqual(text, doc("""
            group
              name=g1
              items
                - sku=a
                  name gum
                  sold 3
                  in-stock 0
                  note hi
                - sku=b
                  name ""
                  sold 0
                  in-stock 0
              by-sku
                - sku=c
                  name x
                  sold 0
                  in-stock 0
            """))
        self.assertEqual(rtoon.loads(text, Group, path="group"), grp)

    def test_empty_record_list(self):
        text = rtoon.dumps({"g": Group("x")}, version=False)
        self.assertEqual(text, "g\n  name=x\n  items\n    -\n"
                               "  by-sku\n    -\n")

    def test_dense_records(self):
        items = [Item("a", "gum", 3), Item("b", "rope")]
        self.assertEqual(rtoon.dumps(items, path="l", dense=True,
                                     version=False),
                         "l\n  {sku=,name,sold,in-stock}\n  a,gum,3,0\n"
                         "  b,rope,0,0\n")


class TestErrors(unittest.TestCase):
    def test_errors(self):
        cases = [
            {"a": float("nan")},
            {"a": float("inf")},
            {"a": decimal.Decimal("NaN")},
            {"a": object()},
            {"a": [1, {"b": 2}]},
            {1: "x"},
            {"a": {frozenset([1]): 2}},
        ]
        for val in cases:
            with self.subTest(val=val):
                with self.assertRaises(DumpError):
                    rtoon.dumps(val)
        with self.assertRaises(DumpError):
            rtoon.dumps([1, 2])
        with self.assertRaises(DumpError):
            rtoon.dumps({"a": Record({"b": {"c": 1}}, keys_=("b",))})
        with self.assertRaises(ValueError):
            rtoon.dumps({}, dense="sometimes")


if __name__ == "__main__":
    unittest.main()
