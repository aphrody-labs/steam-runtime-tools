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
#include "steam-runtime-tools/cli-internal.h"
#include "steam-runtime-tools/launcher-interface-internal.h"
#include "steam-runtime-tools/process-manager-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static const char this_program_name[] = "srt-client-container";

typedef struct
{
  GPtrArray *argv;
  const char * const *envp;
  GPid pid;
  guint child_watch;
  int original_stdout;
  int original_stderr;
  int exit_status;
} ContainerManager;

#define CONTAINER_MANAGER_EMPTY {\
  .original_stdout = -1, \
  .original_stderr = -1, \
}

static void
container_manager_clear (ContainerManager *self)
{
  g_clear_pointer (&self->argv, g_ptr_array_unref);
  g_clear_handle_id (&self->child_watch, g_source_remove);

  if (self->pid > 0)
    {
      /* We kill the subprocess but don't wait for it */
      g_info ("Terminating containerized subprocess");
      kill (self->pid, SIGTERM);
    }
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (ContainerManager,
                                  container_manager_clear)

static gboolean
container_manager_init (ContainerManager *self,
                        const char * const *command,
                        int command_len,
                        const char *pv_entry_point,
                        const char * const *pv_options,
                        size_t n_pv_options,
                        int original_stdout,
                        int original_stderr,
                        GError **error)
{
  g_autoptr(GPtrArray) argv = NULL;
  g_autoptr(GString) run_script = NULL;

  g_return_val_if_fail (self->pid == 0, FALSE);
  g_return_val_if_fail (self->child_watch == 0, FALSE);

  if (pv_entry_point != NULL)
    {
      run_script = g_string_new (pv_entry_point);
    }
  else
    {
      const char *prefix;

      prefix = _srt_find_myself (NULL, NULL, error);

      if (prefix == NULL)
        return FALSE;

      run_script = g_string_new (prefix);

      if (!_srt_string_replace_suffix (run_script, "/pressure-vessel", "/run"))
        return glnx_throw (error,
                           "Unable to find steamrt*c runtime above %s",
                           prefix);
    }

  argv = g_ptr_array_new_full (4 + command_len + n_pv_options, g_free);
  g_ptr_array_add (argv, g_strdup (run_script->str));
  g_ptr_array_add (argv, g_strdup ("--for-steam-client"));

  for (size_t i = 0; i < n_pv_options; i++)
    g_ptr_array_add (argv, g_strdup (pv_options[i]));

  g_ptr_array_add (argv, g_strdup ("--"));

  for (int i = 0; i < command_len; i++)
    g_ptr_array_add (argv, g_strdup (command[i]));

  g_ptr_array_add (argv, NULL);
  self->argv = g_steal_pointer (&argv);

  self->original_stdout = original_stdout;
  self->original_stderr = original_stderr;
  return TRUE;
}

static void
container_manager_child_setup_cb (void *user_data)
{
  ContainerManager *self = user_data;

  if (!_srt_raise_on_parent_death (SIGTERM, NULL))
    _srt_async_signal_safe_error (this_program_name,
                                  "Failed to set up parent-death signal",
                                  LAUNCH_EX_FAILED);

  _srt_child_setup_unblock_signals (NULL);

  if (dup2 (self->original_stdout, STDOUT_FILENO) < 0)
    _srt_async_signal_safe_error (this_program_name,
                                  "Failed to restore original stdout",
                                  LAUNCH_EX_FAILED);

  if (dup2 (self->original_stderr, STDERR_FILENO) < 0)
    _srt_async_signal_safe_error (this_program_name,
                                  "Failed to restore original stderr",
                                  LAUNCH_EX_FAILED);
}

static void
container_died_cb (GPid pid,
                   int wait_status,
                   void *user_data)
{
  ContainerManager *self = user_data;

  g_return_if_fail (pid == self->pid);
  self->pid = 0;
  self->child_watch = 0;

  g_info ("Containerized subprocess %u exited, wait status %d",
          (unsigned int) pid, wait_status);
  self->exit_status = _srt_wait_status_to_exit_status (wait_status);
}

static gboolean
container_manager_start (ContainerManager *self,
                         GError **error)
{
  g_return_val_if_fail (self->argv != NULL, FALSE);
  g_return_val_if_fail (self->pid == 0, FALSE);
  g_return_val_if_fail (self->child_watch == 0, FALSE);

  /* We intentionally don't close inherited file descriptors here:
   * if there are any, then the container manager and the Steam Client
   * get to inherit them. */
  if (!g_spawn_async_with_pipes (NULL,
                                 (char **) self->argv->pdata,
                                 (char **) self->envp,
                                 (G_SPAWN_DO_NOT_REAP_CHILD
                                  | G_SPAWN_CHILD_INHERITS_STDIN
                                  | G_SPAWN_LEAVE_DESCRIPTORS_OPEN),
                                 container_manager_child_setup_cb,
                                 self,
                                 &self->pid,
                                 NULL,    /* inherit stdin */
                                 NULL,    /* inherit stdout */
                                 NULL,    /* inherit stderr */
                                 error))
    return FALSE;

  /* No need for these any more */
  g_clear_pointer (&self->argv, g_ptr_array_unref);

  /* This is higher-priority than watching s-r-launcher-service:
   * if they both exit at the same time, we want to exit without
   * restarting the launcher service. */
  self->child_watch = g_child_watch_add_full (G_PRIORITY_HIGH,
                                              self->pid,
                                              container_died_cb,
                                              self,
                                              NULL);

  return TRUE;
}

typedef struct
{
  gchar *exe;
  gchar *bus_name;
  const char * const *envp;
  GPid pid;
  guint child_watch;
  guint respawn_timeout;
  gboolean respawn;
  gboolean started_once;
} LauncherServiceManager;

#define LAUNCHER_SERVICE_MANAGER_EMPTY {\
  .respawn = TRUE, \
}

