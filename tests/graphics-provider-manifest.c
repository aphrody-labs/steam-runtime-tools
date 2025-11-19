/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/graphics-provider-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/graphics-provider-private.h"
#include "steam-runtime-tools/utils-internal.h"
#include "test-utils.h"

#include <stdint.h>

typedef struct
{
  GError *error;
} Fixture;

typedef struct
{
  int unused;
} Config;

typedef enum
{
  AUTO_FROM_DIRECTORY = 0,
  AUTO_FROM_FILE,
  FROM_DIRECTORY,
  FROM_FILE,
  N_PARSING_TESTS
} ParsingTest;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_error (&f->error);
}

static void
append_flag (GString *str,
             const char *addition)
{
  if (str->len != 0)
    g_string_append (str, " | ");

  g_string_append (str, addition);
}

static void
dump_feature_flags (const char *indent,
                    SrtGraphicsProviderFeatureFlags features)
{
  g_autoptr(GString) str = g_string_new ("");

  if (features & SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VA_API)
    append_flag (str, "VA_API");

  if (features & SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VDPAU)
    append_flag (str, "VDPAU");

  if (str->len == 0)
    g_string_append (str, "NONE");

  g_test_message ("%sfeature flags: %s", indent, str->str);
}

static void
dump_architecture (const SrtGraphicsProviderArchitecture *arch)
{
  GQuark q;

  q = _srt_graphics_provider_architecture_get_tuple (arch);
  g_test_message ("\tarchitecture: %s", g_quark_to_string (q));

  dump_feature_flags ("\t\t", _srt_graphics_provider_architecture_get_features (arch));
}

static void
dump_graphics_provider (SrtGraphicsProvider *provider)
{
  SrtSysroot *root = _srt_graphics_provider_get_root (provider);
  size_t n = 0;

  g_test_message ("graphics provider: %s",
                  _srt_graphics_provider_describe (provider));

  g_test_message ("\tmanifest: %s",
                  _srt_graphics_provider_get_manifest_path (provider) ?: "(none)");

  g_return_if_fail (SRT_IS_SYSROOT (root));
  g_test_message ("\troot: %s", root->path);

  dump_feature_flags ("\t", _srt_graphics_provider_get_features (provider));

  _srt_graphics_provider_get_architectures (provider, &n);

  for (size_t i = 0; i < n; i++)
    {
      const SrtGraphicsProviderArchitecture *arch;

      arch = _srt_graphics_provider_get_nth_architecture (provider, i);
      dump_architecture (arch);
    }
}

static void
assert_arch_equivalent (const SrtGraphicsProviderArchitecture *a,
                        const SrtGraphicsProviderArchitecture *b)
{
  g_assert_cmpstr (g_quark_to_string (_srt_graphics_provider_architecture_get_tuple (a)),
                   ==,
                   g_quark_to_string (_srt_graphics_provider_architecture_get_tuple (b)));
  g_assert_cmpuint (_srt_graphics_provider_architecture_get_features (a),
                    ==,
                    _srt_graphics_provider_architecture_get_features (b));
}

/*
 * Exercise a nearly-minimal graphics provider manifest representing the root.
 */
