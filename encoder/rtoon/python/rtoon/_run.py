#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Running a libxo-enabled command: run() and iterrun()."""

import subprocess

from . import _api
from ._errors import RunError


def _argv(argv, options):
    argv = list(argv)
    if not argv:
        raise ValueError("argv is empty")
    if isinstance(options, (list, tuple)):
        options = ":".join(options)
    opt = "--libxo=@rtoon"
    if options:
        opt += ":" + options
    return [argv[0], opt] + argv[1:]


def _popen(cmd):
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True,
                            encoding="utf-8")


def run(argv, typ=None, *, path=None, options="", strict=False,
        strict_types=False, check=True):
    """Run ``argv`` with ``--libxo=@rtoon`` and read its output.

    ``options`` (a str like "dense" or a list of them) is passed to the
    encoder.  Returns what ``loads()`` would.  Standard error is left
    alone.  With ``check``, a non-zero exit raises RunError after the
    output has been read.
    """
    cmd = _argv(argv, options)
    with _popen(cmd) as proc:
        output = proc.stdout.read()
        proc.wait()
    if check and proc.returncode != 0:
        raise RunError(proc.returncode, cmd, output)
    return _api.loads(output, typ, path=path, strict=strict,
                      strict_types=strict_types)


def iterrun(argv, typ=None, *, path, options="", strict=False,
            strict_types=False, check=True):
    """Run ``argv`` like run(), yielding the instances of the list at
    ``path`` as iterload() does, while the command is still running.

    If the iteration is stopped early, the command is terminated.
    With ``check``, a non-zero exit raises RunError once all the
    output has been read.
    """
    cmd = _argv(argv, options)
    proc = _popen(cmd)
    finished = False
    try:
        yield from _api.iterload(proc.stdout, typ, path=path,
                                 strict=strict, strict_types=strict_types)
        proc.stdout.read()
        finished = True
    finally:
        proc.stdout.close()
        if not finished and proc.poll() is None:
            # Best effort: with its output closed, the command will
            # usually stop on its next write anyway
            try:
                proc.terminate()
            except OSError:
                pass
        proc.wait()
    if check and proc.returncode != 0:
        raise RunError(proc.returncode, cmd)
