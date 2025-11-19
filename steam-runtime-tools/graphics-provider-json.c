/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/graphics-provider-private.h"

#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"

static SrtSysroot *
manifest_read_root (JsonObject *object,
                    const char *member_name,
                    const char *real_manifest,
                    GError **error)
{
  g_autoptr(SrtSysroot) ret = NULL;
  g_autofree char *real_dir = NULL;
  JsonNode *member;

  real_dir = g_path_get_dirname (real_manifest);
  member = json_object_get_member (object, member_name);

  if (member != NULL)
    {
      g_autofree char *joined = NULL;
      g_autofree char *real_root = NULL;
      const char *value;

      value = json_node_get_string (member);

      if (value == NULL)
        return glnx_null_throw (error, "Value of \"%s\" must be a string",
                                member_name);

      if (value[0] == '/')
        {
          real_root = realpath (value, NULL);
        }
      else
        {
          joined = g_build_filename (real_dir, value, NULL);
          real_root = realpath (joined, NULL);
        }

      if (real_root == NULL)
        return glnx_null_throw_errno_prefix (error, "realpath(%s) (from \"%s\")",
                                             joined ?: value, member_name);

      return _srt_sysroot_new_maybe_direct (real_root, error);
    }
  else
    {
      return _srt_sysroot_new_maybe_direct (real_dir, error);
    }
}

static gboolean
manifest_read_features (JsonObject *object,
                        SrtGraphicsProviderFeatureFlags *features_p,
                        GError **error)
{
  static const struct
    {
      const char *name;
      SrtGraphicsProviderFeatureFlags value;
    }
  feature_mappings[] =
    {
        { "va_api", SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VA_API },
        { "vdpau", SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VDPAU },
    };

  for (size_t i = 0; i < G_N_ELEMENTS (feature_mappings); i++)
    {
      const char *member_name = feature_mappings[i].name;
      SrtGraphicsProviderFeatureFlags value = feature_mappings[i].value;

      switch (_srt_json_object_get_boolean_strict (object, member_name, error))
        {
          case SRT_JSON_GET_BOOLEAN_RESULT_ERROR:
            return FALSE;

          case SRT_JSON_GET_BOOLEAN_RESULT_FALSE:
            *features_p &= ~value;
            break;

          case SRT_JSON_GET_BOOLEAN_RESULT_TRUE:
            *features_p |= value;
            break;

          case SRT_JSON_GET_BOOLEAN_RESULT_UNSPECIFIED:
            break;

          default:
            g_warn_if_reached ();
        }
    }

  return TRUE;
}

typedef struct
{
  const char *object_name;
  GArray *archs;
  GError **error;
  SrtGraphicsProviderFeatureFlags default_features;
  gboolean ok;
} ManifestReadArchitecturesData;

static void
foreach_arch_array_cb (JsonArray *archs_arr,
                       guint index_,
                       JsonNode *value,
                       void *user_data)
{
  SrtGraphicsProviderArchitecture arch = SRT_GRAPHICS_PROVIDER_ARCHITECTURE_INIT;
  ManifestReadArchitecturesData *data = user_data;
  const char *s;

  if (!data->ok)
    return;

  arch.features = data->default_features;

  s = json_node_get_string (value);

  if (s == NULL)
    {
      glnx_throw (data->error, "Elements in \"%s\" array must be strings",
                  data->object_name);
      goto fail;
    }

  if (!_srt_architecture_check_plausible_tuple (s, data->error))
    goto fail;

  arch.tuple_quark = g_quark_from_string (s);
  g_array_append_vals (data->archs, &arch, 1);
  return;

fail:
  data->ok = FALSE;
}

static void
foreach_arch_object_cb (JsonObject *archs_object,
                        const char *name,
                        JsonNode *value,
                        void *user_data)
{
  const SrtGraphicsProviderArchitecture blank = SRT_GRAPHICS_PROVIDER_ARCHITECTURE_INIT;
  g_auto(SrtGraphicsProviderArchitecture) arch = SRT_GRAPHICS_PROVIDER_ARCHITECTURE_INIT;
  ManifestReadArchitecturesData *data = user_data;
  JsonObject *object;

  if (!data->ok)
    return;

  arch.features = data->default_features;

  if (!_srt_architecture_check_plausible_tuple (name, data->error))
    goto fail;

  arch.tuple_quark = g_quark_from_string (name);

  if (!JSON_NODE_HOLDS_OBJECT (value))
    {
      glnx_throw (data->error, "Values in \"%s\" object must be objects",
                  data->object_name);
      goto fail;
    }

  object = json_node_get_object (value);

  if (!manifest_read_features (object, &arch.features, data->error))
    goto fail;

  g_array_append_vals (data->archs, &arch, 1);
  /* Ownership of members was moved into the array, do not free them */
  arch = blank;
  return;

fail:
  data->ok = FALSE;
}

