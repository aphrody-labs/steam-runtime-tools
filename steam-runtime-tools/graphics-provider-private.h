/*<private_header>*/
/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "steam-runtime-tools/graphics-provider-internal.h"

#include "steam-runtime-tools/architecture-internal.h"

struct _SrtGraphicsProviderArchitecture
{
  GQuark tuple_quark;
};

#define SRT_GRAPHICS_PROVIDER_ARCHITECTURE_INIT \
{ \
  .tuple_quark = SRT_ARCHITECTURE_QUARK_NONE, \
}

void _srt_graphics_provider_architecture_clear (void *p);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SrtGraphicsProviderArchitecture,
                                  _srt_graphics_provider_architecture_clear)

GArray *_srt_graphics_provider_architecture_array_new (const GQuark *tuple_quarks,
                                                       size_t n_tuple_quarks);

struct _SrtGraphicsProvider
{
  GObject parent;
  SrtSysroot *root;
  char *manifest;
  /* (element-type SrtGraphicsProviderArchitecture) */
  GArray *archs;
  GQuark *tuple_quarks;
};

static inline SrtGraphicsProvider *
_srt_graphics_provider_new (GArray *archs,
                            const char *manifest,
                            SrtSysroot *root)
{
  g_return_val_if_fail (archs != NULL, NULL);
  g_return_val_if_fail (archs->len != 0, NULL);
  /* manifest may be %NULL */
  g_return_val_if_fail (SRT_IS_SYSROOT (root), NULL);

  return g_object_new (SRT_TYPE_GRAPHICS_PROVIDER,
                       "architectures", archs,
                       "manifest", manifest,
                       "root", root,
                       NULL);
}
