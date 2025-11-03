/*<private_header>*/
/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/glib-backports-internal.h>

#include "steam-runtime-tools/system-info.h"

typedef struct _SrtEmulator SrtEmulator;
typedef struct _SrtEmulatorClass SrtEmulatorClass;

#define SRT_TYPE_EMULATOR (_srt_emulator_get_type ())
#define SRT_EMULATOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_EMULATOR, SrtEmulator))
#define SRT_IS_EMULATOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_EMULATOR))
#define SRT_EMULATOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_EMULATOR, SrtEmulatorClass))
#define SRT_EMULATOR_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_EMULATOR, SrtEmulatorClass))
#define SRT_IS_EMULATOR_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_EMULATOR))

GType _srt_emulator_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtEmulator, g_object_unref)

const char * const *_srt_emulator_get_argv (SrtEmulator *self);
const GQuark *_srt_emulator_get_emulated_architectures (SrtEmulator *self,
                                                        size_t *n_out);
const char * const *_srt_emulator_get_required_libraries (SrtEmulator *self);
const char *_srt_emulator_get_manifest (SrtEmulator *self);
const GQuark *_srt_emulator_get_required_architectures (SrtEmulator *self,
                                                        size_t *n_out);

static inline SrtEmulator *
_srt_emulator_new (const char * const *argv,
                   GArray *emulated_architectures,
                   const char *manifest,
                   GArray *required_architectures,
                   const char * const *required_libraries)
{
  g_return_val_if_fail (argv != NULL, NULL);
  g_return_val_if_fail (argv[0] != NULL, NULL);
  g_return_val_if_fail (emulated_architectures != NULL, NULL);
  g_return_val_if_fail (emulated_architectures->len != 0, NULL);
  /* manifest may be NULL */
  /* required_architectures may be NULL */
  /* required_libraries may be NULL */

  return g_object_new (SRT_TYPE_EMULATOR,
                       "argv", argv,
                       "emulated-architectures", emulated_architectures,
                       "manifest", manifest,
                       "required-architectures", required_architectures,
                       "required-libraries", required_libraries,
                       NULL);
}

SrtEmulator *_srt_emulator_new_from_manifest (const char *path,
                                              GError **error);
SrtEmulator *_srt_emulator_new_modified (SrtEmulator *self,
                                         const char *replacement_argv0);
gchar *_srt_emulator_serialize_manifest (SrtEmulator *self);
gboolean _srt_emulator_write_manifest (SrtEmulator *self,
                                       const char *path,
                                       GError **error);
