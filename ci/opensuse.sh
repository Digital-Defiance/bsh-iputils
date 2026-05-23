#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2018-2025 Petr Vorel <pvorel@suse.cz>
set -ex

zypper='zypper --non-interactive install --no-recommends'

if [ "$WITH_TEST_DEPS" ]; then
	TEST_DEPS="
	iputils
	perl-Test-Command
	traceroute
"
	if ! $zypper perl-Socket-GetAddrInfo; then
		$zypper make perl
		PERL_MM_USE_DEFAULT=1 cpan -T Socket::GetAddrInfo
	fi
fi

$zypper \
	clang \
	docbook_5 \
	docbook5-xsl-stylesheets \
	file \
	gcc \
	gettext-tools \
	git \
	iproute2 \
	jq \
	libcap-devel \
	libidn2-devel \
	libsecp256k1-devel \
	libopenssl-devel \
	libxslt-tools \
	meson \
	ninja \
	pkg-config \
	$TEST_DEPS
