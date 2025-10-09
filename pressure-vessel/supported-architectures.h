/*
 * Copyright © 2020-2022 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"

/*
 * PvMultiarchLibdirsFlags:
 * @PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN:
 *  Return all library directories from which we might need to delete
 *  overridden libraries shipped in the runtime.
 */
typedef enum
{
  PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN = (1 << 0),
  PV_MULTIARCH_LIBDIRS_FLAGS_NONE = 0
} PvMultiarchLibdirsFlags;

GPtrArray *pv_known_architecture_get_libdirs (const char *tuple,
                                              const SrtKnownArchitecture *known,
                                              PvMultiarchLibdirsFlags flags);

void pv_architecture_array_populate_from_environ (GArray *arch_quarks);
