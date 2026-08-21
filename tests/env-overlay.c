/*
 * Copyright © 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"

typedef struct
{
  TestsOpenFdSet old_fds;
  SrtEnvOverlay *container_env;
} Fixture;

typedef struct
{
  int unused;
} Config;

static const char * const initial_envp[] =
{
  "FLATPAK_ID=com.valvesoftware.Steam",
  "G_MESSAGES_DEBUG=",
  "LD_AUDIT=audit.so",
  "LD_PRELOAD=libfakeroot.so",
  "STEAM_RUNTIME=0",
  "TMPDIR=/tmp",
  NULL
};

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  f->old_fds = tests_check_fd_leaks_enter ();

  f->container_env = _srt_env_overlay_new ();

  /* In each of these pairs, the first one is filtered by glibc for setuid
   * executables and the second is not (although in fact this doesn't
   * matter, because we treat both cases the same here). */
  _srt_env_overlay_set (f->container_env, "LD_AUDIT", "audit2.so");
  _srt_env_overlay_set (f->container_env, "G_MESSAGES_DEBUG", "all");
  _srt_env_overlay_set (f->container_env, "TMPDIR", NULL);
  _srt_env_overlay_set (f->container_env, "STEAM_RUNTIME", NULL);
  _srt_env_overlay_inherit (f->container_env, "LD_PRELOAD");
  _srt_env_overlay_inherit (f->container_env, "FLATPAK_ID");
  /* These are not syntactically valid shell variables, but they're allowed
   * as environment variables */
  _srt_env_overlay_set (f->container_env, "2weird", "starts with digit");
  _srt_env_overlay_set (f->container_env, " ", "space");
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->container_env, _srt_env_overlay_unref);

  tests_check_fd_leaks_leave (f->old_fds);
}

static void
dump_envp (const char * const *envp)
{
  gsize i;

  g_test_message ("Environment:");

  if (envp == NULL || envp[0] == NULL)
    {
      g_test_message ("\t(empty)");
      return;
    }

  for (i = 0; envp[i] != NULL; i++)
    g_test_message ("\t%s", envp[i]);

  g_test_message ("\t.");
}

static void
test_apply (Fixture *f,
            gconstpointer context)
{
  static const char * const expected[] =
  {
    " =space",                              /* replaced */
    "2weird=starts with digit",             /* replaced */
    "FLATPAK_ID=com.valvesoftware.Steam",   /* inherited */
    "G_MESSAGES_DEBUG=all",                 /* replaced */
    "LD_AUDIT=audit2.so",                   /* replaced */
    "LD_PRELOAD=libfakeroot.so",            /* inherited */
    /* STEAM_RUNTIME has been unset */
    /* TMPDIR has been unset */
    NULL
  };
  g_auto(GStrv) envp = NULL;

  envp = _srt_strdupv (initial_envp);
  envp = _srt_env_overlay_apply (f->container_env, envp);
  qsort (envp, g_strv_length (envp), sizeof (char *), _srt_indirect_strcmp0);
  dump_envp (_srt_const_strv (envp));
  g_assert_cmpstrv (envp, (gchar **) expected);
}

static void
dump_env0 (GBytes *env0)
{
  size_t len;
  const char *data = g_bytes_get_data (env0, &len);

  g_test_message ("env -0: %zu bytes", len);

  while (len > 0)
    {
      size_t next_len = strnlen (data, len);

      g_assert_cmpuint (next_len, <, len);
      g_test_message ("\t%s", data);
      data += (next_len + 1);
      len -= (next_len + 1);
    }
}

