# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) Iputils Project, 2017-2021

inc = include_directories('..')

bping = executable('bping', [
       'bping.c',
       'ping_common.c',
       'ping6_common.c',
       'ping_json.c',
       'ping_output.c',
       'node_info.c',
       git_version_h
],
       include_directories : inc,
       dependencies : [
	       cap_dep,
	       idn_dep,
	       intl_dep,
	       m_dep,
	       resolv_dep
       ],
       link_with : [libcommon],
       install: true)
