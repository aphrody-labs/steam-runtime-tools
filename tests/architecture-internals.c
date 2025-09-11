/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <gelf.h>
#include <libelf.h>

#include <glib.h>

#include "test-utils.h"

typedef struct
{
  int unused;
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
dump_strv (const char *title,
           const char * const *items)
{
  size_t i;

  if (items == NULL)
    {
      g_test_message ("\t%s: (none)", title);
      return;
    }

  g_test_message ("\t%s: %zu item(s)", title, _srt_strv_length (items));

  for (i = 0; items[i] != NULL; i++)
    g_test_message ("\t\t\"%s\"", items[i]);

  g_test_message ("\t\t.");
}

static void
dump_architecture (const SrtKnownArchitecture *arch)
{
  const char *description;

  g_test_message ("Known architecture: %s", arch->multiarch_tuple);
  g_test_message ("\tld.so(8): %s",
                  arch->interoperable_runtime_linker ?: "(unknown)");
  g_test_message ("\tIn OpenXR vocabulary: %s",
                  arch->openxr_1_architecture ?: "(none)");

  switch (arch->machine_type)
    {
      case SRT_MACHINE_TYPE_386:
        description = "386";
        break;
      case SRT_MACHINE_TYPE_X86_64:
        description = "X86_64";
        break;
      case SRT_MACHINE_TYPE_AARCH64:
        description = "AARCH64";
        break;
      default:
        description = "(unknown)";
    }

  g_test_message ("\tELF machine type: 0x%x: %s",
                  arch->machine_type, description);

  switch (arch->elf_class)
    {
      case ELFCLASS64:
        description = "64-bit";
        break;
      case ELFCLASS32:
        description = "32-bit";
        break;
      default:
        description = "(unknown)";
    }

  g_test_message ("\tELF class: 0x%x: %s", arch->elf_class, description);

  switch (arch->elf_encoding)
    {
      case ELFDATA2LSB:
        description = "little-endian two's complement";
        break;
      case ELFDATA2MSB:
        description = "big-endian two's complement";
        break;
      default:
        description = "(unknown)";
    }

  g_test_message ("\tELF encoding: 0x%x: %s", arch->elf_encoding, description);

  g_test_message ("\tsizeof(void *): %u bytes", arch->sizeof_pointer);

  g_test_message ("\tSteam gameoverlayrenderer.so directory: %s",
                  arch->steam_gameoverlayrenderer_dir ?: "(unknown/none)");

  dump_strv ("Multilib suffixes", arch->multilib_suffixes);
  dump_strv ("Extra ld.so.cache filenames", arch->extra_ld_so_caches);
  dump_strv ("libdl ${PLATFORM} can expand to",
             arch->libdl_platform_expansions);
}

static void
test_architecture_get_by_tuple (Fixture *f,
                                gconstpointer context)
{
  const SrtKnownArchitecture *x86_64_arch;
  const SrtKnownArchitecture *i386_arch;
  const SrtKnownArchitecture *aarch64_arch;
  const SrtKnownArchitecture *arch;
  size_t i;

  arch = x86_64_arch = _srt_architecture_get_by_tuple (SRT_ABI_X86_64);
  g_assert_nonnull (x86_64_arch);
  dump_architecture (arch);
  g_assert_cmpstr (x86_64_arch->multiarch_tuple, ==, SRT_ABI_X86_64);
  g_assert_cmpstr (x86_64_arch->interoperable_runtime_linker, ==,
                   "/lib64/ld-linux-x86-64.so.2");
  g_assert_cmpstr (x86_64_arch->openxr_1_architecture, ==, "x86_64");

  g_assert_nonnull (arch->multilib_suffixes);

  for (i = 0; arch->multilib_suffixes[i] != NULL; i++)
    g_assert_false (g_path_is_absolute (arch->multilib_suffixes[i]));

  g_assert_true (g_strv_contains (arch->multilib_suffixes, "lib64"));
  g_assert_true (g_strv_contains (arch->multilib_suffixes, "x86_64-pc-linux-gnu/lib"));
  g_assert_false (g_strv_contains (arch->multilib_suffixes, "lib/x86_64-linux-gnu"));
  g_assert_false (g_strv_contains (arch->multilib_suffixes, "lib"));

  g_assert_nonnull (arch->extra_ld_so_caches);

  for (i = 0; arch->extra_ld_so_caches[i] != NULL; i++)
    g_assert_false (g_path_is_absolute (arch->extra_ld_so_caches[i]));

  g_assert_true (g_strv_contains (arch->extra_ld_so_caches,
                                  "ld-x86_64-pc-linux-gnu.cache"));

  g_assert_nonnull (arch->libdl_platform_expansions);
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "xeon_phi"));
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "haswell"));
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "x86_64"));

  g_assert_cmpstr (arch->steam_gameoverlayrenderer_dir, ==, "ubuntu12_64");
  g_assert_cmpint (x86_64_arch->machine_type, ==, SRT_MACHINE_TYPE_X86_64);
  g_assert_cmpint (x86_64_arch->elf_class, ==, ELFCLASS64);
  g_assert_cmpint (x86_64_arch->elf_encoding, ==, ELFDATA2LSB);
  g_assert_cmpint (x86_64_arch->sizeof_pointer, ==, 8);

  arch = i386_arch = _srt_architecture_get_by_tuple (SRT_ABI_I386);
  g_assert_nonnull (i386_arch);
  dump_architecture (arch);
  g_assert_cmpstr (i386_arch->multiarch_tuple, ==, SRT_ABI_I386);
  g_assert_cmpstr (i386_arch->interoperable_runtime_linker, ==,
                   "/lib/ld-linux.so.2");
  g_assert_cmpstr (i386_arch->openxr_1_architecture, ==, "i686");
  g_assert_nonnull (arch->multilib_suffixes);

  for (i = 0; arch->multilib_suffixes[i] != NULL; i++)
    g_assert_false (g_path_is_absolute (arch->multilib_suffixes[i]));

  g_assert_true (g_strv_contains (arch->multilib_suffixes, "lib32"));
  g_assert_true (g_strv_contains (arch->multilib_suffixes, "i686-pc-linux-gnu/lib"));
  g_assert_false (g_strv_contains (arch->multilib_suffixes, "lib/i386-linux-gnu"));
  g_assert_false (g_strv_contains (arch->multilib_suffixes, "lib"));

  g_assert_nonnull (arch->extra_ld_so_caches);

  for (i = 0; arch->extra_ld_so_caches[i] != NULL; i++)
    g_assert_false (g_path_is_absolute (arch->extra_ld_so_caches[i]));

  g_assert_true (g_strv_contains (arch->extra_ld_so_caches,
                                  "ld-i686-pc-linux-gnu.cache"));

  g_assert_nonnull (arch->libdl_platform_expansions);
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "i686"));
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "i586"));
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "i486"));
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "i386"));

  g_assert_cmpstr (arch->steam_gameoverlayrenderer_dir, ==, "ubuntu12_32");
  g_assert_cmpint (i386_arch->machine_type, ==, SRT_MACHINE_TYPE_386);
  g_assert_cmpint (i386_arch->elf_class, ==, ELFCLASS32);
  g_assert_cmpint (i386_arch->elf_encoding, ==, ELFDATA2LSB);
  g_assert_cmpint (i386_arch->sizeof_pointer, ==, 4);

  arch = aarch64_arch = _srt_architecture_get_by_tuple (SRT_ABI_AARCH64);
  g_assert_nonnull (arch);
  dump_architecture (arch);
  g_assert_cmpstr (arch->multiarch_tuple, ==, SRT_ABI_AARCH64);
  g_assert_cmpstr (arch->interoperable_runtime_linker, ==,
                   "/lib/ld-linux-aarch64.so.1");
  g_assert_cmpstr (arch->openxr_1_architecture, ==, "aarch64");
  g_assert_nonnull (arch->multilib_suffixes);

  for (i = 0; arch->multilib_suffixes[i] != NULL; i++)
    g_assert_false (g_path_is_absolute (arch->multilib_suffixes[i]));

  g_assert_true (g_strv_contains (arch->multilib_suffixes, "aarch64-unknown-linux-gnueabi/lib"));
  g_assert_false (g_strv_contains (arch->multilib_suffixes, "lib/aarch64-linux-gnu"));
  g_assert_false (g_strv_contains (arch->multilib_suffixes, "lib"));

  g_assert_nonnull (arch->extra_ld_so_caches);

  for (i = 0; arch->extra_ld_so_caches[i] != NULL; i++)
    g_assert_false (g_path_is_absolute (arch->extra_ld_so_caches[i]));

  g_assert_true (g_strv_contains (arch->extra_ld_so_caches,
                                  "ld-aarch64-unknown-linux-gnueabi.cache"));

  g_assert_nonnull (arch->libdl_platform_expansions);
  g_assert_true (g_strv_contains (arch->libdl_platform_expansions, "aarch64"));

  g_assert_cmpstr (arch->steam_gameoverlayrenderer_dir, ==, NULL);
  g_assert_cmpint (arch->machine_type, ==, SRT_MACHINE_TYPE_AARCH64);
  g_assert_cmpint (arch->elf_class, ==, ELFCLASS64);
  g_assert_cmpint (arch->elf_encoding, ==, ELFDATA2LSB);
  g_assert_cmpint (arch->sizeof_pointer, ==, 8);

  /* Used in unit tests */
  arch = _srt_architecture_get_by_tuple ("x86_64-mock-abi");
  g_assert_nonnull (arch);
  dump_architecture (arch);
  g_assert_cmpstr (arch->multiarch_tuple, ==, "x86_64-mock-abi");
  g_assert_cmpstr (arch->interoperable_runtime_linker, ==, NULL);
  g_assert_cmpstr (arch->openxr_1_architecture, ==, NULL);
  g_assert_null (arch->multilib_suffixes);
  g_assert_null (arch->extra_ld_so_caches);
  g_assert_null (arch->libdl_platform_expansions);
  g_assert_cmpstr (arch->steam_gameoverlayrenderer_dir, ==, NULL);
  g_assert_cmpint (arch->machine_type, ==, SRT_MACHINE_TYPE_UNKNOWN);
  g_assert_cmpint (arch->elf_class, ==, ELFCLASSNONE);
  g_assert_cmpint (arch->sizeof_pointer, ==, 8);

#ifdef _SRT_MULTIARCH
  arch = _srt_architecture_get_by_tuple (_SRT_MULTIARCH);
  g_assert_nonnull (arch);
#if defined(__x86_64__) && defined(__LP64__)
  g_assert_true (arch == x86_64_arch);
#elif defined(__i386__)
  g_assert_true (arch == i386_arch);
#elif defined(__aarch64__)
  g_assert_true (arch == aarch64_arch);
#endif
  g_assert_cmpstr (arch->multiarch_tuple, ==, _SRT_MULTIARCH);
  g_assert_cmpint (arch->sizeof_pointer, ==, sizeof (void *));
#endif

  /* An architecture we have no knowledge of isn't found */
  arch = _srt_architecture_get_by_tuple ("potato-glados-eabi");
  g_assert_null (arch);
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/architecture/get_by_tuple", Fixture, NULL,
              setup, test_architecture_get_by_tuple, teardown);

  return g_test_run ();
}
