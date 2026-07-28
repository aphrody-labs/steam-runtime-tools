#!/bin/sh
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

if [ -n "${RELOCATABLE_INSTALL_ARCHS-}" ]; then
    oldIFS="$IFS"
    IFS=,
    for arch in $RELOCATABLE_INSTALL_ARCHS; do
        if [ "$(dpkg --print-architecture)" != "$arch" ]; then
            dpkg --add-architecture "$arch"
        fi
    done
    IFS="$oldIFS"
fi

echo "#!/bin/sh" > /usr/sbin/policy-rc.d
echo "exit 101" >> /usr/sbin/policy-rc.d
chmod +x /usr/sbin/policy-rc.d
echo "Acquire::Languages \"none\";" > /etc/apt/apt.conf.d/90nolanguages
echo 'force-unsafe-io' > /etc/dpkg/dpkg.cfg.d/autopkgtest

if [ -e "$STEAMRT_CI_APT_AUTH_CONF" ]; then
    # We can't use /etc/apt/auth.conf.d in scout, only in >= soldier.
    # It can be root:root in apt >= 1.5~beta2 (>= soldier) or in
    # apt << 1.1~exp8 (scout).
    chown root:root "$STEAMRT_CI_APT_AUTH_CONF"
    chmod 0600 "$STEAMRT_CI_APT_AUTH_CONF"

    # shellcheck source=/dev/null
    case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
        (*scout*)
            ln -fns "$STEAMRT_CI_APT_AUTH_CONF" /etc/apt/auth.conf
            ;;
        (*)
            install -d /etc/apt/auth.conf.d
            ln -fns "$STEAMRT_CI_APT_AUTH_CONF" /etc/apt/auth.conf.d/ci.conf
            ;;
    esac
fi

add_beta=yes
trust_ci_key_globally=

# shellcheck source=/dev/null
suite="$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")"
steamrt_n="$suite"

case "$suite" in
    (soldier)
        if [ -n "${SOLDIER_APT_SOURCES_FILE-}" ]; then
            cp "${SOLDIER_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        steamrt_n=steamrt2
        trust_ci_key_globally=1
        ;;

    (sniper)
        if [ -n "${SNIPER_APT_SOURCES_FILE-}" ]; then
            cp "${SNIPER_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        steamrt_n=steamrt3
        trust_ci_key_globally=1
        ;;

    (steamrt3c)
        if [ -n "${STEAMRT3C_APT_SOURCES_FILE-}" ]; then
            cp "${STEAMRT3C_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        trust_ci_key_globally=1
        ;;

    (steamrt4)
        if [ -n "${STEAMRT4_APT_SOURCES_FILE-}" ]; then
            cp "${STEAMRT4_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        ;;

    (steamrt5)
        if [ -n "${STEAMRT5_APT_SOURCES_FILE-}" ]; then
            cp "${STEAMRT5_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        ;;

    (*scout*)
        if [ -n "${SCOUT_APT_SOURCES_FILE-}" ]; then
            cp "${SCOUT_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        # scout needs some special-cases for its beta suite,
        # but the beta is configured in ci/scout.sources.list anyway
        add_beta=
        steamrt_n=steamrt1
        suite=scout
        trust_ci_key_globally=1

        # Go back to Ubuntu 12.04's default gcc/g++/cpp
        update-alternatives --set g++ /usr/bin/g++-4.6
        update-alternatives --set gcc /usr/bin/gcc-4.6
        update-alternatives --set cpp-bin /usr/bin/cpp-4.6
        ;;

    (steamrt[0-9]*)
        ;;

    (*)
        # Assumed to be Debian or Ubuntu rather than the Steam Runtime
        add_beta=
        steamrt_n=
        ;;
esac

# Because we're using beta Docker images, we won't necessarily be able to
# install build-dependencies without multiarch skew unless we add the beta
# apt sources as well
if [ -n "$add_beta" ] && [ -n "$steamrt_n" ]; then
    echo "deb" \
        "[signed-by=/usr/share/keyrings/steamrt-archive-keyring.gpg]" \
        "https://repo.steampowered.com/$steamrt_n/apt" \
        "${suite}_beta main contrib non-free" \
        > /etc/apt/sources.list.d/steamrt-beta.list
fi

if [ -n "$steamrt_n" ] && [ -n "${STEAMRT_CI_APT_PGP_TRUSTED_FILE}" ]; then
    if [ -n "$trust_ci_key_globally" ]; then
        mkdir -p /etc/apt/trusted.gpg.d/
        gpg --dearmor \
            < "${STEAMRT_CI_APT_PGP_TRUSTED_FILE}" \
            > /etc/apt/trusted.gpg.d/steamrt-ci.gpg
    fi

    # STEAMRT4_APT_SOURCES_FILE, etc. are assumed to use
    # [signed-by=/etc/apt/keyrings/steamrt-ci.gpg]
    # where necessary
    mkdir -p /etc/apt/keyrings/
    gpg --dearmor \
        < "${STEAMRT_CI_APT_PGP_TRUSTED_FILE}" \
        > /etc/apt/keyrings/steamrt-ci.gpg
fi

apt-get -y update

apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    "$@"
# Optional
apt-get install -y --no-install-recommends eatmydata || :
apt-get install -y --no-install-recommends python3-tap || :
dbus-uuidgen --ensure || :
