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

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
dist_noinst_HEADERS = \
	include \
	json/include \
	spdlog/include \
	backend-server/include/server_definitions.hpp \
	backend-server/include/json
EXTRA_DIST = \
	COPYING \
	COPYING.LESSER \
	INSTALL \
	README_LINUX.txt \
	CHANGELOG \
	json/LICENSE.MIT \
	json/README.md \
	spdlog/LICENSE \
	spdlog/README.md \
	demo
dig2docdir = $(docdir)
dist_dig2doc_DATA = \
	doc/demo.tar.gz
