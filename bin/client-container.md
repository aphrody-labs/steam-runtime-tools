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

**--verbose**

</dt><dd>

Be more verbose. If used twice, debug messages are shown.

</dd>
</dl>

# ENVIRONMENT

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

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
</dl>

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **srt-client-container** may also be printed
on standard error.

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
:   Invalid arguments were given, or **steam-runtime-supervisor**
    failed to start.

126, 127
:   The *COMMAND* was not found or could not be launched.
    These errors are not currently distinguished.

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

<!-- vim:set sw=4 sts=4 et: -->
