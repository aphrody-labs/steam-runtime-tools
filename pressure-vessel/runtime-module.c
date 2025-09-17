/*
 * Copyright © 2020-2025 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "runtime-module.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/graphics-drivers-internal.h"
#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"
#include "steam-runtime-tools/profiling-internal.h"

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

static IcdDetails *
icd_details_new (PvGraphicsProvider *provider,
                 gpointer icd)
{
  IcdDetails *self;
  gsize i;
  const char *name;

  g_return_val_if_fail (SRT_IS_BASE_GRAPHICS_MODULE (icd), NULL);

  if (SRT_IS_BASE_JSON_GRAPHICS_MODULE (icd))
    {
      SrtBaseJsonGraphicsModule *j = SRT_BASE_JSON_GRAPHICS_MODULE (icd);

      g_return_val_if_fail (j->json_path != NULL, NULL);
      name = j->json_path;
    }
  else if (SRT_IS_DRI_DRIVER (icd)
           || SRT_IS_VA_API_DRIVER (icd)
           || SRT_IS_VDPAU_DRIVER (icd))
    {
      SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (icd);

      g_return_val_if_fail (base->library_path != NULL, NULL);
      name = base->library_path;
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  self = g_slice_new0 (IcdDetails);
  self->provider = g_object_ref (provider);
  self->icd = g_object_ref (icd);
  self->debug_name = name;

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    pv_module_per_arch_init (&self->archs[i]);

  return self;
}

void
icd_details_free (IcdDetails *self)
{
  gsize i;

  g_object_unref (self->icd);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    pv_module_per_arch_clear (&self->archs[i]);

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
 * Build an array of #IcdDetails, and fill in the @resolved_library
 * for architecture @multiarch_index only.
 *
 * Returns: (transfer container) (element-type IcdDetails): An array of
 *  representations of the drivers
 */
void
icd_details_fill_array_single_arch (GPtrArray *details_arr,
                                    PvGraphicsProvider *provider,
                                    const GList *drivers,
                                    gsize multiarch_index)
{
  const GList *icd_iter;
  gsize j;

  for (icd_iter = drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
    {
      SrtBaseGraphicsModule *module = icd_iter->data;
      g_autoptr(IcdDetails) details = icd_details_new (provider, module);
      PvModulePerArch *arch = &details->archs[multiarch_index];

      g_assert (SRT_IS_DRI_DRIVER (module)
                || SRT_IS_VA_API_DRIVER (module)
                || SRT_IS_VDPAU_DRIVER (module));
      g_assert (!SRT_IS_BASE_JSON_GRAPHICS_MODULE (module));

      g_assert (arch->resolved_library == NULL);
      arch->resolved_library = _srt_base_graphics_module_resolve_library_path (module);
      g_assert (arch->resolved_library != NULL);
      g_assert (g_path_is_absolute (arch->resolved_library));
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
static void
icd_details_fill_array_json_based (GPtrArray *details_arr,
                                   PvGraphicsProvider *provider,
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

      g_ptr_array_add (details_arr, icd_details_new (provider, module));
    }
}

IcdStack *
icd_stack_new (void)
{
  return g_slice_new0 (IcdStack);
}

static void
icd_stack_enumerate_egl_icds (IcdStack *self,
                              PvGraphicsProvider *provider,
                              SrtSystemInfo *system_info,
                              const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) egl_icds = NULL;

  timer = _srt_profiling_start ("Enumerating EGL ICDs on %s system", which_system);
  g_debug ("Enumerating EGL ICDs on %s system...", which_system);
  egl_icds = srt_system_info_list_egl_icds (system_info, NULL);

  if (self->egl_icd_details == NULL)
    self->egl_icd_details = icd_details_array_sized_new (g_list_length (egl_icds));

  icd_details_fill_array_json_based (self->egl_icd_details,
                                     provider,
                                     "EGL ICD",
                                     egl_icds);
}

static void
icd_stack_enumerate_egl_ext_platforms (IcdStack *self,
                                       PvGraphicsProvider *provider,
                                       SrtSystemInfo *system_info,
                                       const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) egl_ext_platforms = NULL;

  timer = _srt_profiling_start ("Enumerating EGL external platforms on %s system",
                                which_system);
  g_debug ("Enumerating EGL external platforms on %s system...", which_system);
  egl_ext_platforms = srt_system_info_list_egl_external_platforms (system_info,
                                                                   NULL);

  if (self->egl_ext_platform_details == NULL)
    self->egl_ext_platform_details = icd_details_array_sized_new (g_list_length (egl_ext_platforms));

  icd_details_fill_array_json_based (self->egl_ext_platform_details,
                                     provider,
                                     "EGL external platform",
                                     egl_ext_platforms);
}

