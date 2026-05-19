bclockdiff = executable('bclockdiff', [
    'bclockdiff.c',
    '../clockdiff.c',
    git_version_h
],
    include_directories : include_directories('..'),
    c_args : ['-DBCLOCKDIFF_BUILD'],
    dependencies : [intl_dep, idn_dep],
    link_with : [libcommon],
    install: true)
