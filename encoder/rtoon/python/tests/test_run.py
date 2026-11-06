#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

"""Tests for run() and iterrun(), using a stand-in command."""

import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest

import rtoon
from rtoon import RunError

FAKE = '''#!%s
import sys, time
args = sys.argv[1:]
opt = args.pop(0)
assert opt.startswith("--libxo=@rtoon"), opt
mode = args[0] if args else "doc"
if mode == "opts":
    print("opt " + (opt[len("--libxo=@rtoon"):].lstrip(":") or '""'))
elif mode == "fail":
    print("a 1")
    sys.exit(3)
elif mode == "slow":
    print("top\\n  row", flush=True)
    for i in range(1000):
        try:
            print("    - id=%%d" %% i, flush=True)
        except BrokenPipeError:
            sys.stdout = None
            sys.exit(0)
        if i >= 2:
            time.sleep(0.05)
elif mode == "slowfail":
    print("top\\n  row\\n    - id=1\\n    - id=2")
    sys.exit(4)
else:
    print("@version 1.0.0\\ntop\\n  row\\n    {id=,name}\\n    1,a\\n"
          "    2,b")
'''


class TestRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # RTOON_TEST_TMPDIR is for systems where the default temporary
        # directory does not allow running programs
        cls.tmp = tempfile.mkdtemp(dir=os.environ.get("RTOON_TEST_TMPDIR"))
        cls.cmd = os.path.join(cls.tmp, "fake-cmd")
        with open(cls.cmd, "w", encoding="utf-8") as fp:
            fp.write(FAKE % sys.executable)
        os.chmod(cls.cmd, stat.S_IRWXU)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def test_run(self):
        doc = rtoon.run([self.cmd])
        self.assertEqual(doc["top"]["row"][1], {"id": 2, "name": "b"})
        rows = rtoon.run([self.cmd], path="top/row")
        self.assertEqual(len(rows), 2)

    def test_options(self):
        self.assertEqual(rtoon.run([self.cmd, "opts"]), {"opt": ""})
        self.assertEqual(rtoon.run([self.cmd, "opts"], options="dense"),
                         {"opt": "dense"})
        self.assertEqual(rtoon.run([self.cmd, "opts"],
                                   options=["dense", "no-version"]),
                         {"opt": "dense:no-version"})

    def test_failure(self):
        with self.assertRaises(RunError) as ctx:
            rtoon.run([self.cmd, "fail"])
        err = ctx.exception
        self.assertIsInstance(err, subprocess.CalledProcessError)
        self.assertEqual(err.returncode, 3)
        self.assertEqual(err.output, "a 1\n")
        self.assertEqual(rtoon.run([self.cmd, "fail"], check=False),
                         {"a": 1})

    def test_iterrun(self):
        rows = list(rtoon.iterrun([self.cmd], path="top/row"))
        self.assertEqual([r["id"] for r in rows], [1, 2])

    def test_iterrun_streams_and_stops(self):
        start = time.monotonic()
        it = rtoon.iterrun([self.cmd, "slow"], path="top/row")
        self.assertEqual(next(it)["id"], 0)
        self.assertEqual(next(it)["id"], 1)
        it.close()
        # The whole run would take 50 seconds
        self.assertLess(time.monotonic() - start, 10)

    def test_iterrun_failure(self):
        it = rtoon.iterrun([self.cmd, "slowfail"], path="top/row")
        self.assertEqual(next(it)["id"], 1)
        self.assertEqual(next(it)["id"], 2)
        with self.assertRaises(RunError) as ctx:
            next(it)
        self.assertEqual(ctx.exception.returncode, 4)

    def test_empty_argv(self):
        with self.assertRaises(ValueError):
            rtoon.run([])


if __name__ == "__main__":
    unittest.main()
