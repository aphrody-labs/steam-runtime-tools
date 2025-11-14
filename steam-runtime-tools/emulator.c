/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/emulator-private.h"

#include "steam-runtime-tools/architecture-internal.h"
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
