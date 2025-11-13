/*<private_header>*/
/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "steam-runtime-tools/emulator-internal.h"

struct _SrtEmulator
{
  GObject parent;
  GStrv argv;
  GQuark *emulated_architectures;
  gchar *manifest;
  GQuark *required_architectures;
  GStrv required_libraries;
  size_t n_emulated_architectures;
  size_t n_required_architectures;
};
