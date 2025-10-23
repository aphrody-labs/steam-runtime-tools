/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/emulator-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "test-utils.h"

typedef struct
{
  GError *error;
  gchar *qemu_json;
  gchar *mock_json;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  f->mock_json = g_test_build_filename (G_TEST_DIST, "emulator-mock.json", NULL);
  f->qemu_json = g_test_build_filename (G_TEST_DIST, "emulator-qemu-arm.json", NULL);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_error (&f->error);
  g_free (f->mock_json);
  g_free (f->qemu_json);
}

static void
dump_emulator (const char *label,
               SrtEmulator *emulator)
{
  g_autoptr(GString) str = g_string_new ("");
  const char * const *strv;
  const char *s;
  const GQuark *quarks;
  size_t n;

  strv = _srt_emulator_get_argv (emulator);

  for (size_t i = 0; strv != NULL && strv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = g_shell_quote (strv[i]);

      g_string_append_printf (str, " %s", quoted);
    }

  g_test_message ("%s ->%s", label, str->str);

  s = _srt_emulator_get_manifest (emulator);

  if (s != NULL)
    g_test_message ("\tFrom manifest: %s", s);

  quarks = _srt_emulator_get_emulated_architectures (emulator, &n);

  for (size_t i = 0; i < n; i++)
    g_test_message ("\tEmulates architecture: %s", g_quark_to_string (quarks[i]));

  quarks = _srt_emulator_get_required_architectures (emulator, &n);

  for (size_t i = 0; i < n; i++)
    g_test_message ("\tRequires architecture: %s", g_quark_to_string (quarks[i]));

  strv = _srt_emulator_get_required_libraries (emulator);

  for (size_t i = 0; strv != NULL && strv[i] != NULL; i++)
    g_test_message ("\tRequires library: %s", strv[i]);
}

/*
 * Exercise a minimal emulator manifest with an absolute path
 */
