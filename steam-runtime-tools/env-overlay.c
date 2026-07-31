/*
 * Copyright © 2014-2018 Red Hat, Inc
 * Copyright © 2020-2021 Collabora Ltd.
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

#include "steam-runtime-tools/env-overlay-internal.h"

#include "steam-runtime-tools/utils-internal.h"

#include <fnmatch.h>

G_DEFINE_BOXED_TYPE (SrtEnvOverlay,
                     _srt_env_overlay,
                     _srt_env_overlay_ref,
                     _srt_env_overlay_unref)

static SrtEnvOverlayEdit *
_srt_env_overlay_edit_new (char delimiter)
{
  SrtEnvOverlayEdit *edit = g_slice_new0 (SrtEnvOverlayEdit);

  edit->delimiter = delimiter;
  return edit;
}

static void
_srt_env_overlay_edit_free (gpointer p)
{
  SrtEnvOverlayEdit *edit = p;

  if (edit == NULL)
    return;

  g_free (edit->prepend);
  g_free (edit->append);
  g_slice_free (SrtEnvOverlayEdit, edit);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtEnvOverlayEdit, _srt_env_overlay_edit_free)

/*
 * _srt_env_overlay_join_with_delimiter:
 * @left: (nullable): A string, or %NULL
 * @middle: (nullable): A string, or %NULL
 * @right: (nullable): A string, or %NULL
 * @delimiter: The delimiter to join with, normally `':'`, must not be `'\0'`
 *
 * If any of the strings is %NULL or the empty string,
 * it is skipped entirely (including the delimiter),
 * so we never end up with a spurious leading or trailing delimiter.
 *
 * Returns: (transfer full) (nullable): The joined string, or %NULL if
 *  @left, @middle and @right are all %NULL.
 */
static gchar *
_srt_env_overlay_join_with_delimiter (const char *left,
                                      const char *middle,
                                      const char *right,
                                      char delimiter)
{
  g_autoptr(GString) buf = NULL;

  g_return_val_if_fail (delimiter != '\0', NULL);

  if (left == NULL && middle == NULL && right == NULL)
    return NULL;

  buf = g_string_new (middle);

  if (left != NULL && *left != '\0')
    {
      if (buf->len > 0)
        g_string_prepend_c (buf, delimiter);

      g_string_prepend (buf, left);
    }

  if (right != NULL && *right != '\0')
    {
      if (buf->len > 0)
        g_string_append_c (buf, delimiter);

      g_string_append (buf, right);
    }

  return g_string_free_and_steal (g_steal_pointer (&buf));
}

SrtEnvOverlay *
_srt_env_overlay_new (void)
{
  g_autoptr(SrtEnvOverlay) self = g_slice_new0 (SrtEnvOverlay);

  self->refcount = 1;
  self->values = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, g_free);
  self->edits = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       g_free, _srt_env_overlay_edit_free);
  return g_steal_pointer (&self);
}

SrtEnvOverlay *
_srt_env_overlay_ref (SrtEnvOverlay *self)
{
  g_return_val_if_fail (self->refcount > 0, NULL);
  self->refcount++;
  return self;
}

SrtEnvOverlay *
_srt_env_overlay_copy (const SrtEnvOverlay *self)
{
  g_autoptr(SrtEnvOverlay) other = _srt_env_overlay_new ();

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->refcount > 0, NULL);

  _srt_env_overlay_update (other, self);
  return g_steal_pointer (&other);
}

void
_srt_env_overlay_unref (void *p)
{
  SrtEnvOverlay *self = p;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->refcount > 0);

  if (--self->refcount > 0)
    return;

  g_clear_pointer (&self->values, g_hash_table_unref);
  g_clear_pointer (&self->edits, g_hash_table_unref);
  g_slice_free (SrtEnvOverlay, self);
}

