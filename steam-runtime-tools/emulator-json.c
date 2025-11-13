/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/emulator-private.h"

#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

SrtEmulator *
_srt_emulator_new_from_manifest (const char *path,
                                 GError **error)
{
  GLNX_AUTO_PREFIX_ERROR (path, error);
  g_autoptr(GArray) emulated_architectures = _srt_architecture_array_new ();
  g_autoptr(GArray) required_architectures = _srt_architecture_array_new ();
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_auto(GStrv) argv = NULL;
  g_auto(GStrv) required_libraries = NULL;
  g_autofree char *real_manifest = NULL;
  size_t len = 0;
  JsonArray *array;
  JsonNode *member;
  JsonNode *root;
  JsonObject *object;
  JsonObject *root_object;
  const char *s;

  real_manifest = realpath (path, NULL);

  if (real_manifest == NULL)
    return glnx_null_throw_errno (error);

  if (!json_parser_load_from_file (parser, real_manifest, error))
    return NULL;

  root = json_parser_get_root (parser);

  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    return glnx_null_throw (error, "Top level must be a JSON object");

  root_object = json_node_get_object (root);

  member = json_object_get_member (root_object, "emulator_v0");

  if (member == NULL || !JSON_NODE_HOLDS_OBJECT (member))
    return glnx_null_throw (error, "emulator_v0 member is missing or not an object");

  object = json_node_get_object (member);
  member = json_object_get_member (object, "argv");

  if (member == NULL)
    {
      return glnx_null_throw (error, "argv must be an array or a string");
    }
  else if (JSON_NODE_HOLDS_ARRAY (member))
    {
      array = json_node_get_array (member);
      len = json_array_get_length (array);
      argv = g_new0 (char *, len + 1);

      for (size_t i = 0; i < len; i++)
        {
          JsonNode *element = json_array_get_element (array, i);

          s = json_node_get_string (element);

          if (s == NULL)
            return glnx_null_throw (error, "Elements of argv array must be strings");

          argv[i] = g_strdup (s);
        }

      argv[len] = NULL;
    }
  else if (JSON_NODE_HOLDS_VALUE (member))
    {
      s = json_node_get_string (member);

      if (s == NULL)
        return glnx_null_throw (error, "argv must be an array or a string");

      if (!g_shell_parse_argv (s, NULL, &argv, error))
        return NULL;
    }
  else
    {
      return glnx_null_throw (error, "argv must be an array or a string");
    }

  if (argv == NULL || argv[0] == NULL)
    return glnx_null_throw (error, "Does not declare argv[0]");

  /* TODO: Should "argv": ["myexe"] mean ./myexe or search PATH for myexe?
   * For now we just don't allow it, and we can give it one of those
   * meanings later, if desired */
  if (strchr (argv[0], '/') == NULL)
    return glnx_null_throw (error,
                            "Executable \"%s\" must contain a slash (use ./exe if necessary)",
                            argv[0]);

  if (!g_path_is_absolute (argv[0]))
    {
      g_autofree char *real_dir = g_path_get_dirname (real_manifest);
      g_autofree char *argv0 = g_steal_pointer (&argv[0]);
      const char *exe = argv0;

      if (g_str_has_prefix (exe, "./"))
        exe += 2;

      while (exe[0] == '/')
        exe++;

      argv[0] = g_build_filename (real_dir, exe, NULL);
    }

  member = json_object_get_member (object, "emulated_architectures");

  if (member == NULL || !JSON_NODE_HOLDS_ARRAY (member))
    return glnx_null_throw (error, "emulated_architectures must be an array");

  array = json_node_get_array (member);
  len = json_array_get_length (array);

  for (size_t i = 0; i < len; i++)
    {
      JsonNode *element = json_array_get_element (array, i);
      GQuark arch;

      s = json_node_get_string (element);

      if (s == NULL)
        return glnx_null_throw (error, "Elements of emulated_architectures array must be strings");

      arch = _srt_architecture_guess_from_user_input (s, NULL, error);

      if (arch == SRT_ARCHITECTURE_QUARK_NONE)
        return NULL;

      g_array_append_vals (emulated_architectures, &arch, 1);
    }

  if (emulated_architectures->len == 0)
    return glnx_null_throw (error, "Does not declare support for any architectures");

  member = json_object_get_member (object, "required_libraries");

  if (member == NULL)
    {
      /* OK: presumably the emulator is statically-linked */
    }
  else if (JSON_NODE_HOLDS_ARRAY (member))
    {
      array = json_node_get_array (member);
      len = json_array_get_length (array);
      required_libraries = g_new0 (char *, len + 1);

      for (size_t i = 0; i < len; i++)
        {
          JsonNode *element = json_array_get_element (array, i);

          s = json_node_get_string (element);

          if (s == NULL)
            return glnx_null_throw (error, "Elements of required_libraries array must be strings");

          required_libraries[i] = g_strdup (s);
        }

      required_libraries[len] = NULL;
    }
  else
    {
      return glnx_null_throw (error, "required_libraries must be an array if present");
    }

  member = json_object_get_member (object, "required_architectures");

  if (member == NULL)
    {
      /* OK: presumably the emulator is statically-linked */
    }
  else if (JSON_NODE_HOLDS_ARRAY (member))
    {
      array = json_node_get_array (member);
      len = json_array_get_length (array);

      for (size_t i = 0; i < len; i++)
        {
          JsonNode *element = json_array_get_element (array, i);
          GQuark arch;

          s = json_node_get_string (element);

          if (s == NULL)
            return glnx_null_throw (error, "Elements of required_architectures array must be strings");

          arch = _srt_architecture_guess_from_user_input (s, NULL, error);

          if (arch == SRT_ARCHITECTURE_QUARK_NONE)
            return NULL;

          g_array_append_vals (required_architectures, &arch, 1);
        }
    }
  else
    {
      return glnx_null_throw (error, "required_architectures must be an array if present");
    }

  return _srt_emulator_new (_srt_const_strv (argv),
                            emulated_architectures,
                            real_manifest,
                            required_architectures,
                            _srt_const_strv (required_libraries));
}

/*
 * Return a JSON manifest representing @self, with no trailing newline.
 */
gchar *
_srt_emulator_serialize_manifest (SrtEmulator *self)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autoptr(JsonNode) root = NULL;

  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);

  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "emulator_v0");
      json_builder_begin_object (builder);
        {
          json_builder_set_member_name (builder, "argv");
          json_builder_begin_array (builder);
          for (size_t i = 0; self->argv[i] != NULL; i++)
            json_builder_add_string_value (builder, self->argv[i]);
          json_builder_end_array (builder);

          json_builder_set_member_name (builder, "emulated_architectures");
          json_builder_begin_array (builder);
          for (size_t i = 0; i < self->n_emulated_architectures; i++)
            json_builder_add_string_value (builder,
                                           g_quark_to_string (self->emulated_architectures[i]));
          json_builder_end_array (builder);

          json_builder_set_member_name (builder, "required_architectures");
          json_builder_begin_array (builder);
          for (size_t i = 0; i < self->n_required_architectures; i++)
            json_builder_add_string_value (builder,
                                           g_quark_to_string (self->required_architectures[i]));
          json_builder_end_array (builder);

          json_builder_set_member_name (builder, "required_libraries");
          json_builder_begin_array (builder);
          for (size_t i = 0; self->required_libraries[i] != NULL; i++)
            json_builder_add_string_value (builder, self->required_libraries[i]);
          json_builder_end_array (builder);
        }
      json_builder_end_object (builder);
    }
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  return json_generator_to_data (generator, NULL);
}

/*
 * Write a JSON manifest representing @self to @path.
 */
gboolean
_srt_emulator_write_manifest (SrtEmulator *self,
                              const char *path,
                              GError **error)
{
  g_autofree gchar *json_output = _srt_emulator_serialize_manifest (self);
  size_t len;

  g_return_val_if_fail (json_output != NULL, FALSE);
  len = strlen (json_output);
  g_assert (json_output[len] == '\0');
  json_output[len] = '\n';
  return g_file_set_contents (path, json_output, len + 1, error);
}

