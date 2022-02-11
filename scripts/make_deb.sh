#!/bin/bash
cd "$(dirname "$0")"
cd ..

if [ -z "$1" ]
  then
	echo "Usage: make_deb.sh VERSION"
	exit
fi

VERSION=$1
REV=1
PROJECT="uce"

PKG_NAME="${PROJECT}_$VERSION.$REV"

echo "Making package $PKG_NAME"
echo "==================================="

scripts/build_linux.sh

rm -r "pkg/$PKG_NAME"
mkdir -p "pkg/$PKG_NAME/opt/uce/"
mkdir -p "pkg/$PKG_NAME/DEBIAN/"
mkdir -p "pkg/$PKG_NAME/etc/uce/"
mkdir -p "dist"

cp scripts/control "pkg/$PKG_NAME/DEBIAN/"
echo "Version: $VERSION" >> "pkg/$PKG_NAME/DEBIAN/control"
echo "" >> "pkg/$PKG_NAME/DEBIAN/control"

cp -r bin "pkg/$PKG_NAME/opt/"
cp -r doc "pkg/$PKG_NAME/opt/"
cp -r examples "pkg/$PKG_NAME/opt/"
cp -r scripts "pkg/$PKG_NAME/opt/"
cp -r test "pkg/$PKG_NAME/opt/"

du -sh "pkg/$PKG_NAME/"*

cd pkg
dpkg-deb --build "$PKG_NAME"
mv "$PKG_NAME.deb" "../dist/"
cd ..

ls -lh "dist/" | grep "$PKG_NAME"