static gboolean
manifest_read_architectures (JsonObject *object,
                             const char *member_name,
                             SrtGraphicsProviderFeatureFlags features,
                             GArray *archs,
                             GError **error)
{
  ManifestReadArchitecturesData data =
    {
      .object_name = member_name,
      .archs = archs,
      .default_features = features,
      .error = error,
      .ok = TRUE,
    };
  JsonNode *member;

  member = json_object_get_member (object, member_name);

  if (member == NULL)
    return TRUE;

  if (JSON_NODE_HOLDS_ARRAY (member))
    {
      JsonArray *arr = json_node_get_array (member);

      json_array_foreach_element (arr, foreach_arch_array_cb, &data);
      return data.ok;
    }

  if (JSON_NODE_HOLDS_OBJECT (member))
    {
      JsonObject *member_obj = json_node_get_object (member);

      json_object_foreach_member (member_obj, foreach_arch_object_cb, &data);
      return data.ok;
    }

  return glnx_throw (error, "Value of \"%s\" must be an array or object",
                     member_name);
}

/*
 * _srt_graphics_provider_new_from_manifest:
 * @path: (type filename): Path to a JSON manifest
 * @error:
 *
 * Attempt to parse the JSON manifest at @path and return a graphics provider.
 *
 * If @path cannot be parsed or the root directory referenced by @path
 * cannot be opened, return %NULL with @error set.
 *
 * Returns: (transfer full) (nullable): A graphics provider, or %NULL on error
 */
SrtGraphicsProvider *
_srt_graphics_provider_new_from_manifest (const char *path,
                                          GError **error)
{
  GLNX_AUTO_PREFIX_ERROR (path, error);
  g_autoptr(GArray) archs = NULL;
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(SrtSysroot) sysroot = NULL;
  g_autofree char *real_manifest = NULL;
  SrtGraphicsProviderFeatureFlags features = SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_DEFAULT;
  JsonNode *member;
  JsonNode *root;
  JsonObject *object;
  JsonObject *root_object;

  real_manifest = realpath (path, NULL);

  if (real_manifest == NULL)
    return glnx_null_throw_errno (error);

  if (!json_parser_load_from_file (parser, real_manifest, error))
    return NULL;

  root = json_parser_get_root (parser);

  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    return glnx_null_throw (error, "Top level must be a JSON object");

  root_object = json_node_get_object (root);

  member = json_object_get_member (root_object, "graphics_provider_v0");

  if (member == NULL || !JSON_NODE_HOLDS_OBJECT (member))
    return glnx_null_throw (error,
                            "graphics_provider_v0 member is missing or not an object");

  object = json_node_get_object (member);

  sysroot = manifest_read_root (object, "root", real_manifest, error);

  if (sysroot == NULL)
    return NULL;

  if (!manifest_read_features (object, &features, error))
    return NULL;

  archs = _srt_graphics_provider_architecture_array_new (NULL, 0,
                                                         SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_NONE);

  if (!manifest_read_architectures (object, "architectures", features,
                                    archs, error))
    return NULL;

  if (archs->len == 0)
    return glnx_null_throw (error,
                            "architectures object or array must be non-empty");

  return _srt_graphics_provider_new (archs, features, real_manifest, sysroot);
}

/*
 * _srt_graphics_provider_new_for_path:
 * @path: Path to a directory or a JSON manifest
 * @tuple_quarks: (nullable) (array len=n_tuple_quarks): Architectures
 *  that are assumed to be supported by @path if it is a directory
 * @n_tuple_quarks: Number of architectures
 * @error:
 *
 * Convert a path to a #SrtGraphicsProvider on a "do what I mean" basis.
 * If the @path is a directory, assume that it supports all of @tuple_quarks.
 * If it is a regular file, instead assume that it is a JSON manifest,
 * ignoring @tuple_quarks and @n_tuple_quarks.
 *
 * If @path cannot be used, return %NULL with @error set.
 *
 * Returns: (transfer full) (nullable): A graphics provider, or %NULL on error
 */
SrtGraphicsProvider *
_srt_graphics_provider_new_for_path (const char *path,
                                     const GQuark *tuple_quarks,
                                     size_t n_tuple_quarks,
                                     GError **error)
{
  glnx_autofd int fd = -1;

  if (g_str_equal (path, "/"))
    {
      g_autoptr(SrtSysroot) root = _srt_sysroot_new_direct (error);

      if (root == NULL)
        return NULL;

      return _srt_graphics_provider_new_for_directory (root,
                                                       tuple_quarks,
                                                       n_tuple_quarks);
    }

  fd = glnx_opendirat_with_errno (-1, path, TRUE);    /* follow symlinks */

  if (fd >= 0)
    {
      g_autoptr(SrtSysroot) root = _srt_sysroot_new_take (g_strdup (path),
                                                          g_steal_fd (&fd));

      return _srt_graphics_provider_new_for_directory (root,
                                                       tuple_quarks,
                                                       n_tuple_quarks);
    }
  else if (errno == ENOTDIR)
    {
      return _srt_graphics_provider_new_from_manifest (path, error);
    }
  else
    {
      return glnx_null_throw_errno_prefix (error, "opendir(%s)", path);
    }
}
