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


logger = logging.getLogger('test-client-container')


LAUNCH_EX_FAILED = 125
LAUNCH_EX_USAGE = 125
LAUNCH_EX_CANNOT_INVOKE = 126
LAUNCH_EX_NOT_FOUND = 127

LAUNCHER_NAME_ALONGSIDE_STEAM = (
    "com.steampowered.PressureVessel.LaunchAlongsideSteam"
)
LAUNCHER_ALONGSIDE_STEAM_PREFIX = LAUNCHER_NAME_ALONGSIDE_STEAM + "."

STDIN_FILENO = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2


class TestClientContainer(BaseTest):
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

        if 'SRT_TEST_UNINSTALLED' in os.environ:
            if 'SRT_TEST_MULTIARCH' in os.environ:
                self.launcher_service_exe = Path(
                    self.top_builddir,
                    'bin',
                    os.environ['SRT_TEST_MULTIARCH'] + '-srt-launcher-service',
                )
            else:
                self.launcher_service_exe = Path(
                    self.top_builddir,
                    'bin',
                    'steam-runtime-launcher-service',
                )

            self.launch = self.command_prefix + [
                str(
                    Path(
                        self.top_builddir,
                        'bin',
                        'steam-runtime-launch-client',
                    ),
                ),
            ]
            self.client_container = self.command_prefix + [
                'env',
                'SRT_LAUNCHER_SERVICE=' + str(self.launcher_service_exe),
                str(
                    Path(
                        self.top_builddir,
                        'bin',
                        'srt-client-container'
                    ),
                ),
            ]
            self.mock_container = Path(
                self.top_srcdir,
                'tests',
                'mock-container.sh',
            )
        else:
            self.skipTest('Not available as an installed-test')

    def test_enoent_container(self) -> None:
        '''Container runner not found'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=/nonexistent',
                '--',
                'true',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertNotIn(b'Mock container:', stdout)
        self.assertEqual(proc.returncode, LAUNCH_EX_NOT_FOUND)

    def test_enoent_launcher(self) -> None:
        '''If launcher isn't found, container is never started'''
        proc = subprocess.Popen(
            self.client_container[:-1] + [
                'SRT_LAUNCHER_SERVICE=/nonexistent',
                self.client_container[-1],
                '--pv-entry-point=' + str(self.mock_container),
                '--',
                'true',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertNotIn(b'Mock container:', stdout)
        self.assertEqual(proc.returncode, LAUNCH_EX_NOT_FOUND)

    def test_enoent_payload(self) -> None:
        '''Container starts but payload cannot be found'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=' + str(self.mock_container),
                '--',
                '/nonexistent',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(
            b'Mock container: accepted and ignored "--for-steam-client"\n',
            stdout,
        )
        self.assertEqual(proc.returncode, LAUNCH_EX_NOT_FOUND)

    def test_enoexec_container(self) -> None:
        '''Container runner not executable'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=/dev/null',
                '--',
                'true',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertNotIn(b'Mock container:', stdout)
        self.assertEqual(proc.returncode, LAUNCH_EX_CANNOT_INVOKE)

    def test_enoexec_launcher(self) -> None:
        '''If launcher isn't executable, container is never started'''
        proc = subprocess.Popen(
            self.client_container[:-1] + [
                'SRT_LAUNCHER_SERVICE=/dev/null',
                self.client_container[-1],
                '--pv-entry-point=' + str(self.mock_container),
                '--',
                'true',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertNotIn(b'Mock container:', stdout)
        self.assertEqual(proc.returncode, LAUNCH_EX_CANNOT_INVOKE)

    def test_enoexec_payload(self) -> None:
        '''Container starts but payload is not executable'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=' + str(self.mock_container),
                '--',
                '/dev/null',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(
            b'Mock container: accepted and ignored "--for-steam-client"\n',
            stdout,
        )
        self.assertEqual(proc.returncode, LAUNCH_EX_CANNOT_INVOKE)

    def test_failing_launcher(self) -> None:
        '''If launcher fails to start, container is never started'''

        for not_the_launcher in ('false', 'true'):
            proc = subprocess.Popen(
                self.client_container[:-1] + [
                    'SRT_LAUNCHER_SERVICE=' + not_the_launcher,
                    self.client_container[-1],
                    '--pv-entry-point=' + str(self.mock_container),
                    '--',
                    'true',
                ],
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )
            stdout, _ = proc.communicate()
            self.assertNotIn(b'Mock container:', stdout)
            self.assertEqual(proc.returncode, LAUNCH_EX_FAILED)

    def test_launcher_respawn(self) -> None:
        '''If launcher is terminated, we restart it'''

        if 'SRT_ENABLE_SLOW_TESTS' not in os.environ:
            self.skipTest('Export SRT_ENABLE_SLOW_TESTS=1 to enable')

        quoted_launch = ' '.join(shlex.quote(x) for x in self.launch)
        script = r'''
        {launch} --alongside-steam --terminate
        sleep 5
        {launch} --alongside-steam -- sh -euc \
            'echo "SLSAS=\"$SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM\""'
        '''.format(launch=quoted_launch)

        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=' + str(self.mock_container),
                '--',
                'sh', '-euc', script,
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(
            (
                'SLSAS="'
                + LAUNCHER_ALONGSIDE_STEAM_PREFIX
                + 'Instance'
                + str(proc.pid)
                + '"\n'
            ).encode('ascii'),
            stdout,
        )
        self.assertEqual(proc.returncode, 0)

    def test_options(self) -> None:
        '''Can pass through options to pressure-vessel, and set --verbose'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=' + str(self.mock_container),
                '--pv-option=--dummy=blah blah blah',
                '-v',
                '--',
                'true',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(
            b'Mock container: accepted and ignored "--dummy=blah blah blah"\n',
            stdout,
        )
        self.assertEqual(proc.returncode, 0)

    def test_payload(self) -> None:
        '''Container sets SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=' + str(self.mock_container),
                '--',
                'sh', '-euc',
                r'echo "SLSAS=\"$SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM\""',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(
            b'Mock container: accepted and ignored "--for-steam-client"\n',
            stdout,
        )
        self.assertIn(
            (
                'SLSAS="'
                + LAUNCHER_ALONGSIDE_STEAM_PREFIX
                + 'Instance'
                + str(proc.pid)
                + '"\n'
            ).encode('ascii'),
            stdout,
        )
        self.assertEqual(proc.returncode, 0)

    def test_payload_launch_alongside(self) -> None:
        '''Container has an accompanying s-r-l-s --alongside-steam'''
        proc = subprocess.Popen(
            self.client_container + [
                '--pv-entry-point=' + str(self.mock_container),
                '--',
            ] + self.launch + [
                '--alongside-steam',
                '--',
                'sh', '-euc',
                r'echo "SLSAS=\"$SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM\""',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(
            b'Mock container: accepted and ignored "--for-steam-client"\n',
            stdout,
        )
        # The command inherits this environment variable
        self.assertIn(
            (
                'SLSAS="'
                + LAUNCHER_ALONGSIDE_STEAM_PREFIX
                + 'Instance'
                + str(proc.pid)
                + '"\n'
            ).encode('ascii'),
            stdout,
        )
        self.assertEqual(proc.returncode, 0)

    def test_version(self) -> None:
        proc = subprocess.Popen(
            self.client_container + [
                '--version',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )
        stdout, _ = proc.communicate()
        self.assertIn(b'\n Package: steam-runtime-tools\n', stdout)
        self.assertIn(b'\n Version:', stdout)
        self.assertEqual(proc.returncode, 0)

    def test_wrong_options(self) -> None:
        for options in (
            [],
            ['--'],
            ['--an-unknown-option', '--', 'true'],
        ):
            proc = subprocess.Popen(
                self.client_container + options,
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
