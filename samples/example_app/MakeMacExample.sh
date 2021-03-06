#!/bin/sh

# This file is part of BOINC.
# http://boinc.berkeley.edu
# Copyright (C) 2008 University of California
#
# BOINC is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
# BOINC is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with BOINC.  If not, see <http://www.gnu.org/licenses/>.
#
#
# Script to build Macintosh example_app using Makefile
#
# This will build for OS 10.3.9 and later on Mac OS 10.5 and XCode 3.1
# if you have installed the OS 10.3.9 SDK.
#
# This will build for OS 10.4 and later on Mac OS 10.6 and XCode 3.2 or 
# on Mac OS 10.5 and XCode 3.1 if you have not installed OS 10.3.9 SDK.
#
# by Charlie Fenton 2/16/10
#
## First, build the BOINC libraries using boinc/mac_build/BuildMacBOINC.sh
##
## In Terminal, CD to the example_app directory.
##     cd [path]/example_app/
## then run this script:
##     sh [path]/MakeMacExample.sh
##

rm -fR ppc i386 x86_64

if [ -d /Developer/SDKs/MacOSX10.3.9.sdk/ ]; then
    HAS_1039SDK=1
else
    HAS_1039SDK=0
    echo
    echo "System 10.3.9 SDK is not available.  Building for OS 10.4 and later"
    echo
fi

echo
echo "***************************************************"
echo "********** Building PowerPC Application ***********"
if [ "$HAS_1039SDK" = "1" ]; then
    echo "************ for OS 10.3.9 and later **************"
else
    echo "************* for OS 10.4 and later ***************"
fi
echo "***************************************************"
echo

export CC=/usr/bin/gcc-4.0;export CXX=/usr/bin/g++-4.0
if [ "$HAS_1039SDK" = "1" ]; then
export MACOSX_DEPLOYMENT_TARGET=10.3
export LDFLAGS="-Wl,-syslibroot,/Developer/SDKs/MacOSX10.3.9.sdk,-arch,ppc"
export VARIANTFLAGS="-isysroot /Developer/SDKs/MacOSX10.3.9.sdk -arch ppc"
else
export MACOSX_DEPLOYMENT_TARGET=10.4
export LDFLAGS="-Wl,-syslibroot,/Developer/SDKs/MacOSX10.4u.sdk,-arch,ppc"
export VARIANTFLAGS="-isysroot /Developer/SDKs/MacOSX10.4u.sdk -arch ppc -fvisibility=hidden -fvisibility-inlines-hidden"
fi

rm -f uc2.o
rm -f uc2_graphics.o
rm -f uc2
rm -f uc2_graphics
make -f Makefile_mac all

if [  $? -ne 0 ]; then exit 1; fi

mkdir ppc
mv uc2 ppc/
mv uc2_graphics ppc/

echo
echo "***************************************************"
echo "******* Building 32-bit Intel Application *********"
echo "***************************************************"
echo

export MACOSX_DEPLOYMENT_TARGET=10.4
export CC=/usr/bin/gcc-4.0;export CXX=/usr/bin/g++-4.0
export LDFLAGS="-Wl,-syslibroot,/Developer/SDKs/MacOSX10.4u.sdk,-arch,i386"
export VARIANTFLAGS="-isysroot /Developer/SDKs/MacOSX10.4u.sdk -arch i386 -fvisibility=hidden -fvisibility-inlines-hidden"

rm -f uc2.o
rm -f uc2_graphics.o
rm -f uc2
rm -f uc2_graphics
make -f Makefile_mac all

if [  $? -ne 0 ]; then exit 1; fi

mkdir i386
mv uc2 i386/
mv uc2_graphics i386/

echo
echo "***************************************************"
echo "******* Building 64-bit Intel Application *********"
echo "***************************************************"
echo

export MACOSX_DEPLOYMENT_TARGET=10.5
export CC=/usr/bin/gcc-4.0;export CXX=/usr/bin/g++-4.0
export LDFLAGS="-Wl,-syslibroot,/Developer/SDKs/MacOSX10.5.sdk,-arch,x86_64"
export VARIANTFLAGS="-isysroot /Developer/SDKs/MacOSX10.5.sdk -arch x86_64 -fvisibility=hidden -fvisibility-inlines-hidden"

rm -f uc2.o
rm -f uc2_graphics.o
rm -f uc2
rm -f uc2_graphics
make -f Makefile_mac all

if [  $? -ne 0 ]; then exit 1; fi

mkdir x86_64
mv uc2 x86_64/
mv uc2_graphics x86_64/


echo
echo "***************************************************"
echo "**************** Build Succeeded! *****************"
echo "***************************************************"
echo

export CC="";export CXX=""
export LDFLAGS=""
export CPPFLAGS=""
export CFLAGS=""
export SDKROOT=""

exit 0