static void
test_basic (Fixture *f,
            gconstpointer context)
{
  g_autoptr(SrtSysroot) root_direct = NULL;
  g_autofree char *json = NULL;
  GQuark glados = g_quark_from_static_string ("aperture-glados-mockup");
  GQuark i386 = g_quark_from_static_string (SRT_ABI_I386);

  json = g_test_build_filename (G_TEST_DIST, "gfx-provider-root.json", NULL);
  root_direct = _srt_sysroot_new_direct (&f->error);
  g_assert_no_error (f->error);

  for (ParsingTest mode = 0; mode < N_PARSING_TESTS; mode++)
    {
      g_autoptr(GArray) archs_copy = NULL;
      g_autoptr(SrtGraphicsProvider) provider = NULL;
      g_autoptr(SrtSysroot) root_copy = NULL;
      g_autofree char *manifest_copy = NULL;
      SrtSysroot *root;
      SrtGraphicsProviderFeatureFlags features;
      const SrtGraphicsProviderArchitecture *arch;
      const SrtGraphicsProviderArchitecture *arch_copy;
      size_t n = 0;
      const char *s;
      const GQuark *quarks;
      GQuark q;

      switch (mode)
        {
          case AUTO_FROM_DIRECTORY:
            g_test_message ("From directory (automatic)");
            provider = _srt_graphics_provider_new_for_path ("/", &glados, 1,
                                                            &f->error);
            break;

          case AUTO_FROM_FILE:
            g_test_message ("From JSON file (automatic)");
            provider = _srt_graphics_provider_new_for_path (json,
                                                            /* should be ignored */
                                                            &i386, 1,
                                                            &f->error);
            break;

          case FROM_DIRECTORY:
            g_test_message ("From directory (explicitly)");
            provider = _srt_graphics_provider_new_for_directory (root_direct,
                                                                 &glados, 1);
            break;

          case FROM_FILE:
            g_test_message ("From JSON file (explicitly)");
            provider = _srt_graphics_provider_new_from_manifest (json, &f->error);
            break;

          case N_PARSING_TESTS:
          default:
            g_assert_not_reached ();
        }

      g_assert_no_error (f->error);
      g_assert_nonnull (provider);
      g_assert_nonnull (_srt_graphics_provider_describe (provider));
      dump_graphics_provider (provider);

      switch (mode)
        {
          case AUTO_FROM_DIRECTORY:
          case FROM_DIRECTORY:
            g_assert_cmpstr (_srt_graphics_provider_get_manifest_path (provider),
                             ==, NULL);
            break;

          case AUTO_FROM_FILE:
          case FROM_FILE:
              {
                g_autofree char *expected = NULL;

                expected = realpath (json, NULL);
                g_assert_cmpstr (_srt_graphics_provider_get_manifest_path (provider),
                                 ==, expected);
              }
            break;

          case N_PARSING_TESTS:
          default:
            g_assert_not_reached ();
        }

      root = _srt_graphics_provider_get_root (provider);
      g_assert_nonnull (root);
      g_assert_cmpstr (root->path, ==, "/");
      g_assert_true (_srt_sysroot_is_direct (root));

      s = _srt_graphics_provider_get_root_path (provider);
      g_assert_cmpstr (s, ==, root->path);

      features = _srt_graphics_provider_get_features (provider);
      g_assert_cmpuint (features,
                        ==,
                        SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_DEFAULT);

      quarks = _srt_graphics_provider_get_architectures (provider, &n);
      g_assert_cmpuint (n, ==, 1);
      g_assert_cmpstr (g_quark_to_string (quarks[0]),
                       ==, g_quark_to_string (glados));
      g_assert_cmpuint (quarks[1], ==, 0);

      quarks = _srt_graphics_provider_get_architectures (provider, NULL);
      g_assert_cmpstr (g_quark_to_string (quarks[0]),
                       ==, g_quark_to_string (glados));
      g_assert_cmpuint (quarks[1], ==, 0);

      arch = _srt_graphics_provider_get_nth_architecture (provider, 0);
      q = _srt_graphics_provider_architecture_get_tuple (arch);
      g_assert_cmpstr (g_quark_to_string (q),
                       ==, g_quark_to_string (glados));
      features = _srt_graphics_provider_architecture_get_features (arch);
      g_assert_cmpuint (features,
                        ==,
                        SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_DEFAULT);

      g_object_get (provider,
                    "architectures", &archs_copy,
                    "features", &features,
                    "manifest", &manifest_copy,
                    "root", &root_copy,
                    NULL);
      g_assert_true (root == root_copy);
      g_assert_cmpuint (features,
                        ==,
                        SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_DEFAULT);
      g_assert_cmpstr (_srt_graphics_provider_get_manifest_path (provider),
                       ==, manifest_copy);

      g_assert_cmpuint (archs_copy->len, ==, 1);

      arch_copy = &g_array_index (archs_copy, const SrtGraphicsProviderArchitecture, 0);
      /* It's a copy, to avoid concurrent modification */
      g_assert_true (arch != arch_copy);
      assert_arch_equivalent (arch, arch_copy);
    }
}

/*
 * Exercise a completely minimal graphics provider manifest.
 */
