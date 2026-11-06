#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Binding RTOON data to the caller's own classes.

``@record`` registers a class; its schema (field names, types, keys,
defaults) is built the first time the class is used, so the decorator
can go above or below ``@dataclass`` and classes may refer to each
other.  Each target type is compiled once into a converter object,
cached per type.
"""

import collections.abc
import dataclasses
import datetime
import decimal
import enum
import types
import typing

from ._errors import BindError
from ._lex import ExactFloat, ExactInt, number_kind
from ._tree import Record, RList

MISSING = dataclasses.MISSING

_registry = {}          # class -> _Options
_schemas = {}           # class -> _Schema
_convs = {}             # type -> converter
_converters = []        # [(type, load, dump)], in registration order


# ------------------------------------------------------------------
# Field markers
# ------------------------------------------------------------------

class Field:
    """RTOON metadata for one field.

    Use it in ``typing.Annotated``::

        in_stock: Annotated[int, rtoon.Field("stock-count")] = 0

    ``name`` is the RTOON name; ``key`` marks a key field; ``convert``
    is a function ``f(value, raw)`` used instead of the type's own
    conversion; ``dump`` is a function ``f(value)`` used by the writer;
    ``skip`` leaves the field out entirely.
    """

    __slots__ = ("name", "key", "convert", "dump", "skip")

    def __init__(self, name=None, *, key=False, convert=None, dump=None,
                 skip=False):
        self.name = name
        self.key = key
        self.convert = convert
        self.dump = dump
        self.skip = skip

    def __repr__(self):
        return ("Field(name=%r, key=%r, skip=%r)"
                % (self.name, self.key, self.skip))


def _merge(base, extra):
    """Merge two Field objects; set values in ``extra`` win."""
    if base is None:
        return extra
    if extra is None:
        return base
    return Field(extra.name or base.name, key=base.key or extra.key,
                 convert=extra.convert or base.convert,
                 dump=extra.dump or base.dump,
                 skip=base.skip or extra.skip)


def field(name=None, *, key=False, convert=None, dump=None,
          default=MISSING, default_factory=MISSING, **kwargs):
    """A field default carrying RTOON metadata.

    Returns a ``dataclasses.field()``, so on a dataclass it behaves as
    an ordinary default; it accepts the other ``dataclasses.field()``
    arguments too.  On a plain class, ``@rtoon.record`` replaces it
    with its default value.
    """
    metadata = dict(kwargs.pop("metadata", None) or {})
    metadata["rtoon"] = Field(name, key=key, convert=convert, dump=dump)
    return dataclasses.field(default=default,
                             default_factory=default_factory,
                             metadata=metadata, **kwargs)


def key(name=None, **kwargs):
    """``field()`` for a key field."""
    return field(name, key=True, **kwargs)


def skip(**kwargs):
    """``field()`` for a field RTOON leaves alone."""
    metadata = dict(kwargs.pop("metadata", None) or {})
    metadata["rtoon"] = Field(skip=True)
    return dataclasses.field(metadata=metadata, **kwargs)


# ------------------------------------------------------------------
# The decorator
# ------------------------------------------------------------------

class _Marker:
    """A ``field()`` default captured from a plain class body."""

    __slots__ = ("meta", "default", "default_factory")

    def __init__(self, meta, default, default_factory):
        self.meta = meta
        self.default = default
        self.default_factory = default_factory


class _Options:
    __slots__ = ("rename", "keys", "naming", "missing", "extra",
                 "construct", "markers")

    def __init__(self, rename, keys, naming, missing, extra, construct):
        self.rename = dict(rename or {})
        self.keys = tuple(keys or ())
        self.naming = naming
        self.missing = missing
        self.extra = extra
        self.construct = construct
        self.markers = {}


def _from_rtoon(cls, data, *, path=None, strict=False, strict_types=False):
    """Build an instance from RTOON text or an already-loaded Record."""
    from . import _api
    if isinstance(data, str):
        return _api.loads(data, cls, path=path, strict=strict,
                          strict_types=strict_types)
    return bind(data, cls, strict=strict, strict_types=strict_types)


def record(cls=None, /, *, rename=None, keys=None, naming="auto",
           missing="error", extra="ignore", construct=None):
    """Mark a class as a target for binding RTOON data.

    Returns the same class.  It adds only ``from_rtoon()`` (unless the
    class has one) and ``__rtoon__``; ``__init__``, ``__eq__`` and the
    rest are untouched.  It may be called directly on a class you
    cannot edit: ``rtoon.record(vendor.Interface, rename={...})``.

    rename     dict from attribute name to RTOON name
    keys       attribute names of the key fields
    naming     "auto" (``in_stock`` matches ``in-stock`` or
               ``in_stock``), "exact", or a function from attribute
               name to RTOON name
    missing    "error" (a missing field with no default raises
               BindError) or "none" (it is set to None)
    extra      "ignore", "error" or "keep" (passed to
               ``__rtoon_loaded__``)
    construct  "init", "setattr" or a function ``f(cls, fields)``
    """
    if missing not in ("error", "none"):
        raise ValueError("missing must be 'error' or 'none'")
    if extra not in ("ignore", "error", "keep"):
        raise ValueError("extra must be 'ignore', 'error' or 'keep'")
    if naming not in ("auto", "exact") and not callable(naming):
        raise ValueError("naming must be 'auto', 'exact' or a function")
    if construct not in (None, "init", "setattr") \
            and not callable(construct):
        raise ValueError("construct must be 'init', 'setattr' or a "
                         "function")

    def wrap(target):
        if not isinstance(target, type):
            raise TypeError("@rtoon.record needs a class")
        opts = _Options(rename, keys, naming, missing, extra, construct)
        _capture_markers(target, opts)
        _registry[target] = opts
        _schemas.pop(target, None)
        try:
            type.__setattr__(target, "__rtoon__", opts)
        except (TypeError, AttributeError):
            pass
        if not hasattr(target, "from_rtoon"):
            try:
                type.__setattr__(target, "from_rtoon",
                                 classmethod(_from_rtoon))
            except (TypeError, AttributeError):
                pass
        return target

    if cls is None:
        return wrap
    return wrap(cls)


def _capture_markers(cls, opts):
    """Take ``rtoon.field()`` markers out of a class that is not (yet)
    a dataclass.  A marker with a plain default is replaced by that
    default and one with no default is removed, which leaves a later
    ``@dataclass`` exactly what it would have seen anyway.  A marker
    with a default_factory stays: ``@dataclass`` needs it, and a plain
    class gets the factory called for each instance instead."""
    if dataclasses.is_dataclass(cls):
        return
    for name, val in list(vars(cls).items()):
        if not isinstance(val, dataclasses.Field):
            continue
        meta = val.metadata.get("rtoon") if val.metadata else None
        opts.markers[name] = _Marker(meta, val.default,
                                     val.default_factory)
        if val.default_factory is not MISSING:
            continue
        try:
            if val.default is MISSING:
                type.__delattr__(cls, name)
            else:
                type.__setattr__(cls, name, val.default)
        except (TypeError, AttributeError):
            pass


def options_for(cls):
    if not isinstance(cls, type):
        return None
    opts = _registry.get(cls)
    if opts is None:
        opts = getattr(cls, "__rtoon__", None)
        if not isinstance(opts, _Options):
            return None
    return opts


def is_record_class(tp):
    return options_for(tp) is not None


def is_record(obj):
    """True if ``obj`` is an instance of a record class."""
    return not isinstance(obj, type) and is_record_class(type(obj))


# ------------------------------------------------------------------
# Converters (for types and fields from the caller)
# ------------------------------------------------------------------

def converter(tp, *, dump=None):
    """Register a conversion for a type not supported out of the box::

        @rtoon.converter(ipaddress.IPv4Address, dump=str)
        def _to_ipv4(value, raw):
            return ipaddress.IPv4Address(raw)

    The function gets the scalar value as loaded and its exact text.
    ``dump`` turns a value of the type back into something the writer
    knows (a str, number, list or dict).
    """
    def wrap(func):
        _converters.insert(0, (tp, func, dump))
        _convs.clear()
        return func
    return wrap


def _custom_for(tp):
    for ctp, load, dump in _converters:
        if ctp is tp:
            return load, dump
    if isinstance(tp, type):
        for ctp, load, dump in _converters:
            if isinstance(ctp, type) and issubclass(tp, ctp):
                return load, dump
    return None


def custom_dump_for(value):
    """The registered dump function for a value, or None."""
    for ctp, _load, dump in _converters:
        if dump is not None and isinstance(ctp, type) \
                and isinstance(value, ctp):
            return dump
    return None


# ------------------------------------------------------------------
# Schemas
# ------------------------------------------------------------------

class _FieldInfo:
    __slots__ = ("attr", "names", "out_name", "key", "tp", "conv_",
                 "convert", "dump", "default", "default_factory",
                 "init", "optional")

    def __init__(self, attr, names, out_name, key, tp, meta, default,
                 default_factory, init):
        self.attr = attr
        self.names = names
        self.out_name = out_name
        self.key = key
        self.tp = tp
        self.conv_ = None
        self.convert = meta.convert if meta else None
        self.dump = meta.dump if meta else None
        self.default = default
        self.default_factory = default_factory
        self.init = init
        self.optional = _is_optional(tp)

    @property
    def conv(self):
        if self.conv_ is None:
            self.conv_ = get_conv(self.tp)
        return self.conv_

    def has_default(self):
        return (self.default is not MISSING
                or self.default_factory is not MISSING)

    def make_default(self):
        if self.default_factory is not MISSING:
            return self.default_factory()
        return self.default


class _Schema:
    __slots__ = ("cls", "opts", "fields", "by_name", "keys", "construct")

    def __init__(self, cls, opts, fields, construct):
        self.cls = cls
        self.opts = opts
        self.fields = fields
        self.by_name = {}
        for fi in fields:
            for name in fi.names:
                self.by_name.setdefault(name, fi)
        self.keys = [fi for fi in fields if fi.key]
        self.construct = construct


def _is_namedtuple(cls):
    return (isinstance(cls, type) and issubclass(cls, tuple)
            and hasattr(cls, "_fields"))


def _get_hints(cls):
    localns = {c.__name__: c for c in _registry}
    localns.update({c.__name__: c for c in list(_schemas)})
    try:
        return typing.get_type_hints(cls, localns=localns,
                                     include_extras=True)
    except NameError as err:
        raise BindError("cannot resolve the type hints of %s: %s"
                        % (cls.__qualname__, err)) from None


def _names_for(attr, meta, opts):
    if meta is not None and meta.name:
        return (meta.name,), meta.name
    if attr in opts.rename:
        name = opts.rename[attr]
        return (name,), name
    if opts.naming == "exact":
        return (attr,), attr
    if callable(opts.naming):
        name = opts.naming(attr)
        return (name,), name
    dashed = attr.replace("_", "-")
    if dashed == attr:
        return (attr,), attr
    return (dashed, attr), dashed


def schema_for(cls):
    sch = _schemas.get(cls)
    if sch is not None:
        return sch
    opts = options_for(cls)
    if opts is None:
        raise BindError("%s is not an @rtoon.record class"
                        % getattr(cls, "__qualname__", cls))
    sch = _build_schema(cls, opts)
    _schemas[cls] = sch
    return sch


def _build_schema(cls, opts):
    hints = _get_hints(cls)
    is_dc = dataclasses.is_dataclass(cls)
    is_attrs = hasattr(cls, "__attrs_attrs__")
    is_nt = _is_namedtuple(cls)

    dc_fields = {}
    if is_dc:
        dc_fields = {f.name: f for f in dataclasses.fields(cls)}
        order = [f.name for f in dataclasses.fields(cls)]
    elif is_nt:
        order = list(cls._fields)
    elif is_attrs:
        order = [a.name for a in cls.__attrs_attrs__]
    else:
        order = list(hints)
    attrs_fields = {}
    if is_attrs:
        attrs_fields = {a.name: a for a in cls.__attrs_attrs__}

    # Markers from rtoon.field() on a plain class: from this class and
    # from any record base classes
    markers = {}
    for base in reversed(cls.__mro__):
        bopts = options_for(base)
        if bopts is not None and base is not cls:
            markers.update(bopts.markers)
    markers.update(opts.markers)

    fields = []
    for attr in order:
        if attr.startswith("_") or attr not in hints:
            continue
        tp = hints[attr]
        if typing.get_origin(tp) is typing.ClassVar or tp is typing.ClassVar:
            continue
        meta = None
        if typing.get_origin(tp) is typing.Annotated:
            for item in tp.__metadata__:
                if isinstance(item, Field):
                    meta = _merge(meta, item)
            tp = typing.get_args(tp)[0]
            if typing.get_origin(tp) is typing.ClassVar:
                continue

        default = MISSING
        factory = MISSING
        init = True
        dcf = dc_fields.get(attr)
        if dcf is not None:
            if dcf._field_type is not dataclasses._FIELD:
                continue
            if dcf.metadata and "rtoon" in dcf.metadata:
                meta = _merge(meta, dcf.metadata["rtoon"])
            default, factory, init = dcf.default, dcf.default_factory, \
                dcf.init
        elif attr in attrs_fields:
            att = attrs_fields[attr]
            init = att.init
            dflt = att.default
            if dflt is not None and type(dflt).__name__ == "Factory":
                if not getattr(dflt, "takes_self", False):
                    factory = dflt.factory
            elif dflt is not _attrs_nothing():
                default = dflt
        elif is_nt:
            default = cls._field_defaults.get(attr, MISSING)
        else:
            val = getattr(cls, attr, MISSING)
            if not isinstance(val, dataclasses.Field):
                default = val
        mark = markers.get(attr)
        if mark is not None:
            meta = _merge(meta, mark.meta)
            if dcf is None:
                default, factory = mark.default, mark.default_factory
                # Now that the class is in use and is still not a
                # dataclass, settle a factory marker left in place
                if not is_dc and isinstance(vars(cls).get(attr),
                                            dataclasses.Field):
                    try:
                        type.__delattr__(cls, attr)
                    except (TypeError, AttributeError):
                        pass

        if meta is not None and meta.skip:
            continue
        names, out_name = _names_for(attr, meta, opts)
        is_key = bool(meta and meta.key) or attr in opts.keys \
            or out_name in opts.keys
        fields.append(_FieldInfo(attr, names, out_name, is_key, tp, meta,
                                 default, factory, init))

    construct = opts.construct
    if construct is None:
        construct = "init" if (is_dc or is_attrs or is_nt) else "setattr"
    if is_nt and construct == "setattr":
        raise BindError("a NamedTuple cannot be built with setattr")
    return _Schema(cls, opts, fields, construct)


def _attrs_nothing():
    try:
        import attr
        return attr.NOTHING
    except ImportError:
        return object()


def _is_optional(tp):
    origin = typing.get_origin(tp)
    if origin is typing.Union or origin is types.UnionType:
        return type(None) in typing.get_args(tp)
    return tp is type(None)


# ------------------------------------------------------------------
# Conversion
# ------------------------------------------------------------------

class _Ctx:
    """Where we are in the data, for error messages."""

    __slots__ = ("path", "strict", "strict_types", "line")

    def __init__(self, prefix, strict, strict_types):
        self.path = list(prefix)
        self.strict = strict
        self.strict_types = strict_types
        self.line = None

    def where(self):
        out = ""
        for seg in self.path:
            if seg.startswith("["):
                out += seg
            elif out:
                out += "/" + seg
            else:
                out = seg
        return out

    def error(self, msg):
        return BindError(msg, self.where() or None, self.line)


def raw_of(value):
    """The exact text a scalar was written as."""
    if isinstance(value, str):
        return value
    if value is True:
        return "true"
    if value is False:
        return "false"
    if value is None:
        return "null"
    if isinstance(value, (ExactFloat, ExactInt)):
        return value.raw
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    return None


def _kind(value):
    if isinstance(value, (Record, dict)):
        return "a container"
    if isinstance(value, list):
        return "a list"
    if value is None:
        return "null"
    return "%s %r" % (type(value).__name__, value)


def _is_scalar(value):
    return value is None or isinstance(value, (str, int, float))


class _AnyConv:
    def load(self, value, ctx):
        return value


class _DictConv:
    def load(self, value, ctx):
        if not isinstance(value, dict):
            raise ctx.error("expected a container, got %s" % _kind(value))
        return value


class _ScalarConv:
    def __init__(self, tp):
        self.tp = tp

    def load(self, value, ctx):
        tp = self.tp
        if not _is_scalar(value):
            raise ctx.error("expected %s, got %s" % (tp.__name__,
                                                     _kind(value)))
        if tp is str:
            if value is None:
                raise ctx.error("expected str, got null")
            return raw_of(value)
        if tp is type(None):
            if value is not None:
                raise ctx.error("expected null, got %s" % _kind(value))
            return None
        if tp is bool:
            if isinstance(value, bool):
                return value
            if not ctx.strict_types and value in ("true", "false"):
                return value == "true"
        elif tp is int:
            if isinstance(value, int) and not isinstance(value, bool):
                return int(value)
            if not ctx.strict_types and isinstance(value, str) \
                    and number_kind(value.strip()) == "int":
                return int(value.strip())
        elif tp is float:
            if isinstance(value, (int, float)) \
                    and not isinstance(value, bool):
                return float(value)
            if not ctx.strict_types and isinstance(value, str) \
                    and number_kind(value.strip()) is not None:
                return float(value.strip())
        elif tp is decimal.Decimal:
            if isinstance(value, (int, float)) \
                    and not isinstance(value, bool):
                return decimal.Decimal(raw_of(value))
            if isinstance(value, decimal.Decimal):
                return value
            if not ctx.strict_types and isinstance(value, str) \
                    and number_kind(value.strip()) is not None:
                return decimal.Decimal(value.strip())
        raise ctx.error("expected %s, got %s" % (tp.__name__, _kind(value)))


class _EnumConv:
    def __init__(self, tp):
        self.tp = tp

    def load(self, value, ctx):
        tp = self.tp
        if _is_scalar(value):
            try:
                return tp(value)
            except (ValueError, TypeError):
                pass
            raw = raw_of(value)
            for member in tp:
                if raw_of(member.value) == raw:
                    return member
            if raw in tp.__members__:
                return tp.__members__[raw]
        raise ctx.error("%s is not a %s" % (_kind(value), tp.__name__))


class _DateConv:
    def __init__(self, tp):
        self.tp = tp

    def load(self, value, ctx):
        if isinstance(value, str):
            try:
                return self.tp.fromisoformat(value)
            except ValueError:
                pass
        raise ctx.error("%s is not an ISO 8601 %s"
                        % (_kind(value), self.tp.__name__))


class _CustomConv:
    def __init__(self, func):
        self.func = func

    def load(self, value, ctx):
        try:
            return self.func(value, raw_of(value))
        except BindError:
            raise
        except (ValueError, TypeError) as err:
            raise ctx.error(str(err)) from err


class _OptionalConv:
    def __init__(self, inner):
        self.inner = inner

    def load(self, value, ctx):
        if value is None:
            return None
        return self.inner.load(value, ctx)


class _UnionConv:
    def __init__(self, convs, names):
        self.convs = convs
        self.names = names

    def load(self, value, ctx):
        for conv in self.convs:
            try:
                return conv.load(value, ctx)
            except BindError:
                continue
        raise ctx.error("%s matches none of %s" % (_kind(value),
                                                   self.names))


class _SeqConv:
    def __init__(self, ctor, item):
        self.ctor = ctor
        self.item = item

    def load(self, value, ctx):
        if not isinstance(value, list):
            raise ctx.error("expected a list, got %s" % _kind(value))
        out = []
        for i, item in enumerate(value):
            ctx.path.append("[%d]" % i)
            out.append(self.item.load(item, ctx))
            ctx.path.pop()
        return out if self.ctor is list else self.ctor(out)


class _TupleConv:
    def __init__(self, items):
        self.items = items

    def load(self, value, ctx):
        if not isinstance(value, list) or len(value) != len(self.items):
            raise ctx.error("expected a list of %d values, got %s"
                            % (len(self.items), _kind(value)))
        out = []
        for i, (conv, item) in enumerate(zip(self.items, value)):
            ctx.path.append("[%d]" % i)
            out.append(conv.load(item, ctx))
            ctx.path.pop()
        return tuple(out)


class _MapConv:
    """dict[str, T] from a container, field by field."""

    def __init__(self, key, val):
        self.key = key
        self.val = val

    def load(self, value, ctx):
        if not isinstance(value, dict):
            raise ctx.error("expected a container, got %s" % _kind(value))
        out = {}
        for name, item in value.items():
            ctx.path.append(name)
            out[self.key.load(name, ctx)] = self.val.load(item, ctx)
            ctx.path.pop()
        return out


class _KeyedConv:
    """dict[K, R] (R a record class) from a list, keyed by R's keys."""

    def __init__(self, key, rec):
        self.key = key
        self.rec = rec

    def load(self, value, ctx):
        if isinstance(value, dict):
            return _MapConv(self.key, self.rec).load(value, ctx)
        if not isinstance(value, list):
            raise ctx.error("expected a list, got %s" % _kind(value))
        sch = schema_for(self.rec.cls)
        out = {}
        for i, item in enumerate(value):
            ctx.path.append("[%d]" % i)
            obj = self.rec.load(item, ctx)
            if sch.keys:
                k = key_of(obj)
            else:
                keys = getattr(item, "keys_", ()) or \
                    getattr(value, "keys_", ())
                if not keys:
                    raise ctx.error("%s has no key fields"
                                    % sch.cls.__qualname__)
                vals = tuple(item.get(n) for n in keys)
                k = self.key.load(vals[0] if len(vals) == 1
                                  else list(vals), ctx)
            if k in out:
                raise ctx.error("duplicate key %r" % (k,))
            out[k] = obj
            ctx.path.pop()
        return out


