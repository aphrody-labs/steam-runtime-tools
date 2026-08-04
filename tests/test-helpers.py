#!/usr/bin/env python3
# Copyright 2020-2026 Collabora Ltd.
# SPDX-License-Identifier: MIT

import logging
import os
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import typing
from pathlib import Path

from testutils import (
    BaseTest,
    run_subprocess,
    test_main,
)


logger = logging.getLogger('test-helpers')


LAUNCH_EX_FAILED = 125
LAUNCH_EX_USAGE = 125
LAUNCH_EX_CANNOT_INVOKE = 126
LAUNCH_EX_NOT_FOUND = 127

STDIN_FILENO = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2


class TestHelpers(BaseTest):
    def run_subprocess(
        self,
        args,           # type: typing.Union[typing.List[str], str]
        check=False,
        input=None,     # type: typing.Optional[bytes]
        timeout=None,   # type: typing.Optional[int]
        **kwargs        # type: typing.Any
    ):
        logger.info('Running: %r', args)
        return run_subprocess(
            args, check=check, input=input, timeout=timeout, **kwargs
        )

    def setUp(self) -> None:
        super().setUp()
        self.uname = os.uname()

        if 'SRT_TEST_UNINSTALLED' not in os.environ:
            self.skipTest('Not available as an installed-test')
        elif 'SRT_TEST_MULTIARCH' not in os.environ:
            self.skipTest('Not available when multiarch tuple is unknown')
        else:
            self.exec_exe = Path(
                self.top_builddir,
                'helpers',
                os.environ['SRT_TEST_MULTIARCH'] + '-exec',
            )

    def test_exec_argument_passthrough(self) -> None:
        proc = subprocess.Popen(
            [str(self.exec_exe), '--', 'sh', '-euc', 'echo Yes'],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(b'Yes', stdout)
        self.assertEqual(proc.returncode, 0)

    def test_exec_basic(self) -> None:
        proc = subprocess.Popen(
            [str(self.exec_exe), 'echo', 'Yes'],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(b'Yes', stdout)
        self.assertEqual(proc.returncode, 0)

    def test_exec_enoent(self) -> None:
        proc = subprocess.Popen(
            [str(self.exec_exe), '/nonexistent'],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.communicate()
        self.assertEqual(proc.returncode, LAUNCH_EX_NOT_FOUND)

    def test_exec_enoexec(self) -> None:
        proc = subprocess.Popen(
            [str(self.exec_exe), '/dev/null'],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.communicate()
        self.assertEqual(proc.returncode, LAUNCH_EX_CANNOT_INVOKE)

    def test_exec_version(self) -> None:
        proc = subprocess.Popen(
            [str(self.exec_exe), '--version'],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(b'\n Package: steam-runtime-tools\n', stdout)
        self.assertIn(b'\n Version:', stdout)
        self.assertEqual(proc.returncode, 0)

    def test_exec_wrong_options(self) -> None:
        for options in (
            [],
            ['--'],
            ['--an-unknown-option', '--', 'true'],
        ):
            proc = subprocess.Popen(
                [str(self.exec_exe)] + options,
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )
            proc.wait()
            self.assertEqual(proc.returncode, LAUNCH_EX_USAGE)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