static void
test_default_root (Fixture *f,
                   gconstpointer context)
{
  g_autofree char *json = NULL;
  g_autofree char *expected = NULL;
  g_autofree char *expected_root = NULL;
  g_autoptr(SrtGraphicsProvider) provider = NULL;
  SrtSysroot *root;

  json = g_test_build_filename (G_TEST_DIST, "gfx-provider-minimal.json", NULL);
  provider = _srt_graphics_provider_new_from_manifest (json, &f->error);

  g_assert_no_error (f->error);
  g_assert_nonnull (provider);
  g_assert_nonnull (_srt_graphics_provider_describe (provider));
  dump_graphics_provider (provider);

  expected = realpath (json, NULL);
  g_assert_cmpstr (_srt_graphics_provider_get_manifest_path (provider),
                   ==, expected);

  expected_root = g_path_get_dirname (expected);
  g_assert_cmpstr (_srt_graphics_provider_get_root_path (provider),
                   ==, expected_root);

  root = _srt_graphics_provider_get_root (provider);
  g_assert_nonnull (root);
  g_assert_cmpstr (root->path, ==, expected_root);
  g_assert_false (_srt_sysroot_is_direct (root));

  /* Everything else is the same as in test_basic(), no need to test again */
}

/*
 * Exercise a maximal graphics provider manifest with a relative path
 */