class _RecordConv:
    def __init__(self, cls):
        self.cls = cls

    def load(self, value, ctx):
        if not isinstance(value, dict):
            raise ctx.error("expected a container for %s, got %s"
                            % (self.cls.__qualname__, _kind(value)))
        sch = schema_for(self.cls)
        opts = sch.opts
        lines = getattr(value, "lines_", None)
        rec_line = getattr(value, "line_", None)
        if ctx.strict and isinstance(value, Record):
            want = {fi.out_name for fi in sch.keys}
            have = set(value.keys_)
            got = {fi.out_name for name in have
                   for fi in (sch.by_name.get(name),) if fi is not None}
            if got != want or len(got) != len(have):
                ctx.line = rec_line
                raise ctx.error("document keys (%s) differ from %s's (%s)"
                                % (",".join(sorted(have)),
                                   self.cls.__qualname__,
                                   ",".join(sorted(want))))

        values = {}
        used = set()
        for fi in sch.fields:
            found = MISSING
            for name in fi.names:
                if name in value:
                    found = value[name]
                    used.add(name)
                    break
            if found is MISSING:
                continue
            ctx.path.append(name)
            saved = ctx.line
            ctx.line = (lines.get(name) if lines else None) or rec_line
            if fi.convert is not None:
                try:
                    values[fi.attr] = fi.convert(found, raw_of(found))
                except BindError:
                    raise
                except (ValueError, TypeError) as err:
                    raise ctx.error(str(err)) from err
            else:
                values[fi.attr] = fi.conv.load(found, ctx)
            ctx.line = saved
            ctx.path.pop()

        extra = {}
        if len(used) != len(value):
            extra = {k: v for k, v in value.items() if k not in used}
            if extra and opts.extra == "error":
                ctx.line = rec_line
                raise ctx.error("unexpected field(s) for %s: %s"
                                % (self.cls.__qualname__,
                                   ", ".join(extra)))
        if opts.extra != "keep":
            extra = {}

        obj = self._construct(sch, values, ctx, rec_line)
        hook = getattr(obj, "__rtoon_loaded__", None)
        if hook is not None:
            hook(extra)
        return obj

    def _construct(self, sch, values, ctx, rec_line):
        opts = sch.opts
        how = sch.construct
        for fi in sch.fields:
            if fi.attr in values:
                continue
            if how == "init" and fi.has_default() and fi.init:
                continue
            if fi.has_default():
                values[fi.attr] = fi.make_default()
            elif fi.optional or opts.missing == "none":
                values[fi.attr] = None
            else:
                ctx.line = rec_line
                raise ctx.error("missing field '%s' for %s"
                                % (fi.out_name, self.cls.__qualname__))

        cls = self.cls
        if callable(how):
            return how(cls, values)
        if how == "init":
            later = {}
            kwargs = {}
            for fi in sch.fields:
                if fi.attr not in values:
                    continue
                if fi.init:
                    kwargs[fi.attr] = values[fi.attr]
                else:
                    later[fi.attr] = values[fi.attr]
            try:
                obj = cls(**kwargs)
            except BindError:
                raise
            except (TypeError, ValueError) as err:
                ctx.line = rec_line
                raise ctx.error("cannot build %s: %s"
                                % (cls.__qualname__, err)) from err
            for name, val in later.items():
                object.__setattr__(obj, name, val)
            return obj
        obj = cls.__new__(cls)
        for name, val in values.items():
            try:
                setattr(obj, name, val)
            except AttributeError:
                object.__setattr__(obj, name, val)
        return obj