/* Exercise _srt_env_overlay_to_env0() and _srt_env_overlay_copy() */
static void
test_to_env0 (Fixture *f,
              gconstpointer context)
{
  static const char * const expected[] =
  {
    " =space",
    "2weird=starts with digit",
    "G_MESSAGES_DEBUG=all",
    "LD_AUDIT=audit2.so",
    NULL
  };
  g_autoptr(GBytes) env0 = NULL;
  g_autoptr(SrtEnvOverlay) copy = _srt_env_overlay_copy (f->container_env);
  size_t len;
  size_t i = 0;
  const char *data;

  /* Modifying the original does not change the copy */
  _srt_env_overlay_set (f->container_env, "LD_AUDIT", "different");

  env0 = _srt_env_overlay_to_env0 (copy);
  dump_env0 (env0);
  data = g_bytes_get_data (env0, &len);

  while (len > 0)
    {
      size_t next_len = strnlen (data, len);

      g_assert_cmpuint (next_len, <, len);
      g_assert_cmpstr (data, ==, expected[i]);
      i++;
      data += (next_len + 1);
      len -= (next_len + 1);
    }

  g_assert_null (expected[i]);
}

static void
test_to_shell (Fixture *f,
               gconstpointer context)
{
  static const char * const expected_names[] =
  {
    "G_MESSAGES_DEBUG",
    "LD_AUDIT",
    "STEAM_RUNTIME",
    "TMPDIR",
    NULL
  };
  static const char * const expected_values[] =
  {
    "all",
    "audit2.so",
    NULL,
    NULL,
  };
  g_autoptr(SrtEnvOverlay) parsed = NULL;
  g_autoptr(GList) vars = NULL;
  g_autofree gchar *sh = _srt_env_overlay_to_shell (f->container_env);
  g_auto(GStrv) lines = NULL;
  gsize i;
  GList *iter;

  g_test_message ("%s", sh);

  /* Parse the resulting shell script, like a subset of eval(1posix) */
  parsed = _srt_env_overlay_new ();
  lines = g_strsplit (sh, "\n", -1);

  for (i = 0; lines[i] != NULL; i++)
    {
      g_autoptr(GError) error = NULL;
      gboolean ok;
      int argc;
      g_auto(GStrv) argv = NULL;
      const char *name;
      const char *value;

      if (lines[i][0] == '\0')
        continue;

      ok = g_shell_parse_argv (lines[i], &argc, &argv, &error);
      g_assert_no_error (error);
      g_assert_true (ok);
      g_assert_cmpint (argc, ==, 2);

      if (g_strcmp0 (argv[0], "export") == 0)
        {
          char *equals = strchr (argv[1], '=');

          g_assert_nonnull (equals);
          *equals = '\0';
          name = argv[1];
          value = equals + 1;
        }
      else
        {
          g_assert_cmpstr (argv[0], ==, "unset");
          name = argv[1];
          value = NULL;
        }

      g_assert_false (_srt_env_overlay_contains (parsed, name));
      _srt_env_overlay_set (parsed, name, value);
    }

  /* Assert that the result is what we expect */
  vars = _srt_env_overlay_get_vars (parsed);

  for (i = 0, iter = vars;
       i < G_N_ELEMENTS (expected_names);
       i++, iter = iter->next)
    {
      if (expected_names[i] == NULL)
        {
          g_assert_null (iter);
          break;
        }
      else
        {
          g_assert_nonnull (iter);
          g_assert_cmpstr ((const char *) iter->data, ==, expected_names[i]);
          g_assert_cmpstr (_srt_env_overlay_get (parsed, iter->data),
                           ==,
                           expected_values[i]);
        }
    }
}