/*
 * _srt_env_overlay_set:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 * @val: (type filename): A value for the environment variable
 *
 * Set @var to @val, which may be %NULL to unset it.
 * The effect of any previous call to _srt_env_overlay_prepend()
 * or _srt_env_overlay_append() is cancelled.
 */
void
_srt_env_overlay_set (SrtEnvOverlay *self,
                      const char *var,
                      const char *val)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  /* val may be NULL, to unset it */

  _srt_env_overlay_take (self, g_strdup (var), g_strdup (val));
}

/*
 * _srt_env_overlay_set_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * If @value is a valid environment variable setting in the format
 * `NAME=VALUE`, set *NAME* to *VALUE* as if via _srt_env_overlay_set().
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_env_overlay_set_cli (SrtEnvOverlay *self,
                          const char *option_name,
                          const char *value,
                          GError **error)
{
  g_auto(GStrv) split = NULL;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  split = g_strsplit (value, "=", 2);

  if (split == NULL ||
      split[0] == NULL ||
      split[0][0] == 0 ||
      split[1] == NULL)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Invalid environment variable format \"%s\" for %s, should be NAME=VALUE",
                   value, option_name);
      return FALSE;
    }

  _srt_env_overlay_take (self,
                         g_steal_pointer (&split[0]),
                         g_steal_pointer (&split[1]));
  return TRUE;
}

/*
 * _srt_env_overlay_unset_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * If @value is a valid environment variable name, mark it to be unset,
 * as if via _srt_env_overlay_set() with @val set to %NULL.
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_env_overlay_unset_cli (SrtEnvOverlay *self,
                            const char *option_name,
                            const char *value,
                            GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (value == NULL || value[0] == '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Environment variable name for %s must be non-empty",
                   option_name);
      return FALSE;
    }

  _srt_env_overlay_set (self, value, NULL);
  return TRUE;
}

/*
 * _srt_env_overlay_take:
 * @self: Environment variables to set or unset
 * @var: (type filename) (transfer full): An environment variable
 * @val: (type filename) (transfer full): A value for the environment variable
 *
 * Set @var to @val, which may be %NULL to unset it.
 * The effect of any previous call to _srt_env_overlay_prepend()
 * or _srt_env_overlay_append() is cancelled.
 * Ownership of the two strings is taken.
 */
void
_srt_env_overlay_take (SrtEnvOverlay *self,
                       gchar *var,
                       gchar *val)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  /* val may be NULL, to unset it */

  g_hash_table_remove (self->edits, var);
  g_hash_table_replace (self->values, var, val);
}

/*
 * _srt_env_overlay_inherit:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 *
 * Set @var to whatever value it happens to have inherited.
 * The effect of any previous call to _srt_env_overlay_prepend()
 * or _srt_env_overlay_append() is cancelled.
 */
void
_srt_env_overlay_inherit (SrtEnvOverlay *self,
                          const char *var)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);

  g_hash_table_remove (self->edits, var);
  g_hash_table_remove (self->values, var);
}

/*
 * _srt_env_overlay_inherit_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * If @value is a valid environment variable name, set it to be inherited
 * as if via _srt_env_overlay_inherit().
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_env_overlay_inherit_cli (SrtEnvOverlay *self,
                              const char *option_name,
                              const char *value,
                              GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (value == NULL || value[0] == '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Environment variable name for %s must be non-empty",
                   option_name);
      return FALSE;
    }

  _srt_env_overlay_inherit (self, value);
  return TRUE;
}

/*
 * _srt_env_overlay_get_vars:
 * @self: Environment variables to set or unset
 *
 * Return a sorted list of environment variables that are to be overridden.
 *
 * Returns: (transfer container): The variables that are either set,
 *  forced to be unset,
 *  prepended to or
 *  appended to,
 *  but not the variables that are inherited.
 *  The caller must free the result with g_list_free(), but must not
 *  free or modify the strings that it points to.
 */
