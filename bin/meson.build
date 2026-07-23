# Copyright © 2019-2021 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

srt_bin = []
uninstalled_env_vars += {'SRT_BIN_PATH': meson.current_build_dir()}

srt_bin_programs = [
  { 'name': 'check-requirements' },
  { 'name': 'identify-library-abi' },
  { 'name': 'input-monitor', 'json-glib': true },
  {
    'name': 'launch-client',
    # Needs pv_include_dirs for flatpak-*.h
    'include_directories': pv_include_dirs,
    'threads': true,
  },
  { 'name': 'launcher-interface-0', 'glib': false },
  {
    'name': 'logger',
    # Prefixed with srt- so that `pkill steam` will leave
    # it running for long enough to finish writing its log!
    'prefix': 'srt-',
    'sources': ['logger.c', 'logger-mkfifo.c'],
    'pkglibexec': true,
  },
  {
    'name': 'run-outside-ldlp',
    # Prefixed with srt- because it's an internal implementation detail
    'prefix': 'srt-',
    'pkglibexec': true,
    'rpath': false,
  },
  { 'name': 'steam-remote' },
  { 'name': 'supervisor' },
  { 'name': 'system-info', 'json-glib': true },
  { 'name': 'urlopen' },
]

# Slightly different: needs threads and codegen, and install in libexecdir
# with an architecture prefix so we can use it during emulation
if multiarch != ''
  launcher_service_exe = multiarch + '-srt-launcher-service'
  launcher_service_dir = pkglibexecdir
  launcher_service_rpath = pkglibexec_rpath

  # Note that this doesn't work in scout because its meson is too old:
  # we fix that up in debian/rules
  if meson.version().version_compare('>=0.61')
    install_symlink(
      'steam-runtime-launcher-service',
      install_dir : get_option('bindir'),
      # This assumes that bindir is a single path component and
      # pkglibexecdir is a relative path, but we already make similar
      # assumptions about RPATHs
      pointing_to : '..' / pkglibexecdir / launcher_service_exe,
    )
  endif
else
  launcher_service_exe = 'steam-runtime-launcher-service'
  launcher_service_dir = get_option('bindir')
  launcher_service_rpath = bin_rpath
endif

srt_bin += executable(
  launcher_service_exe,
  sources: [
    'launcher-service.c',
  ],
  c_args : srt_c_args,
  dependencies : [
    launcher_codegen_dep,
    threads,
    gio_unix,
    libglnx_dep,
    libsteamrt_static_dep,
  ],
  install : true,
  install_dir: launcher_service_dir,
  build_rpath : launcher_service_rpath,
  install_rpath : launcher_service_rpath,
)

# Different: needs threads and UI
if sdl2_dep.found() and sdl2_ttf_dep.found()
  srt_bin_programs += {
    'name': 'dialog-ui',
    'sdl2-ui': true,
    'threads': true,
  }
endif

sh_scripts = [
  'dialog',
  'launch-options',
]

foreach script : sh_scripts
  install_data(
    script + '.sh',
    rename : ['steam-runtime-' + script],
    install_dir : get_option('bindir'),
    install_mode : 'rwxr-xr-x',
  )
endforeach

libexec_data = [
  'logger-0.bash',
]

foreach file : libexec_data
  install_data(
    file,
    install_dir : pkglibexecdir,
    install_mode : 'rw-r--r--',
  )
endforeach

libexec_scripts = [
  'launch-options.py',
]

foreach script : libexec_scripts
  install_data(
    script,
    install_dir : pkglibexecdir,
    install_mode : 'rwxr-xr-x',
  )
endforeach

foreach bin_details : srt_bin_programs
  bin_name = bin_details['name']
  name_prefix = bin_details.get('prefix', 'steam-runtime-')
  sources = bin_details.get('sources', [bin_name + '.c'])

  if bin_details.get('json-glib', false)
    dependencies = [glib, gobject, gio_unix, json_glib, libglnx_dep, libsteamrt_static_dep]
    # Usually use the adjacent json-glib, ignoring LD_LIBRARY_PATH even if set
    use_rpath = bin_details.get('rpath', true)
  elif bin_details.get('glib', true)
    dependencies = [glib, gobject, gio_unix, libglnx_dep, libsteamrt_static_dep]
    # Usually use the adjacent GLib, ignoring LD_LIBRARY_PATH even if set
    use_rpath = bin_details.get('rpath', true)
  else
    dependencies = [libsteamrt_libc_utils_dep]
    # Usually don't use adjacent libraries if we only have glibc dependencies
    use_rpath = bin_details.get('rpath', false)
  endif

  if bin_details.get('threads', false)
    dependencies = [threads] + dependencies
  endif

  if bin_details.get('sdl2-ui', false)
    dependencies += [
      libsteamrt_ui_dep,
      sdl2_dep,
      sdl2_ttf_dep,
      x11_dep,
    ]
  endif

  pkglibexec = bin_details.get('pkglibexec', false)

  if pkglibexec
    install_dir = pkglibexecdir
    rpath = pkglibexec_rpath
  else
    install_dir = get_option('bindir')
    rpath = bin_rpath
  endif

  if use_rpath
    srt_bin += executable(
      name_prefix + bin_name,
      sources,
      c_args : srt_c_args,
      dependencies : dependencies,
      include_directories : bin_details.get('include_directories', []),
      install : true,
      install_dir : install_dir,
      build_rpath : rpath,
      install_rpath : rpath,
    )
  else
    srt_bin += executable(
      name_prefix + bin_name,
      sources,
      c_args : srt_c_args,
      dependencies : dependencies,
      include_directories : bin_details.get('include_directories', []),
      install : true,
      install_dir : install_dir,
    )
  endif
endforeach

if build_man_pages
  has_man_page = []

  foreach bin_details : srt_bin_programs
    if bin_details.get('manpage', true)
      has_man_page += bin_details
    endif
  endforeach

  foreach bin_name : (
    sh_scripts + [
      'launcher-service',
    ]
  )
    has_man_page += { 'name': bin_name }
  endforeach

  foreach bin_details : has_man_page
    bin_name = bin_details['name']
    name_prefix = bin_details.get('prefix', 'steam-runtime-')

    custom_target(
      name_prefix + bin_name + '.1',
      build_by_default : true,
      command : [
        pandoc,
        '-s',
        '-o', '@OUTPUT@',
        '-f', pandoc_markdown_nosmart,
        '-t', 'man',
        '@INPUT@',
      ],
      input : bin_name + '.md',
      output : name_prefix + bin_name + '.1',
      install : true,
      install_dir : join_paths(get_option('prefix'), get_option('mandir'), 'man1'),
    )
  endforeach
endif

bubblewrap_subproject = subproject(
  'bubblewrap',
  default_options : [
    'bash_completion=disabled',
    'build_rpath=' + pkglibexec_rpath,
    'bwrapdir=' + pkglibexecdir,
    'install_rpath=' + pkglibexec_rpath,
    'man=disabled',
    'program_prefix=srt-',
    'selinux=disabled',
    'tests=false',
    'zsh_completion=disabled',
  ],
)
srt_bin += bubblewrap_subproject.get_variable('bwrap')

test_depends += srt_bin

# vim:set sw=2 sts=2 et:
