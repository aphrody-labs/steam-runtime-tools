#!/bin/bash
# Copyright © 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eu

if [ -n "${TESTS_ONLY-}" ]; then
    echo "1..0 # SKIP This distro is too old to run populate-depot.py"
    exit 0
fi

populate_depot_args=( \
    "--scripts-version=test test" \
    "--steam-app-id=1628350" \
    "--steam-depot-id=1628351" \
)

if [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --credential-env IMAGES_DOWNLOAD_CREDENTIAL \
    )
fi

if [ -n "${IMAGES_DOWNLOAD_URL-}" ] && [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --images-uri "${IMAGES_DOWNLOAD_URL}"/steamrt-SUITE/snapshots \
    )
elif [ -n "${IMAGES_SSH_HOST-}" ] && [ -n "${IMAGES_SSH_PATH-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --ssh-host "${IMAGES_SSH_HOST}" \
        --ssh-path "${IMAGES_SSH_PATH}" \
    )
else
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --version latest-container-runtime-public-beta \
    )
fi

pressure_vessel_args=()

if [ -n "${PRESSURE_VESSEL_SSH_HOST-"${IMAGES_SSH_HOST-}"}" ] && [ -n "${PRESSURE_VESSEL_SSH_PATH-}" ]; then
    pressure_vessel_args=( \
        "${pressure_vessel_args[@]}" \
        --ssh-host="${PRESSURE_VESSEL_SSH_HOST-"${IMAGES_SSH_HOST}"}" \
        --ssh-path="${PRESSURE_VESSEL_SSH_PATH}" \
    )
elif [ -n "${PRESSURE_VESSEL_DOWNLOAD_URL-}" ]; then
    pressure_vessel_args=( \
        "${pressure_vessel_args[@]}" \
        --uri="${PRESSURE_VESSEL_DOWNLOAD_URL}" \
    )
fi

echo "1..1"

mkdir -p .cache
python3 ./download-pressure-vessel.py "${pressure_vessel_args[@]}" .cache

rm -fr depots/test-sniper-unpacked
mkdir -p depots/test-sniper-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-sniper-unpacked \
    --depot-archive=depots/SteamLinuxRuntime_sniper.tar.xz \
    --fast \
    --no-include-archives \
    --pressure-vessel-archive=.cache/pressure-vessel-bin.tar.gz \
    --add-bin-directory \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "${populate_depot_args[@]}" \
    sniper \
    ${NULL+}

find depots/test-sniper-unpacked -ls > depots/test-sniper-unpacked.txt

if ! [ -e "depots/test-sniper-unpacked/steampipe/app_build_1628350.vdf" ]; then
    echo "Bail out! app_build_1628350.vdf not found"
    exit 1
fi

if ! [ -e "depots/test-sniper-unpacked/bin" ]; then
    echo "Bail out! bin/ not found"
    exit 1
fi

# This will be a loop across multiple depots later
# shellcheck disable=SC2043
for depot_id in 1628351; do
    path_prefix=""

    if ! [ -e "depots/test-sniper-unpacked/steampipe/depot_build_$depot_id.vdf" ]; then
        echo "Bail out! depot_build_$depot_id.vdf not found"
        exit 1
    fi

    for path in \
        README.md \
        VERSIONS.txt \
        _v2-entry-point \
        mtree.txt.gz \
        pressure-vessel \
        run \
        run-in-sniper \
        toolmanifest.vdf \
    ; do
        if [ -e "depots/test-sniper-unpacked/${path_prefix}${path}" ]; then
            echo "# $depot_id has $path on-disk"
        else
            echo "Bail out! Depot $depot_id lacks $path"
            exit 1
        fi

        if [ -d "depots/test-sniper-unpacked/${path_prefix}${path}" ]; then
            path_suffix='/*'
        else
            path_suffix=''
        fi

        if grep -q -F "\"LocalPath\" \"../${path_prefix}${path}${path_suffix}\"" \
            "depots/test-sniper-unpacked/steampipe/depot_build_$depot_id.vdf"
        then
            echo "# $depot_id VDF has $path"
        else
            echo "Bail out! depot_build_$depot_id.vdf lacks $path"
            exit 1
        fi
    done

    for path in \
        pressure-vessel/bin/pressure-vessel-wrap \
        pressure-vessel/libexec/steam-runtime-tools-0/i386-linux-gnu-exec \
        pressure-vessel/libexec/steam-runtime-tools-0/x86_64-linux-gnu-exec \
    ; do
        if [ -e "depots/test-sniper-unpacked/${path_prefix}${path}" ]; then
            echo "# $depot_id has $path on-disk"
        else
            echo "Bail out! Depot $depot_id lacks $path"
            exit 1
        fi
    done

    pv_verify=

    case "$(uname -m)" in
        (x86_64)
            pv_verify=depots/test-sniper-unpacked/pressure-vessel/bin/pv-verify
            ;;
        # In principle we could run a different copy of pv-verify on non-x86,
        # but our CI runners and development systems are x86 so this is
        # good enough
    esac

    if [ -n "$pv_verify" ]; then
        if ! "$pv_verify" depots/test-sniper-unpacked; then
            echo "Bail out! Depot $depot_id verification failed"
            exit 1
        fi
    fi
done

echo "ok 1 - sniper, running from unpacked directory"

# vim:set sw=4 sts=4 et:
