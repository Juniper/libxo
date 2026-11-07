===========
libxo-rtoon
===========

Read and write RTOON, the output of libxo's ``rtoon`` encoder, from
Python.

RTOON is a compact, indentation-based text form of the data a
libxo-enabled command emits (the same data as its ``--libxo json``
output).  This package parses it into Python values or into your own
classes, writes it back, and runs commands with ``--libxo=@rtoon``.
The format itself is described in ``rtoon-spec.md``, in libxo's
``encoder/rtoon`` directory.

The package needs Python 3.10 or later and nothing outside the
standard library.  The distribution is named ``libxo-rtoon``; the
module is ``rtoon``.

Installing
==========

From a libxo source tree::

    pip install encoder/rtoon/python

libxo's own ``make install`` does not install the package.

Reading a document without classes
==================================

``rtoon.loads(text)`` and ``rtoon.load(fp)`` return the document as
Python values::

    >>> doc = rtoon.loads(out)
    >>> doc["top"]["data"]["item"][0]
    {'sku': 'GRO-000-415', 'name': 'gum', 'sold': 1412,
     'in-stock': 54, 'on-order': 10}

The mapping from RTOON to Python is:

  ============================  =====================================
  RTOON                         Python
  ============================  =====================================
  document (top level)          ``rtoon.Record``
  container                     ``rtoon.Record``
  list (sparse or dense)        ``rtoon.RList`` of ``rtoon.Record``
  list instance or dense row    ``rtoon.Record``
  leaf-list (``tags a,b,``)     ``list`` of scalars
  ``true`` / ``false``          ``bool``
  ``null``                      ``None``
  number without ``.`` or exp   ``int``
  other number                  ``float``
  anything else, or quoted      ``str``
  ============================  =====================================

``Record`` is a ``dict`` subclass, so it compares equal to a plain
dict and works anywhere a dict does.  It adds ``keys_``, a tuple of
the names of the fields that were written as keys (``name=value``, or
a trailing ``=`` in a dense fields-line), and ``line_``, the line the
record started on.  ``RList`` is a ``list`` subclass with the same
``keys_`` and ``line_`` attributes, and ``dense``, which is True when
the list was written in dense form.  The trailing underscores keep
these from hiding ``dict.keys()`` and friends.

A number whose text would not come back from ``repr()`` (``1.50``,
``1e3``, ``-0``) loads as ``rtoon.ExactFloat`` or ``rtoon.ExactInt``.
These are ordinary ``float`` and ``int`` subclasses that also keep the
text in ``raw``, so ``dumps()`` and ``str`` fields can reproduce it.

The form a list was written in never changes the result: the sparse
and dense spellings of the same data load to equal values.  Columns
that the encoder flattened (``via/interface``) are put back into a
nested ``Record`` (``via`` with an ``interface`` field), so the result
has the same shape as the JSON output of the same command.

A flattened name may have more than one slash; each slash is one
level of nesting.  In ``{a/b,a/c,a/d/e}``, ``a`` becomes one child
``Record`` holding ``b``, ``c`` and a further child ``d`` holding
``e``.  Columns with the same prefix are gathered together wherever
they appear in the fields-line.

An empty container loads as an empty ``Record``; an empty list
(``list -``, or ``-`` alone under the list) loads as an empty
``RList``.

Like ``json.loads()``, ``loads()`` accepts ``parse_int`` and
``parse_float`` to change how numbers are converted, for example
``parse_float=decimal.Decimal``.

Reading part of a document
==========================

``path`` selects a node by its names, separated by slashes, starting
from the top of the document::

    items = rtoon.loads(out, path="top/data/item")

A path passes through list instances without naming them:
``top/data/item/sku`` is the ``sku`` field of an item.  ``loads()``
returns the first node that matches, which may be a field.  A path
that matches nothing raises ``rtoon.PathError``.

Binding to classes
==================

