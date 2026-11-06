#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Exception classes for the rtoon package."""

import subprocess


class RtoonError(ValueError):
    """Base class for every error raised by the rtoon package."""


class ParseError(RtoonError):
    """The text is not valid RTOON.

    ``line`` is the 1-based line number, ``column`` the 1-based column
    (or None when the whole line is at fault), and ``text`` the
    offending line, without its newline.
    """

    def __init__(self, msg, line=None, column=None, text=None):
        self.msg = msg
        self.line = line
        self.column = column
        self.text = text
        where = ""
        if line is not None:
            where = "line %d" % line
            if column is not None:
                where += ", column %d" % column
            where += ": "
        super().__init__(where + msg)


class PathError(RtoonError):
    """The ``path`` given to a reader did not match the document."""


class DumpError(RtoonError):
    """The writer was given something it cannot represent in RTOON."""


class BindError(RtoonError):
    """The data does not fit the target type.

    ``path`` names the place in the data (``top/data/item[3]/sold``)
    and ``line`` is the line the value came from, when known.
    """

    def __init__(self, msg, path=None, line=None):
        self.msg = msg
        self.path = path
        self.line = line
        where = ""
        if path:
            where += path
        if line is not None:
            where += " (line %d)" % line
        if where:
            where += ": "
        super().__init__(where + msg)


class RunError(RtoonError, subprocess.CalledProcessError):
    """A command run by ``run()`` or ``iterrun()`` exited non-zero.

    It is also a ``subprocess.CalledProcessError``, with
    ``returncode``, ``cmd`` and ``output`` set as usual.
    """

    def __init__(self, returncode, cmd, output=None, stderr=None):
        subprocess.CalledProcessError.__init__(self, returncode, cmd,
                                               output, stderr)

    def __str__(self):
        return subprocess.CalledProcessError.__str__(self)
