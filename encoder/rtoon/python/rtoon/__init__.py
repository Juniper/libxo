#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Read and write RTOON, the output of libxo's "rtoon" encoder.

Reading::

    doc = rtoon.loads(text)                     # Record / RList tree
    items = rtoon.loads(text, list[Item], path="top/data/item")
    for item in rtoon.iterload(fp, Item, path="top/data/item"):
        ...

Writing::

    text = rtoon.dumps(doc)

Running a libxo-enabled command::

    items = rtoon.run(["ls-tool", "-l"], list[Entry], path="files/entry")

See README.rst for the full guide and rtoon-spec.md for the format.
"""

from ._errors import (BindError, DumpError, ParseError, PathError,
                      RtoonError, RunError)
from ._lex import ExactFloat, ExactInt
from ._parse import (END_CONTAINER, END_INSTANCE, END_LIST, FIELD,
                     LEAF_LIST, START_CONTAINER, START_INSTANCE,
                     START_LIST, VERSION, Event, iterparse)
from ._tree import Record, RList
from ._api import iterload, load, loads
from ._bind import (Field, bind, converter, field, is_record, key,
                    key_of, record, skip)
from ._dump import dump, dumps
from ._run import iterrun, run

__version__ = "1.0.0"

__all__ = [
    "BindError", "DumpError", "ParseError", "PathError", "RtoonError",
    "RunError",
    "ExactFloat", "ExactInt",
    "END_CONTAINER", "END_INSTANCE", "END_LIST", "FIELD", "LEAF_LIST",
    "START_CONTAINER", "START_INSTANCE", "START_LIST", "VERSION",
    "Event", "iterparse",
    "Record", "RList",
    "iterload", "load", "loads",
    "Field", "bind", "converter", "field", "is_record", "key", "key_of",
    "record", "skip",
    "dump", "dumps",
    "iterrun", "run",
]
