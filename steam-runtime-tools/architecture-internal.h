/*<private_header>*/
/*
 * Copyright © 2019-2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <glib.h>

#include "steam-runtime-tools/subprocess-internal.h"

typedef struct
{
  const char *multiarch_tuple;
  /* $DEB_HOST_ARCHITECTURE */
  const char *debian_architecture;
  const char *interoperable_runtime_linker;
  /* One of the values from:
   * https://registry.khronos.org/OpenXR/specs/1.1/loader.html#architecture-identifiers
   */
  const char *openxr_1_architecture;
  /* Directories relative to ${prefix} other than lib that we must search
   * for loadable modules, least-ambiguous first, most-ambiguous last,
   * not including Debian-style multiarch directories which are
   * automatically derived from @multiarch_tuple:
   * - Exherbo <GNU-tuple>/lib
   * - Red-Hat- or Arch-style lib<QUAL>
   * - etc. */
  const char * const *multilib_suffixes;
  /* Alternative paths for ld.so.cache, other than ld.so.cache itself. */
  const char * const *extra_ld_so_caches;
  /* Known values that ${PLATFORM} can expand to. */
  const char * const *libdl_platform_expansions;
  /* Directory used in Steam for gameoverlayrenderer.so. */
  const char *steam_gameoverlayrenderer_dir;
  /* Any other names for the architecture not covered by the above */
  const char * const *aliases;
  guint16 machine_type;
  guint8 elf_class;
  guint8 elf_encoding;
  guint8 sizeof_pointer;
} SrtKnownArchitecture;

void _srt_architecture_init_known (void);

G_GNUC_INTERNAL gboolean _srt_architecture_check_plausible_tuple (const char *tuple,
                                                                  GError **error);
G_GNUC_INTERNAL const SrtKnownArchitecture *_srt_architecture_get_known (void);
G_GNUC_INTERNAL const SrtKnownArchitecture *_srt_architecture_get_by_tuple (const char *multiarch_tuple);

G_GNUC_INTERNAL gboolean _srt_architecture_can_run (SrtSubprocessRunner *runner,
                                                    const char *multiarch);

const gchar *_srt_architecture_guess_from_elf (int dfd,
                                               const char *file_path,
                                               GError **error);

GQuark _srt_architecture_guess_from_user_input (const char *alias,
                                                const SrtKnownArchitecture **known_out,
                                                GError **error);

/*
 * _srt_architecture_array_new:
 *
 * Return an empty array of GQuark (interned strings) representing
 * architectures/ABIs by their Debian-style multiarch tuple.
 *
 * The array has an additional item `g_array_index (arr, GQuark, arr->len)`
 * with value 0, so `arr->data` can be used as a 0-terminated array of GQuark.
 *
 * Newly added items are cleared to 0.
 *
 * Returns: An empty array to hold GQuark
 */
static inline GArray *
_srt_architecture_array_new (void)
{
  return g_array_new (TRUE,             /* add an extra 0 entry at the end */
                      TRUE,             /* zero-fill new entries */
                      sizeof (GQuark));
}

void _srt_architecture_array_populate_with_defaults (GArray *arch_quarks);