Pass a type as the second argument, and the document (or the node
``path`` selects) is converted to that type::

    doc = rtoon.loads(out, Top)                     # a record class
    items = rtoon.loads(out, list[Item], path="top/data/item")
    by_sku = rtoon.loads(out, dict[str, Item], path="top/data/item")

The type can be anything in "Supported types" below, including
``list[...]`` and ``dict[...]`` of record classes.

Declaring a record class
------------------------

``@rtoon.record`` marks a class as a target for binding::

    @rtoon.record
    class Item:
        sku: str
        name: str
        sold: int = 0
        in_stock: int = 0

The decorator returns the same class object.  It does not add
``__init__``, ``__eq__``, ``__repr__`` or anything else that changes
how the class behaves; it records how to fill the class from RTOON,
and adds one class method, ``from_rtoon()`` (skipped if the class
already defines one).  This makes it safe to add to classes that
already exist, including dataclasses, attrs classes and classes with
``__slots__``, and the order relative to ``@dataclass`` does not
matter::

    @rtoon.record           # either order works
    @dataclass
    class Item:
        ...

Because a decorator is just a function, a class you cannot edit can be
registered by calling it::

    rtoon.record(vendor.Interface, rename={"mtu": "if-mtu"})

The fields of a record are the class's annotated attributes, including
inherited ones, in definition order.  ``ClassVar`` annotations, names
starting with ``_``, and fields marked ``rtoon.skip()`` are left out.

Field names
-----------

By default a Python attribute ``in_stock`` matches an RTOON field
named ``in-stock`` or ``in_stock`` (libxo writes dashes, or
underscores when the ``underscores`` option is given).  Other names
are given in one of three ways, whichever suits the class:

1. ``typing.Annotated`` metadata, which works for any class::

       in_stock: Annotated[int, rtoon.Field("stock-count")] = 0

2. A field default, for dataclasses and plain classes::

       in_stock: int = rtoon.field("stock-count", default=0)

   On a dataclass, ``rtoon.field()`` returns a ``dataclasses.field()``
   carrying the RTOON metadata, so the dataclass still sees an ordinary
   default (it accepts ``default``, ``default_factory`` and the other
   ``dataclasses.field()`` arguments).  On a plain class the decorator
   replaces the marker with its default value.

3. Decorator arguments, for classes that cannot be edited::

       @rtoon.record(rename={"in_stock": "stock-count"})

``naming="exact"`` turns off the dash/underscore matching, and
``naming=`` may also be a function from attribute name to RTOON name.

Keys
----

``rtoon.key()`` (or ``rtoon.Field(key=True)``, or ``keys=("sku",)`` on
the decorator) declares a key field.  Keys are used three ways:

- ``rtoon.key_of(obj)`` returns an object's key: the value for a
  single key field, a tuple for a composite key.
- A list bound to ``dict[K, Item]`` becomes a dict from key to
  instance.  A duplicate key is an error.
- With ``strict=True``, the reader checks that the document marks the
  same fields as keys, and raises ``BindError`` if it does not.

Supported types
---------------

  =================================  ==================================
  Annotation                         Accepts
  =================================  ==================================
  ``str``                            any scalar; a number keeps the
                                     exact text written (``1.50``
                                     stays ``"1.50"``)
  ``int``, ``float``                 a number; also a string holding a
                                     number, unless ``strict_types``
  ``bool``                           ``true`` / ``false``; also the
                                     strings ``"true"`` / ``"false"``
                                     unless ``strict_types``
  ``decimal.Decimal``                a number or numeric string, from
                                     the exact text written
  ``enum.Enum`` subclass             a value of the enum, or a name
  ``datetime``, ``date``, ``time``   an ISO 8601 string
  ``X | None``, ``Optional[X]``      ``null``, a missing field, or X
  ``Union[A, B]``                    the first type that accepts it
  ``list[T]``, ``tuple[T, ...]``     a leaf-list (scalar T) or a list
                                     (record T)
  ``set[T]``, ``frozenset[T]``       a leaf-list
  ``dict[K, R]`` (R a record)        a list, keyed by R's keys
  ``dict[str, T]``                   a container, field by field
  a record class                     a container or list instance
  ``rtoon.Record``, ``dict``         a container, unconverted
  ``Any``, ``object``                anything, unconverted
  =================================  ==================================