static void
launcher_service_manager_clear (LauncherServiceManager *self)
{
  g_clear_pointer (&self->exe, g_free);
  g_clear_pointer (&self->bus_name, g_free);
  g_clear_handle_id (&self->child_watch, g_source_remove);
  g_clear_handle_id (&self->respawn_timeout, g_source_remove);

  if (self->pid > 0)
    {
      /* We kill the s-r-l-s instance but don't wait for it */
      kill (self->pid, SIGTERM);
      self->pid = 0;
    }
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (LauncherServiceManager,
                                  launcher_service_manager_clear)

static gboolean
launcher_service_manager_init (LauncherServiceManager *self,
                               GError **error)
{
  const char *prefix = NULL;
  const char *pkglibexecdir = NULL;
  const char *env;

  g_return_val_if_fail (self->exe == NULL, FALSE);
  g_return_val_if_fail (self->bus_name == NULL, FALSE);
  g_return_val_if_fail (self->pid == 0, FALSE);
  g_return_val_if_fail (!self->started_once, FALSE);

  prefix = _srt_find_myself (NULL, &pkglibexecdir, error);

  if (prefix == NULL)
    return FALSE;

  /* Check for environment variable override */
  env = g_getenv ("SRT_LAUNCHER_SERVICE");

  if (env != NULL)
    {
      self->exe = g_strdup (env);
    }
  else
    {
#ifdef _SRT_MULTIARCH
      self->exe = g_strdup_printf ("%s/%s-srt-launcher-service",
                                   pkglibexecdir, _SRT_MULTIARCH);
#else
      self->exe = g_strdup_printf ("%s/bin/steam-runtime-launcher-service",
                                   prefix);
#endif
    }

  self->bus_name = g_strdup_printf ("%s.Instance%u",
                                    LAUNCHER_NAME_ALONGSIDE_STEAM,
                                    (unsigned int) getpid ());
  return TRUE;
}

static gboolean launcher_service_manager_start (LauncherServiceManager *self,
                                                GError **error);

static gboolean
launcher_service_respawn_cb (void *user_data)
{
  LauncherServiceManager *self = user_data;
  g_autoptr(GError) local_error = NULL;

  g_info ("Restarting subprocess launcher...");

  if (!launcher_service_manager_start (self, &local_error))
    {
      g_warning ("Unable to restart subprocess launcher: %s",
                 local_error->message);
      self->respawn = FALSE;
    }

  self->respawn_timeout = 0;
  return G_SOURCE_REMOVE;
}

static void
launcher_service_died_cb (GPid pid,
                          int wait_status,
                          void *user_data)
{
  LauncherServiceManager *self = user_data;

  g_return_if_fail (pid == self->pid);
  self->pid = 0;
  self->child_watch = 0;

  g_info ("Subprocess launcher service process %u exited, wait status %d",
          (unsigned int) pid, wait_status);

  if (self->respawn && self->respawn_timeout == 0)
    self->respawn_timeout = g_timeout_add_seconds (1,
                                                   launcher_service_respawn_cb,
                                                   self);
}

static void
launcher_service_manager_child_setup_cb (void *user_data)
{
  if (!_srt_raise_on_parent_death (SIGTERM, NULL))
    _srt_async_signal_safe_error (this_program_name,
                                  "Failed to set up parent-death signal",
                                  LAUNCH_EX_FAILED);

  g_fdwalk_set_cloexec (3);
  _srt_child_setup_unblock_signals (NULL);
}

static gboolean
launcher_service_manager_start (LauncherServiceManager *self,
                                GError **error)
{
  g_autoptr(GPtrArray) argv = NULL;
  g_autoptr(GString) buf = NULL;
  glnx_autofd int stdout_fd = -1;

  g_return_val_if_fail (self->exe != NULL, FALSE);
  g_return_val_if_fail (self->bus_name != NULL, FALSE);
  g_return_val_if_fail (self->pid == 0, FALSE);
  g_return_val_if_fail (self->child_watch == 0, FALSE);

  argv = g_ptr_array_new_full (3, g_free);
  g_ptr_array_add (argv, g_strdup (self->exe));
  g_ptr_array_add (argv, g_strdup ("--alongside-steam"));
  g_ptr_array_add (argv, g_strdup_printf ("--bus-name=%s", self->bus_name));
  g_ptr_array_add (argv, NULL);

  /* We use LEAVE_DESCRIPTORS_OPEN and set CLOEXEC in the child_setup,
   * to work around a deadlock in GLib < 2.60 */
  if (!g_spawn_async_with_pipes (NULL,
                                 (char **) argv->pdata,
                                 (char **) self->envp,
                                 (G_SPAWN_SEARCH_PATH
                                  | G_SPAWN_DO_NOT_REAP_CHILD
                                  | G_SPAWN_LEAVE_DESCRIPTORS_OPEN),
                                 launcher_service_manager_child_setup_cb,
                                 NULL,    /* child setup data */
                                 &self->pid,
                                 NULL,    /* stdin */
                                 &stdout_fd,
                                 NULL,    /* stderr */
                                 error))
    return FALSE;

  /* Read and discard output: when EOF has been reached,
   * the s-r-l-s is ready for use */
  buf = g_string_new ("");

  if (!_srt_string_read_fd_until_eof (buf, stdout_fd, error))
    return FALSE;

  _srt_string_chomp_newline (buf);
  g_debug ("Output from s-r-l-s: %s", buf->str);

  if (!self->started_once)
    {
      int wait_status = -1;
      pid_t exited;

      if (buf->len == 0)
        {
          g_debug ("No output from s-r-l-s, assuming it failed to start");
          exited = waitpid (self->pid, &wait_status, 0);
        }
      else
        {
          g_debug ("Had output from s-r-l-s, hopefully it's working");
          exited = waitpid (self->pid, &wait_status, WNOHANG);
        }

      /* The good waitpid() result is 0, meaning self->pid is still running
       * after emitting EOF on stdout (which means it's ready). */

      if (exited < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to wait for child process");

      if (exited > 0)
        {
          self->pid = 0;

          if (!g_spawn_check_wait_status (wait_status, error))
            {
              g_prefix_error (error, "Couldn't start subprocess launcher: ");
              return FALSE;
            }

          return glnx_throw (error,
                             "Couldn't start subprocess launcher: "
                             "unexpectedly exited with status 0");
        }
    }

  self->started_once = TRUE;
  self->child_watch = g_child_watch_add_full (G_PRIORITY_DEFAULT,
                                              self->pid,
                                              launcher_service_died_cb,
                                              self,
                                              NULL);
  return TRUE;
}

static gchar *opt_pv_entry_point = NULL;
static gchar **opt_pv_options = NULL;

static void
free_option_globals (void)
{
  g_clear_pointer (&opt_pv_entry_point, g_free);
  g_clear_pointer (&opt_pv_options, g_strfreev);
}

static GOptionEntry options[] =
{
  { "pv-entry-point", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_pv_entry_point,
    "Use the given entry point to start the container.",
    "/path/to/run" },
  { "pv-option", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY, &opt_pv_options,
    "Pass the given OPTION to pressure-vessel. May be repeated.",
    "OPTION" },

  SRT_CLI_OPTION_ENTRY_VERBOSE,
  SRT_CLI_OPTION_ENTRY_VERSION,
  G_OPTION_ENTRY_NULL
};

static gboolean
run (int argc,
     char *argv[],
     int *exit_status,
     GError **error)
{
  g_auto(ContainerManager) container = CONTAINER_MANAGER_EMPTY;
  g_auto(LauncherServiceManager) launcher_service = LAUNCHER_SERVICE_MANAGER_EMPTY;
  g_auto(GStrv) envp = NULL;
  g_autoptr(GOptionContext) context = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;
  size_t n_pv_options;

  *exit_status = -1;
  setlocale (LC_ALL, "");
  envp = g_get_environ ();

  if (!_srt_cli_init (this_program_name, G_LOG_DOMAIN,
                      (SRT_LOG_FLAGS_DIVERT_STDOUT
                       | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL),
                      &original_stdout, &original_stderr,
                      error))
    return FALSE;

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] as though it was the main executable of "
      "the Steam Client.\n");

  g_option_context_add_main_entries (context, options, NULL);

  if (!_srt_cli_option_context_parse (context, original_stdout,
                                      &argc, &argv, error))
    return FALSE;

  g_clear_pointer (&context, g_option_context_free);

  _srt_setenv_disable_gio_modules ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Usage: %s [OPTIONS] COMMAND [ARG...]",
                   g_get_prgname ());
      return FALSE;
    }

  if (opt_pv_options != NULL)
    n_pv_options = g_strv_length (opt_pv_options);
  else
    n_pv_options = 0;

  if (!container_manager_init (&container,
                               _srt_const_strv (argv + 1), argc - 1,
                               opt_pv_entry_point,
                               _srt_const_strv (opt_pv_options), n_pv_options,
                               original_stdout,
                               original_stderr,
                               error))
    return FALSE;

  if (!launcher_service_manager_init (&launcher_service, error))
    return FALSE;

  envp = g_environ_setenv (envp,
                           "SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM",
                           launcher_service.bus_name,
                           TRUE);
  container.envp = _srt_const_strv (envp);
  launcher_service.envp = _srt_const_strv (envp);

  if (!launcher_service_manager_start (&launcher_service, error))
    return FALSE;

  if (!container_manager_start (&container, error))
    return FALSE;

  while (container.pid != 0)
    g_main_context_iteration (NULL, TRUE);

  g_info ("Shutting down");
  g_return_val_if_fail (container.exit_status >= 0, FALSE);
  *exit_status = container.exit_status;
  return TRUE;
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  int exit_status = -1;
  gboolean exit_normally;

  exit_normally = run (argc, argv, &exit_status, &error);

  free_option_globals ();

  if (exit_normally)
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
