/*
 * Copyright © 2019-2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <gelf.h>
#include <libelf.h>

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/elf-utils-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <glib-object.h>

/**
 * SECTION:architecture
 * @title: Architectures
 * @short_description: CPU architectures and ABIs
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * On a typical x86 PC, it might be possible to run 32-bit and/or 64-bit
 * executables, depending on the capabilities of the CPU, OS kernel and
 * operating system.
 */

G_DEFINE_QUARK (srt-architecture-error-quark, srt_architecture_error)

/* Backport the ARM 64 bit definition to support older toolchains */
#ifndef EM_AARCH64
#define EM_AARCH64	183	/* ARM AARCH64 */
#endif

#define SRT_ABI_X32 "x86_64-linux-gnux32"

G_STATIC_ASSERT (SRT_MACHINE_TYPE_UNKNOWN == EM_NONE);
G_STATIC_ASSERT (SRT_MACHINE_TYPE_386 == EM_386);
G_STATIC_ASSERT (SRT_MACHINE_TYPE_X86_64 == EM_X86_64);
G_STATIC_ASSERT (SRT_MACHINE_TYPE_AARCH64 == EM_AARCH64);

static const SrtKnownArchitecture known_architectures[] =
{
    {
      .multiarch_tuple = SRT_ABI_X86_64,
      .debian_architecture = "amd64",
      .interoperable_runtime_linker = "/lib64/ld-linux-x86-64.so.2",
      .openxr_1_architecture = "x86_64",
      .multilib_suffixes = ((const char * const[]){
          "x86_64-pc-linux-gnu/lib",
          "lib64",
          NULL
      }),
      .extra_ld_so_architectures = ((const char * const[]){
          SRT_ABI_I386,
          SRT_ABI_X32,
          NULL
      }),
      .extra_ld_so_caches = ((const char * const[]){
          "ld-x86_64-pc-linux-gnu.cache",
          NULL
      }),
      .libdl_platform_expansions = ((const char * const[]){
          /* Reference: sysdeps/x86/cpu-features.c and
           * sysdeps/x86/dl-procinfo.c in glibc */
          "xeon_phi",
          "haswell",
          "x86_64",
          NULL
      }),
      .aliases = ((const char * const[]){
          "x64",
          "x86-64",
          NULL
      }),
      .steam_gameoverlayrenderer_dir = "ubuntu12_64",
      .machine_type = EM_X86_64,
      .elf_class = ELFCLASS64,
      .elf_encoding = ELFDATA2LSB,
      .sizeof_pointer = 8,
    },

    {
      .multiarch_tuple = SRT_ABI_I386,
      .debian_architecture = "i386",
      .interoperable_runtime_linker = "/lib/ld-linux.so.2",
      .openxr_1_architecture = "i686",
      .multilib_suffixes = ((const char * const[]){
          "i686-pc-linux-gnu/lib",
          "lib32",
          NULL
      }),
      .extra_ld_so_architectures = ((const char * const[]){
          SRT_ABI_X86_64,
          SRT_ABI_X32,
          NULL
      }),
      .extra_ld_so_caches = ((const char * const[]){
          "ld-i686-pc-linux-gnu.cache",
          NULL
      }),
      .libdl_platform_expansions = ((const char * const[]){
          /* Reference: sysdeps/x86/cpu-features.c and
           * sysdeps/x86/dl-procinfo.c in glibc */
          "i686",
          "i586",
          "i486",
          "i386",
          NULL
      }),
      .aliases = ((const char * const[]){
          "x86",
          "i686-linux-gnu",
          "i586-linux-gnu",
          "i486-linux-gnu",
          NULL
      }),
      .steam_gameoverlayrenderer_dir = "ubuntu12_32",
      .machine_type = EM_386,
      .elf_class = ELFCLASS32,
      .elf_encoding = ELFDATA2LSB,
      .sizeof_pointer = 4,
    },

    {
      .multiarch_tuple = SRT_ABI_X32,
      .debian_architecture = "x32",
      .interoperable_runtime_linker = "/libx32/ld-linux-x32.so.2",
      .openxr_1_architecture = "x32",
      .multilib_suffixes = ((const char * const[]){ "libx32", NULL }),
      .extra_ld_so_architectures = ((const char * const[]){
          SRT_ABI_X86_64,
          SRT_ABI_I386,
          NULL
      }),
      .extra_ld_so_caches = ((const char * const[]){
          "ld-i686-pc-linux-gnu.cache",
          NULL
      }),
      .machine_type = EM_X86_64,
      .elf_class = ELFCLASS32,
      .elf_encoding = ELFDATA2LSB,
      .sizeof_pointer = 4,
    },

    {
      .multiarch_tuple = SRT_ABI_AARCH64,
      .debian_architecture = "arm64",
      .interoperable_runtime_linker = "/lib/ld-linux-aarch64.so.1",
      .openxr_1_architecture = "aarch64",
      .multilib_suffixes = ((const char * const[]){
          "aarch64-unknown-linux-gnueabi/lib",
          NULL
      }),
      /* Technically an aarch64 ldconfig(8) can also enumerate and cache
       * 32-bit ARM libraries (arm-linux-gnu OABI, arm-linux-gnueabi
       * and arm-linux-gnueabihf) but we don't expect this to ever
       * matter for the Steam Runtime. */
      .extra_ld_so_caches = ((const char * const[]){
          "ld-aarch64-unknown-linux-gnueabi.cache",
          NULL
      }),
      .libdl_platform_expansions = ((const char * const[]){ "aarch64", NULL }),
      .machine_type = EM_AARCH64,
      .elf_class = ELFCLASS64,
      .elf_encoding = ELFDATA2LSB,
      .sizeof_pointer = 8,
    },

    { NULL }
};

/*
 * _srt_architecture_init_known:
 *
 * Ensure that each known architecture is cached as an interned string
 * (a #GQuark).
 */
void
_srt_architecture_init_known (void)
{
  /* Note that this cannot be a library constructor: GQuark functions are
   * documented with "must not be used before library constructors have
   * finished running". It's OK to call from class_init or similar
   * places, though. */
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (known_architectures); i++)
    (void) g_quark_from_static_string (known_architectures[i].multiarch_tuple);

#ifdef _SRT_MULTIARCH
  (void) g_quark_from_static_string (_SRT_MULTIARCH);
#endif
}

/*
 * Carry out basic validation of a multiarch tuple.
 * This intentionally does not detect every invalid multiarch tuple
 * in order to avoid having to hard-code a table of supported architectures:
 * it only detects tuples that are syntactically invalid (might cause
 * problematic filenames, etc.) or are an obvious mistake.
 *
 * Returns: %TRUE if it is plausible that @tuple might be a valid
 *  multiarch tuple
 */
gboolean
_srt_architecture_check_plausible_tuple (const char *tuple,
                                         GError **error)
{
  gsize i;
  gboolean had_dash = FALSE;

  g_return_val_if_fail (tuple != NULL, FALSE);

  if (tuple[0] == '-')
    return glnx_throw (error, "Multiarch tuples cannot start with a dash");

  for (i = 0; tuple[i] != '\0'; i++)
    {
      if (tuple[i] == '-')
        had_dash = TRUE;
      else if (!g_ascii_isalnum (tuple[i]) && tuple[i] != '_')
        return glnx_throw (error,
                           "Multiarch tuples contain only letters, "
                           "digits, dash and underscore");

      if (tuple[i] >= 'A' && tuple[i] <= 'Z')
        return glnx_throw (error, "Multiarch tuples must be lower-case");
    }

  /* Anything that is supportable by Steam has two dashes (foo-linux-gnu)
   * but for completness let's assume that i386-gnu could conceivably be
   * relevant somehow */
  if (!had_dash)
    return glnx_throw (error, "Multiarch tuples must contain a dash");

  /* Catch some likely mistakes */
  if (g_str_has_prefix (tuple, "i486-")
      || g_str_has_prefix (tuple, "i586-")
      || g_str_has_prefix (tuple, "i686-"))
    return glnx_throw (error, "Multiarch tuple for 32-bit x86 is " SRT_ABI_I386);

  if (g_str_has_prefix (tuple, "amd64-"))
    return glnx_throw (error, "Multiarch tuple for AMD64 is " SRT_ABI_X86_64);

  if (g_str_has_prefix (tuple, "arm64-"))
    return glnx_throw (error, "Multiarch tuple for ARM64 is " SRT_ABI_AARCH64);

  return TRUE;
}

/*
 * Returns: A table of known architectures, terminated by one
 *  with @multiarch_tuple set to %NULL.
 */
const SrtKnownArchitecture *
_srt_architecture_get_known (void)
{
  return &known_architectures[0];
}

gboolean
_srt_architecture_can_run (SrtSubprocessRunner *runner,
                           GQuark arch_quark)
{
  g_autoptr(SrtCompletedSubprocess) completed = NULL;
  GPtrArray *argv = NULL;
  GError *error = NULL;
  gboolean ret = FALSE;
  SrtHelperFlags helper_flags = SRT_HELPER_FLAGS_NONE;
  const char *multiarch;

  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (runner), FALSE);
  g_return_val_if_fail (arch_quark != SRT_ARCHITECTURE_QUARK_NONE, FALSE);
  g_return_val_if_fail (_srt_check_not_setuid (), FALSE);

  multiarch = g_quark_to_string (arch_quark);
  argv = _srt_subprocess_runner_get_helper (runner, multiarch, "true",
                                            helper_flags, &error);

  if (argv == NULL)
    {
      g_debug ("%s", error->message);
      goto out;
    }

  g_ptr_array_add (argv, NULL);

  completed = _srt_subprocess_runner_run_sync (runner,
                                               helper_flags,
                                               arch_quark,
                                               (const char * const *) argv->pdata,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               &error);

  if (completed == NULL || !_srt_completed_subprocess_check (completed, &error))
    {
      g_debug ("... %s", error->message);
      goto out;
    }

  g_debug ("... it works");
  ret = TRUE;
out:
  g_clear_error (&error);
  g_clear_pointer (&argv, g_ptr_array_unref);
  return ret;
}

/**
 * srt_architecture_can_run_i386:
 *
 * Check whether we can run an i386 (%SRT_ABI_I386) executable.
 *
 * For this check to work as intended, the contents of the
 * `libsteam-runtime-tools-0-helpers:i386` package must be available
 * in the same directory hierarchy as the `libsteam-runtime-tools-0`
 * shared library, something like this:
 *
 * |[
 * any directory/
 *      lib/
 *          x86_64-linux-gnu/
 *              libsteam-runtime-tools-0.so.0
 *      libexec/
 *          steam-runtime-tools-0/
 *              i386-linux-gnu-*
 *              x86_64-linux-gnu-*
 * ]|
 *
 * Returns: %TRUE if we can run an i386 executable.
 */
gboolean
srt_architecture_can_run_i386 (void)
{
  g_autoptr(SrtSubprocessRunner) runner = _srt_subprocess_runner_new ();

  return _srt_architecture_can_run (runner, g_quark_from_static_string (SRT_ABI_I386));
}

/**
 * srt_architecture_can_run_x86_64:
 *
 * Check whether we can run an x86_64 (%SRT_ABI_X86_64) executable.
 *
 * For this check to work as intended, the contents of the
 * `libsteam-runtime-tools-0-helpers:amd64` package must be available
 * in the same directory hierarchy as the `libsteam-runtime-tools-0`
 * shared library. See srt_architecture_can_run_i386() for details.
 *
 * Returns: %TRUE if we can run an x86_64 executable.
 */
gboolean
srt_architecture_can_run_x86_64 (void)
{
  g_autoptr(SrtSubprocessRunner) runner = _srt_subprocess_runner_new ();

  return _srt_architecture_can_run (runner, g_quark_from_static_string (SRT_ABI_X86_64));
}

/*
 * _srt_architecture_ldconfig_knows_architecture:
 * @maybe_known: (nullable): A #SrtKnownArchitecture or %NULL
 * @tuple: An architecture
 *
 * If a ldconfig(8) provided by architecture @maybe_known would write
 * architectures from @tuple into `/etc/ld.so.cache`, return %TRUE.
 *
 * If @maybe_known is %NULL, pessimistically return %FALSE.
 *
 * Otherwise return %FALSE.
 *
 * Returns: %TRUE if @tuple libraries will appear in `ld.so.cache`
 */