static void
icd_stack_enumerate_vulkan_icds (IcdStack *self,
                                 PvGraphicsProvider *provider,
                                 SrtSystemInfo *system_info,
                                 const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) vulkan_icds = NULL;

  timer = _srt_profiling_start ("Enumerating Vulkan ICDs on %s system", which_system);
  g_debug ("Enumerating Vulkan ICDs on %s system...", which_system);
  vulkan_icds = srt_system_info_list_vulkan_icds (system_info, NULL);

  if (self->vulkan_icd_details == NULL)
    self->vulkan_icd_details = icd_details_array_sized_new (g_list_length (vulkan_icds));

  icd_details_fill_array_json_based (self->vulkan_icd_details,
                                     provider,
                                     "Vulkan ICD",
                                     vulkan_icds);
}

static void
icd_stack_enumerate_vulkan_layers (IcdStack *self,
                                   PvGraphicsProvider *provider,
                                   SrtSystemInfo *system_info,
                                   const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) vulkan_exp_layers = NULL;
  g_autoptr(SrtObjectList) vulkan_imp_layers = NULL;

  timer = _srt_profiling_start ("Enumerating Vulkan layers on %s system", which_system);

  g_debug ("Enumerating Vulkan explicit layers on %s system...", which_system);
  vulkan_exp_layers = srt_system_info_list_explicit_vulkan_layers (system_info);

  if (self->vulkan_exp_layer_details == NULL)
    self->vulkan_exp_layer_details = icd_details_array_sized_new (g_list_length (vulkan_exp_layers));

  icd_details_fill_array_json_based (self->vulkan_exp_layer_details,
                                     provider,
                                     "Vulkan explicit layer",
                                     vulkan_exp_layers);

  g_debug ("Enumerating Vulkan implicit layers on %s system...", which_system);
  vulkan_imp_layers = srt_system_info_list_implicit_vulkan_layers (system_info);

  if (self->vulkan_imp_layer_details == NULL)
    self->vulkan_imp_layer_details = icd_details_array_sized_new (g_list_length (vulkan_imp_layers));

  icd_details_fill_array_json_based (self->vulkan_imp_layer_details,
                                     provider,
                                     "Vulkan implicit layer",
                                     vulkan_imp_layers);
}

static void
icd_stack_enumerate_openxr_1_runtimes (IcdStack *self,
                                       PvGraphicsProvider *provider,
                                       SrtSystemInfo *system_info,
                                       const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  gsize i;

  timer = _srt_profiling_start ("Enumerating OpenXR 1 runtimes on %s system",
                                which_system);
  g_debug ("Enumerating OpenXR 1 runtimes on %s system...", which_system);

  if (self->openxr_1_runtime_details == NULL)
    self->openxr_1_runtime_details = g_hash_table_new_full (g_str_hash,
                                                            g_str_equal,
                                                            g_free,
                                                            (GDestroyNotify) icd_details_free);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autoptr(SrtOpenXr1Runtime) rt = NULL;
      g_autoptr(GError) local_error = NULL;
      const char *multiarch_tuple = pv_multiarch_tuples[i];
      const char *path;

      rt = srt_system_info_dup_openxr_1_runtime (system_info, multiarch_tuple);
      if (rt == NULL)
        continue;

      path = srt_openxr_1_runtime_get_json_path (rt);

      if (!srt_openxr_1_runtime_check_error (rt, &local_error))
        {
          g_warning ("Failed to load OpenXR 1 runtime for %s from %s: %s",
                     multiarch_tuple, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_info ("OpenXR 1 runtime for %s at %s: %s",
              multiarch_tuple, path,
              srt_openxr_1_runtime_get_library_path (rt));

      g_hash_table_insert (self->openxr_1_runtime_details,
                           g_strdup (multiarch_tuple),
                           icd_details_new (provider, rt));
    }
}

void
icd_stack_enumerate (IcdStack *self,
                     PvRuntimeFlags runtime_flags,
                     PvGraphicsProvider *provider,
                     const char *which_system)
{
  SrtSystemInfo *system_info;

  system_info = pv_graphics_provider_get_system_info (provider, 0);
  icd_stack_enumerate_egl_icds (self, provider, system_info, which_system);
  icd_stack_enumerate_egl_ext_platforms (self, provider, system_info, which_system);
  icd_stack_enumerate_vulkan_icds (self, provider, system_info, which_system);

  if (runtime_flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    icd_stack_enumerate_vulkan_layers (self, provider, system_info, which_system);

  if (runtime_flags & PV_RUNTIME_FLAGS_IMPORT_OPENXR_1_RUNTIMES)
    icd_stack_enumerate_openxr_1_runtimes (self, provider, system_info, which_system);
}

void
icd_stack_free (IcdStack *self)
{
  g_clear_pointer (&self->egl_icd_details, g_ptr_array_unref);
  g_clear_pointer (&self->egl_ext_platform_details, g_ptr_array_unref);
  g_clear_pointer (&self->vulkan_icd_details, g_ptr_array_unref);
  g_clear_pointer (&self->vulkan_exp_layer_details, g_ptr_array_unref);
  g_clear_pointer (&self->vulkan_imp_layer_details, g_ptr_array_unref);
  g_clear_pointer (&self->openxr_1_runtime_details, g_hash_table_unref);
  g_slice_free (IcdStack, self);
}
