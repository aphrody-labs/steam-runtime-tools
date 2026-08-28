#!/usr/bin/env python3
# Copyright 2020-2026 Collabora Ltd.
# SPDX-License-Identifier: MIT

import contextlib
import logging
import os
import subprocess
import sys
import typing
from pathlib import Path

from testutils import (
    BaseTest,
    ensure_terminated,
    run_subprocess,
    test_main,
)


logger = logging.getLogger('test-invocation')

STDERR_FILENO = 2


class TestHelpers(BaseTest):
    def run_subprocess(
        self,
        args: typing.Union[typing.List[str], str],
        check: bool = False,
        input: typing.Optional[bytes] = None,
        timeout: typing.Optional[int] = None,
        **kwargs: typing.Any
    ) -> None:
        logger.info('Running: %r', args)
        return run_subprocess(
            args, check=check, input=input, timeout=timeout, **kwargs
        )

    def setUp(self) -> None:
        super().setUp()
        self.uname = os.uname()

        if 'SRT_TEST_UNINSTALLED' not in os.environ:
            self.skipTest('Not available as an installed-test')

    def test_help_and_version(self) -> None:
        tools = [
            'pressure-vessel-wrap',
            'pv-adverb',
            'pv-try-setlocale',
            'pv-verify',
            'srt-client-container',
            'srt-logger',
            'srt-run-outside-ldlp',
            'steam-runtime-check-requirements',
            'steam-runtime-dialog-ui',
            'steam-runtime-identify-library-abi',
            'steam-runtime-input-monitor',
            'steam-runtime-launch-client',
            'steam-runtime-launcher-service',
            'steam-runtime-libcurl-compat-setup',
            'steam-runtime-steam-remote',
            'steam-runtime-supervisor',
            'steam-runtime-system-info',
            'steam-runtime-urlopen',
        ]

        multiarch = os.environ.get('SRT_TEST_MULTIARCH', '')

        for tool in tools:
            with contextlib.ExitStack() as stack:
                logger.debug('%s...', tool)
                stack.enter_context(self.subTest(tool))

                if (
                    tool == 'steam-runtime-launcher-service'
                    and multiarch
                ):
                    exe = Path(
                        self.top_builddir,
                        'bin',
                        multiarch + '-srt-' + tool[len('steam-runtime-'):],
                    )
                elif tool == 'steam-runtime-libcurl-compat-setup':
                    exe = Path(self.top_builddir, 'libcurl-compat', tool)
                elif tool.startswith(('pv-', 'pressure-vessel-')):
                    exe = Path(self.top_builddir, 'pressure-vessel', tool)
                else:
                    exe = Path(self.top_builddir, 'bin', tool)

                if (
                    (
                        tool in (
                            'srt-client-container',
                            'steam-runtime-dialog-ui',
                            'steam-runtime-libcurl-compat-setup',
                        )
                        or tool.startswith(('pv-', 'pressure-vessel-'))
                    )
                    and not exe.exists()
                ):
                    # These are not always compiled
                    continue

                if tool == 'steam-runtime-steam-remote':
                    # s-r-steam-remote doesn't have --help or --version
                    continue

                help_mentions_help = True
                has_version = True
                has_version_only = False
                yaml_version = True

                if tool in (
                    'pv-try-setlocale',
                    'srt-run-outside-ldlp',
                ):
                    has_version = yaml_version = False
                elif tool == 'steam-runtime-urlopen':
                    yaml_version = False
                elif tool == 'pressure-vessel-wrap':
                    has_version_only = True

                if tool in (
                    'pv-try-setlocale',
                    'steam-runtime-dialog-ui',
                    'steam-runtime-system-info',
                ):
                    # TODO: Ideally we'd be more consistent with formatting
                    help_mentions_help = False

                proc = subprocess.Popen(
                    ['env', 'LC_ALL=C', str(exe), '--help'],
                    stdout=subprocess.PIPE,
                    stderr=STDERR_FILENO,
                )
                stack.enter_context(ensure_terminated(proc))
                stdout, _ = proc.communicate(input=b'', timeout=10)

                if help_mentions_help:
                    self.assertIn(b'--help', stdout)
                else:
                    self.assertIn(b'Usage:', stdout)

                self.assertEqual(proc.returncode, 0)

                if has_version:
                    proc = subprocess.Popen(
                        [str(exe), '--version'],
                        stdout=subprocess.PIPE,
                        stderr=STDERR_FILENO,
                    )
                    stack.enter_context(ensure_terminated(proc))
                    stdout, _ = proc.communicate(input=b'', timeout=10)

                    if yaml_version:
                        self.assertIn(
                            b'\n Package: steam-runtime-tools\n',
                            stdout,
                        )
                        self.assertIn(b'\n Version:', stdout)
                    else:
                        self.assertNotIn(b'Package:', stdout)
                        self.assertNotIn(b'Version:', stdout)

                    self.assertEqual(proc.returncode, 0)

                if has_version_only:
                    proc = subprocess.Popen(
                        [str(exe), '--version-only'],
                        stdout=subprocess.PIPE,
                        stderr=STDERR_FILENO,
                    )
                    stack.enter_context(ensure_terminated(proc))
                    stdout, _ = proc.communicate(input=b'', timeout=10)
                    self.assertNotIn(b'Package:', stdout)
                    self.assertNotIn(b'Version:', stdout)
                    self.assertEqual(proc.returncode, 0)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