GList *
_srt_env_overlay_get_vars (const SrtEnvOverlay *self)
{
  GList *vars;

  g_return_val_if_fail (self != NULL, NULL);

  vars = g_list_concat (g_hash_table_get_keys (self->values),
                       g_hash_table_get_keys (self->edits));
  return g_list_sort (vars, _srt_generic_strcmp0);
}

/*
 * _srt_env_overlay_contains:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 *
 * Returns: %TRUE if @var is either set or forced to be unset,
 *  or %FALSE if it is inherited
 */
gboolean
_srt_env_overlay_contains (const SrtEnvOverlay *self,
                           const char *var)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return (g_hash_table_lookup_extended (self->values, var, NULL, NULL) ||
          g_hash_table_lookup_extended (self->edits, var, NULL, NULL));
}

/*
 * _srt_env_overlay_get:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 *
 * Get the value of an overridden environment variable.
 *
 * Note that this does not consult any pending prepend/append edit for
 * @var (see _srt_env_overlay_get_edit()): if @var only has a pending
 * edit and no fully-resolved value, this returns %NULL, the same as if
 * @var were inherited.
 *
 * Returns: (nullable): The value of @var, or %NULL if @var is either
 *  unset or inherited
 */
const char *
_srt_env_overlay_get (const SrtEnvOverlay *self,
                      const char *var)
{
  g_return_val_if_fail (self != NULL, NULL);

  return g_hash_table_lookup (self->values, var);
}

/*
 * _srt_env_overlay_get_edit:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 * @delimiter_out: (out) (optional): Set to the edit's delimiter if this
 *  returns %TRUE
 * @prepend_out: (out) (optional) (nullable): Set to a borrowed pointer
 *  to the edit's prepend fragment (which may itself be %NULL) if this
 *  returns %TRUE
 * @append_out: (out) (optional) (nullable): Set to a borrowed pointer
 *  to the edit's append fragment (which may itself be %NULL) if this
 *  returns %TRUE
 *
 * Get the raw components of a pending prepend/append edit for @var,
 * without joining them against the not-yet-known base value.
 *
 * Returns: %TRUE if @var has a pending edit, %FALSE otherwise (in which
 *  case none of the out-parameters are touched)
 */
gboolean
_srt_env_overlay_get_edit (const SrtEnvOverlay *self,
                           const char *var,
                           char *delimiter_out,
                           const char **prepend_out,
                           const char **append_out)
{
  SrtEnvOverlayEdit *edit;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (var != NULL, FALSE);

  edit = g_hash_table_lookup (self->edits, var);

  if (edit == NULL)
    return FALSE;

  if (delimiter_out != NULL)
    *delimiter_out = edit->delimiter;

  if (prepend_out != NULL)
    *prepend_out = edit->prepend;

  if (append_out != NULL)
    *append_out = edit->append;

  return TRUE;
}

/*
 * _srt_env_overlay_prepend:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 * @prefix: (type filename): A prefix to prepend to @var's value
 * @delimiter: The delimiter to join with
 *
 * Arrange for @prefix to be prepended to @var's value, joined by
 * @delimiter.
 *
 * If @var already has a fully-resolved value in @self (for example added via
 * _srt_env_overlay_set()), the prepend happens immediately.
 *
 * Otherwise (@var is still inherited, or already has a pending edit), the
 * prepend is deferred: it is recorded and only resolved against @var's real
 * value when _srt_env_overlay_apply() is called.
 *
 * Repeated calls accumulate: each new prepend is inserted before any
 * previously-prepended fragments to the base value.
 *
 * For example:
 *
 *  - `_srt_env_overlay_prepend (env, "PATH", "x", ':');` followed by
 *  - `_srt_env_overlay_prepend (env, "PATH", "y", ':');`
 *
 * will result in `PATH=y:x:..."`
 *
 * If the same #SrtEnvOverlay both prepends and appends to the same
 * environment variable, specifying different delimiters, then the delimiters
 * that will be used when the environment variable's final value is computed
 * are unspecified.
 *
 * If @delimiter is '\0' the prepend will not be performed.
 */
