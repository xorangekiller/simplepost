#!/bin/sh

rm -vf aclocal.m4
rm -vrf autom4te.cache/
rm -vrf build-aux/
rm -vf config.*

find -type f -executable -name configure -exec rm -vf {} \;
find -type f -executable -name simplepost -exec rm -vf {} \;

find -type f -name Makefile -exec rm -vf {} \;
find -type f -name Makefile.in -exec rm -vf {} \;

rm -vf INSTALL
rm -vf src/config.h

find -type f -name 'stamp-h?' -exec rm -vf {} \;
find -type f -name '*.o' -exec rm -vf {} \;
find -type d -name .deps -exec rm -vrf {} \;