gboolean
_srt_architecture_ldconfig_knows_architecture (const SrtKnownArchitecture *maybe_known,
                                               const char *tuple)
{
  if (maybe_known == NULL)
    {
      /* If we couldn't figure out the architecture that ldconfig(8)
       * in the container is going to have, pessimistically assume
       * that it won't write this architecture into ld.so.cache,
       * and therefore we need to keep it in the LD_LIBRARY_PATH
       * even after regenerating ld.so.cache. */
      g_debug ("Assuming unknown ldconfig(8) will not put %s libraries "
               "in ld.so.cache",
               tuple);
      return FALSE;
    }

  if (g_str_equal (maybe_known->multiarch_tuple, tuple))
    {
      /* If we're going to run an x86_64 ldconfig(8), then we don't
       * need to keep /overrides/lib/x86_64-linux-gnu in the
       * LD_LIBRARY_PATH, because those libraries will be listed
       * in the ld.so.cache after we have re-run ldconfig(8).
       * We prefer to do this because it more closely mirrors what would
       * happen on the "real" host system. */
      g_debug ("%s ldconfig(8) will put its own libraries in ld.so.cache",
               tuple);
      return TRUE;
    }

  if (maybe_known->extra_ld_so_architectures == NULL)
    {
      /* If we're going to run, for example, an aarch64 ldconfig(8),
       * then we *do* need to keep /overrides/lib/x86_64-linux-gnu in the
       * LD_LIBRARY_PATH, because the ldconfig(8) does not know about
       * cross-library architectures, so those libraries will *not* be
       * listed in the ld.so.cache. */
      g_debug ("%s ldconfig(8) will not put any foreign libraries in ld.so.cache",
               maybe_known->multiarch_tuple);
      return FALSE;
    }

  for (gsize i = 0; maybe_known->extra_ld_so_architectures[i] != NULL; i++)
    {
      /* If we're going to run an x86_64 ldconfig(8), then we don't
       * need to keep /overrides/lib/i386-linux-gnu in the
       * LD_LIBRARY_PATH because x86_64 ldconfig(8) *does* know about
       * i386 libraries, and vice versa. */
      if (g_str_equal (maybe_known->extra_ld_so_architectures[i], tuple))
        {
          g_debug ("%s ldconfig(8) will put %s libraries in ld.so.cache",
                   maybe_known->multiarch_tuple, tuple);
          return TRUE;
        }
    }

  g_debug ("%s ldconfig(8) will not put %s libraries in ld.so.cache",
           maybe_known->multiarch_tuple, tuple);
  return FALSE;
}

/**
 * srt_architecture_get_expected_runtime_linker:
 * @multiarch_tuple: A multiarch tuple defining an ABI, as printed
 *  by `gcc -print-multiarch` in the Steam Runtime
 *
 * Return the interoperable path to the runtime linker `ld.so(8)`,
 * if known. For example, for x86_64, this returns
 * `/lib64/ld-linux-x86-64.so.2`.
 *
 * Returns: (type filename) (transfer none): An absolute path,
 *  or %NULL if not known
 */
const char *
srt_architecture_get_expected_runtime_linker (const char *multiarch_tuple)
{
  const SrtKnownArchitecture *arch;

  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  arch = _srt_architecture_get_by_tuple (multiarch_tuple);

  if (arch != NULL)
    return arch->interoperable_runtime_linker;

  return NULL;
}

#ifdef _SRT_MULTIARCH
const SrtKnownArchitecture this_abi =
{
  .multiarch_tuple = _SRT_MULTIARCH,
  .sizeof_pointer = sizeof (void *),
};
#endif

const SrtKnownArchitecture mock_abi =
{
  .multiarch_tuple = "x86_64-mock-abi",
  .sizeof_pointer = 8,
};

