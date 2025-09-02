/*
 * Copyright © 2020-2025 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "runtime-module.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

const char *
icd_kind_to_string (IcdKind kind)
{
  switch (kind)
    {
      case ICD_KIND_NONEXISTENT:
        return "does not exist";

      case ICD_KIND_ABSOLUTE:
        return "absolute path";

      case ICD_KIND_SONAME:
        return "SONAME";

      case ICD_KIND_META_LAYER:
        return "Vulkan meta-layer";

      default:
        g_return_val_if_reached ("(internal error!)");
    }
}

IcdDetails *
icd_details_new (gpointer icd)
{
  IcdDetails *self;
  gsize i;
  const char *name;

  g_return_val_if_fail (G_IS_OBJECT (icd), NULL);

  if (SRT_IS_DRI_DRIVER (icd))
    name = srt_dri_driver_get_library_path (icd);
  else if (SRT_IS_EGL_ICD (icd))
    name = srt_egl_icd_get_json_path (icd);
  else if (SRT_IS_EGL_EXTERNAL_PLATFORM (icd))
    name = srt_egl_external_platform_get_json_path (icd);
  else if (SRT_IS_VULKAN_ICD (icd))
    name = srt_vulkan_icd_get_json_path (icd);
  else if (SRT_IS_VULKAN_LAYER (icd))
    name = srt_vulkan_layer_get_json_path (icd);
  else if (SRT_IS_VDPAU_DRIVER (icd))
    name = srt_vdpau_driver_get_library_path (icd);
  else if (SRT_IS_VA_API_DRIVER (icd))
    name = srt_va_api_driver_get_library_path (icd);
  else if (SRT_IS_OPENXR_1_RUNTIME (icd))
    name = srt_openxr_1_runtime_get_json_path (icd);
  else
    g_return_val_if_reached (NULL);

  self = g_slice_new0 (IcdDetails);
  self->icd = g_object_ref (icd);
  self->debug_name = name;

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      self->resolved_libraries[i] = NULL;
      self->kinds[i] = ICD_KIND_NONEXISTENT;
      self->paths_in_container[i] = NULL;
    }

  return self;
}

void
icd_details_free (IcdDetails *self)
{
  gsize i;

  g_object_unref (self->icd);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_free (self->resolved_libraries[i]);
      g_free (self->paths_in_container[i]);
    }

  g_slice_free (IcdDetails, self);
}
