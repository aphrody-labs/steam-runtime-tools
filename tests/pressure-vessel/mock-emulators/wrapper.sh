#!/bin/sh
# Yes the documentation for PRESSURE_VESSEL_EMULATOR says the emulator
# can't be a script, but that doesn't apply when nothing is really being
# emulated.
set -e
exec "$@"
