#!/bin/sh
# Yes the documentation for PRESSURE_VESSEL_EMULATOR says the emulator
# can't be a script, but that doesn't apply when nothing is really being
# emulated.
set -e

if [ "$PV_TEST_SET_WHEN_EMULATING" != set ]; then
    echo "PV_TEST_SET_WHEN_EMULATING should be 'set' but is: $PV_TEST_SET_WHEN_EMULATING" >&2
    exit 1
fi

if [ "${PV_TEST_UNSET_WHEN_EMULATING+set}" = set ]; then
    echo "PV_TEST_UNSET_WHEN_EMULATING should be unset but is: $PV_TEST_UNSET_WHEN_EMULATING" >&2
    exit 1
fi

if [ -d /run/pressure-vessel ]; then
    if [ "$PV_TEST_SET_IN_CONTAINER" != set ]; then
        echo "PV_TEST_SET_IN_CONTAINER should be 'set' but is: $PV_TEST_SET_IN_CONTAINER" >&2
        exit 1
    fi
    if [ "${PV_TEST_UNSET_IN_CONTAINER+set}" = set ]; then
        echo "PV_TEST_UNSET_IN_CONTAINER should be unset but is: $PV_TEST_UNSET_IN_CONTAINER" >&2
        exit 1
    fi
else
    if [ "$PV_TEST_SET_IN_CONTAINER" = set ]; then
        echo "PV_TEST_SET_IN_CONTAINER should not apply when not in PV" >&2
        exit 1
    fi
fi

exec "$@"