const SrtKnownArchitecture *
_srt_architecture_get_by_tuple (const char *tuple)
{
  gsize i;

  for (i = 0; known_architectures[i].multiarch_tuple != NULL; i++)
    {
      if (strcmp (tuple, known_architectures[i].multiarch_tuple) == 0)
        return &known_architectures[i];
    }

#ifdef _SRT_MULTIARCH
  if (strcmp (tuple, _SRT_MULTIARCH) == 0)
    return &this_abi;
#endif

  /* For unit tests */
  if (strcmp (tuple, "x86_64-mock-abi") == 0)
    return &mock_abi;

  return NULL;
}

/*
 * @dfd: a directory file descriptor, `AT_FDCWD` or -1,
 *  or an open file descriptor for the library or executable itself
 *  if @file_path is %NULL
 * @file_path: (type filename): Filename to open, or %NULL if @dfd
 *  is an open file descriptor for the executable or library
 * @cls: (not optional) (out): ELF class, normally `ELFCLASS32` or `ELFCLASS64`
 * @data_encoding: (not optional) (out): ELF data encoding, normally
 *  `ELFDATA2LSB` or `ELFDATA2MSB`
 * @machine: (not optional) (out): ELF machine, for example `EM_X86_64` or
 *  `EM_386`
 * @error: On failure set to GIOErrorEnum, used to describe the error
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_architecture_read_elf (int dfd,
                            const char *file_path,
                            guint8 *cls,
                            guint8 *data_encoding,
                            guint16 *machine,
                            GError **error)
{
  glnx_autofd int fd = -1;
  g_autoptr(Elf) elf = NULL;
  GElf_Ehdr eh;

  g_return_val_if_fail (file_path == NULL || file_path[0] != '\0', FALSE);
  g_return_val_if_fail (file_path != NULL || dfd >= 0, FALSE);
  g_return_val_if_fail (cls != NULL, FALSE);
  g_return_val_if_fail (machine != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!_srt_open_elf (dfd, file_path, &fd, &elf, error))
    return FALSE;

  if (gelf_getehdr (elf, &eh) == NULL)
    return glnx_throw (error, "Error reading \"%s\" ELF header: %s",
                       file_path, elf_errmsg (elf_errno ()));

  *cls = eh.e_ident[EI_CLASS];
  *data_encoding = eh.e_ident[EI_DATA];
  *machine = eh.e_machine;

  return TRUE;
}

/*
 * @dfd: a directory file descriptor, `AT_FDCWD` or -1,
 *  or an open file descriptor for the library or executable itself
 *  if @file_path is %NULL
 * @file_path: (type filename): Filename to open, or %NULL if @dfd
 *  is an open file descriptor for the executable or library
 * @error: On failure set to GIOErrorEnum or SrtArchitectureError, used to
 *  describe the error
 *
 * Returns: (nullable): The known architecture object, or %NULL on error
 */
const SrtKnownArchitecture *
_srt_architecture_guess_from_elf (int dfd,
                                  const char *file_path,
                                  GError **error)
{
  guint8 cls = ELFCLASSNONE;
  guint8 data_encoding = ELFDATANONE;
  guint16 machine = EM_NONE;
  gsize i;

  g_return_val_if_fail (file_path == NULL || file_path[0] != '\0', NULL);
  g_return_val_if_fail (file_path != NULL || dfd >= 0, NULL);

  if (!_srt_architecture_read_elf (dfd, file_path, &cls, &data_encoding, &machine, error))
    return NULL;

  for (i = 0; i < G_N_ELEMENTS (known_architectures); i++)
    {
      if (known_architectures[i].multiarch_tuple != NULL
          && machine == known_architectures[i].machine_type
          && cls == known_architectures[i].elf_class
          && data_encoding == known_architectures[i].elf_encoding)
        return &known_architectures[i];
    }

  g_set_error (error, SRT_ARCHITECTURE_ERROR, SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
               "ELF class, data encoding and machine (%u,%u,%u) are unknown",
               cls, data_encoding, machine);
  return NULL;
}

static GQuark
known_as_quark (const SrtKnownArchitecture *known,
                const SrtKnownArchitecture **known_out)
{
  if (known_out != NULL)
    *known_out = known;

  return g_quark_from_static_string (known->multiarch_tuple);
}

