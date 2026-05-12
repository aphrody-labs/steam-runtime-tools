/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "pressure-vessel/adverb-preload.h"
#include "pressure-vessel/flatpak-exports-private.h"
#include "pressure-vessel/runtime.h"

#include "pressure-vessel/wrap-interactive.h"
#include "pressure-vessel/utils.h"

typedef struct _PvWrapContext PvWrapContext;
typedef struct _PvWrapContextClass PvWrapContextClass;

/*
 * PvWrapExportFlags:
 * @PV_WRAP_EXPORT_FLAGS_OS_QUIET: Quietly ignore OS paths such as /usr/share
 *  instead of logging a warning
 * @PV_WRAP_EXPORT_FLAGS_NONE: None of the above
 *
 * Flags affecting how we export paths.
 */
typedef enum
{
  PV_WRAP_EXPORT_FLAGS_OS_QUIET = (1 << 0),
  PV_WRAP_EXPORT_FLAGS_NONE = 0
} PvWrapExportFlags;

/*
 * PvWrapTestFlags:
 * @PV_WRAP_TEST_FLAGS_MOCK_BWRAP: Use a mock path for bwrap, instead of
 *  really checking for it
 * @PV_WRAP_TEST_FLAGS_NONE: None of the above
 *
 * Flags used during testing. Use @PV_WRAP_TEST_FLAGS_NONE in production.
 */
typedef enum
{
  PV_WRAP_TEST_FLAGS_MOCK_BWRAP = (1 << 0),
  PV_WRAP_TEST_FLAGS_NONE = 0
} PvWrapTestFlags;

typedef enum
{
  TRISTATE_NO = 0,
  TRISTATE_YES,
  TRISTATE_MAYBE
} Tristate;

typedef struct
{
  PvPreloadVariableIndex which;
  gchar *preload;
} WrapPreloadModule;

void wrap_preload_module_clear (gpointer p);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WrapPreloadModule, wrap_preload_module_clear)

typedef struct
{
  GArray *architectures;
  SrtEmulator *emulator;
  gchar *emulator_manifest;
  GStrv env_if_host;
  GStrv filesystems;
  gchar *freedesktop_app_id;
  gchar *graphics_provider;
  GHashTable *arch_graphics_providers;
  gchar *home;
  GArray *pass_fds;
  GArray *preload_modules;
  gchar *runtime;
  gchar *runtime_base;
  gchar *steam_app_id;
  gchar *variable_dir;
  gchar *write_final_argv;

  double terminate_idle_timeout;
  double terminate_timeout;

  PvShell shell;
  PvTerminal terminal;
  Tristate share_home;
  Tristate share_root_dirs;

  gboolean batch;
  gboolean copy_runtime;
  gboolean deterministic;
  gboolean devel;
  gboolean for_steam_client;
  gboolean gc_runtimes;
  gboolean generate_locales;
  gboolean import_ca_certs;
  gboolean import_openxr_1_runtimes;
  gboolean import_openxr_1_layers;
  gboolean import_vulkan_layers;
  gboolean launcher;
  gboolean only_prepare;
  gboolean remove_game_overlay;
  gboolean share_pid;
  gboolean single_thread;
  gboolean systemd_scope;
  gboolean test;
  gboolean verbose;
  gboolean version;
  gboolean version_only;
} PvWrapOptions;

struct _PvWrapContext
{
  GObject parent_instance;

  FlatpakBwrap *flatpak_subsandbox;
  FlatpakExports *exports;
  GHashTable *paths_not_exported;
  PvRuntime *runtime;
  SrtSysroot *current_root;
  gchar **original_argv;
  gchar **original_environ;
  gchar *bwrap_executable;
  gchar *current_home;
  SrtSubprocessRunner *run_in_current_context;

  PvWrapOptions options;

  PvWorkaroundFlags workarounds;
  SrtBwrapFlags bwrap_flags;

  gboolean is_flatpak_env;
  int original_argc;
  int current_home_fd;
};

#define PV_TYPE_WRAP_CONTEXT \
  (pv_wrap_context_get_type ())
#define PV_WRAP_CONTEXT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), PV_TYPE_WRAP_CONTEXT, PvWrapContext))
#define PV_IS_WRAP_CONTEXT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), PV_TYPE_WRAP_CONTEXT))
#define PV_WRAP_CONTEXT_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), PV_TYPE_WRAP_CONTEXT, PvWrapContextClass))
#define PV_WRAP_CONTEXT_CLASS(c) \
  (G_TYPE_CHECK_CLASS_CAST ((c), PV_TYPE_WRAP_CONTEXT, PvWrapContextClass))
#define PV_IS_WRAP_CONTEXT_CLASS(c) \
  (G_TYPE_CHECK_CLASS_TYPE ((c), PV_TYPE_WRAP_CONTEXT))

GType pv_wrap_context_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvWrapContext, g_object_unref)

PvWrapContext *pv_wrap_context_new (SrtSysroot *current_root,
                                    const char *current_home,
                                    GError **error);

gboolean pv_wrap_options_parse_environment (PvWrapOptions *self,
                                            GError **error);

gboolean pv_wrap_context_parse_argv (PvWrapContext *self,
                                     int *argcp,
                                     char ***argvp,
                                     GError **error);

gboolean pv_wrap_options_parse_argv (PvWrapOptions *self,
                                     int *argcp,
                                     char ***argvp,
                                     GError **error);

gboolean pv_wrap_options_parse_environment_after_argv (PvWrapOptions *self,
                                                       SrtSysroot *interpreter_root,
                                                       GError **error);

gboolean pv_wrap_context_after_parsing_arguments (PvWrapContext *self,
                                                  PvWrapTestFlags test_flags,
                                                  GError **error);

gboolean pv_wrap_context_export_if_allowed (PvWrapContext *self,
                                            FlatpakFilesystemMode export_mode,
                                            const char *path,
                                            const char *host_path,
                                            const char *source,
                                            const char *before,
                                            const char *after,
                                            PvWrapExportFlags flags);

static inline const GQuark *
pv_wrap_options_get_architectures (PvWrapOptions *self,
                                   gsize *n)
{
  return _srt_architecture_array_peek_data (self->architectures, n);
}

static inline gboolean
pv_wrap_options_has_architecture (PvWrapOptions *self,
                                  GQuark q)
{
  return _srt_architecture_array_has (self->architectures, q);
}

gboolean pv_wrap_context_append_payload_command (PvWrapContext *self,
                                                 const char * const *argv,
                                                 int argc,
                                                 FlatpakBwrap *argv_in_container,
                                                 SrtEnvOverlay *container_env,
                                                 GError **error);
