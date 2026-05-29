color_env = environment()
color_env.set('MESON_BUILD_ROOT', meson.project_build_root())
color_env.set('PERL5LIB', test_dir)

build_root = meson.project_build_root()
color_tool_dirs = [
  join_paths(build_root, 'bping'),
  join_paths(build_root, 'bclockdiff'),
  join_paths(build_root, 'btraceroute'),
  join_paths(build_root, 'bmtr'),
  join_paths(build_root, 'baudit'),
]
color_tool_path = ''
foreach d : color_tool_dirs
  if color_tool_path == ''
    color_tool_path = d
  else
    color_tool_path = color_tool_path + ':' + d
  endif
endforeach
color_env.prepend('PATH', color_tool_path, separator : ':')

bping_bin = join_paths(build_root, 'bping', 'bping')

test('color-01-basic', find_program('color-01-basic.t'), args: bping_bin, env : color_env)
test('color-02-env', find_program('color-02-env.t'), args: bping_bin, env : color_env)
test('color-03-help', find_program('color-03-help.t'), env : color_env)
