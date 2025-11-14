/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/emulator-internal.h"

#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/*
 * SrtEmulator:
 *
 * Object representing a user-space emulator such as FEX or qemu-user.
 *
 * All fields are read-only (immutable) after construction,
 * so this object can safely be shared between threads,
 * as long as each thread holds a reference.
 */
struct _SrtEmulator
{
  GObject parent;
  GStrv argv;
  GQuark *emulated_architectures;
  gchar *manifest;
  GQuark *required_architectures;
  GStrv required_libraries;
  size_t n_emulated_architectures;
  size_t n_required_architectures;
};

struct _SrtEmulatorClass
{
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_ARGV,
  PROP_EMULATED_ARCHITECTURES,
  PROP_MANIFEST,
  PROP_REQUIRED_ARCHITECTURES,
  PROP_REQUIRED_LIBRARIES,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE (SrtEmulator, _srt_emulator, G_TYPE_OBJECT)

static void
_srt_emulator_init (SrtEmulator *self)
{
}

static void
_srt_emulator_finalize (GObject *object)
{
  SrtEmulator *self = SRT_EMULATOR (object);

  g_strfreev (self->argv);
  g_free (self->emulated_architectures);
  g_free (self->manifest);
  g_free (self->required_architectures);
  g_strfreev (self->required_libraries);

  G_OBJECT_CLASS (_srt_emulator_parent_class)->finalize (object);
}

static void
_srt_emulator_get_property (GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  SrtEmulator *self = SRT_EMULATOR (object);

  switch (prop_id)
    {
      case PROP_ARGV:
        g_value_set_boxed (value, self->argv);
        break;

      case PROP_EMULATED_ARCHITECTURES:
        g_value_take_boxed (value,
                            _srt_architecture_array_new_from_quarks (self->emulated_architectures,
                                                                     self->n_emulated_architectures));
        break;

      case PROP_REQUIRED_LIBRARIES:
        g_value_set_boxed (value, self->required_libraries);
        break;

      case PROP_MANIFEST:
        g_value_set_string (value, self->manifest);
        break;

      case PROP_REQUIRED_ARCHITECTURES:
        g_value_take_boxed (value,
                            _srt_architecture_array_new_from_quarks (self->required_architectures,
                                                                     self->n_required_architectures));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_srt_emulator_set_property (GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  SrtEmulator *self = SRT_EMULATOR (object);
  GArray *arr;

  switch (prop_id)
    {
      case PROP_ARGV:
        /* Construct-only */
        g_return_if_fail (self->argv == NULL);
        self->argv = g_value_dup_boxed (value);
        break;

      case PROP_EMULATED_ARCHITECTURES:
        /* Construct-only */
        g_return_if_fail (self->emulated_architectures == NULL);
        g_return_if_fail (self->n_emulated_architectures == 0);
        arr = g_value_get_boxed (value);

        if (arr != NULL)
          self->emulated_architectures = _srt_architecture_array_copy_data (arr,
                                                                            &self->n_emulated_architectures);
        else
          self->emulated_architectures = g_new0 (GQuark, 1);

        break;

      case PROP_REQUIRED_LIBRARIES:
        /* Construct-only */
        g_return_if_fail (self->required_libraries == NULL);
        self->required_libraries = g_value_dup_boxed (value);
        break;

      case PROP_MANIFEST:
        /* Construct-only */
        g_return_if_fail (self->manifest == NULL);
        self->manifest = g_value_dup_string (value);
        break;

      case PROP_REQUIRED_ARCHITECTURES:
        /* Construct-only */
        g_return_if_fail (self->required_architectures == NULL);
        g_return_if_fail (self->n_required_architectures == 0);
        arr = g_value_get_boxed (value);

        if (arr != NULL)
          self->required_architectures = _srt_architecture_array_copy_data (arr,
                                                                            &self->n_required_architectures);
        else
          self->required_architectures = g_new0 (GQuark, 1);

        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_srt_emulator_constructed (GObject *object)
{
  SrtEmulator *self = SRT_EMULATOR (object);

  G_OBJECT_CLASS (_srt_emulator_parent_class)->constructed (object);

  g_return_if_fail (self->argv != NULL);
  g_return_if_fail (self->n_emulated_architectures != 0);
  g_return_if_fail (self->emulated_architectures != NULL);
  /* manifest may be NULL */
  g_return_if_fail (self->required_architectures != NULL);
  /* n_required_architectures may be 0 */

  if (self->required_libraries == NULL)
    {
      self->required_libraries = g_new0 (char *, 1);
      self->required_libraries[0] = NULL;
    }
}

/*
 * _srt_emulator_get_argv:
 *
 * Return the arguments to be prepended to `argv` to execute
 * a binary from one of the emulated architectures.
 * Never %NULL or empty unless a programming error has occurred.
 *
 * For example, qemu-user might return `{ "/usr/bin/qemu-x86_64", NULL }`.
 *
 * Returns: (transfer none) (array zero-terminated=1) (element-type filename):
 */
const char * const *
_srt_emulator_get_argv (SrtEmulator *self)
{
  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);

  return _srt_const_strv (self->argv);
}

/*
 * _srt_emulator_get_emulated_architectures:
 * @n_out: (out) (optional): The number of nonzero items in the result
 *
 * Return the ABIs emulated by this architecture, as an array of
 * interned strings (#GQuark) representing Debian multiarch tuples,
 * in an unspecified order,
 * followed by %SRT_ARCHITECTURE_QUARK_NONE (= 0).
 *
 * For example, an x86 emulator like FEX-Emu or qemu-x86_64 might return
 * `{ g_quark_from_string (SRT_ABI_X86_64), g_quark_from_string (SRT_ABI_I386), 0 }`,
 * setting `*n_out` to 2.
 *
 * Returns: (transfer none) (array zero-terminated=1) (element-type filename):
 */
const GQuark *
_srt_emulator_get_emulated_architectures (SrtEmulator *self,
                                          size_t *n_out)
{
  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);

  if (n_out != NULL)
    *n_out = self->n_emulated_architectures;

  return self->emulated_architectures;
}

/*
 * _srt_emulator_get_manifest:
 *
 * Return the path to the JSON file describing this emulator, or %NULL.
 *
 * Returns: (transfer none) (type filename) (nullable):
 */
const char *
_srt_emulator_get_manifest (SrtEmulator *self)
{
  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);

  return self->manifest;
}

/*
 * _srt_emulator_get_required_architectures:
 * @n_out: (out) (optional): The number of nonzero items in the result
 *
 * Return the ABIs required by this emulator,
 * as an array of interned strings (#GQuark) representing Debian
 * multiarch tuples,
 * in an unspecified order,
 * followed by %SRT_ARCHITECTURE_QUARK_NONE (= 0).
 *
 * For example, FEX-Emu on an aarch64 system would return
 * `{ g_quark_to_string (SRT_ABI_AARCH64), 0 }`,
 * setting `*n_out` to 1.
 *
 * If the emulator is a statically-linked executable with no dependencies,
 * the result might have no items before the zero termination,
 * in which case `*n_out` will be 0.
 *
 * Returns: (transfer none) (array zero-terminated=1) (element-type filename):
 */
const GQuark *
_srt_emulator_get_required_architectures (SrtEmulator *self,
                                          size_t *n_out)
{
  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);

  if (n_out != NULL)
    *n_out = self->n_required_architectures;

  return self->required_architectures;
}

/*
 * _srt_emulator_get_required_libraries:
 *
 * Return the SONAMEs of libraries required by this emulator.
 * Never %NULL unless a programming error has occurred.
 *
 * For example, a statically-linked emulator like qemu-x86_64
 * would return an empty array, but FEX-Emu would return
 * an array containing at least `libstdc++.so.6` followed by %NULL.
 *
 * Returns: (transfer none) (array zero-terminated=1) (element-type filename):
 */
const char * const *
_srt_emulator_get_required_libraries (SrtEmulator *self)
{
  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);

