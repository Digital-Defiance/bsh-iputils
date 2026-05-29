# SPDX-License-Identifier: GPL-2.0-or-later

foreach t : ['bmtr-01-basic.t']
  test(t, find_program(t),
       args: join_paths(meson.project_build_root(), 'bmtr', 'bmtr'),
       env: perl_test_env,
       timeout: 120)
endforeach
