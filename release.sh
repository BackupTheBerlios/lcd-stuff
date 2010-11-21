#!/bin/sh
set -e

read VERSION < VERSION
ARCHIVE=lcd-stuff-$VERSION.tar.bz2
git archive --format=tar --prefix=lcd-stuff-$VERSION/ master | bzip2 > $ARCHIVE
echo "Archive '$ARCHIVE' ready!"