def _type_name(tp):
    return getattr(tp, "__name__", None) or repr(tp)


def get_conv(tp):
    try:
        conv = _convs.get(tp)
    except TypeError:
        return _compile(tp)
    if conv is None:
        conv = _compile(tp)
        _convs[tp] = conv
    return conv


_SEQ_ORIGINS = {list: list, tuple: tuple, set: set, frozenset: frozenset,
                collections.abc.Sequence: list,
                collections.abc.MutableSequence: list,
                collections.abc.Set: frozenset,
                collections.abc.MutableSet: set,
                collections.abc.Iterable: list,
                collections.abc.Collection: list}

_MAP_ORIGINS = (dict, collections.abc.Mapping,
                collections.abc.MutableMapping)


def _compile(tp):
    if tp is typing.Any or tp is object:
        return _AnyConv()
    origin = typing.get_origin(tp)
    if origin is typing.Annotated:
        return get_conv(typing.get_args(tp)[0])

    custom = _custom_for(tp)
    if custom is not None:
        return _CustomConv(custom[0])

    if origin is typing.Union or origin is types.UnionType:
        args = typing.get_args(tp)
        rest = [a for a in args if a is not type(None)]
        if len(rest) < len(args):
            if len(rest) == 1:
                return _OptionalConv(get_conv(rest[0]))
            return _OptionalConv(_UnionConv([get_conv(a) for a in rest],
                                            _type_name(tp)))
        return _UnionConv([get_conv(a) for a in args], _type_name(tp))

    if origin in _SEQ_ORIGINS:
        args = typing.get_args(tp)
        if origin is tuple and args and not (len(args) == 2
                                             and args[1] is Ellipsis):
            return _TupleConv([get_conv(a) for a in args])
        item = get_conv(args[0]) if args else _AnyConv()
        return _SeqConv(_SEQ_ORIGINS[origin], item)

    if origin in _MAP_ORIGINS:
        args = typing.get_args(tp) or (str, typing.Any)
        kconv = get_conv(args[0])
        if is_record_class(args[1]):
            return _KeyedConv(kconv, _RecordConv(args[1]))
        return _MapConv(kconv, get_conv(args[1]))

    if tp in (list, tuple, set, frozenset):
        return _SeqConv(tp, _AnyConv())
    if tp is Record or tp is dict:
        return _DictConv()
    if tp is RList:
        return _SeqConv(list, _AnyConv())

    if is_record_class(tp):
        return _RecordConv(tp)
    if tp in (str, int, float, bool, decimal.Decimal, type(None)):
        return _ScalarConv(tp)
    if isinstance(tp, type) and issubclass(tp, enum.Enum):
        return _EnumConv(tp)
    if tp in (datetime.datetime, datetime.date, datetime.time):
        return _DateConv(tp)
    raise BindError("unsupported type %s (register one with "
                    "@rtoon.converter)" % _type_name(tp))


