/*
 * Run the part of the Steam Client that needs to be in a container.
 *
 * Copyright © 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "config.h"

#include <locale.h>

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/launcher-interface-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static const char this_program_name[] = "srt-client-container";

static int opt_verbose = 0;
static gboolean opt_version = FALSE;

static gboolean
opt_verbose_cb (const char *name,
                const char *value,
                gpointer data,
                GError **error)
{
  if (opt_verbose < 2)
    opt_verbose++;

  return TRUE;
}

static GOptionEntry options[] =
{
  { "verbose", 'v',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_verbose_cb,
    "Be more verbose. If given twice, log debug messages too.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

static gboolean
run (int argc,
     char *argv[],
     int *exit_status,
     GError **error)
{
  g_auto(GStrv) envp = NULL;
  g_autoptr(GOptionContext) context = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;

  setlocale (LC_ALL, "");

  if (!_srt_util_set_glib_log_handler (this_program_name,
                                       G_LOG_DOMAIN,
                                       SRT_LOG_FLAGS_DIVERT_STDOUT,
                                       &original_stdout, &original_stderr,
                                       error))
    return FALSE;

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] as though it was the main executable of "
      "the Steam Client.\n");

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    return FALSE;

  g_clear_pointer (&context, g_option_context_free);

  if (opt_version)
    {
      if (!_srt_util_restore_saved_fd (original_stdout, STDOUT_FILENO, error))
        return FALSE;

      g_print ("%s:\n"
               " Package: steam-runtime-tools\n"
               " Version: %s\n",
               argv[0], VERSION);
      *exit_status = 0;
      return TRUE;
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN,
                                       (SRT_LOG_FLAGS_DIVERT_STDOUT
                                        | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL
                                        | (opt_verbose >= 2 ? SRT_LOG_FLAGS_DEBUG : 0)
                                        | (opt_verbose ? SRT_LOG_FLAGS_INFO : 0)),
                                       NULL, NULL, error))
    return FALSE;

  _srt_setenv_disable_gio_modules ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      g_printerr ("%s: Usage: %s [OPTIONS] COMMAND [ARG...]\n",
                  g_get_prgname (),
                  g_get_prgname ());
      return FALSE;
    }

  /* TODO: Run steam-runtime-launcher-service sidecar */
  g_critical ("Should run steam-runtime-launcher-service now");

  /* TODO: Run steamrt3c container, containing argv[1...] */
  g_critical ("Should run \"%s\" now", argv[1]);

  *exit_status = 0;
  return TRUE;
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  int exit_status = -1;

  if (run (argc, argv, &exit_status, &error))
    return exit_status;

  if (error == NULL)
    g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Assertion failure");

  _srt_log_failure ("%s", error->message);

  if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
    return LAUNCH_EX_NOT_FOUND;
  else if (error->domain == G_SPAWN_ERROR)
    return LAUNCH_EX_CANNOT_INVOKE;
  else
    return LAUNCH_EX_FAILED;
}
