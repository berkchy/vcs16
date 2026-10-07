#!/bin/bash

cd $GITHUB_WORKSPACE

#export VITASDK=/usr/local/vitasdk

# vdpm is a pacman frontend now, it asks for confirmation unless told otherwise
export VDPM_NONINTERACTIVE=1

install_package()
{
	./vdpm install "$1" || exit 1
}

echo "Downloading vitasdk..."
git clone https://github.com/vitasdk/vdpm.git --depth=1 || exit 1
pushd vdpm || exit 1
./bootstrap-vitasdk.sh || exit 1

# The shell wrapper in this repo predates the current SDK layout and looks for
# the package client in $VITASDK/bin, while the bootstrap now ships it under
# libexec/vdpm - so vdpm died with "package client is not executable" before
# installing anything. Point it at the real path when that is where it is.
if [ -x "$VITASDK/libexec/vdpm/pacman" ]; then
	export VDPM_PACMAN="$VITASDK/libexec/vdpm/pacman"
fi

install_package taihen
install_package kubridge
install_package zlib
install_package SceShaccCgExt
install_package vitaShaRK
install_package libmathneon
popd || exit 1

echo "Building vrtld..."
git clone https://github.com/fgsfdsfgs/vita-rtld.git --depth=1 || exit 1
pushd vita-rtld || die
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release || die_configure
cmake --build build -- -j$JOBS || die
cmake --install build || die
popd
