#!/usr/bin/env bash
# Copyright 2026 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eu

while [ "$1" != -- ] ; do
    echo "Mock container: accepted and ignored \"$1\""
    shift
done

shift

echo "Mock container: running$(printf " %q" "$@") as if in container"
exec "$@"
