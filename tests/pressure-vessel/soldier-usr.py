#!/usr/bin/env python3
# Copyright 2020-2026 Collabora Ltd.
# SPDX-License-Identifier: MIT

"""
See tests/pressure-vessel/containers.py
"""

import logging
import os
import sys

from containers import BaseContainersTest
from testutils import test_main


logger = logging.getLogger('test-soldier-usr')


class TestSoldierUsr(BaseContainersTest):
    def test_soldier_usr(self) -> None:
        soldier = os.path.join(self.containers_dir, 'soldier')

        with self.subTest('only-prepare'):
            self._test_soldier(
                'soldier_prep', soldier, copy=True, only_prepare=True,
            )

        with self.subTest('copy'):
            self._test_soldier(
                'soldier_copy', soldier, copy=True, locales=True,
            )

        cls = self.__class__
        if cls.is_x86:
            with self.subTest('prepare-mock-emulator-usr'):
                self._test_soldier(
                    'soldier_prepare_mock_emulator_usr', soldier,
                    copy=True,
                    locales=True,
                    mock_emulator='env.json',
                    only_prepare=True,
                )

            with self.subTest('prepare-mock-emulator-local'):
                self._test_soldier(
                    'soldier_prepare_mock_emulator_local', soldier,
                    copy=True,
                    locales=True,
                    mock_emulator='sh.json',
                    only_prepare=True,
                )

            with self.subTest('mock-emulator-usr'):
                self._test_soldier(
                    'soldier_mock_emulator_usr', soldier,
                    copy=True,
                    locales=True,
                    mock_emulator='env.json',
                )

            with self.subTest('mock-emulator-local'):
                self._test_soldier(
                    'soldier_mock_emulator_local', soldier,
                    copy=True,
                    locales=True,
                    mock_emulator='sh.json',
                )

        with self.subTest('fake-home,bwrap-setuid'):
            self._test_soldier(
                'soldier_fake_home',
                soldier,
                copy=True,
                fake_home=True,
                workarounds=['+bwrap-setuid'],
            )

        with self.subTest('transient'):
            self._test_soldier('soldier', soldier)


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
