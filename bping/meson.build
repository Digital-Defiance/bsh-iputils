bping = executable('bping', [
    'bping.c',
    git_version_h
],
    include_directories : include_directories('..'),
    dependencies : [m_dep, libbrightlink_dep],
    link_with : [libcommon],
    install: true)
