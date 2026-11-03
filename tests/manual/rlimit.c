/*
 * Copyright 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <stdint.h>
#include <sys/resource.h>

static const GOptionEntry option_entries[] = { { NULL } };

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GOptionContext) option_context = NULL;
  struct rlimit rlim = { 0, 0 };
  int result;
  int saved_errno;

  option_context = g_option_context_new ("COMMAND [ARGUMENTS...]");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, error))
    return FALSE;

  result = _srt_set_compatible_resource_limits (0, &local_error);
  saved_errno = errno;

  if (result == 0)
    {
      g_assert_no_error (local_error);
      g_message ("set compatible resource limits => success");
    }
  else
    {
      g_assert_nonnull (local_error);
      g_assert_cmpint (result, ==, -saved_errno);
      g_message ("set compatible resource limits => %s", local_error->message);
      g_clear_error (&local_error);
    }

  g_assert_no_errno (prlimit (0, RLIMIT_NOFILE, NULL, &rlim));
  g_message ("RLIMIT_NOFILE soft=%ju, hard=%ju",
             (uintmax_t) rlim.rlim_cur,
             (uintmax_t) rlim.rlim_max);

  return TRUE;
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;

  if (!run (argc, argv, &error))
    {
      if (error == NULL)
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Assertion failure");

      g_printerr ("%s\n", error->message);
      return 1;
    }

  return 0;
}

