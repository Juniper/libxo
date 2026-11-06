#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for binding to classes: @rtoon.record and friends."""

from __future__ import annotations

import dataclasses
import datetime
import decimal
import enum
import ipaddress
import os
import typing
import unittest
from typing import Annotated, NamedTuple, Optional

import rtoon
from rtoon import BindError

DATA = os.path.join(os.path.dirname(__file__), "data")


def data(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as fp:
        return fp.read()


INVENTORY = data("inventory.rtoon")
INTERFACES = data("interfaces.rtoon")


@rtoon.record
class Item:
    sku: str
    name: str
    sold: int = 0
    in_stock: int = 0
    on_order: int = 0


@rtoon.record
@dataclasses.dataclass
class DItem:
    sku: str = rtoon.key()
    name: str = ""
    sold: int = 0
    in_stock: int = rtoon.field("in-stock", default=0)


@dataclasses.dataclass
@rtoon.record
class DItem2:
    sku: str = rtoon.key()
    name: str = ""
    tags: list = rtoon.field(default_factory=list)


@rtoon.record
@dataclasses.dataclass(slots=True, frozen=True)
class SlotItem:
    sku: str
    sold: int = 0


@rtoon.record
class NItem(NamedTuple):
    sku: str
    sold: int = 0


@rtoon.record
@dataclasses.dataclass
class Address:
    family: str = rtoon.key()
    local: ipaddress.IPv4Address | ipaddress.IPv6Address | None = None
    prefix_length: int = 0


@rtoon.record
@dataclasses.dataclass
class Interface:
    name: str = rtoon.key()
    admin_status: str = "up"
    mtu: int = 1500
    speed: Optional[int] = None
    flags: list[str] = dataclasses.field(default_factory=list)
    address: list[Address] = dataclasses.field(default_factory=list)


@rtoon.record
@dataclasses.dataclass
class InterfaceInfo:
    physical_interface: dict[str, Interface] = dataclasses.field(
        default_factory=dict)


@rtoon.converter(ipaddress.IPv4Address, dump=str)
def _ipv4(value, raw):
    return ipaddress.IPv4Address(raw)


@rtoon.converter(ipaddress.IPv6Address, dump=str)
def _ipv6(value, raw):
    return ipaddress.IPv6Address(raw)


class Color(enum.Enum):
    RED = "red"
    GREEN = "green"


class Level(enum.IntEnum):
    LOW = 1
    HIGH = 2


class TestPlainClass(unittest.TestCase):
    def test_list(self):
        items = rtoon.loads(INVENTORY, list[Item], path="top/data/item")
        self.assertEqual(len(items), 5)
        self.assertIsInstance(items[0], Item)
        self.assertEqual(items[0].sku, "GRO-000-415")
        self.assertEqual(items[0].in_stock, 54)
        self.assertEqual(items[3].on_order, 42)

    def test_class_untouched(self):
        self.assertNotIn("__init__", Item.__dict__)
        self.assertNotIn("__eq__", Item.__dict__)
        self.assertTrue(hasattr(Item, "from_rtoon"))

    def test_from_rtoon(self):
        items = rtoon.loads(INVENTORY, path="top/data/item")
        self.assertEqual(Item.from_rtoon(items[1]).name, "rope")
        self.assertEqual(Item.from_rtoon("sku x\nname y\n").name, "y")

    def test_missing_with_default(self):
        it = rtoon.loads("sku a\nname b\n", Item)
        self.assertEqual(it.sold, 0)

    def test_missing_without_default(self):
        with self.assertRaises(BindError) as ctx:
            rtoon.loads("sku a\n", Item)
        self.assertIn("name", str(ctx.exception))


class TestDataclass(unittest.TestCase):
    def test_dict_by_key(self):
        by_sku = rtoon.loads(INVENTORY, dict[str, DItem],
                             path="top/data/item")
        self.assertEqual(list(by_sku)[0], "GRO-000-415")
        self.assertEqual(by_sku["HRD-000-632"],
                         DItem("HRD-000-632", "bolt", 4123, 144))

    def test_decorator_order(self):
        it = rtoon.loads("sku a\ntags x,y,\n", DItem2)
        self.assertEqual(it, DItem2("a", "", ["x", "y"]))
        self.assertEqual(DItem2(sku="b").tags, [])
        self.assertEqual(rtoon.key_of(it), "a")

    def test_slots_frozen(self):
        it = rtoon.loads("sku a\nsold 3\n", SlotItem)
        self.assertEqual(it, SlotItem("a", 3))

    def test_namedtuple(self):
        it = rtoon.loads("sku a\n", NItem)
        self.assertEqual(it, NItem("a", 0))

    def test_nested(self):
        info = rtoon.loads(INTERFACES, InterfaceInfo,
                           path="interface-information")
        ge0 = info.physical_interface["ge-0/0/0"]
        self.assertEqual(ge0.mtu, 1514)
        self.assertEqual(ge0.flags, ["up", "running", "broadcast"])
        self.assertEqual(ge0.address[0].local,
                         ipaddress.IPv4Address("10.0.0.1"))
        self.assertEqual(ge0.address[1].local,
                         ipaddress.IPv6Address("fe80::1"))
        ge1 = info.physical_interface["ge-0/0/1"]
        self.assertIsNone(ge1.speed)
        self.assertEqual(ge1.address, [])
        self.assertEqual(ge1.flags, [])


class TestErrors(unittest.TestCase):
    def test_path_and_line(self):
        text = INVENTORY.replace("85,4,2", "lots,4,2")
        with self.assertRaises(BindError) as ctx:
            rtoon.loads(text, list[Item], path="top/data/item")
        err = ctx.exception
        self.assertEqual(err.path, "top/data/item[1]/sold")
        self.assertEqual(err.line, 7)
        self.assertIn("line 7", str(err))

    def test_bool_is_not_int(self):
        with self.assertRaises(BindError):
            rtoon.loads("sku a\nname b\nsold true\n", Item)

    def test_strict_types(self):
        @rtoon.record
        class S:
            n: int
        self.assertEqual(rtoon.loads('n "12"\n', S).n, 12)
        with self.assertRaises(BindError):
            rtoon.loads('n "12"\n', S, strict_types=True)

    def test_extra(self):
        @rtoon.record(extra="error")
        class E:
            a: int
        with self.assertRaises(BindError):
            rtoon.loads("a 1\nb 2\n", E)

        @rtoon.record(extra="keep")
        class K:
            a: int

            def __rtoon_loaded__(self, extra):
                self.extra = extra
        self.assertEqual(rtoon.loads("a 1\nb 2\n", K).extra, {"b": 2})

    def test_missing_none(self):
        @rtoon.record(missing="none")
        class M:
            a: int
            b: str
        obj = rtoon.loads("a 1\n", M)
        self.assertIsNone(obj.b)

    def test_unsupported_type(self):
        @rtoon.record
        class U:
            a: complex
        with self.assertRaises(BindError):
            rtoon.loads("a 1\n", U)

    def test_strict_keys(self):
        text = "l\n  - sku=a\n    name b\n"
        rtoon.loads(text, list[DItem], path="l", strict=True)
        with self.assertRaises(BindError):
            rtoon.loads("l\n  - sku a\n    name b\n", list[DItem],
                        path="l", strict=True)


class TestTypes(unittest.TestCase):
    def test_scalars(self):
        @rtoon.record
        class T:
            s: str
            f: float
            d: decimal.Decimal
            b: bool
            c: Color
            lv: Level
            when: datetime.date
            any_: typing.Any = None
        obj = rtoon.loads("s 0042\nf 3\nd 1.10\nb false\nc red\nlv 2\n"
                          "when 2026-11-05\nany- x\n", T)
        self.assertEqual(obj.s, "0042")
        self.assertEqual(obj.f, 3.0)
        self.assertIsInstance(obj.f, float)
        self.assertEqual(str(obj.d), "1.10")
        self.assertIs(obj.b, False)
        self.assertIs(obj.c, Color.RED)
        self.assertIs(obj.lv, Level.HIGH)
        self.assertEqual(obj.when, datetime.date(2026, 11, 5))
        self.assertEqual(obj.any_, "x")

    def test_str_keeps_exact_text(self):
        @rtoon.record
        class T:
            a: str
            b: str
        obj = rtoon.loads("a 1.50\nb 007\n", T)
        self.assertEqual((obj.a, obj.b), ("1.50", "007"))

    def test_containers(self):
        @rtoon.record
        class T:
            tags: set[str]
            pair: tuple[int, str]
            counts: dict[str, int]
            more: tuple[int, ...] = ()
        obj = rtoon.loads("tags b,a,b,\npair 1,x,\nmore 1,2,\n"
                          "counts\n  x 1\n  y 2\n", T)
        self.assertEqual(obj.tags, {"a", "b"})
        self.assertEqual(obj.pair, (1, "x"))
        self.assertEqual(obj.more, (1, 2))
        self.assertEqual(obj.counts, {"x": 1, "y": 2})

    def test_union(self):
        @rtoon.record
        class T:
            v: int | str
        self.assertEqual(rtoon.loads("v 3\n", T).v, 3)
        self.assertEqual(rtoon.loads("v x\n", T).v, "x")


class TestOptions(unittest.TestCase):
    def test_rename_and_annotated(self):
        @rtoon.record(rename={"count": "item-count"})
        class R:
            count: int
            other: Annotated[int, rtoon.Field("oth")] = 0
        obj = rtoon.loads("item-count 3\noth 4\n", R)
        self.assertEqual((obj.count, obj.other), (3, 4))

    def test_naming(self):
        @rtoon.record(naming="exact")
        class X:
            in_stock: int = 0
        self.assertEqual(rtoon.loads("in-stock 3\n", X).in_stock, 0)
        self.assertEqual(rtoon.loads("in_stock 3\n", X).in_stock, 3)

        @rtoon.record
        class A:
            in_stock: int = 0
        self.assertEqual(rtoon.loads("in_stock 3\n", A).in_stock, 3)
        self.assertEqual(rtoon.loads("in-stock 4\n", A).in_stock, 4)

        @rtoon.record(naming=str.upper)
        class F:
            v: int = 0
        self.assertEqual(rtoon.loads("V 5\n", F).v, 5)

    def test_construct(self):
        calls = []

        @rtoon.record(construct=lambda cls, f: calls.append(f) or "made")
        class C:
            a: int
        self.assertEqual(rtoon.loads("a 1\n", C), "made")
        self.assertEqual(calls, [{"a": 1}])

        @rtoon.record
        @dataclasses.dataclass
        class P:
            a: int

            def __post_init__(self):
                self.a *= 10
        self.assertEqual(rtoon.loads("a 1\n", P).a, 10)

    def test_field_convert(self):
        @rtoon.record
        class C:
            hexval: int = rtoon.field(convert=lambda v, raw: int(raw, 16))
        self.assertEqual(rtoon.loads("hexval ff\n", C).hexval, 255)

    def test_skip(self):
        @rtoon.record
        @dataclasses.dataclass
        class S:
            a: int = 0
            cache: dict = rtoon.skip(default_factory=dict)
        obj = rtoon.loads("a 1\ncache 2\n", S)
        self.assertEqual(obj.cache, {})

    def test_keys_option(self):
        @rtoon.record(keys=["id"])
        class K:
            id: str
            v: int = 0
        obj = rtoon.loads("id x\nv 1\n", K)
        self.assertEqual(rtoon.key_of(obj), "x")

    def test_register_existing_class(self):
        class Vendor:
            def __init__(self, a):
                raise AssertionError("not called")
            mtu: int
        rtoon.record(Vendor, rename={"mtu": "if-mtu"})
        self.assertEqual(rtoon.loads("if-mtu 9000\n", Vendor).mtu, 9000)

    def test_forward_refs(self):
        @rtoon.record
        @dataclasses.dataclass
        class Tree:
            name: str
            child: list[Tree] = dataclasses.field(default_factory=list)
        text = "name a\nchild\n  - name b\n  - name c\n    child\n" \
               "      - name d\n"
        tree = rtoon.loads(text, Tree)
        self.assertEqual(tree.child[1].child[0].name, "d")

    def test_bad_options(self):
        with self.assertRaises(ValueError):
            rtoon.record(missing="maybe")
        with self.assertRaises(TypeError):
            rtoon.record(3)


if __name__ == "__main__":
    unittest.main()