static void
test_full (Fixture *f,
           gconstpointer context)
{
  g_autofree char *json = NULL;
  GQuark x86_64_quark, i386_quark;

  json = g_test_build_filename (G_TEST_DIST, "gfx-provider-full.json", NULL);
  x86_64_quark = g_quark_from_static_string (SRT_ABI_X86_64);
  i386_quark = g_quark_from_static_string (SRT_ABI_I386);

  for (ParsingTest mode = 0; mode < N_PARSING_TESTS; mode++)
    {
      g_autoptr(GArray) archs_copy = NULL;
      g_autoptr(SrtGraphicsProvider) provider = NULL;
      g_autoptr(SrtSysroot) root_copy = NULL;
      g_autofree char *manifest_copy = NULL;
      SrtSysroot *root;
      SrtGraphicsProviderFeatureFlags features;
      const SrtGraphicsProviderArchitecture *arch;
      const SrtGraphicsProviderArchitecture *i386;
      const SrtGraphicsProviderArchitecture *x86_64;
      size_t n = 0;
      size_t x86_64_index = SIZE_MAX;
      size_t i386_index = SIZE_MAX;
      const char *s;
      const GQuark *quarks;
      GQuark q;

      switch (mode)
        {
          case AUTO_FROM_DIRECTORY:
            g_test_message ("From directory (automatic)");
            provider = _srt_graphics_provider_new_for_path (g_test_get_dir (G_TEST_DIST),
                                                            &i386_quark, 1,
                                                            &f->error);
            break;

          case AUTO_FROM_FILE:
            g_test_message ("From JSON file (automatic)");
            provider = _srt_graphics_provider_new_for_path (json,
                                                            /* should be ignored */
                                                            &i386_quark, 1,
                                                            &f->error);
            break;

          case FROM_DIRECTORY:
            g_test_message ("From directory (explicitly)");
              {
                g_autoptr(SrtSysroot) dir = _srt_sysroot_new (g_test_get_dir (G_TEST_DIST),
                                                              &f->error);

                g_assert_no_error (f->error);
                provider = _srt_graphics_provider_new_for_directory (dir,
                                                                     &i386_quark, 1);
              }
            break;

          case FROM_FILE:
            g_test_message ("From JSON file (explicitly)");
            provider = _srt_graphics_provider_new_from_manifest (json, &f->error);
            break;

          case N_PARSING_TESTS:
          default:
            g_assert_not_reached ();
        }

      g_assert_no_error (f->error);
      g_assert_nonnull (provider);
      g_assert_nonnull (_srt_graphics_provider_describe (provider));
      dump_graphics_provider (provider);

      root = _srt_graphics_provider_get_root (provider);
      g_assert_nonnull (root);
      g_assert_false (_srt_sysroot_is_direct (root));

        {
          g_autofree char *expected = NULL;
          g_autofree char *actual = NULL;

          expected = realpath (g_test_get_dir (G_TEST_DIST), NULL);
          g_assert_no_errno (expected == NULL ? -1 : 0);
          actual = realpath (root->path, NULL);
          g_assert_no_errno (actual == NULL ? -1 : 0);
          g_assert_cmpstr (expected, ==, actual);
        }

      s = _srt_graphics_provider_get_root_path (provider);
      g_assert_cmpstr (s, ==, root->path);

      switch (mode)
        {
          case AUTO_FROM_DIRECTORY:
          case FROM_DIRECTORY:
            /* We didn't actually read the manifest, so we have
             * some defaults. */
            features = _srt_graphics_provider_get_features (provider);
            g_assert_cmpuint (features,
                              ==,
                              SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_DEFAULT);

            /* We told the constructor to assume that only i386 is supported */
            quarks = _srt_graphics_provider_get_architectures (provider, &n);
            g_assert_cmpuint (n, ==, 1);
            g_assert_cmpstr (g_quark_to_string (quarks[0]), ==, SRT_ABI_I386);
            g_assert_cmpuint (quarks[1], ==, 0);
            continue;

          case AUTO_FROM_FILE:
          case FROM_FILE:
            /* We can test in greater detail */
            break;

          case N_PARSING_TESTS:
          default:
            g_assert_not_reached ();
        }

      features = _srt_graphics_provider_get_features (provider);
      g_assert_cmpuint (features,
                        ==,
                        SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VDPAU);

      quarks = _srt_graphics_provider_get_architectures (provider, &n);
      g_assert_cmpuint (n, ==, 2);

      for (size_t i = 0; i < n; i++)
        {
          if (quarks[i] == x86_64_quark)
            x86_64_index = i;
          else if (quarks[i] == i386_quark)
            i386_index = i;
          else
            g_return_if_reached ();
        }

      g_assert_cmpuint (x86_64_index, <, 2);
      g_assert_cmpuint (i386_index, <, 2);
      g_assert_cmpuint (quarks[2], ==, 0);

      quarks = _srt_graphics_provider_get_architectures (provider, NULL);
      g_assert_cmpuint (quarks[x86_64_index], ==, x86_64_quark);
      g_assert_cmpuint (quarks[i386_index], ==, i386_quark);
      g_assert_cmpuint (quarks[2], ==, 0);

      x86_64 = _srt_graphics_provider_get_architecture (provider, x86_64_quark);
      g_assert_true (_srt_graphics_provider_get_nth_architecture (provider, x86_64_index)
                     == x86_64);
      q = _srt_graphics_provider_architecture_get_tuple (x86_64);
      g_assert_cmpstr (g_quark_to_string (q), ==, SRT_ABI_X86_64);
      /* In the example file, x86_64 explicitly turns on VA-API */
      features = _srt_graphics_provider_architecture_get_features (x86_64);
      g_assert_cmpuint (features,
                        ==,
                        (SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VA_API
                         | SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VDPAU));

      i386 = _srt_graphics_provider_get_nth_architecture (provider, i386_index);
      g_assert_true (_srt_graphics_provider_get_architecture (provider, i386_quark)
                     == i386);
      q = _srt_graphics_provider_architecture_get_tuple (i386);
      g_assert_cmpstr (g_quark_to_string (q), ==, SRT_ABI_I386);
      /* In the example file, i386 explicitly turns off VA-API */
      features = _srt_graphics_provider_architecture_get_features (i386);
      g_assert_cmpuint (features, ==, SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_NONE);

      g_object_get (provider,
                    "architectures", &archs_copy,
                    "features", &features,
                    "manifest", &manifest_copy,
                    "root", &root_copy,
                    NULL);
      g_assert_true (root == root_copy);
      g_assert_cmpuint (features,
                        ==,
                        SRT_GRAPHICS_PROVIDER_FEATURE_FLAGS_VDPAU);
      g_assert_cmpstr (_srt_graphics_provider_get_manifest_path (provider),
                       ==, manifest_copy);

      arch = &g_array_index (archs_copy,
                             const SrtGraphicsProviderArchitecture,
                             x86_64_index);
      g_assert_true (arch != x86_64);
      assert_arch_equivalent (arch, x86_64);

      arch = &g_array_index (archs_copy,
                             const SrtGraphicsProviderArchitecture,
                             i386_index);
      g_assert_true (arch != i386);
      assert_arch_equivalent (arch, i386);
    }
}