/*
 * _srt_architecture_guess_from_user_input:
 * @known_out: (out) (nullable) (optional): Set to a #SrtKnownArchitecture or %NULL
 *
 * Returns: A quark representing a plausible multiarch tuple, or 0 on error
 */
GQuark
_srt_architecture_guess_from_user_input (const char *alias,
                                         const SrtKnownArchitecture **known_out,
                                         GError **error)
{
  g_autofree gchar *normalized = NULL;
  const SrtKnownArchitecture *known;
  gsize i;

  if (known_out != NULL)
    *known_out = NULL;

  g_return_val_if_fail (alias != NULL, SRT_ARCHITECTURE_QUARK_NONE);

  normalized = g_ascii_strdown (alias, -1);
  known = _srt_architecture_get_by_tuple (normalized);

  if (known != NULL)
    return known_as_quark (known, known_out);

  for (i = 0; known_architectures[i].multiarch_tuple != NULL; i++)
    {
      known = &known_architectures[i];

      if (g_strcmp0 (normalized, known->debian_architecture) == 0
          || g_strcmp0 (normalized, known->openxr_1_architecture) == 0
          || g_strcmp0 (normalized, known->steam_gameoverlayrenderer_dir) == 0)
        return known_as_quark (known, known_out);

      if (known->aliases != NULL)
        {
          gsize j;

          for (j = 0; known->aliases[j] != NULL; j++)
            {
              if (g_str_equal (normalized, known->aliases[j]))
                return known_as_quark (known, known_out);
            }
        }

      if (known->libdl_platform_expansions != NULL)
        {
          gsize j;

          for (j = 0; known->libdl_platform_expansions[j] != NULL; j++)
            {
              if (g_str_equal (normalized, known->libdl_platform_expansions[j]))
                return known_as_quark (known, known_out);
            }
        }
    }

  if (!_srt_architecture_check_plausible_tuple (normalized, error))
    {
      g_prefix_error (error, "Invalid architecture \"%s\": ", normalized);
      return SRT_ARCHITECTURE_QUARK_NONE;
    }

  /* We don't recognise this architecture but it looks plausible,
   * give the user the benefit of the doubt */
  return g_quark_from_string (normalized);
}

static const char * const default_architectures[] =
{
#if defined(__i386__) || defined(__x86_64__)
  SRT_ABI_X86_64,
  SRT_ABI_I386,
#elif defined(__aarch64__)
  SRT_ABI_AARCH64,
#elif defined(_SRT_MULTIARCH)
  _SRT_MULTIARCH,
#else
#warning Unknown architecture, please build with -Dmultiarch_tuple=...
  /* Assuming we are interested in x86 is marginally better than nothing */
  SRT_ABI_X86_64,
  SRT_ABI_I386,
#endif
};

void
_srt_architecture_array_populate_with_defaults (GArray *arch_quarks)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (default_architectures); i++)
    {
      GQuark arch_quark = g_quark_from_static_string (default_architectures[i]);

      g_return_if_fail (arch_quark != SRT_ARCHITECTURE_QUARK_NONE);
      g_array_append_val (arch_quarks, arch_quark);
    }
}

#if defined(__x86_64__)
#define CURRENT_TUPLE SRT_ABI_X86_64
#elif defined(__i386__)
#define CURRENT_TUPLE SRT_ABI_I386
#elif defined(__aarch64__)
#define CURRENT_TUPLE SRT_ABI_AARCH64
#elif defined(_SRT_MULTIARCH)
#define CURRENT_TUPLE _SRT_MULTIARCH
#else
#warning Unknown architecture, please build with -Dmultiarch_tuple=...
#endif

/*
 * Return the architecture for which the current process was compiled.
 *
 * If the current architecture's multiarch tuple cannot be determined,
 * instead return 0.
 * This can only happen on unusual architectures and only if
 * steam-runtime-tools was configured without `-Dmultiarch_tuple`.
 *
 * Returns: interned string representing a multiarch tuple, or 0 if unknown
 */
const SrtKnownArchitecture *
_srt_architecture_get_current (void)
{
#ifdef CURRENT_TUPLE
  return _srt_architecture_get_by_tuple (CURRENT_TUPLE);
#else
  return 0;
#endif
}
