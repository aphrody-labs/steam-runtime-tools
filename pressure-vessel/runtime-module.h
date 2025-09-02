/*
 * Copyright © 2020-2025 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx.h"

#include "supported-architectures.h"

/*
 * IcdKind:
 * @ICD_KIND_NONEXISTENT: The module doesn't exist on this architecture.
 * @ICD_KIND_ABSOLUTE: The module is an absolute path (possibly containing
 *  libdl dynamic string tokens such as `${LIB}`) which can be resolved
 *  to a library suitable for this architecture.
 * @ICD_KIND_SONAME: The module is a basename such as
 *  `libVkLayer_MESA_device_select.so` which can be resolved to a library
 *  suitable for this architecture.
 * @ICD_KIND_META_LAYER: The module is a Vulkan meta-layer or similar,
 *  and therefore is not backed by a single library (although it might
 *  point to concrete layers that are, themselves, backed by libraries).
 *
 * The disposition of a specific driver/layer on a specific architecture.
 */
typedef enum
{
  ICD_KIND_NONEXISTENT,
  ICD_KIND_ABSOLUTE,
  ICD_KIND_SONAME,
  ICD_KIND_META_LAYER,
} IcdKind;

const char *icd_kind_to_string (IcdKind kind);

/*
 * IcdDetails:
 * @icd: (type SrtBaseGraphicsModule): The module object.
 *  Its type can be any of:
 *  #SrtEglIcd, #SrtEglExternalPlatform,
 *  #SrtVulkanIcd, #SrtVulkanLayer,
 *  #SrtDriDriver, #SrtVaApiDriver, #SrtVdpauDriver,
 *  #SrtOpenxr1Runtime
 * @debug_name: Some sort of name for the module, to be used in diagnostic
 *  messages only. Assumed to be owned by @icd.
 * @resolved_libraries: A per-architecture array keyed by the index of
 *  a multiarch tuple in @pv_multiarch_tuples. Each item depends on
 *  the #IcdKind: for %ICD_KIND_SONAME it is the basename of the library,
 *  for %ICD_KIND_ABSOLUTE it is the absolute path in the provider's
 *  namespace (but possibly contains libdl dynamic string tokens),
 *  and for other kinds it is %NULL.
 * @kinds: A per-architecture array as above, indicating the disposition
 *  of this library on each architecture.
 * @paths_in_container: If the kind is %ICD_KIND_ABSOLUTE, then this is
 *  a concrete, absolute path accessible in the container from which the
 *  library will be loadable, with no libdl dynamic string tokens,
 *  for example "/run/host/usr/lib/.../libfoo.so" or
 *  "/overrides/lib/.../libfoo.so". Otherwise it is %NULL.
 *
 * Details of a driver, Vulkan layer or similar thing discovered in a
 * graphics provider.
 *
 * For modules that are described by a JSON manifest, such as Vulkan
 * drivers and layers, the same JSON manifest may serve multiple
 * architectures. For example,
 * `/usr/share/vulkan/implicit_layer.d/VkLayer_MESA_device_select.json`
 * typically describes `libVkLayer_MESA_device_select.so`, which can
 * be found at a different path in each architecture's library directory,
 * while `/usr/share/vulkan/implicit_layer.d/MangoHud.json` might describe
 * `/usr/$LIB/mangohud/libMangoHud.so`, which resolves to a different path
 * for each architecture when the `$LIB` dynamic string token is expanded.
 *
 * If `libVkLayer_MESA_device_select.so` exists for both x86_64 and i386,
 * `libMangoHud.so` only exists for x86_64, and nothing exists on aarch64,
 * then we might represent that situation as:
 *
 * ```
 * {
 *   icd: <SrtVulkanLayer for libVkLayer_MESA_device_select.so>,
 *   debug_name: ".../VkLayer_MESA_device_select.json",
 *   kinds: [
 *     [x86_64]: ICD_KIND_SONAME,
 *     [i386]: ICD_KIND_SONAME,
 *     [aarch64]: ICD_KIND_NONEXISTENT,
 *   ],
 *   resolved_libraries: [
 *     [x86_64]: "libVkLayer...",
 *     [i386]: "libVkLayer...",
 *     [aarch64]: NULL,
 *   ],
 *   paths_in_container: [
 *     [x86_64]: NULL,
 *     [i386]: NULL,
 *     [aarch64]: NULL,
 *   ],
 * },
 * {
 *   icd: <SrtVulkanLayer for libMangoHud.so>,
 *   debug_name: ".../MangoHud.json",
 *   kinds: [
 *     [x86_64]: ICD_KIND_ABSOLUTE,
 *     [i386]: ICD_KIND_NONEXISTENT,
 *     [aarch64]: ICD_KIND_NONEXISTENT,
 *   ],
 *   resolved_libraries: [
 *     [x86_64]: "/usr/lib/$LIB/mangohud/libMangoHud.so",
 *     [i386]: NULL,
 *     [aarch64]: NULL,
 *   ],
 *   paths_in_container: [
 *     [x86_64]: "/run/host/usr/lib/x86_64-.../libMangoHud.so",
 *     [i386]: NULL,
 *     [aarch64]: NULL,
 *   ],
 * },
 * ```
 */
