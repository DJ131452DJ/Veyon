#!/usr/bin/env bash

set -e

SRC=$1
BUILD=$2

mkdir -p $BUILD
cd $BUILD

cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DLTO=ON $CMAKE_FLAGS $SRC

if [ -z "$3" ] ; then
	ninja

	fakeroot ninja package

	LIBDIR=$(grep VEYON_LIB_DIR CMakeCache.txt |cut -d "=" -f2)
	BUILD_PWD=$(pwd)

	mkdir -p $LIBDIR
	cd $LIBDIR
	find $BUILD_PWD/plugins -name "*.so" -exec ln -s '{}' ';'
	cd $BUILD_PWD

	./cli/veyon-cli help
	./cli/veyon-cli about
else
	fakeroot ninja package
fi