void
_srt_env_overlay_prepend (SrtEnvOverlay *self,
                          const char *var,
                          const char *prefix,
                          char delimiter)
{
  SrtEnvOverlayEdit *edit;
  gpointer current = NULL;
  gchar *combined;

  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  g_return_if_fail (prefix != NULL);
  g_return_if_fail (delimiter != '\0');

  if (g_hash_table_lookup_extended (self->values, var, NULL, &current))
    {
      combined = _srt_env_overlay_join_with_delimiter (prefix, current, NULL, delimiter);
      g_hash_table_replace (self->values, g_strdup (var), combined);
    }
  else
    {
      edit = g_hash_table_lookup (self->edits, var);

      if (edit == NULL)
        {
          edit = _srt_env_overlay_edit_new (delimiter);
          g_hash_table_replace (self->edits, g_strdup (var), edit);
        }
      else
        {
          if (edit->delimiter != delimiter)
            g_warning ("%s is already being edited with \'%c\', delimiter \'%c\' is going to be discarded.",
                       var, edit->delimiter, delimiter);
        }

      combined = _srt_env_overlay_join_with_delimiter (prefix, edit->prepend, NULL, edit->delimiter);
      g_free (edit->prepend);
      edit->prepend = combined;
    }
}

/*
 * _srt_env_overlay_append:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 * @suffix: (type filename): A suffix to append to @var's value
 * @delimiter: The delimiter to join with
 *
 * Arrange for @suffix to be appended to @var's value, joined by
 * @delimiter.
 *
 * If @var already has a fully-resolved value in @self (for example added via
 * _srt_env_overlay_set()), the append happens immediately.
 *
 * Otherwise (@var is still inherited, or already has a pending edit), the
 * append is deferred: it is recorded and only resolved against @var's real
 * value when _srt_env_overlay_apply() is called.
 *
 * Repeated calls accumulate: each new append is inserted after any
 * previously-appended fragments to the base value.
 *
 * For example:
 *
 *  - `_srt_env_overlay_append (env, "PATH", "x", ':');` followed by
 *  - `_srt_env_overlay_append (env, "PATH", "y", ':');`
 *
 * will result in `PATH=...:x:y"`
 *
 * If the same #SrtEnvOverlay both prepends and appends to the same
 * environment variable, specifying different delimiters, then the delimiters
 * that will be used when the environment variable's final value is computed
 * are unspecified.
 *
 * If @delimiter is '\0' the append will not be performed.
 */
void
_srt_env_overlay_append (SrtEnvOverlay *self,
                         const char *var,
                         const char *suffix,
                         char delimiter)
{
  SrtEnvOverlayEdit *edit;
  gpointer current = NULL;
  gchar *combined;

  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  g_return_if_fail (suffix != NULL);
  g_return_if_fail (delimiter != '\0');

  if (g_hash_table_lookup_extended (self->values, var, NULL, &current))
    {
      combined = _srt_env_overlay_join_with_delimiter (NULL, current, suffix, delimiter);
      g_hash_table_replace (self->values, g_strdup (var), combined);
    }
  else
    {
      edit = g_hash_table_lookup (self->edits, var);

      if (edit == NULL)
        {
          edit = _srt_env_overlay_edit_new (delimiter);
          g_hash_table_replace (self->edits, g_strdup (var), edit);
        }
      else
        {
          if (edit->delimiter != delimiter)
            g_warning ("%s is already being edited with \'%c\', delimiter \'%c\' is going to be discarded.",
                       var, edit->delimiter, delimiter);
        }

      combined = _srt_env_overlay_join_with_delimiter (NULL, edit->append, suffix, edit->delimiter);
      g_free (edit->append);
      edit->append = combined;
    }
}