Other types are added with the ``rtoon.converter`` decorator::

    import ipaddress

    @rtoon.converter(ipaddress.IPv4Address)
    def _to_ipv4(value, raw):
        return ipaddress.IPv4Address(raw)

The function gets the scalar value as loaded and its exact text.  A
single field can also carry its own converter:
``rtoon.Field(convert=parse_speed)``.

Missing, extra and repeated fields
----------------------------------

- A missing field takes its default.  A missing field with no default
  raises ``BindError``, unless the decorator says ``missing="none"``,
  in which case it is set to None.
- Fields in the document that the class does not name are ignored by
  default.  ``extra="error"`` raises ``BindError``; ``extra="keep"``
  stores them in a dict passed to the ``__rtoon_loaded__`` hook (below).
- A name should not appear twice in one container.  If it does, the
  last one wins, as with ``json.loads()``; with ``strict=True`` it
  raises ``ParseError`` instead.

Building the objects
--------------------

``construct=`` on the decorator chooses how instances are made:

- ``"init"``: call ``cls(**fields)``.  This is the default for
  dataclasses, attrs classes and ``NamedTuple`` classes, so their
  ``__post_init__`` and validators run.
- ``"setattr"``: create the object with ``cls.__new__(cls)`` and set
  each field with ``setattr()``; class-level defaults are left in place
  for missing fields.  This is the default for other classes, whose
  ``__init__`` may expect arguments that do not come from RTOON.
- a function ``f(cls, fields)`` that returns the instance.

If the class defines ``__rtoon_loaded__(self, extra)``, it is called
after the object is filled, with the dict of extra fields (empty
unless ``extra="keep"``).

Forward references and recursive classes are fine: the decorator
resolves type hints the first time the class is used, not when it is
decorated, so it works with ``from __future__ import annotations`` and
with classes that refer to each other.

Streaming
=========

``rtoon.iterload(fp, Item, path="top/data/item")`` yields each list
instance as soon as it has been read, so a long listing can be
processed without holding it all in memory.  ``path`` must name a
list; if several lists match (``group/member`` under every instance
of ``group``), the instances of each are yielded in turn.  With no
type, it yields ``Record`` objects.  ``fp`` may be a file, any
iterable of strings, or a str.

For lower-level work, ``rtoon.iterparse(fp)`` yields ``rtoon.Event``
objects, much like ``xml.etree.ElementTree.iterparse``:

  ===================  ==========================================
  Event                Fields
  ===================  ==========================================
  ``VERSION``          ``value``
  ``START_CONTAINER``  ``name``
  ``END_CONTAINER``    ``name``
  ``START_LIST``       ``name``, ``dense``
  ``END_LIST``         ``name``
  ``START_INSTANCE``   (none)
  ``END_INSTANCE``     (none)
  ``FIELD``            ``name``, ``value``, ``raw``, ``key``
  ``LEAF_LIST``        ``name``, ``values``, ``raws``, ``key``
  ===================  ==========================================

Every event also has ``kind`` (one of the constants above) and
``line``, the line number it came from; an ``END_*`` event has the
line of its ``START_*``.  Dense rows produce the same
``START_INSTANCE`` / ``FIELD`` / ``END_INSTANCE`` events as sparse
instances, and flattened columns produce ``START_CONTAINER`` /
``END_CONTAINER`` around their fields, so a consumer never needs to
know which form was used.

Strict and lenient reading
==========================

``strict=False`` (the default) accepts what the spec says a decoder
must tolerate, such as whitespace around ``=`` in hand-edited input,
or a dense row with fewer cells than its fields-line (the missing
fields are absent).  ``strict=True`` also enforces what the spec says
a writer must do:

- key fields are marked consistently on every instance (spec 4.2);
- each fields-line extends the one before it (spec 6.2);
- each dense row has as many cells as its fields-line (spec 6.3);
- names are not repeated within a container.

Both modes reject tabs in indentation, indentation that jumps more
than one level, an unknown ``@`` instruction, a ``@version`` whose
major number is not 1, unterminated quotes, bad escapes (including a
surrogate ``\uXXXX``), and a raw control character inside quotes.

Writing RTOON
=============

``rtoon.dumps(obj)`` returns RTOON text, and ``rtoon.dump(obj, fp)``
writes it to a file.  ``obj`` may be:

- a ``Record`` / ``RList`` tree from ``loads()``: key markings and each
  list's form (``RList.dense``) are kept, so ``dumps(loads(text))``
  gives back ``text`` for what libxo's encoder writes;
- plain Python values: a ``dict`` is a container, a list of dicts is a
  list, a list of scalars is a leaf-list.  There are no keys, and
  lists are sparse unless ``dense`` says otherwise;
- an instance of a record class (or a list or dict of them): fields
  are written in schema order, under their RTOON names, and the
  schema's keys are marked.  Fields that are None are left out.

Options:

- ``dense``: ``False`` writes every list sparse; ``True`` writes every
  list dense; ``"auto"`` (the default) uses ``RList.dense`` where it is
  known and sparse elsewhere.  A list that cannot be dense (one whose
  instances hold nested lists or leaf-lists) is written sparse
  whatever this says.  In a dense list, nested containers are
  flattened into ``a/b`` columns.
- ``version``: write the ``@version 1.0.0`` line (default True).  An
  empty document is written as nothing at all.
- ``path``: wrap the output in the containers named by a slash
  path, so ``dumps(items, path="top/data/item")`` writes the same
  shape it was read from.

Quoting follows libxo's encoder.  The writer also quotes a value of
``-`` and a name starting with ``#`` or ``@``, which would otherwise
read back as something else.

A value the writer cannot represent (a NaN or infinity, a ``set`` of
dicts, a key field whose value is a container, an unsupported type
with no converter) raises ``rtoon.DumpError``.  Converters added with
``rtoon.converter`` may give a matching ``dump=`` function to write
their type.

Running a command
=================

``rtoon.run(argv, typ=None, *, path=None, options="", strict=False,
strict_types=False, check=True)`` runs ``argv`` with
``--libxo=@rtoon`` inserted after the command name, reads its
standard output, and returns what ``loads()`` would.  ``options`` (a
string, or a list of strings joined with ``:``) is passed to the
encoder, for example ``options="dense"``.  ``rtoon.iterrun()`` takes
the same arguments (``path`` is required) and yields instances as
``iterload()`` does, reading the output while the command is still
running; if the iteration is stopped early, the command is
terminated.

Standard error is passed through to the caller's standard error.  If
the command exits non-zero and ``check`` is true, ``rtoon.RunError``
(a ``subprocess.CalledProcessError``) is raised after the output has
been read; with ``iterrun()`` it is raised when the iteration ends.

Errors
======

All errors are subclasses of ``rtoon.RtoonError``, which is a
``ValueError``:

- ``ParseError``: the text is not valid RTOON.  It has ``line``,
  ``column`` and ``text`` (the offending line).
- ``PathError``: ``path`` did not match.
- ``DumpError``: ``dumps()`` was given something it cannot write.
- ``RunError``: from ``run()`` and ``iterrun()``; see above.
- ``BindError``: the data does not fit the class.  It has ``path``,
  naming the place in the data (``top/data/item[3]/sold``), and
  ``line``, the line the value came from.

The message always names the line, so a failure in a long document
can be found.

Running the tests
=================

The tests use ``unittest`` and live in ``tests/``.  In a libxo build
tree they run as part of ``make tests`` (in ``tests/rtoon-py``), which
keeps the bytecode cache and temporary files in the build tree.  To
run them by hand without writing ``__pycache__`` here::

    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests
