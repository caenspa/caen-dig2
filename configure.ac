#	Copyright (C) 2020-2023 CAEN SpA
#
#	This file is part of the CAEN Dig2 Library.
#
#	The CAEN Dig2 Library is free software; you can redistribute it and/or
#	modify it under the terms of the GNU Lesser General Public
#	License as published by the Free Software Foundation; either
#	version 3 of the License, or (at your option) any later version.
#
#	The CAEN Dig2 Library is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#	Lesser General Public License for more details.
#
#	You should have received a copy of the GNU Lesser General Public
#	License along with the CAEN Dig2 Library; if not, see
#	https://www.gnu.org/licenses/.
#
#	SPDX-License-Identifier: LGPL-3.0-or-later

# Tested with autoconf 2.69, released in 2012
AC_PREREQ([2.69])
AC_INIT(CAEN_Dig2, [m4_esyscmd_s([echo $VERSION])], support.computing@caen.it)

# Search in m4 folder for custom macros. It requires also ACLOCAL_AMFLAGS on
# Makefile.am, unless we use AC_CONFIG_MACRO_DIRS introduced on autoconf 2.70.
AC_CONFIG_MACRO_DIR([m4])

# tar-ustar required to remove the filename length limit on default tar-v7 format
# is old enough to be supported everywhere
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects tar-ustar])
AM_PROG_AR

# Requires constructor to be invoked at startup, cannot be used as static
LT_INIT([disable-static])

# Set langage to C++
AC_PROG_CXX
AC_LANG([C++])

# Checks for other programs
AC_PROG_INSTALL

# Get compiler version and vendor
AX_COMPILER_VENDOR
AX_COMPILER_VERSION
AX_COMPARE_VERSION([$ax_cv_cxx_compiler_version], [eq1], [11], [cxx_compiler_version_11="yes"])

# Checks for compiler support
AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory])
AX_CXX_COMPILE_STDCXX([17], [noext], [optional])
AS_IF(
[test "x$ax_cv_cxx_compiler_vendor" = x"clang" && test "x$cxx_compiler_version_11" = x"yes"], [
	# Clang 11 crashes when compiling with -std=c++20
], [
	AX_CXX_COMPILE_STDCXX([20], [noext], [optional])
])

# If available, use -fvisibility=hidden to hide internal names
AX_APPEND_COMPILE_FLAGS([-fvisibility=hidden])

# If available, use -zdefs to improve linker consistency
AX_APPEND_LINK_FLAGS([-Wl,-z,defs])

AX_PTHREAD([], [AC_MSG_ERROR([pthread required.])])

# Add support for --disable-assert
AC_HEADER_ASSERT

# Defines
AC_DEFINE([BOOST_ERROR_CODE_HEADER_ONLY], [])
AC_DEFINE([BOOST_SYSTEM_NO_DEPRECATED], [])
AC_DEFINE([BOOST_ENABLE_ASSERT_DEBUG_HANDLER], [])

# This define disables support for 128-bit integers on fmt, currently unused by this
# program. This is required to fix build pipelines running on new Clang conan-io images.
# The issue seems related to GNU Libtool not adding a shared object to resolve __udivti3,
# when building in new images like conanio/clang14-ubuntu16.04, while is works on old
# images like conanio/clang9. Alternatively, it could be fixed also by adding -lgcc manually.
# Hopefully this workaround may also fix build issues on some other configurations.
AC_DEFINE([FMT_USE_INT128], [0])

# Check headers
AC_CHECK_HEADER([CAEN_FELib.h], [], [AC_MSG_ERROR(CAEN_FELib.h required.)])

# Checks for Boost 1.67.0 or greater
AX_BOOST_BASE([1.67.0], [], [AC_MSG_ERROR(Boost library >= 1.67.0 (headers only) required.)])

AC_CONFIG_FILES([
	Makefile
	src/Makefile
])

AC_OUTPUT
