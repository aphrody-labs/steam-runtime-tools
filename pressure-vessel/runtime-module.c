/*
 * Copyright © 2020-2025 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "runtime-module.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/graphics-drivers-internal.h"
#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"

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

/*
 * icd_details_fill_array_single_arch:
 * @details_arr: (element-type IcdDetails): The array to populate,
 *  as returned by icd_details_array_sized_new()
 * @drivers: (element-type SrtBaseGraphicsModule): The drivers as discovered
 *  by #SrtSystemInfo, which must be DRI, VA-API or VDPAU
 * @multiarch_index: An index into @pv_multiarch_tuples
 *
 * Build an array of #IcdDetails, and fill in the @resolved_libraries,
 * for architecture @multiarch_index only.
 *
 * Returns: (transfer container) (element-type IcdDetails): An array of
 *  representations of the drivers
 */
void
icd_details_fill_array_single_arch (GPtrArray *details_arr,
                                    const GList *drivers,
                                    gsize multiarch_index)
{
  const GList *icd_iter;
  gsize j;

  for (icd_iter = drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
    {
      SrtBaseGraphicsModule *module = icd_iter->data;
      g_autoptr(IcdDetails) details = icd_details_new (module);

      g_assert (SRT_IS_DRI_DRIVER (module)
                || SRT_IS_VA_API_DRIVER (module)
                || SRT_IS_VDPAU_DRIVER (module));
      g_assert (!SRT_IS_BASE_JSON_GRAPHICS_MODULE (module));

      g_assert (details->resolved_libraries[multiarch_index] == NULL);
      details->resolved_libraries[multiarch_index] = _srt_base_graphics_module_resolve_library_path (module);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
      g_assert (g_path_is_absolute (details->resolved_libraries[multiarch_index]));
      g_ptr_array_add (details_arr, g_steal_pointer (&details));
    }
}

/*
 * icd_details_fill_array_json_based:
 * @details_arr: (element-type IcdDetails): The array to populate,
 *  as returned by icd_details_array_sized_new()
 * @which: A description of the kind of driver, for example
 *  "Vulkan implicit layer"
 * @drivers: (element-type SrtBaseJsonGraphicsModule): The drivers as
 *  discovered by #SrtSystemInfo, which must be based on a JSON manifest
 *
 * Build an array of those @drivers that are not in an error state.
 *
 * Returns: (transfer container) (element-type IcdDetails): An array of
 *  representations of the drivers
 */
void
icd_details_fill_array_json_based (GPtrArray *details_arr,
                                   const char *which,
                                   const GList *drivers)
{
  gsize i;
  const GList *icd_i;

  for (icd_i = drivers, i = 0; icd_i != NULL; icd_i = icd_i->next, i++)
    {
      SrtBaseJsonGraphicsModule *module = icd_i->data;
      SrtBaseGraphicsModule *base = &module->parent;
      const gchar *path;
      GError *local_error = NULL;

      g_return_if_fail (SRT_IS_BASE_JSON_GRAPHICS_MODULE (module));
      path = module->json_path;

      if (!_srt_base_graphics_module_check_error (base, &local_error))
        {
          g_warning ("Failed to load %s #%" G_GSIZE_FORMAT " from %s: %s",
                     which, i, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      const gchar *description = base->library_path;

      if (description == NULL)
        description = "meta-layer";

      g_info ("%s #%" G_GSIZE_FORMAT " at %s: %s",
              which, i, path, description);

      g_ptr_array_add (details_arr, icd_details_new (module));
    }
}