/*
 * _srt_env_overlay_inherit_matching_pattern:
 * @self: Environment variables to set or unset
 * @pattern: (type filename): A glob pattern for environment variable names
 *
 * For each environment variable that is either set or unset in @self,
 * if the name matches @pattern, mark it to be inherited as if via
 * _srt_env_overlay_inherit().
 */
void
_srt_env_overlay_inherit_matching_pattern (SrtEnvOverlay *self,
                                           const char *pattern)
{
  GHashTableIter iter;
  gpointer k;

  g_return_if_fail (self != NULL);
  g_return_if_fail (pattern != NULL);

  g_hash_table_iter_init (&iter, self->values);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      if (fnmatch (pattern, k, 0) == 0)
        g_hash_table_iter_remove (&iter);
    }

  g_hash_table_iter_init (&iter, self->edits);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      if (fnmatch (pattern, k, 0) == 0)
        g_hash_table_iter_remove (&iter);
    }
}

/*
 * _srt_env_overlay_inherit_matching_pattern_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * Set all environment variables matching the glob-style pattern @value
 * to be inherited as if via _srt_env_overlay_inherit().
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_env_overlay_inherit_matching_pattern_cli (SrtEnvOverlay *self,
                                               const char *option_name,
                                               const char *value,
                                               GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  _srt_env_overlay_inherit_matching_pattern (self, value);
  return TRUE;
}

/*
 * _srt_env_overlay_pass_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @envp: Environment block used to look up @value
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * Set the environment variable @value to whatever its value is in @envp,
 * or mark it to be unset if it is not in @envp.
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_pass_cli (SrtEnvOverlay *self,
                           const char *option_name,
                           const char *value,
                           const char * const *envp,
                           GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  _srt_env_overlay_set (self, value, _srt_environ_getenv (envp, value));
  return TRUE;
}

/*
 * _srt_env_overlay_pass_matching_pattern_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @envp: Environment block used to look up @value
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * Set all environment variables matching the glob-style pattern @value
 * to be inherited as if via _srt_env_overlay_inherit().
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_pass_matching_pattern_cli (SrtEnvOverlay *self,
                                            const char *option_name,
                                            const char *value,
                                            const char * const *envp,
                                            GError **error)
{
  const char * const *iter;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (envp == NULL)
    return TRUE;

  for (iter = envp; *iter != NULL; iter++)
    {
      g_auto(GStrv) split = g_strsplit (*iter, "=", 2);

      if (split == NULL ||
          split[0] == NULL ||
          split[0][0] == 0 ||
          split[1] == NULL)
        continue;

      if (fnmatch (value, split[0], 0) == 0)
        _srt_env_overlay_take (self,
                               g_steal_pointer (&split[0]),
                               g_steal_pointer (&split[1]));
    }

  return TRUE;
}

static gboolean
_srt_env_overlay_env_fd_cli (SrtEnvOverlay *self,
                             const char *option_name,
                             const char *value,
                             GError **error)
{
  g_autofree gchar *proc_filename = NULL;
  g_autofree gchar *env_block = NULL;
  gsize remaining;
  const char *p;
  guint64 fd;
  gchar *endptr;

  g_assert (self != NULL);

  fd = g_ascii_strtoull (value, &endptr, 10);

  if (endptr == NULL || *endptr != '\0' || fd > G_MAXINT)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Not a valid file descriptor: %s", value);
      return FALSE;
    }

  proc_filename = g_strdup_printf ("/proc/self/fd/%d", (int) fd);

  if (!g_file_get_contents (proc_filename, &env_block, &remaining, error))
    return FALSE;

  p = env_block;

  while (remaining > 0)
    {
      g_autofree gchar *var = NULL;
      g_autofree gchar *val = NULL;
      size_t len = strnlen (p, remaining);
      const char *equals;

      g_assert (len <= remaining);

      equals = memchr (p, '=', len);

      if (equals == NULL || equals == p)
        {
          g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                       "Environment variable must be given in the form VARIABLE=VALUE, not %.*s",
                       (int) len, p);
          return FALSE;
        }

      var = g_strndup (p, equals - p);
      val = g_strndup (equals + 1, len - (equals - p) - 1);
      _srt_env_overlay_take (self,
                             g_steal_pointer (&var),
                             g_steal_pointer (&val));

      p += len;
      remaining -= len;

      if (remaining > 0)
        {
          g_assert (*p == '\0');
          p += 1;
          remaining -= 1;
        }
    }

  if (fd >= 3)
    close (fd);

  return TRUE;
}

static gchar *
_srt_env_overlay_edit_to_string (const char *var, SrtEnvOverlayEdit *edit)
{
  return g_strdup_printf ("var %s, prepend \"%s\", append \"%s\", delimiter \'%c\'",
                          var, edit->prepend, edit->append, edit->delimiter);
}

/*
 * _srt_env_overlay_debug_log:
 * @self: Variables to set and unset
 *
 * Log debug messages for all the variables in @self.
 */
