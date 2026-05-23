#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2019-2025 Petr Vorel <petr.vorel@gmail.com>
set -ex

apk update

if [ "$WITH_TEST_DEPS" ]; then
	TEST_DEPS="
	iputils
	perl-socket-getaddrinfo
	perl-test-command
	traceroute
"
fi

# NOTE: libidn2-dev is not in 3.10, only in edge
apk add \
	clang \
	docbook-xml \
	docbook-xsl \
	file \
	gcc \
	git \
	gettext-dev \
	iproute2 \
	jq \
	libcap-dev \
	libsecp256k1-dev \
	libxslt \
	meson \
	musl-dev \
	openssl-dev \
	pkgconfig \
	$TEST_DEPS