static void
test_prepend_append_immediate (Fixture *f,
                               gconstpointer context)
{
  /* Test all possible combinations of "prefix" and "current" for
   * _srt_env_overlay_prepend */

  /* NULL prefix can't happen */
  _srt_env_overlay_set (f->container_env, "VAR", "current");
  g_test_expect_message ("steam-runtime-tools", G_LOG_LEVEL_CRITICAL,
                         "_srt_env_overlay_prepend: assertion 'prefix != NULL' failed");
  _srt_env_overlay_prepend (f->container_env, "VAR", NULL, ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "current");

  /* Test prefix == "" with different current values */
  _srt_env_overlay_set (f->container_env, "VAR", NULL);
  _srt_env_overlay_prepend (f->container_env, "VAR", "", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "");

  _srt_env_overlay_set (f->container_env, "VAR", "");
  _srt_env_overlay_prepend (f->container_env, "VAR", "", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "");

  _srt_env_overlay_set (f->container_env, "VAR", "current");
  _srt_env_overlay_prepend (f->container_env, "VAR", "", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "current");

  /* Test prefix == "prefix" with different current values */
  _srt_env_overlay_set (f->container_env, "VAR", NULL);
  _srt_env_overlay_prepend (f->container_env, "VAR", "prefix", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "prefix");

  _srt_env_overlay_set (f->container_env, "VAR", "");
  _srt_env_overlay_prepend (f->container_env, "VAR", "prefix", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "prefix");

  _srt_env_overlay_set (f->container_env, "VAR", "current");
  _srt_env_overlay_prepend (f->container_env, "VAR", "prefix", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "VAR"),
                   ==, "prefix:current");

  /* Test combining immediate prepend and append */

  /* G_MESSAGES_DEBUG is already set to "all" by setup() */
  _srt_env_overlay_prepend (f->container_env, "G_MESSAGES_DEBUG", "x", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "G_MESSAGES_DEBUG"),
                   ==, "x:all");

  _srt_env_overlay_append (f->container_env, "G_MESSAGES_DEBUG", "y", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "G_MESSAGES_DEBUG"),
                   ==, "x:all:y");

  /* Immediate edit with a different delimiter, however strange, is allowed. */
  _srt_env_overlay_append (f->container_env, "G_MESSAGES_DEBUG", "z", ' ');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "G_MESSAGES_DEBUG"),
                   ==, "x:all:y z");
}

static void
test_prepend_append_deferred (Fixture *f,
                              gconstpointer context)
{
  char out_delimiter = '\0';
  const char *out_prepend = NULL;
  const char *out_append = NULL;
  g_auto(GStrv) envp = NULL;

  /* NOT_IN_ENV not in the env overlay, this tests unresolved pending edits. */
  _srt_env_overlay_prepend (f->container_env, "NOT_IN_ENV", "a", ':');
  _srt_env_overlay_append (f->container_env, "NOT_IN_ENV", "x", ':');
  _srt_env_overlay_prepend (f->container_env, "NOT_IN_ENV", "b", ':');
  _srt_env_overlay_append (f->container_env, "NOT_IN_ENV", "y", ':');

  /* Deferred edits with a different delimiter happen with the first
   * specified delimiter. */
  g_test_expect_message ("steam-runtime-tools", G_LOG_LEVEL_WARNING,
                         "NOT_IN_ENV is already being edited with ':', delimiter ' ' is going to be discarded.");
  _srt_env_overlay_prepend (f->container_env, "NOT_IN_ENV", "c", ' ');
  g_test_expect_message ("steam-runtime-tools", G_LOG_LEVEL_WARNING,
                         "NOT_IN_ENV is already being edited with ':', delimiter ' ' is going to be discarded.");
  _srt_env_overlay_append (f->container_env, "NOT_IN_ENV", "z", ' ');

  g_assert_true (_srt_env_overlay_contains (f->container_env, "NOT_IN_ENV"));
  g_assert_null (_srt_env_overlay_get (f->container_env, "NOT_IN_ENV"));

  g_assert_true (_srt_env_overlay_get_edit (f->container_env, "NOT_IN_ENV",
                                            &out_delimiter,
                                            &out_prepend, &out_append));
  g_assert_cmpint (out_delimiter, ==, ':');
  g_assert_cmpstr (out_prepend, ==, "c:b:a");
  g_assert_cmpstr (out_append, ==, "x:y:z");

  /* FLATPAK_ID is inherited (not in values) after setup(); a deferred
   * prepend/append must not be resolved yet. */
  g_assert_false (_srt_env_overlay_contains (f->container_env, "FLATPAK_ID"));
  _srt_env_overlay_prepend (f->container_env, "FLATPAK_ID", "pre", ':');
  _srt_env_overlay_append (f->container_env, "FLATPAK_ID", "post", ':');

  /* _srt_env_overlay_get() still returns NULL,
   * exactly as if it were inherited: the real value is not known yet. */
  g_assert_true (_srt_env_overlay_contains (f->container_env, "FLATPAK_ID"));
  g_assert_null (_srt_env_overlay_get (f->container_env, "FLATPAK_ID"));

  /* But the edit is tracked, _srt_env_overlay_get_edit() sees it */
  g_assert_true (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                            &out_delimiter, &out_prepend, &out_append));
  g_assert_cmpint (out_delimiter, ==, ':');
  g_assert_cmpstr (out_prepend, ==, "pre");
  g_assert_cmpstr (out_append, ==, "post");

  /* A variable with no pending edit at all returns FALSE. */
  g_assert_false (_srt_env_overlay_get_edit (f->container_env, "LD_AUDIT",
                                             NULL, NULL, NULL));

  /* Resolved against the real envp at apply() time. */
  envp = _srt_strdupv  (initial_envp);
  envp = _srt_env_overlay_apply (f->container_env, envp);
  g_assert_cmpstr (g_environ_getenv (envp, "FLATPAK_ID"), ==,
                   "pre:com.valvesoftware.Steam:post");
}