/*
 * Exercise parsing invalid graphics provider manifests.
 */
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
    { "no graphics_provider_* group",
      "{}\n" },
    { "no graphics_provider_v0 group",
      "{\"graphics_provider_v1\": {\"architectures\": [\"i386-linux-gnu\"]}}\n" },
    { "no architectures",
      "{\"graphics_provider_v0\": {}}\n" },
    { "architectures not a valid type",
      "{\"graphics_provider_v0\": {\"architectures\": true}}\n" },
    { "non-value architectures in array",
      "{\"graphics_provider_v0\": {\"architectures\": [\"i386-linux-gnu\", {}]}}\n" },
    { "non-string architectures in array",
      "{\"graphics_provider_v0\": {\"architectures\": [\"i386-linux-gnu\", 386]}}\n" },
    { "invalid architecture in array",
      "{\"graphics_provider_v0\": {\"architectures\": [\"nope\"]}}\n" },
    { "empty architectures array",
      "{\"graphics_provider_v0\": {\"architectures\": []}}\n" },
    { "empty architectures object",
      "{\"graphics_provider_v0\": {\"architectures\": {}}}\n" },
    { "invalid architecture in object",
      "{\"graphics_provider_v0\": {\"architectures\": {\"nope\": {}}}}\n" },
    { "non-object architectures value",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": {\n"
      "    \"x86_64-linux-gnu\": {},\n"
      "    \"i386-linux-gnu\": []\n"
      "  }\n"
      "}}\n" },
    { "non-object architectures value",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": {\n"
      "    \"x86_64-linux-gnu\": {},\n"
      "    \"i386-linux-gnu\": false\n"
      "  }\n"
      "}}\n" },
    { "non-string root",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": [\"i386-linux-gnu\"],\n"
      "  \"root\": []\n"
      "}}\n" },
    { "cannot open root",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": [\"i386-linux-gnu\"],\n"
      "  \"root\": \"/nonexistent\"\n"
      "}}\n" },
    { "non-boolean va_api",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": [\"i386-linux-gnu\"],\n"
      "  \"va_api\": 1\n"
      "}}\n" },
    { "non-boolean vdpau",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": [\"i386-linux-gnu\"],\n"
      "  \"vdpau\": \"yes\"\n"
      "}}\n" },
    { "non-boolean va_api in architecture",
      "{\"graphics_provider_v0\": {\n"
      "  \"architectures\": {\n"
      "    \"i386-linux-gnu\": {\n"
      "      \"va_api\": []\n"
      "    }\n"
      "  }\n"
      "}}\n" },
  };
  g_auto(GLnxTmpDir) tmpdir = { .initialized = FALSE };
  g_autofree char *path = NULL;
  GQuark glados = g_quark_from_static_string ("aperture-glados-mockup");

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &f->error);
  g_assert_no_error (f->error);
  path = g_build_filename (tmpdir.path, "gfx.json", NULL);

  for (ParsingTest mode = 0; mode < N_PARSING_TESTS; mode++)
    {
      g_autoptr(SrtGraphicsProvider) provider = NULL;

      g_test_message ("/nonexistent...");

      switch (mode)
        {
          case AUTO_FROM_DIRECTORY:
          case AUTO_FROM_FILE:
            g_test_message ("From nonexistent location of unspecified type");
            provider = _srt_graphics_provider_new_for_path ("/nonexistent",
                                                            /* should be ignored */
                                                            &glados, 1,
                                                            &f->error);
            break;

          case FROM_DIRECTORY:
            /* Not applicable: by design we can't create a SrtSysroot
             * for a directory that doesn't exist */
            continue;

          case FROM_FILE:
            g_test_message ("From nonexistent file, explicitly");
            provider = _srt_graphics_provider_new_from_manifest ("/nonexistent",
                                                                 &f->error);
            break;

          case N_PARSING_TESTS:
          default:
            g_assert_not_reached ();
        }

      g_assert_nonnull (f->error);
      g_test_message ("/nonexistent -> %s %d %s",
                      g_quark_to_string (f->error->domain),
                      f->error->code,
                      f->error->message);
      g_assert_null (provider);
      g_clear_error (&f->error);
    }

  for (size_t i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_autoptr(SrtGraphicsProvider) provider = NULL;

      g_test_message ("%s...", tests[i].label);
      g_file_set_contents (path, tests[i].input, -1, &f->error);
      g_assert_no_error (f->error);
      provider = _srt_graphics_provider_new_from_manifest (path, &f->error);
      g_assert_nonnull (f->error);
      g_test_message ("%s -> %s %d %s",
                      tests[i].label,
                      g_quark_to_string (f->error->domain),
                      f->error->code,
                      f->error->message);
      g_assert_null (provider);
      g_clear_error (&f->error);
    }
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/graphics-provider/basic", Fixture, NULL,
              setup, test_basic, teardown);
  g_test_add ("/graphics-provider/default-root", Fixture, NULL,
              setup, test_default_root, teardown);
  g_test_add ("/graphics-provider/full", Fixture, NULL,
              setup, test_full, teardown);
  g_test_add ("/graphics-provider/invalid", Fixture, NULL,
              setup, test_invalid, teardown);

  return g_test_run ();
}
