# Vendored test/Test/Command.pm for macOS and other systems without perl-Test-Command.
test_dir = meson.current_source_dir()
perl_test_env = environment({'PERL5LIB': test_dir})

subdir('bclockdiff')
subdir('btraceroute')
subdir('bmtr')
subdir('baudit')
subdir('color')
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2021 Petr Vorel <pvorel@suse.cz>

if build_arping == true
  subdir('arping')
endif

if build_clockdiff == true
  subdir('clockdiff')
endif

if build_ping == true
  subdir('ping')
endif

if build_tracepath == true
  subdir('tracepath')
endif