static void
test_basic (Fixture *f,
            gconstpointer context)
{
  g_autoptr(SrtEmulator) emulator = NULL;
  g_autoptr(SrtEmulator) reread = NULL;
  const char * const expected_argv[] =
    {
      "/usr/bin/qemu-arm",
      "-L",
      "/usr/lib/check that quoting is handled/%M",
      NULL
    };
  const char * const expected_archs[] =
    {
      "arm-linux-gnueabi",
      "arm-linux-gnueabihf",
      "arm-linux-gnu",
      NULL
    };
  const char * const * libraries;
  const GQuark *arch_quarks;
  const GQuark *arch_quarks_again;
  size_t n = (size_t) -1;
  g_autoptr(GArray) archs_copy = NULL;
  g_autoptr(GArray) required_archs_copy = NULL;
  g_autoptr(GPtrArray) arch_strings = NULL;
  g_auto(GStrv) argv_copy = NULL;
  g_auto(GStrv) libraries_copy = NULL;
  g_autofree char *manifest_realpath = NULL;
  g_autofree char *manifest_copy = NULL;
  g_auto(GLnxTmpDir) tmpdir = { .initialized = FALSE };
  g_autofree char *write_path = NULL;
  gboolean ok;

  manifest_realpath = realpath (f->qemu_json, NULL);
  emulator = _srt_emulator_new_from_manifest (f->qemu_json, &f->error);
  g_assert_no_error (f->error);
  g_assert_nonnull (emulator);
  dump_emulator (f->qemu_json, emulator);

  g_assert_cmpstrv (_srt_emulator_get_argv (emulator), expected_argv);
  g_assert_cmpstr (_srt_emulator_get_manifest (emulator), ==, manifest_realpath);
  libraries = _srt_emulator_get_required_libraries (emulator);
  g_assert_nonnull (libraries);
  g_assert_null (libraries[0]);

  arch_quarks = _srt_emulator_get_emulated_architectures (emulator, &n);

  for (size_t i = 0; i < n; i++)
    {
      g_assert_cmpuint (arch_quarks[i], !=, SRT_ARCHITECTURE_QUARK_NONE);
      g_assert_cmpstr (g_quark_to_string (arch_quarks[i]), ==, expected_archs[i]);
    }

  g_assert_cmpuint (arch_quarks[n], ==, SRT_ARCHITECTURE_QUARK_NONE);

  arch_quarks_again = _srt_emulator_get_emulated_architectures (emulator, NULL);

  for (size_t i = 0; i <= n; i++)
    g_assert_cmpuint (arch_quarks[i], ==, arch_quarks_again[i]);

  arch_quarks = _srt_emulator_get_required_architectures (emulator, &n);
  g_assert_cmpuint (n, ==, 0);
  g_assert_nonnull (arch_quarks);
  g_assert_cmpuint (arch_quarks[0], ==, SRT_ARCHITECTURE_QUARK_NONE);
  arch_quarks_again = _srt_emulator_get_required_architectures (emulator, NULL);
  g_assert_nonnull (arch_quarks_again);
  g_assert_cmpuint (arch_quarks_again[0], ==, SRT_ARCHITECTURE_QUARK_NONE);

  g_object_get (emulator,
                "argv", &argv_copy,
                "emulated-architectures", &archs_copy,
                "manifest", &manifest_copy,
                "required-architectures", &required_archs_copy,
                "required-libraries", &libraries_copy,
                NULL);
  g_assert_cmpstrv (_srt_const_strv (argv_copy), expected_argv);
  g_assert_cmpuint (archs_copy->len, ==, G_N_ELEMENTS (expected_archs) - 1);
  g_assert_cmpuint (required_archs_copy->len, ==, 0);
  g_assert_nonnull (libraries_copy);
  g_assert_null (libraries_copy[0]);
  g_assert_cmpstr (manifest_copy, ==, manifest_realpath);

  for (size_t i = 0; i < archs_copy->len; i++)
    {
      GQuark q = g_array_index (archs_copy, GQuark, i);

      g_assert_cmpuint (q, !=, SRT_ARCHITECTURE_QUARK_NONE);
      g_assert_cmpstr (g_quark_to_string (q), ==, expected_archs[i]);
    }

  g_clear_object (&emulator);
  emulator = _srt_emulator_new (expected_argv,
                                archs_copy,
                                NULL,
                                NULL,
                                NULL);
  g_assert_cmpstrv (_srt_emulator_get_argv (emulator), expected_argv);
  g_assert_cmpstr (_srt_emulator_get_manifest (emulator), ==, NULL);
  arch_quarks = _srt_emulator_get_emulated_architectures (emulator, &n);
  g_assert_cmpuint (n, ==, archs_copy->len);
  arch_quarks = _srt_emulator_get_required_architectures (emulator, &n);
  g_assert_cmpuint (n, ==, 0);
  libraries = _srt_emulator_get_required_libraries (emulator);
  g_assert_nonnull (libraries);
  g_assert_null (libraries[0]);

    {
      g_autofree char *content = _srt_emulator_serialize_manifest (emulator);

      g_assert_nonnull (content);
      g_test_message ("serialized: %s", content);
    }

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &f->error);
  g_assert_no_error (f->error);
  write_path = g_build_filename (tmpdir.path, "emu.json", NULL);

  ok = _srt_emulator_write_manifest (emulator, write_path, &f->error);
  g_assert_no_error (f->error);
  g_assert_true (ok);

  reread = _srt_emulator_new_from_manifest (write_path, &f->error);
  g_assert_no_error (f->error);
  g_assert_nonnull (reread);
  dump_emulator ("read back from temp file", reread);
  g_assert_cmpstrv (_srt_emulator_get_argv (emulator),
                    _srt_emulator_get_argv (reread));

  arch_quarks = _srt_emulator_get_emulated_architectures (reread, &n);
  g_assert_cmpuint (n, ==, 3);

  arch_quarks = _srt_emulator_get_required_architectures (reread, &n);
  g_assert_cmpuint (n, ==, 0);

  libraries = _srt_emulator_get_required_libraries (reread);
  g_assert_nonnull (libraries);
  g_assert_null (libraries[0]);
}