static void
test_prepend_append_deferred_absent (Fixture *f,
                                     gconstpointer context)
{
  g_auto(GStrv) envp = NULL;

  /* NOT_IN_ENVP is inherited, and does not appear in initial_envp at all. */
  _srt_env_overlay_prepend (f->container_env, "NOT_IN_ENVP", "pre", ':');
  _srt_env_overlay_append (f->container_env, "NOT_IN_ENVP", "post", ':');

  envp = _srt_strdupv (initial_envp);
  envp = _srt_env_overlay_apply (f->container_env, envp);
  /* No spurious delimiter next to the missing base value. */
  g_assert_cmpstr (g_environ_getenv (envp, "NOT_IN_ENVP"), ==, "pre:post");
}

static void
test_prepend_append_accumulate (Fixture *f,
                                gconstpointer context)
{
  g_auto(GStrv) envp = NULL;

  /* Repeated prepend/append calls on a resolved variable apply immediately.
   * G_MESSAGES_DEBUG is already set to "all" by setup() */

  /* Each new prepend call is pushed further ahead of the base value. */
  _srt_env_overlay_prepend (f->container_env, "G_MESSAGES_DEBUG", "a", ':');
  _srt_env_overlay_prepend (f->container_env, "G_MESSAGES_DEBUG", "b", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "G_MESSAGES_DEBUG"),
                   ==, "b:a:all");

  /* Repeated append calls accumulate in call order. */
  _srt_env_overlay_append (f->container_env, "G_MESSAGES_DEBUG", "x", ':');
  _srt_env_overlay_append (f->container_env, "G_MESSAGES_DEBUG", "y", ':');
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "G_MESSAGES_DEBUG"),
                   ==, "b:a:all:x:y");

  /* Repeated prepend calls on a still-deferred variable. */

  /* Each new prepend call is pushed further ahead of the base value. */
  _srt_env_overlay_prepend (f->container_env, "FLATPAK_ID", "a", ':');
  _srt_env_overlay_prepend (f->container_env, "FLATPAK_ID", "b", ':');
  /* Repeated append calls accumulate in call order. */
  _srt_env_overlay_append (f->container_env, "FLATPAK_ID", "x", ':');
  _srt_env_overlay_append (f->container_env, "FLATPAK_ID", "y", ':');

  envp = _srt_strdupv (initial_envp);
  envp = _srt_env_overlay_apply (f->container_env, envp);
  g_assert_cmpstr (g_environ_getenv (envp, "FLATPAK_ID"), ==,
                   "b:a:com.valvesoftware.Steam:x:y");
}

static void
test_prepend_append_mutual_exclusivity (Fixture *f,
                                        gconstpointer context)
{
  /* A deferred edit on FLATPAK_ID... */
  _srt_env_overlay_prepend (f->container_env, "FLATPAK_ID", "pre", ':');
  g_assert_true (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                            NULL, NULL, NULL));

  /* ...is cleared by an explicit set()... */
  _srt_env_overlay_set (f->container_env, "FLATPAK_ID", "explicit");
  g_assert_false (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                             NULL, NULL, NULL));
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "FLATPAK_ID"),
                   ==, "explicit");

  /* ...and also by an explicit inherit(). First reset to inherited state. */
  _srt_env_overlay_inherit (f->container_env, "FLATPAK_ID");
  _srt_env_overlay_prepend (f->container_env, "FLATPAK_ID", "pre", ':');
  g_assert_true (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                            NULL, NULL, NULL));
  _srt_env_overlay_inherit (f->container_env, "FLATPAK_ID");
  g_assert_false (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                             NULL, NULL, NULL));
  g_assert_false (_srt_env_overlay_contains (f->container_env, "FLATPAK_ID"));
}

static void
test_resolve_edits (Fixture *f,
                    gconstpointer context)
{
  g_auto(GStrv) envp = NULL;

  /* LD_PRELOAD is inherited, and appears in initial_envp. */
  _srt_env_overlay_prepend (f->container_env, "LD_PRELOAD", "pre", ':');
  _srt_env_overlay_append (f->container_env, "LD_PRELOAD", "post", ':');

  /* NOT_IN_ENVP is inherited, and does not appear in initial_envp at all. */
  _srt_env_overlay_prepend (f->container_env, "NOT_IN_ENVP", "pre", ':');
  _srt_env_overlay_append (f->container_env, "NOT_IN_ENVP", "post", ':');

  _srt_env_overlay_resolve_edits (f->container_env, initial_envp);

  /* Edits have been resolved. */
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "LD_PRELOAD"), ==, "pre:libfakeroot.so:post");
  g_assert_false (_srt_env_overlay_get_edit (f->container_env, "LD_PRELOAD", NULL, NULL, NULL));
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "NOT_IN_ENVP"), ==, "pre:post");
  g_assert_false (_srt_env_overlay_get_edit (f->container_env, "NOT_IN_ENVP", NULL, NULL, NULL));
}

static void
test_inherit_matching_pattern_clears_edits (Fixture *f,
                                            gconstpointer context)
{
  /* A deferred edit on FLATPAK_ID... */
  _srt_env_overlay_prepend (f->container_env, "FLATPAK_ID", "pre", ':');
  g_assert_true (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                            NULL, NULL, NULL));
  g_assert_true (_srt_env_overlay_contains (f->container_env, "FLATPAK_ID"));

  /* ...must be cleared by inherit_matching_pattern() when the name matches. */
  _srt_env_overlay_inherit_matching_pattern (f->container_env, "FLATPAK_*");

  g_assert_false (_srt_env_overlay_get_edit (f->container_env, "FLATPAK_ID",
                                             NULL, NULL, NULL));
  g_assert_false (_srt_env_overlay_contains (f->container_env, "FLATPAK_ID"));

  /* A deferred append on a non-matching variable must be unaffected. */
  _srt_env_overlay_append (f->container_env, "LD_PRELOAD", "extra", ':');
  _srt_env_overlay_inherit_matching_pattern (f->container_env, "FLATPAK_*");
  g_assert_true (_srt_env_overlay_get_edit (f->container_env, "LD_PRELOAD",
                                            NULL, NULL, NULL));
}

