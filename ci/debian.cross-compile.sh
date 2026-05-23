#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2019-2025 Petr Vorel <pvorel@suse.cz>
# Brightdate fork: adds libsecp256k1-dev:$ARCH and libssl-dev:$ARCH for
# the libBrightLink subproject build, plus the matching cross-arch
# pkg-config wrapper so meson's host-machine dependency resolution
# finds the .pc files under /usr/lib/$gnutriplet/pkgconfig instead of
# the build host's directory.
set -ex

if [ -z "$ARCH" ]; then
	echo "missing \$ARCH!" >&2
	exit 1
fi

case "$ARCH" in
arm64)
	gcc_arch="aarch64"
	meson_arch="aarch64"
	;;
ppc64el)
	gcc_arch="powerpc64le"
	meson_arch="ppc64"
	;;
s390x)
	gcc_arch="$ARCH"
	meson_arch="$ARCH"
	;;
*) echo "unsupported arch: '$1'!" >&2; exit 1;;
esac

dpkg --add-architecture $ARCH
apt update

# pkg-config-<gcc_arch>-linux-gnu provides the cross-arch wrapper that
# meson invokes via the [binaries].pkgconfig line in meson.cross. Without
# it, the wrapper that ships with `pkg-config` in noble/trixie can't find
# .pc files under /usr/lib/<gnutriplet>/pkgconfig and pkg-config falls
# back to the build host's pkgconfig dir, which contains nothing for the
# target arch. Result: 'Pkg-config for machine host machine not found'.
apt install -y --no-install-recommends \
	dpkg-dev \
	gcc-${gcc_arch}-linux-gnu \
	libc6-dev-${ARCH}-cross \
	libcap-dev:$ARCH \
	libidn2-0-dev:$ARCH \
	libsecp256k1-dev:$ARCH \
	libssl-dev:$ARCH \
	pkg-config

cat <<EOF > meson.cross
[binaries]
c = '${gcc_arch}-linux-gnu-gcc'
pkgconfig = 'pkg-config'

[properties]
# Tell pkg-config where to look for the cross-arch .pc files. Without
# PKG_CONFIG_LIBDIR meson would let pkg-config see the host's libdir
# and try to link host-arch libraries into a target-arch binary.
pkg_config_libdir = '/usr/lib/${gcc_arch}-linux-gnu/pkgconfig:/usr/share/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = '$meson_arch'
cpu = '$meson_arch'
endian = 'little'
EOF