def bind(value, tp, *, strict=False, strict_types=False, path=None):
    """Convert loaded data (Records, RLists, scalars) to ``tp``."""
    prefix = list(path or ())
    ctx = _Ctx(prefix, strict, strict_types)
    ctx.line = getattr(value, "line_", None)
    return get_conv(tp).load(value, ctx)


def item_type(tp):
    """For streaming: the element type of list[T] (or T itself)."""
    origin = typing.get_origin(tp)
    if origin in _SEQ_ORIGINS and origin is not tuple:
        args = typing.get_args(tp)
        return args[0] if args else typing.Any
    return tp


def key_of(obj):
    """An object's key: the value of its single key field, or a tuple
    for a composite key.  Works on record instances and on Records."""
    if isinstance(obj, Record):
        keys = obj.keys_
        if not keys:
            raise BindError("record has no key fields")
        vals = tuple(obj.get(k) for k in keys)
    else:
        sch = schema_for(type(obj))
        if not sch.keys:
            raise BindError("%s has no key fields"
                            % type(obj).__qualname__)
        vals = tuple(getattr(obj, fi.attr) for fi in sch.keys)
    return vals[0] if len(vals) == 1 else vals


def dump_fields(obj):
    """For the writer: (rtoon name, value, is_key, field info) for each
    field of a record instance, in schema order."""
    sch = schema_for(type(obj))
    out = []
    for fi in sch.fields:
        try:
            val = getattr(obj, fi.attr)
        except AttributeError:
            continue
        out.append((fi.out_name, val, fi.key, fi))
    return out


def wants_list(fi):
    """For the writer: does this field hold a list of records (so an
    empty value is an empty list, not an empty leaf-list)?"""
    tp = fi.tp
    origin = typing.get_origin(tp)
    if origin is typing.Union or origin is types.UnionType:
        args = [a for a in typing.get_args(tp) if a is not type(None)]
        if len(args) == 1:
            tp = args[0]
            origin = typing.get_origin(tp)
    if origin in _MAP_ORIGINS:
        args = typing.get_args(tp)
        return bool(args) and is_record_class(args[1])
    if origin in _SEQ_ORIGINS:
        args = typing.get_args(tp)
        return bool(args) and is_record_class(args[0])
    return tp is RList