static void
test_update (Fixture *f,
             gconstpointer context)
{
  g_autoptr(SrtEnvOverlay) other = _srt_env_overlay_new ();

  _srt_env_overlay_set (other, "LD_AUDIT", NULL);
  _srt_env_overlay_set (other, "TMPDIR", "/tmp");
  _srt_env_overlay_inherit (other, "G_MESSAGES_DEBUG");

  _srt_env_overlay_update (f->container_env, other);
  /* Setting a variable in the source sets it in the destination */
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "TMPDIR"), ==, "/tmp");
  /* Unsetting a variable in the source unsets it in the destination */
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "LD_AUDIT"), ==, NULL);
  g_assert_true (_srt_env_overlay_contains (f->container_env, "LD_AUDIT"));
  /* Setting a variable to be inherited in the source has no effect on
   * the destination */
  g_assert_cmpstr (_srt_env_overlay_get (f->container_env, "G_MESSAGES_DEBUG"),
                   ==, "all");
}

static void
test_update_edits (Fixture *f,
                   gconstpointer context)
{
  g_autoptr(SrtEnvOverlay) self = NULL;
  g_autoptr(SrtEnvOverlay) other = NULL;
  g_autoptr(SrtEnvOverlay) self_updated = NULL;
  char out_delimiter = '\0';
  const char *out_prepend = NULL;
  const char *out_append = NULL;

  self = _srt_env_overlay_new();
  _srt_env_overlay_set (self, "VAR1", "val1_self");
  _srt_env_overlay_set (self, "VAR2", "val2_self");
  _srt_env_overlay_set (self, "VAR5", "val5_self");
  _srt_env_overlay_append (self, "VAR3", "val3_post_self", ':');
  _srt_env_overlay_prepend (self, "VAR4", "val4_pre_self", ':');
  _srt_env_overlay_append (self, "VAR4", "val4_post_self", ':');
  _srt_env_overlay_prepend (self, "VAR7", "val7_pre_self", ':');
  _srt_env_overlay_append (self, "VAR7", "val7_post_self", ':');

  other = _srt_env_overlay_new();
  _srt_env_overlay_set (other, "VAR1", "val1_other");
  _srt_env_overlay_set (other, "VAR3", NULL);
  _srt_env_overlay_set (other, "VAR6", "val6_other");
  _srt_env_overlay_prepend (other, "VAR2", "val2_pre_other", ':');
  _srt_env_overlay_append (other, "VAR2", "val2_post_other", ':');
  _srt_env_overlay_prepend (other, "VAR4", "val4_pre_other", ':');
  _srt_env_overlay_prepend (other, "VAR8", "val8_pre_other", ':');
  _srt_env_overlay_append (other, "VAR8", "val8_post_other", ':');

  self_updated = _srt_env_overlay_copy(self);
  _srt_env_overlay_update (self_updated, other);

  /* VAR1 is taken from `other->values` */
  g_assert_cmpstr (_srt_env_overlay_get (self_updated, "VAR1"),
                   ==, "val1_other");
  g_assert_false (_srt_env_overlay_get_edit (self_updated, "VAR1",
                                             NULL, NULL, NULL));

  /* The VAR2 from `other->edits` is applied to `self->values` */
  g_assert_cmpstr (_srt_env_overlay_get (self_updated, "VAR2"),
                   ==, "val2_pre_other:val2_self:val2_post_other");
  g_assert_false (_srt_env_overlay_get_edit (self_updated, "VAR2",
                                             NULL, NULL, NULL));

  /* VAR3 is unset by `other->values` replacing the one in `self->edits` */
  g_assert_cmpstr (_srt_env_overlay_get (self_updated, "VAR3"),
                   ==, NULL);
  g_assert_false (_srt_env_overlay_get_edit (self_updated, "VAR3",
                                             NULL, NULL, NULL));

  /* The VAR4 from `other->edits` is applied to `self->edits` */
  g_assert_true (_srt_env_overlay_get_edit (self_updated, "VAR4",
                                            &out_delimiter, &out_prepend, &out_append));
  g_assert_cmpint (out_delimiter, ==, ':');
  g_assert_cmpstr (out_prepend, ==, "val4_pre_other:val4_pre_self");
  g_assert_cmpstr (out_append, ==, "val4_post_self");
  g_assert_null (_srt_env_overlay_get (self_updated, "VAR4"));

  /* VAR5 was only in `self->values` and is kept intact */
  g_assert_cmpstr (_srt_env_overlay_get (self_updated, "VAR5"),
                   ==, "val5_self");
  g_assert_false (_srt_env_overlay_get_edit (self_updated, "VAR5",
                                             NULL, NULL, NULL));

  /* VAR6 was only in `other->values` and is updated in `self` */
  g_assert_cmpstr (_srt_env_overlay_get (self_updated, "VAR6"),
                   ==, "val6_other");
  g_assert_false (_srt_env_overlay_get_edit (self_updated, "VAR6",
                                             NULL, NULL, NULL));

  /* VAR7 was only in `self->edits` and is kept intact */
  g_assert_true (_srt_env_overlay_get_edit (self_updated, "VAR7",
                                            &out_delimiter, &out_prepend, &out_append));
  g_assert_cmpint (out_delimiter, ==, ':');
  g_assert_cmpstr (out_prepend, ==, "val7_pre_self");
  g_assert_cmpstr (out_append, ==, "val7_post_self");
  g_assert_null (_srt_env_overlay_get (self_updated, "VAR7"));

  /* VAR8 was only in `other->edits` and is updated in `self` */
  g_assert_true (_srt_env_overlay_get_edit (self_updated, "VAR8",
                                            &out_delimiter, &out_prepend, &out_append));
  g_assert_cmpint (out_delimiter, ==, ':');
  g_assert_cmpstr (out_prepend, ==, "val8_pre_other");
  g_assert_cmpstr (out_append, ==, "val8_post_other");
  g_assert_null (_srt_env_overlay_get (self_updated, "VAR8"));

  /* In different terms, check that applying `self_updated` to the env
   * is equivalent to sequentially applying `self` and `other`. */
  {
    g_auto(GStrv) envp1 = NULL;
    g_auto(GStrv) envp2 = NULL;

    envp1 = _srt_env_overlay_apply (self_updated, envp1);

    envp2 = _srt_env_overlay_apply (self, envp2);
    envp2 = _srt_env_overlay_apply (other, envp2);

    qsort (envp1, g_strv_length (envp1), sizeof (char *), _srt_indirect_strcmp0);
    qsort (envp2, g_strv_length (envp2), sizeof (char *), _srt_indirect_strcmp0);
    g_assert_cmpstrv (envp1, envp2);
  }
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/env-overlay/apply", Fixture, NULL,
              setup, test_apply, teardown);
  g_test_add ("/env-overlay/to-env0", Fixture, NULL,
              setup, test_to_env0, teardown);
  g_test_add ("/env-overlay/to-shell", Fixture, NULL,
              setup, test_to_shell, teardown);
  g_test_add ("/env-overlay/prepend-append-immediate", Fixture, NULL,
              setup, test_prepend_append_immediate, teardown);
  g_test_add ("/env-overlay/prepend-append-deferred", Fixture, NULL,
              setup, test_prepend_append_deferred, teardown);
  g_test_add ("/env-overlay/prepend-append-deferred-absent", Fixture, NULL,
              setup, test_prepend_append_deferred_absent, teardown);
  g_test_add ("/env-overlay/prepend-append-accumulate", Fixture, NULL,
              setup, test_prepend_append_accumulate, teardown);
  g_test_add ("/env-overlay/prepend-append-mutual-exclusivity", Fixture, NULL,
              setup, test_prepend_append_mutual_exclusivity, teardown);
  g_test_add ("/env-overlay/inherit-matching-pattern-clears-edits", Fixture, NULL,
              setup, test_inherit_matching_pattern_clears_edits, teardown);
  g_test_add ("/env-overlay/resolve-edits", Fixture, NULL,
              setup, test_resolve_edits, teardown);
  g_test_add ("/env-overlay/update", Fixture, NULL,
              setup, test_update, teardown);
  g_test_add ("/env-overlay/update-edits", Fixture, NULL,
              setup, test_update_edits, teardown);

  return g_test_run ();
}