  return _srt_const_strv (self->required_libraries);
}

static void
_srt_emulator_class_init (SrtEmulatorClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = _srt_emulator_get_property;
  object_class->set_property = _srt_emulator_set_property;
  object_class->constructed = _srt_emulator_constructed;
  object_class->finalize = _srt_emulator_finalize;

  properties[PROP_ARGV] =
    g_param_spec_boxed ("argv", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_EMULATED_ARCHITECTURES] =
    g_param_spec_boxed ("emulated-architectures", NULL, NULL,
                        G_TYPE_ARRAY,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_MANIFEST] =
    g_param_spec_string ("manifest", NULL, NULL, NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_REQUIRED_ARCHITECTURES] =
    g_param_spec_boxed ("required-architectures", NULL, NULL,
                        G_TYPE_ARRAY,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_REQUIRED_LIBRARIES] =
    g_param_spec_boxed ("required-libraries", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

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
 * Return a version of @self with modifications.
 *
 * If @replacement_argv0 is non-%NULL, it replaces `argv[0]`.
 * For example, this can be used to swap `/usr/bin/emulator`
 * for `/run/host/usr/bin/emulator` when making an emulator
 * available inside a container.
 */
SrtEmulator *
_srt_emulator_new_modified (SrtEmulator *self,
                            const char *replacement_argv0)
{
  g_autoptr(GArray) emulated_architectures = _srt_architecture_array_new ();
  g_autoptr(GArray) required_architectures = _srt_architecture_array_new ();
  g_autofree const char **new_argv = NULL;
  size_t argc;

  g_return_val_if_fail (SRT_IS_EMULATOR (self), NULL);
  argc = g_strv_length (self->argv);
  new_argv = g_new0 (const char *, argc + 1);

  for (size_t i = 0; i <= argc; i++)
    new_argv[i] = self->argv[i];

  if (replacement_argv0 != NULL)
    new_argv[0] = replacement_argv0;

  if (self->n_emulated_architectures != 0)
    g_array_append_vals (emulated_architectures,
                         self->emulated_architectures,
                         self->n_emulated_architectures);

  if (self->n_required_architectures != 0)
    g_array_append_vals (required_architectures,
                         self->required_architectures,
                         self->n_required_architectures);

  return _srt_emulator_new (new_argv,
                            emulated_architectures,
                            NULL,   /* no manifest */
                            required_architectures,
                            _srt_const_strv (self->required_libraries));
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
