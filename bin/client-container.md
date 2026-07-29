---
title: srt-client-container
section: 1
...

<!-- This document:
Copyright © 2026 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

srt-client-container - run the Steam Client in a container

# SYNOPSIS

**srt-client-container**
[**--pv-entry-point** *SCRIPT*]
[**--pv-option** *OPTION*...]
[**--verbose** [**--verbose**]]
[**--**]
[*COMMAND* [*ARGUMENTS...*]]

but in practice,

**srt-client-container**
*OPTIONS*
**-- …/steamrt64/steam**
[*ARGUMENTS...*]

# DESCRIPTION

**srt-client-container** is primarily used to run the Steam Client main
executable inside a `steamrt3c` container.

As well as the container itself,
it is also responsible for running an instance of
**steam-runtime-launcher-service**(1),
and setting environment variables so that whenever either the Steam Client
or a child process of the **steam-runtime-launcher-service**
runs `steam-runtime-launch-client --alongside-steam`,
it is that instance of **steam-runtime-launcher-service** that will be
contacted.

Instead of running the actual Steam Client,
**srt-client-container** can also be used to run some other command inside a
similar container,
for debugging purposes.

# OPTIONS

<dl>
<dt>

**--pv-entry-point** *SCRIPT*

</dt><dd>

Use the given *SCRIPT* as the entry point for a `steamrt3c` container.
It must accept the same options as **pressure-vessel-wrap**(1).
The default is to assume that **srt-client-container** is in the
`pressure-vessel/` subdirectory of a container runtime,
and use the adjacent `./run` script.

</dd>
<dt>

**--pv-option** *OPTION*

</dt><dd>

Pass the given *OPTION* to the entry-point script,
and therefore to **pressure-vessel-wrap**(1).
Any option that is accepted by **pressure-vessel-wrap**(1) can be used.

Often the syntax with an **=** sign,
such as **--pv-option=--devel** or **--pv-option=--terminal=xterm**,
will be the most convenient.

</dd>
<dt>

**--verbose**, **-v**

</dt><dd>

Be more verbose.
If used twice (**-vv**),
debug messages are shown.

</dd>
</dl>

# ENVIRONMENT

## Read by srt-client-container

<dl>
<dt>

`PRESSURE_VESSEL_LOG_INFO` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=info` or **--verbose**

</dd>
<dt>

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=timestamp`

</dd>
<dt>

`SRT_LAUNCHER_SERVICE` (path)

</dt><dd>

If set,
use this executable instead of automatically selecting a nearby
`libexec/steam-runtime-tools-0/${architecture}-srt-launcher-service`
or
`bin/steam-runtime-launcher-service`
executable.

</dd>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
</dl>

## Set by srt-client-container

<dl>
<dt>

`SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM` (D-Bus name)

</dt><dd>

Set to the bus name of the accompanying **steam-runtime-launcher-service**
instance,
in both the Steam Runtime container and the
**steam-runtime-launcher-service**.

The result is that if the *COMMAND* starts another command using
`steam-runtime-launcher-service --alongside-steam -- OTHER`,
or if any command that is a descendant of the
**steam-runtime-launcher-service** does the same thing,
then the command *OTHER* will be run by the
**steam-runtime-launcher-service** that was started by
**srt-client-container**
(as opposed to any other instance of
`steam-runtime-launcher-service --alongside-steam` that might exist),
as long as none of the descendant processes overwrite
the `SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM` environment variable.

</dd>
</dl>

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **srt-client-container**,
**steam-runtime-launcher-service**,
**pressure-vessel**,
etc. may also be printed on standard error.

# PROCESS LIFECYCLE

**srt-client-container** continues to run for as long as the *COMMAND* does.

If the *COMMAND* exits or is killed by a signal,
**srt-client-container** will terminate its
**steam-runtime-launcher-service** child with `SIGTERM`,
then exit with a representation of the exit status of the *COMMAND*.

If the **steam-runtime-launcher-service** child exits or crashes,
a new instance will be started with the same D-Bus name,
hopefully minimizing disruption to the *COMMAND*.

If **srt-client-container** exits or crashes for any reason,
both the container that contains the *COMMAND*
and the **steam-runtime-launcher-service** child will be killed by `SIGTERM`.
Normally this results in the *COMMAND* also being killed by `SIGTERM`.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given,
    or **srt-client-container** failed to start.

126
:   Either the *COMMAND*,
    the pressure-vessel container entry point,
    or the **steam-runtime-launcher-service** was found
    but could not be executed.

127
:   Either the *COMMAND*,
    the pressure-vessel container entry point,
    or the **steam-runtime-launcher-service** was not found.

255
:   The *COMMAND* was launched, but its exit status could not be
    determined. This happens if the wait-status was neither
    normal exit nor termination by a signal.

Any value
:   The *COMMAND* exited unsuccessfully with the status indicated.

128 + *n*
:   The *COMMAND* was killed by signal *n*.
    (This is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1).)

# EXAMPLES

    cd ~/.steam/root
    steamrt="$(pwd)/steamrt64/pv-runtime/steam-runtime-steamrt"
    pv="$steamrt/pressure-vessel"

    $pv/libexec/steam-runtime-tools-0/srt-client-container \
        -- \
        steam-runtime-launch-client --alongside-steam -- pstree -Tahlp

    $pv/libexec/steam-runtime-tools-0/srt-client-container \
        --pv-entry-point="$steamrt/run"
        --pv-option=--devel \
        --pv-option=--runtime=…/sdk/files \
        -- \
        steamrt64/steam

<!-- vim:set sw=4 sts=4 et: -->
