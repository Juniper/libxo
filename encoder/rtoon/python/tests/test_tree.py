#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for loads() / load() without classes: Record and RList trees."""

import decimal
import json
import os
import unittest

import rtoon
from rtoon import PathError, Record, RList

DATA = os.path.join(os.path.dirname(__file__), "data")


def data(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as fp:
        return fp.read()


class TestTree(unittest.TestCase):
    def test_matches_json(self):
        doc = rtoon.loads(data("inventory.rtoon"))
        self.assertEqual(doc, json.loads(data("inventory.json")))

    def test_types(self):
        doc = rtoon.loads(data("inventory.rtoon"))
        self.assertIs(type(doc), Record)
        items = doc["top"]["data"]["item"]
        self.assertIs(type(items), RList)
        self.assertTrue(items.dense)
        self.assertEqual(items.keys_, ("sku",))
        self.assertEqual(items[0].keys_, ("sku",))
        self.assertIs(type(items[0]["sold"]), int)

    def test_sparse(self):
        doc = rtoon.loads(data("interfaces.rtoon"))
        ifs = doc["interface-information"]["physical-interface"]
        self.assertFalse(ifs.dense)
        self.assertEqual(ifs.keys_, ("name",))
        self.assertEqual(ifs[0]["flags"], ["up", "running", "broadcast"])
        self.assertEqual(ifs[1]["flags"], [])
        self.assertIsNone(ifs[1]["speed"])
        self.assertEqual(ifs[1]["address"], [])
        self.assertIs(type(ifs[1]["address"]), RList)
        self.assertEqual(ifs[0]["address"][1],
                         {"family": "inet6", "local": "fe80::1",
                          "prefix-length": 64})

    def test_load_file(self):
        with open(os.path.join(DATA, "inventory.rtoon"),
                  encoding="utf-8") as fp:
            doc = rtoon.load(fp)
        self.assertEqual(doc, rtoon.loads(data("inventory.rtoon")))

    def test_line_attr(self):
        doc = rtoon.loads(data("interfaces.rtoon"))
        ifs = doc["interface-information"]["physical-interface"]
        self.assertEqual(ifs[0].line_, 4)
        self.assertEqual(ifs[1].line_, 16)

    def test_flatten_multi_slash(self):
        doc = rtoon.loads("l\n  {a/b,x,a/c,a/d/e}\n  1,2,3,4\n")
        self.assertEqual(doc["l"][0],
                         {"a": {"b": 1, "c": 3, "d": {"e": 4}}, "x": 2})

    def test_parse_hooks(self):
        doc = rtoon.loads("a 1.10\nb 7\n", parse_float=decimal.Decimal,
                          parse_int=str)
        self.assertEqual(doc["a"], decimal.Decimal("1.10"))
        self.assertEqual(doc["b"], "7")

    def test_record_is_dict(self):
        rec = rtoon.loads("a=1\nb 2\n")
        self.assertEqual(rec, {"a": 1, "b": 2})
        self.assertEqual(rec.keys_, ("a",))
        self.assertEqual(list(rec.keys()), ["a", "b"])
        self.assertEqual(rec.copy().keys_, ("a",))


class TestPath(unittest.TestCase):
    def test_list(self):
        items = rtoon.loads(data("inventory.rtoon"), path="top/data/item")
        self.assertEqual(len(items), 5)
        self.assertEqual(items[3]["name"], "bolt")

    def test_container_and_field(self):
        text = data("inventory.rtoon")
        self.assertEqual(list(rtoon.loads(text, path="top/data")),
                         ["item"])
        self.assertEqual(rtoon.loads(text, path=["top", "data", "item"]),
                         rtoon.loads(text, path="/top/data/item/"))
        doc = "a\n  b x\n"
        self.assertEqual(rtoon.loads(doc, path="a/b"), "x")

    def test_first_match(self):
        text = "l\n  - c\n      x 1\n  - c\n      x 2\n"
        # A path passes through list instances; the first match wins
        self.assertEqual(rtoon.loads(text, path="l/c"), {"x": 1})
        self.assertEqual(rtoon.loads("a\n  x 1\nb\n  x 2\n", path="b/x"), 2)

    def test_not_found(self):
        with self.assertRaises(PathError):
            rtoon.loads(data("inventory.rtoon"), path="top/nope")
        with self.assertRaises(PathError):
            rtoon.loads("a 1\n", path="")


if __name__ == "__main__":
    unittest.main()