void
_srt_env_overlay_debug_log (const SrtEnvOverlay *self)
{
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_if_fail (self != NULL);

  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *var = iter->data;
      const char *value = g_hash_table_lookup (self->values, var);
      gpointer edit = NULL;

      if (value != NULL)
        {
          g_debug ("Set environment: %s=%s", var, value);
        }
      else if (g_hash_table_lookup_extended (self->edits, var, NULL, &edit))
        {
          g_autofree gchar *str = _srt_env_overlay_edit_to_string (var, edit);
          g_debug ("Pending edit: %s", str);
        }
      else
        {
          g_debug ("Unset environment: %s", var);
        }
    }
}

/*
 * _srt_env_overlay_apply:
 * @self: Variables to set and unset
 * @envp: (transfer full): An environment block
 *
 * Return a version of @envp that has been modified to set and unset
 * its environment variables according to the instructions in @self.
 *
 * Returns: (transfer full): The new environment block
 */
GStrv
_srt_env_overlay_apply (const SrtEnvOverlay *self,
                        GStrv envp)
{
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_val_if_fail (self != NULL, envp);

  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *value = g_hash_table_lookup (self->values, iter->data);

      if (value != NULL)
        envp = g_environ_setenv (envp, iter->data, value, TRUE);
      else
        envp = g_environ_unsetenv (envp, iter->data);
    }

  return g_steal_pointer (&envp);
}

static void
byte_array_append_env0 (GByteArray *arr,
                        const char *var,
                        const char *val)
{
  g_byte_array_append (arr, (const guint8 *) var, strlen (var));
  g_byte_array_append (arr, (const guint8 *) "=", 1);
  /* This appends the value plus \0 */
  g_byte_array_append (arr, (const guint8 *) val, strlen (val) + 1);
}

/*
 * _srt_env_overlay_to_env0:
 * @self: Variables to set and unset
 *
 * Return all of the variables set by @self, as a buffer in `env -0` format.
 * Unset and inherited variables are ignored.
 *
 * Returns: (transfer full): `NAME=VALUE\0NAME=VALUE\0...`
 */
GBytes *
_srt_env_overlay_to_env0 (const SrtEnvOverlay *self)
{
  GByteArray *arr = NULL;
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_val_if_fail (self != NULL, NULL);

  arr = g_byte_array_new ();
  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *value = g_hash_table_lookup (self->values, iter->data);

      if (value != NULL)
        byte_array_append_env0 (arr, iter->data, value);
    }

  return g_byte_array_free_to_bytes (arr);
}

/*
 * _srt_env_overlay_to_shell:
 * @self: Variables to set and unset
 *
 * Return all of the variables set by @self, as a buffer in a format
 * that could be evaluated by a POSIX shell using `sh -c` or the
 * `eval` builtin.
 *
 * Variables that are set to a value are output in the form
 * `export NAME=VALUE` followed by a newline, with *VALUE* quoted if required.
 *
 * Variables that are unset are output in the form `unset NAME` followed
 * by a newline.
 *
 * Variables whose names would not be acceptable in shell syntax are ignored.
 *
 * Inherited variables are ignored.
 *
 * Returns: (transfer full): a shell script
 */
