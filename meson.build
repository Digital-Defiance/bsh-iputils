# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) Iputils Project, 2017-2024
# Brightdate fork: only builds b* utilities (bping, bclockdiff)

project('iputils', 'c',
	default_options : [
		'c_std=c99',
		'warning_level=3',
		'localstatedir=var',
	],
	meson_version : '>=0.56',
	version : '20260529')

cc = meson.get_compiler('c')

prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
localedir = join_paths(prefix, get_option('localedir'))
sbindir = join_paths(prefix, get_option('sbindir'))

conf = configuration_data()
conf.set_quoted('PACKAGE_NAME', meson.project_name())

add_project_arguments(
	'-include', 'config.h',
	'-include', 'git-version.h',
	language : 'c'
)

# Optional: I18N / gettext
gettext = get_option('USE_GETTEXT')
intl_dep = dependency('', required : false)
if gettext == true
	if host_machine.system() != 'darwin'
		intl_dep = cc.find_library('intl', required : false)
	endif
	add_project_arguments(['-DGETTEXT_PACKAGE="iputils"'], language : 'c')
	conf.set('ENABLE_NLS', 1, description : 'If set enable I18N.')
	conf.set_quoted('LOCALEDIR', localedir)
endif

# Optional: IDN support
idn = get_option('USE_IDN')
if idn == true
	idn_dep = dependency('libidn2', required : false)
	if idn_dep.found()
		add_project_arguments('-DUSE_IDN', language : 'c')
		conf.set('USE_IDN', 1,
			description : 'If set use Internationalized Domain Name library.')
	endif
else
	idn_dep = dependency('', required : false)
endif

# Math and resolver libraries
m_dep = cc.find_library('m', required : false)
resolv_dep = cc.find_library('resolv', required : false)

# libBrightLink — canonical C client for the BrightLink Protocol. Lives
# at subprojects/libbrightlink/ as vendored source (see
# tools/update-libbrightlink.sh). Launchpad recipe builds cannot rely on
# git submodules. The protocol code (DD-ECIES, AES-GCM, HKDF, P-256
# transcript verify, TOFU pin storage) lives there; bright-iputils only
# carries the four b* tools and their geo glue.
#
# We ask for libbrightlink's test tree to be built when iputils tests are
# enabled — the mock-brightnexus binary built there is consumed by our
# own Perl integration tests in test/.
brightlink_tests_opt = get_option('SKIP_TESTS') ? 'false' : 'true'
brightlink_proj = subproject('libbrightlink',
	default_options : ['libbrightlink_tests=' + brightlink_tests_opt])
libbrightlink_dep = brightlink_proj.get_variable('libbrightlink_dep')
# USE_CAP is Linux-only; silently force it off on macOS regardless of what was passed
use_cap = get_option('USE_CAP')
if use_cap and host_machine.system() == 'darwin'
	use_cap = false
	message('Disabling USE_CAP: libcap is not available on macOS')
endif

build_mans = get_option('BUILD_MANS')
build_html_mans = get_option('BUILD_HTML_MANS')
run_tests = not get_option('SKIP_TESTS')

# config.h detection
if host_machine.endian() == 'big'
	conf.set('WORDS_BIGENDIAN', '1')
endif

foreach h : ['error.h']
	if cc.has_header(h) and cc.has_function('error')
		conf.set('HAVE_' + h.to_upper().underscorify(), 1,
			description : 'Define if ' + h + ' can be included.')
	endif
endforeach

# Shared version header
git_version_h = vcs_tag(
	input : 'git-version.h.meson',
	output : 'git-version.h',
	fallback : meson.project_version()
)

# Shared common library
common_sources = files(
	'iputils_common.h', 'iputils_common.c',
	'iputils_color.h', 'iputils_color.c',
	'md5.h', 'md5.c'
)
libcommon = static_library(
	'common',
	[common_sources, git_version_h],
	install : false)

# config.h output (must be after all conf.set calls)
config_h = configure_file(
	output : 'config.h',
	configuration : conf)

# Build b* utilities only
subdir('bping')
subdir('bclockdiff')
subdir('btraceroute')
subdir('bmtr')
subdir('baudit')

if gettext == true
	subdir('po')
endif

# These are false since only b* utilities are built
build_arping = false
build_clockdiff = false
build_ping = false
build_tracepath = false

if run_tests
	subdir('test')
endif

# Summary
output = '\n\nAPPLICATIONS (build)\n'
output += 'bping: true\n'
output += 'bclockdiff: true\n'
output += '\nCONFIGURATION\n'
output += 'IDN (with libidn2): ' + idn.to_string() + '\n'
output += 'I18N (with gettext): ' + gettext.to_string() + '\n'
output += 'run tests: ' + run_tests.to_string() + '\n'
output += '\nSYSTEM PATHS\n'
output += 'prefix: ' + prefix + '\n'
output += 'bindir: ' + bindir + '\n'
output += 'localedir: ' + localedir + '\n'
output += 'sbindir: ' + sbindir + '\n'
message(output)
