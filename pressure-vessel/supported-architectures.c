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

#include "config.h"

#include "supported-architectures.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "steam-runtime-tools/architecture.h"
#include "libglnx.h"

/*
 * pv_known_architecture_get_libdirs:
 * @tuple: (nullable): A multiarch tuple, or %NULL
 * @known: (nullable): A known architecture, or %NULL
 *
 * Get the absolute paths of the library directories associated with @known,
 * most important or unambiguous first.
 *
 * If @tuple is %NULL, only return generic directories like `/usr/lib`.
 * @known must also be %NULL in this case.
 *
 * If only @known is %NULL, only return directories that can be derived from
 * the @tuple, like `/usr/lib/TUPLE`, plus generic directories as above.
 *
 * Returns: (transfer container) (element-type filename):
 */
GPtrArray *
pv_known_architecture_get_libdirs (const char *tuple,
                                   const SrtKnownArchitecture *known,
                                   PvMultiarchLibdirsFlags flags)
{
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  gsize j;

  /* It doesn't make sense to have tuple == NULL and known != NULL */
  g_return_val_if_fail (tuple != NULL || known == NULL, NULL);

  if (tuple != NULL)
    {
      /* Multiarch is the least ambiguous so we put it first.
       *
       * We historically searched /usr/lib before /lib, but Debian actually
       * does the opposite, and we follow that here.
       *
       * Arguably we should search /usr/local/lib before /lib before /usr/lib,
       * but we don't currently try /usr/local/lib. We could add a flag
       * for that if we don't want to do it unconditionally. */
      g_ptr_array_add (dirs,
                       g_build_filename ("/lib", tuple, NULL));
      g_ptr_array_add (dirs,
                       g_build_filename ("/usr", "lib", tuple, NULL));

      if (flags & PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN)
        g_ptr_array_add (dirs,
                         g_build_filename ("/usr", "lib", tuple, "mesa",
                                           NULL));
    }

  if (known != NULL && known->multilib_suffixes != NULL)
    {
      /* Try other multilib variants next. This includes
       * Exherbo/cross-compilation-style per-architecture prefixes,
       * Red-Hat-style lib64 and Arch-style lib32. */
      for (j = 0; known->multilib_suffixes[j] != NULL; j++)
        {
          const char *multilib = known->multilib_suffixes[j];

          g_ptr_array_add (dirs,
                           g_build_filename ("/", multilib, NULL));
          g_ptr_array_add (dirs,
                           g_build_filename ("/usr", multilib, NULL));
        }
    }

  /* /lib and /usr/lib are lowest priority because they're the most
   * ambiguous: we don't know whether they're meant to contain 32- or
   * 64-bit libraries. */
  g_ptr_array_add (dirs, g_strdup ("/lib"));
  g_ptr_array_add (dirs, g_strdup ("/usr/lib"));

  return g_steal_pointer (&dirs);
}

void
pv_architecture_array_populate_from_environ (GArray *arch_quarks)
{
  const char *value;

  value = g_getenv ("PRESSURE_VESSEL_ARCHITECTURES");

  if (value != NULL)
    {
      g_auto(GStrv) entries = g_strsplit (value, ":", -1);
      gsize i;

      for (i = 0; entries != NULL && entries[i] != NULL; i++)
        {
          g_autoptr(GError) local_error = NULL;
          const SrtKnownArchitecture *known;
          GQuark arch_quark;

          arch_quark = _srt_architecture_guess_from_user_input (entries[i],
                                                                &known,
                                                                &local_error);

          if (arch_quark == 0)
            {
              g_warning ("%s", local_error->message);
              continue;
            }

          if (g_quark_try_string (entries[i]) != arch_quark)
            g_warning ("Canonicalized architecture \"%s\" to \"%s\"",
                       entries[i], g_quark_to_string (arch_quark));

          if (known == NULL)
            g_message ("Unknown architecture \"%s\", assuming you know what you're doing",
                       g_quark_to_string (arch_quark));

          g_array_append_val (arch_quarks, arch_quark);
        }

      if (arch_quarks->len == 0)
        g_warning ("No valid architectures found in "
                   "$PRESSURE_VESSEL_ARCHITECTURES, will use defaults");
    }
}
