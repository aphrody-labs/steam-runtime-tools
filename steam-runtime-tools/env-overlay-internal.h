/*<private_header>*/
/*
 * Copyright © 2014-2018 Red Hat, Inc
 * Copyright © 2020 Collabora Ltd.
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

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#define SRT_TYPE_ENV_OVERLAY (_srt_env_overlay_get_type ())
GType _srt_env_overlay_get_type (void);

typedef struct _SrtEnvOverlayEdit SrtEnvOverlayEdit;

/*
 * SrtEnvOverlayEdit:
 *
 * A deferred prepends and/or append operation for a single environment
 * variable whose base value is not yet known (i.e. it would otherwise
 * be inherited from the execution environment).

 * The deferred operation will be resolved against the real value
 * when _srt_env_overlay_apply() gets called.
 */
struct _SrtEnvOverlayEdit
{
  char delimiter;
  gchar *prepend;  /* (nullable) */
  gchar *append;   /* (nullable) */
};

typedef struct _SrtEnvOverlay SrtEnvOverlay;

/*
 * SrtEnvOverlay:
 *
 * A set of environment variables, each in one of these states:
 *
 * - Set to a value (empty or non-empty)
 * - Forced to be unset
 * - Inherited from some execution environment that is unknown to us
 * - Has a pending prepend/append edit (deferred resolution)
 *
 * We represent this as follows:
 *
 * - Set to a value: values[VAR] = VAL
 * - Forced to be unset: values[VAR] = NULL
 * - Inherited from the execution environment: VAR not in `values` and not in `edits`
 * - Has a deferred edit: edits[VAR] = SrtEnvOverlayEdit (VAR is never in `values`)
 */
struct _SrtEnvOverlay
{
  size_t refcount;

  /* (element-type filename filename) */
  GHashTable *values;

  /* (element-type filename SrtEnvOverlayEdit) */
  GHashTable *edits;
};

SrtEnvOverlay *_srt_env_overlay_new (void);
SrtEnvOverlay *_srt_env_overlay_ref (SrtEnvOverlay *self);
SrtEnvOverlay *_srt_env_overlay_copy (const SrtEnvOverlay *self);
void _srt_env_overlay_update (SrtEnvOverlay *self,
                              const SrtEnvOverlay *other);
void _srt_env_overlay_unref (void *p);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtEnvOverlay, _srt_env_overlay_unref)

void _srt_env_overlay_set (SrtEnvOverlay *self,
                           const char *var,
                           const char *val);
void _srt_env_overlay_take (SrtEnvOverlay *self,
                            gchar *var,
                            gchar *val);

void _srt_env_overlay_prepend (SrtEnvOverlay *self,
                               const char *var,
                               const char *prefix,
                               char delimiter);
void _srt_env_overlay_append (SrtEnvOverlay *self,
                              const char *var,
                              const char *suffix,
                              char delimiter);

void _srt_env_overlay_inherit (SrtEnvOverlay *self,
                               const char *var);

void _srt_env_overlay_inherit_matching_pattern (SrtEnvOverlay *self,
                                                const char *pattern);

gboolean _srt_env_overlay_pass_cli (SrtEnvOverlay *self,
                                    const char *option_name,
                                    const char *value,
                                    const char * const *envp,
                                    GError **error);
gboolean _srt_env_overlay_pass_matching_pattern_cli (SrtEnvOverlay *self,
                                                     const char *option_name,
                                                     const char *value,
                                                     const char * const *envp,
                                                     GError **error);

GList *_srt_env_overlay_get_vars (const SrtEnvOverlay *self);
gboolean _srt_env_overlay_contains (const SrtEnvOverlay *self,
                                    const char *var);
const char *_srt_env_overlay_get (const SrtEnvOverlay *self,
                                  const char *var);

gboolean _srt_env_overlay_get_edit (const SrtEnvOverlay *self,
                                    const char *var,
                                    char *delimiter_out,
                                    const char **prepend_out,
                                    const char **append_out);

void _srt_env_overlay_debug_log (const SrtEnvOverlay *self);
void _srt_env_overlay_resolve_edits (SrtEnvOverlay *self,
                                     const char * const *envp);
GStrv _srt_env_overlay_apply (const SrtEnvOverlay *self,
                              GStrv envp);
GBytes *_srt_env_overlay_to_env0 (const SrtEnvOverlay *self);
gchar *_srt_env_overlay_to_shell (const SrtEnvOverlay *self);

GOptionGroup *_srt_env_overlay_create_option_group (SrtEnvOverlay *self);

/*
 * Returns: %TRUE if this environment overlay does not set or unset anything,
 *          and has no pending edits.
 */
static inline gboolean
_srt_env_overlay_is_empty (const SrtEnvOverlay *self)
{
  return (g_hash_table_size (self->values) == 0 &&
          g_hash_table_size (self->edits) == 0);
}
