# SPDX-License-Identifier: GPL-2.0-or-later

foreach t : ['btraceroute-01-basic.t']
  test(t, find_program(t),
       args: join_paths(meson.project_build_root(), 'btraceroute', 'btraceroute'),
       timeout: 120)
endforeach