static void
test_invalid (Fixture *f,
              gconstpointer context)
{
  static const struct
  {
    const char *label;
    const char *input;
  } tests[] =
  {
    { "not valid JSON",
      "[}\n" },
    { "not an object",
      "[]\n" },
    { "no emulator_v0 group",
      "{}\n" },
    { "no argv",
      ("{ \"emulator_v0\": {\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
    { "argv neither value nor array",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": {},\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
    { "argv value not a string",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": 2.0,\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
    { "empty argv",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
    { "empty argv array",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": [],\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
    { "non-string in argv",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": [\"/bin/true\", null],\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
    { "no emulated archs",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": [\"/bin/true\"]"
       "}}\n") },
    { "empty emulated archs",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": []"
       "}}\n") },
    { "emulated archs not an array",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": true"
       "}}\n") },
    { "invalid emulated arch",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"potato-bird\", \"glados\"]"
       "}}\n") },
    { "non-string emulated arch",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [null]"
       "}}\n") },
    { "non-value emulated arch",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [{}]"
       "}}\n") },
    { "required archs not an array",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_architectures\": true"
       "}}\n") },
    { "invalid required arch",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_architectures\": [\"glados\"]"
       "}}\n") },
    { "non-value required arch",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_architectures\": [{}]"
       "}}\n") },
    { "non-string required arch",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_architectures\": [null]"
       "}}\n") },
    { "non-array required libraries",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_libraries\": true"
       "}}\n") },
    { "non-value required library",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_libraries\": [[]]"
       "}}\n") },
    { "non-string required library",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"/bin/true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"],\n"
       "\"required_libraries\": [null]"
       "}}\n") },
    { "PATH search for argv not allowed at the moment",
      ("{ \"emulator_v0\": {\n"
       "\"argv\": \"true\",\n"
       "\"emulated_architectures\": [\"x86_64-linux-gnu\", \"i386-linux-gnu\"]"
       "}}\n") },
  };
  g_auto(GLnxTmpDir) tmpdir = { .initialized = FALSE };
  g_autoptr(SrtEmulator) emulator = NULL;
  g_autofree char *path = NULL;

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &f->error);
  g_assert_no_error (f->error);
  path = g_build_filename (tmpdir.path, "emu.json", NULL);

  g_test_message ("/nonexistent...");
  emulator = _srt_emulator_new_from_manifest ("/nonexistent", &f->error);
  g_assert_nonnull (f->error);
  g_test_message ("/nonexistent -> %s %d %s",
                  g_quark_to_string (f->error->domain),
                  f->error->code,
                  f->error->message);
  g_assert_null (emulator);
  g_clear_error (&f->error);

  for (size_t i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_test_message ("%s...", tests[i].label);
      g_file_set_contents (path, tests[i].input, -1, &f->error);
      g_assert_no_error (f->error);
      emulator = _srt_emulator_new_from_manifest (path, &f->error);
      g_assert_nonnull (f->error);
      g_test_message ("%s -> %s %d %s",
                      tests[i].label,
                      g_quark_to_string (f->error->domain),
                      f->error->code,
                      f->error->message);
      g_assert_null (emulator);
      g_clear_error (&f->error);
    }
}

/*
 * Exercise a maximal emulator manifest with a relative path
 */
static void
test_relative (Fixture *f,
               gconstpointer context)
{
  g_autoptr(SrtEmulator) emulator = NULL;
  g_autoptr(SrtEmulator) modified = NULL;
  g_autoptr(SrtEmulator) reread = NULL;
  const GQuark *arch_quarks;
  size_t n;
  const char * const expected_args[] = { "--", NULL };
  const char * const expected_libraries[] =
    {
      "libc.so.6",
      "libstdc++.so.6",
      "libm.so.6",
      "libgcc_s.so.1",
      NULL
    };
  g_autofree char *expected_path = NULL;
  g_autofree char *expected_realpath = NULL;
  g_autofree char *actual_realpath = NULL;
  const char * const * argv;
  g_autofree char *manifest_realpath = NULL;
  g_auto(GLnxTmpDir) tmpdir = { .initialized = FALSE };
  g_autofree char *write_path = NULL;
  gboolean ok;

  manifest_realpath = realpath (f->mock_json, NULL);
  emulator = _srt_emulator_new_from_manifest (f->mock_json, &f->error);
  g_assert_no_error (f->error);
  g_assert_nonnull (emulator);
  dump_emulator (f->qemu_json, emulator);

  argv = _srt_emulator_get_argv (emulator);
  g_assert_nonnull (argv);
  g_assert_nonnull (argv[0]);
  g_assert_cmpstrv (&argv[1], expected_args);
  actual_realpath = realpath (argv[0], NULL);
  expected_path = g_test_build_filename (G_TEST_DIST, "mock-emulator.sh", NULL);
  expected_realpath = realpath (expected_path, NULL);
  g_assert_cmpstr (expected_realpath, ==, actual_realpath);

  arch_quarks = _srt_emulator_get_emulated_architectures (emulator, &n);
  g_assert_cmpuint (n, ==, 1);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[0]), ==, "mock-emulated");
  g_assert_cmpuint (arch_quarks[1], ==, SRT_ARCHITECTURE_QUARK_NONE);

  g_assert_cmpstrv (_srt_emulator_get_required_libraries (emulator),
                    expected_libraries);
  g_assert_cmpstr (_srt_emulator_get_manifest (emulator), ==, manifest_realpath);

  arch_quarks = _srt_emulator_get_required_architectures (emulator, &n);
  g_assert_cmpuint (n, ==, 2);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[0]), ==, SRT_ABI_X86_64);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[1]), ==, SRT_ABI_I386);
  g_assert_cmpuint (arch_quarks[2], ==, SRT_ARCHITECTURE_QUARK_NONE);

  modified = _srt_emulator_new_modified (emulator, "/run/host/bin/false");
  dump_emulator ("modified", modified);

  argv = _srt_emulator_get_argv (modified);
  g_assert_nonnull (argv);
  g_assert_nonnull (argv[0]);
  g_assert_cmpstr (argv[0], ==, "/run/host/bin/false");
  g_assert_cmpstrv (&argv[1], expected_args);

  arch_quarks = _srt_emulator_get_emulated_architectures (modified, &n);
  g_assert_cmpuint (n, ==, 1);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[0]), ==, "mock-emulated");
  g_assert_cmpuint (arch_quarks[1], ==, SRT_ARCHITECTURE_QUARK_NONE);

  arch_quarks = _srt_emulator_get_required_architectures (modified, &n);
  g_assert_cmpuint (n, ==, 2);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[0]), ==, SRT_ABI_X86_64);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[1]), ==, SRT_ABI_I386);
  g_assert_cmpuint (arch_quarks[2], ==, SRT_ARCHITECTURE_QUARK_NONE);

  g_assert_cmpstrv (_srt_emulator_get_required_libraries (modified),
                    expected_libraries);
  g_assert_cmpstr (_srt_emulator_get_manifest (modified), ==, NULL);

    {
      g_autofree char *content = _srt_emulator_serialize_manifest (modified);

      g_assert_nonnull (content);
      g_test_message ("serialized: %s", content);
    }

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &f->error);
  g_assert_no_error (f->error);
  write_path = g_build_filename (tmpdir.path, "emu.json", NULL);

  ok = _srt_emulator_write_manifest (modified, write_path, &f->error);
  g_assert_no_error (f->error);
  g_assert_true (ok);

  reread = _srt_emulator_new_from_manifest (write_path, &f->error);
  g_assert_no_error (f->error);
  g_assert_nonnull (reread);
  dump_emulator ("read back from temp file", reread);
  g_assert_cmpstrv (_srt_emulator_get_argv (modified),
                    _srt_emulator_get_argv (reread));

  arch_quarks = _srt_emulator_get_emulated_architectures (reread, &n);
  g_assert_cmpuint (n, ==, 1);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[0]), ==, "mock-emulated");
  g_assert_cmpuint (arch_quarks[1], ==, SRT_ARCHITECTURE_QUARK_NONE);

  arch_quarks = _srt_emulator_get_required_architectures (reread, &n);
  g_assert_cmpuint (n, ==, 2);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[0]), ==, SRT_ABI_X86_64);
  g_assert_cmpstr (g_quark_to_string (arch_quarks[1]), ==, SRT_ABI_I386);
  g_assert_cmpuint (arch_quarks[2], ==, SRT_ARCHITECTURE_QUARK_NONE);

  g_assert_cmpstrv (_srt_emulator_get_required_libraries (reread),
                    expected_libraries);
  g_assert_cmpstr (_srt_emulator_get_manifest (modified), ==, NULL);
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/emulator/basic", Fixture, NULL,
              setup, test_basic, teardown);
  g_test_add ("/emulator/invalid", Fixture, NULL,
              setup, test_invalid, teardown);
  g_test_add ("/emulator/relative", Fixture, NULL,
              setup, test_relative, teardown);

  return g_test_run ();
}