gchar *
_srt_env_overlay_to_shell (const SrtEnvOverlay *self)
{
  g_autoptr(GString) buf = NULL;
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_val_if_fail (self != NULL, NULL);

  buf = g_string_new ("");
  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *name = iter->data;
      const char *value = g_hash_table_lookup (self->values, name);

      if (!_srt_is_identifier (name))
        {
          g_debug ("Cannot export variable \"%s\": not a valid shell variable",
                   name);
          continue;
        }

      if (value != NULL)
        {
          g_autofree gchar *escaped = g_shell_quote (value);

          g_string_append_printf (buf, "export %s=%s\n", name, escaped);
        }
      else
        {
          g_string_append_printf (buf, "unset %s\n", name);
        }
    }

  return g_string_free_and_steal (g_steal_pointer (&buf));
}

static gboolean
opt_env_cb (const char *option_name,
            const gchar *value,
            gpointer data,
            GError **error)
{
  return _srt_env_overlay_set_cli (data, option_name, value, error);
}

static gboolean
opt_env_fd_cb (const char *option_name,
               const gchar *value,
               gpointer data,
               GError **error)
{
  return _srt_env_overlay_env_fd_cli (data, option_name, value, error);
}

static gboolean
opt_inherit_env_cb (const char *option_name,
                    const gchar *value,
                    gpointer data,
                    GError **error)
{
  return _srt_env_overlay_inherit_cli (data, option_name, value, error);
}

static gboolean
opt_inherit_env_matching_cb (const char *option_name,
                             const gchar *value,
                             gpointer data,
                             GError **error)
{
  return _srt_env_overlay_inherit_matching_pattern_cli (data, option_name,
                                                        value, error);
}

static gboolean
opt_unset_env_cb (const char *option_name,
                  const gchar *value,
                  gpointer data,
                  GError **error)
{
  return _srt_env_overlay_unset_cli (data, option_name, value, error);
}

static const GOptionEntry options[] =
{
  { "env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_env_cb,
    "Set environment variable.", "VAR=VALUE" },
  { "env-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_env_fd_cb,
    "Read environment variables in env -0 format from FD", "FD" },
  { "inherit-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_inherit_env_cb,
    "Undo a previous --env, --unset-env, --pass-env, etc.", "VAR" },
  { "inherit-env-matching", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_inherit_env_matching_cb,
    "Undo previous --env, --unset-env, etc. matching a shell-style wildcard",
    "WILDCARD" },
  { "unset-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_unset_env_cb,
    "Unset environment variable, like env -u.", "VAR" },
  { NULL }
};

/*
 * Returns: (transfer full): a #GOptionGroup
 */
GOptionGroup *
_srt_env_overlay_create_option_group (SrtEnvOverlay *self)
{
  GOptionGroup *group = NULL;

  group = g_option_group_new ("environment",
                              "Environment Options:",
                              "Environment options",
                              _srt_env_overlay_ref (self),
                              _srt_env_overlay_unref);
  g_option_group_add_entries (group, options);

  return g_steal_pointer (&group);
}

/*
 * For each variable that would be set by @other,
 * update @self to set it to the same value.
 *
 * For each variable that would be unset by @other,
 * update @self to unset it.
 */
void
_srt_env_overlay_update (SrtEnvOverlay *self,
                         const SrtEnvOverlay *other)
{
  GHashTableIter iter;
  void *k, *v;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->refcount > 0);
  g_return_if_fail (other != NULL);
  g_return_if_fail (other->refcount > 0);

  g_hash_table_iter_init (&iter, other->values);

  while (g_hash_table_iter_next (&iter, &k, &v))
    g_hash_table_replace (self->values, g_strdup (k), g_strdup (v));
}
