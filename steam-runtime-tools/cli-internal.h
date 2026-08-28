/*<private_header>*/
/*
 * Copyright © 2022 Collabora Ltd.
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

#pragma once

#include <glib.h>

#include <steam-runtime-tools/macros.h>

typedef enum
{
  SRT_LOG_FLAGS_DEBUG = (1 << 0),
  SRT_LOG_FLAGS_INFO = (1 << 1),
  SRT_LOG_FLAGS_TIMESTAMP = (1 << 2),
  SRT_LOG_FLAGS_DIFFABLE = (1 << 3),
  SRT_LOG_FLAGS_PID = (1 << 4),
  SRT_LOG_FLAGS_TIMING = (1 << 5),
  SRT_LOG_FLAGS_DIVERT_STDOUT = (1 << 6),
  SRT_LOG_FLAGS_OPTIONALLY_JOURNAL = (1 << 7),
  SRT_LOG_FLAGS_JOURNAL = (1 << 8),
  SRT_LOG_FLAGS_LEVEL = (1 << 9),
  SRT_LOG_FLAGS_DEBUG_IMPLIES_TIMING = (1 << 10),
  SRT_LOG_FLAGS_NONE = 0
} SrtLogFlags;

#define SRT_LOG_FLAGS_VERBOSITY_MASK (SRT_LOG_FLAGS_DEBUG | SRT_LOG_FLAGS_INFO)

#define SRT_LOG_LEVEL_FAILURE (1 << G_LOG_LEVEL_USER_SHIFT)
#define SRT_LOG_LEVEL_WARNING (1 << (G_LOG_LEVEL_USER_SHIFT + 1))

#define _srt_log_failure(...) \
  g_log (G_LOG_DOMAIN, SRT_LOG_LEVEL_FAILURE, __VA_ARGS__)
#define _srt_log_warning(...) \
  g_log (G_LOG_DOMAIN, SRT_LOG_LEVEL_WARNING, __VA_ARGS__)

int _srt_journal_stream_fd (const char *identifier,
                            int priority,
                            gboolean enable_level_prefixes,
                            GError **error);

gboolean _srt_cli_init (const char *prgname,
                        const char *app_log_domain,
                        SrtLogFlags flags,
                        int *original_stdout_out,
                        int *original_stderr_out,
                        GError **error);
gboolean _srt_cli_update_log_flags (SrtLogFlags flags,
                                    GError **error);
gboolean _srt_cli_restore_saved_fd (int saved_fd,
                                    int target_fd,
                                    GError **error);
gboolean _srt_cli_is_verbose (void);
gboolean _srt_cli_is_debugging (void);
SrtLogFlags _srt_cli_get_log_flags (void);
void _srt_cli_increase_verbosity (SrtLogFlags flags);
void _srt_cli_set_diffable (void);

gboolean _srt_cli_option_context_parse (GOptionContext *context,
                                        int original_stdout_fd,
                                        int *argc,
                                        char ***argv,
                                        GError **error);

gboolean _srt_cli_option_arg_func_verbose (const char *name,
                                           const char *value,
                                           void *data,
                                           GError **error);

#define SRT_CLI_OPTION_ENTRY_VERBOSE \
{ "verbose", 'v', \
  G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, _srt_cli_option_arg_func_verbose, \
  "Be more verbose. If given twice, log debug messages too.", NULL }

gboolean _srt_cli_option_arg_func_version (const char *name,
                                           const char *value,
                                           void *data,
                                           GError **error) G_GNUC_NORETURN;

gboolean _srt_cli_option_arg_func_version_only (const char *name,
                                                const char *value,
                                                void *data,
                                                GError **error) G_GNUC_NORETURN;

#define SRT_CLI_OPTION_ENTRY_VERSION \
{ "version", '\0', \
  G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, _srt_cli_option_arg_func_version, \
  "Print version number and exit", NULL }

#define SRT_CLI_OPTION_ENTRY_VERSION_IS_VERSION_ONLY \
{ "version", '\0', \
  G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, \
  _srt_cli_option_arg_func_version_only, \
  "Print version number and exit", NULL }

#define SRT_CLI_OPTION_ENTRY_VERSION_ONLY \
{ "version-only", '\0', \
  G_OPTION_FLAG_NO_ARG | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK, \
  _srt_cli_option_arg_func_version_only, \
  "Print version number and exit", NULL }
