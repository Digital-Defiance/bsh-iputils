#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2019-2025 Petr Vorel <petr.vorel@gmail.com>
set -ex

if [ "$DISTRO_VERSION" = "xenial" ]; then
	cat <<EOF | tee /etc/apt/sources.list.d/xenial-backports.list
deb http://archive.ubuntu.com/ubuntu xenial-backports main restricted universe multiverse
EOF
	BACKPORT_REPO="xenial-backports"
fi

export DEBIAN_FRONTEND="noninteractive"

apt update

if [ "$WITH_TEST_DEPS" ]; then
	TEST_DEPS="
	iputils-ping
	libsocket-getaddrinfo-perl
	libtest-command-perl
	traceroute
"
fi

apt install -y --no-install-recommends \
	clang \
	docbook-xsl-ns \
	file \
	gcc \
	gettext \
	git \
	iproute2 \
	jq \
	libcap-dev \
	libidn2-0-dev \
	libsecp256k1-dev \
	libssl-dev \
	meson \
	pkg-config \
	xsltproc \
	$TEST_DEPS