typedef struct
{
  gpointer icd;
  const char *debug_name;
  gchar *resolved_libraries[PV_N_SUPPORTED_ARCHITECTURES];
  IcdKind kinds[PV_N_SUPPORTED_ARCHITECTURES];
  gchar *paths_in_container[PV_N_SUPPORTED_ARCHITECTURES];
} IcdDetails;

IcdDetails *icd_details_new (gpointer icd);
void icd_details_free (IcdDetails *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IcdDetails, icd_details_free)

/*
 * icd_details_array_sized_new:
 * @n: Number of entries to allocate space for. This is only an
 *  optimization and it's safe to pass 0.
 *
 * Returns: (transfer container) (element-type IcdDetails): An empty array of
 *  representations of drivers, to which more can be added
 */
static inline GPtrArray *
icd_details_array_sized_new (gsize n)
{
  return g_ptr_array_new_full (n, (GDestroyNotify) G_CALLBACK (icd_details_free));
}

void icd_details_fill_array_single_arch (GPtrArray *details_arr,
                                         const GList *drivers,
                                         gsize multiarch_index);

void icd_details_fill_array_json_based (GPtrArray *details_arr,
                                        const char *which,
                                        const GList *drivers);

/*
 * IcdStack:
 * @egl_icd_details: (element-type IcdDetails): Array of details each
 *  based on a #SrtEglIcd
 * @egl_ext_platform_details: (element-type IcdDetails): Array of details each
 *  based on a #SrtEglExternalPlatform
 * @vulkan_icd_details: (element-type IcdDetails): Array of details each
 *  based on a #SrtVulkanIcd
 * @vulkan_exp_layer_details: (element-type IcdDetails): Array of details each
 *  based on a #SrtVulkanLayer representing an explicit layer
 * @vulkan_imp_layer_details: (element-type IcdDetails): Array of details each
 *  based on a #SrtVulkanLayer representing an implicit layer
 * @openxr_1_runtime_details: (element-type utf8 IcdDetails): Map with
 *  owned string multiarch tuples as keys and owned IcdDetails as values
 *
 * Details of all drivers, Vulkan layers or similar things discovered in the
 * graphics provider via a JSON manifest.
 */
typedef struct
{
  GPtrArray *egl_icd_details;
  GPtrArray *egl_ext_platform_details;
  GPtrArray *vulkan_icd_details;
  GPtrArray *vulkan_exp_layer_details;
  GPtrArray *vulkan_imp_layer_details;
  GHashTable *openxr_1_runtime_details;
} IcdStack;

IcdStack *icd_stack_new (void);

void icd_stack_free (IcdStack *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IcdStack, icd_stack_free)
