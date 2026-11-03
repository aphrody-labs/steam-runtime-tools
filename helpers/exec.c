/*
 * Copyright © 2019-2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/*
 * Helper executable that just execs its arguments, searching the PATH.
 *
 * This might seem useless at first glance, but for example this is useful
 * if we need to run an executable that might be from the PATH, while
 * wrapped by an emulator that does not search the PATH, and this needs
 * to be done in an executable of the same architecture as the target.
 *
 * It can also carry out simple changes to the execution environment.
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

/* Chosen to be similar to env(1) */
enum
{
  LAUNCH_EX_USAGE = 125,
  LAUNCH_EX_FAILED = 125,
  LAUNCH_EX_CANNOT_INVOKE = 126,
  LAUNCH_EX_NOT_FOUND = 127,
  LAUNCH_EX_CANNOT_REPORT = 255
};

enum
{
  OPTION_HELP = 1,
  OPTION_VERSION,
  OPTION_COMPATIBLE_RLIMIT,
};

struct option long_options[] =
{
    { "help", no_argument, NULL, OPTION_HELP },
    { "compatible-rlimit", no_argument, NULL, OPTION_COMPATIBLE_RLIMIT },
    { "version", no_argument, NULL, OPTION_VERSION },
    { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS] -- PROGRAM [ARG...]\n",
           program_invocation_short_name);
  exit (code);
}

/*
 * Try to set a widely interoperable RLIMIT_NOFILE as described in
 * https://0pointer.net/blog/file-descriptor-limits.html:
 * soft limit exactly FD_SETSIZE (= 1024),
 * and hard limit takes whatever value it already had.
 * See doc-comment of _srt_set_compatible_resource_limits()
 * for more details.
 */
static void
set_compatible_rlimit (void)
{
  struct rlimit rlim = { 0, 0 };
  int ret;

  ret = getrlimit (RLIMIT_NOFILE, &rlim);

  if (ret < 0)
    {
      warn ("Unable to discover file descriptor resource limit");
      return;
    }

  if (rlim.rlim_cur != FD_SETSIZE
      && (rlim.rlim_max >= FD_SETSIZE
          || rlim.rlim_max == RLIM_INFINITY))
    {
      struct rlimit new_rlim = rlim;

      new_rlim.rlim_cur = FD_SETSIZE;
      ret = setrlimit (RLIMIT_NOFILE, &new_rlim);

      if (ret < 0)
        {
          const char *verb;

          if (rlim.rlim_cur == RLIM_INFINITY
              || rlim.rlim_cur > new_rlim.rlim_cur)
            verb = "lower";
          else
            verb = "raise";

          warn ("Unable to %s file descriptor resource limit from %ju to %ju",
                verb,
                (uintmax_t) rlim.rlim_cur,
                (uintmax_t) new_rlim.rlim_cur);
          return;
        }
    }
}

int
main (int argc,
      char **argv)
{
  int opt;

  while ((opt = getopt_long (argc, argv, "+", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_COMPATIBLE_RLIMIT:
            set_compatible_rlimit ();
            break;

          case OPTION_HELP:
            usage (0);
            break;

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                program_invocation_short_name, VERSION);
            return 0;

          case '?':
          default:
            usage (LAUNCH_EX_USAGE);
            break;  /* not reached */
        }
    }

  if (argc < optind + 1)
    usage (LAUNCH_EX_USAGE);

  execvp (argv[optind], &argv[optind]);

  if (errno == ENOENT)
    return LAUNCH_EX_NOT_FOUND;

  return LAUNCH_EX_CANNOT_INVOKE;
}
