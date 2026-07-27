#!/bin/sh
# Copyright 2021-2026 Collabora Ltd.
# SPDX-License-Identifier: MIT

# Build a runtime that is mostly equivalent to an official beta runtime,
# except that pressure-vessel/ is replaced by the just-built local version,
# and supporting scripts, etc. are taken from this source tree.

set -eux

suite="${1:-steamrt4}"
builddir="${2:-_build}"
PRESSURE_VESSEL="${3:-_build/pressure-vessel-bin.tar.gz}"

rm -fr "$builddir/depot"
rm -fr "$builddir/depot-template"
mkdir -p "$builddir/depot"
mkdir -p "$builddir/depot-template/common"

case "$suite" in
    (steamrt*c)
        dest="steam-runtime-steamrt-amd64"
        ;;
    (steamrt*)
        dest="SteamLinuxRuntime_${suite#steamrt}"
        ;;
    (*)
        dest="SteamLinuxRuntime_$suite"
        ;;
esac

set --

case "$suite" in
    (soldier | sniper | steamrt4)
        set -- "$@" --toolmanifest
        ;;

    (steamrt*c)
        set -- "$@" --add-bin-directory
        ;;
esac

if [ -f "$builddir/VERSION.txt" ]; then
    set -- "$@" --scripts-version="$(cat "$builddir/VERSION.txt")"
fi

exec python3 ./subprojects/container-runtime/populate-depot.py \
    --cache="$builddir/cache" \
    --depot="$builddir/depots/$dest" \
    --depot-archive="${dest}.tar.xz" \
    --pressure-vessel-archive "${PRESSURE_VESSEL}" \
    --version=latest-public-beta \
    "$@" \
    "${suite}"

# vim:set sw=4 sts=4 et:
