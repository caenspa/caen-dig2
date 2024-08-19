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

lib_LTLIBRARIES = libCAEN_Dig2.la
libCAEN_Dig2_la_SOURCES = \
	api.cpp \
	CAENDig2.cpp \
	discovery.cpp \
	client.cpp \
	last_error.cpp \
	library_logger.cpp \
	endpoints/endpoint.cpp \
	endpoints/hw_endpoint.cpp \
	endpoints/sw_endpoint.cpp \
	endpoints/aggregate_endpoint.cpp \
	endpoints/dpppha.cpp \
	endpoints/dpppsd.cpp \
	endpoints/dppzle.cpp \
	endpoints/events.cpp \
	endpoints/opendpp.cpp \
	endpoints/raw.cpp \
	endpoints/rawudp.cpp \
	endpoints/scope.cpp
libCAEN_Dig2_la_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-isystem $(top_srcdir)/spdlog/include \
	-isystem $(top_srcdir)/json/include \
	-isystem $(top_srcdir)/backend-server/include \
	$(BOOST_CPPFLAGS)
libCAEN_Dig2_la_CXXFLAGS = \
	$(PTHREAD_CFLAGS)
libCAEN_Dig2_la_LIBADD = \
	$(PTHREAD_LIBS)
libCAEN_Dig2_la_LDFLAGS = \
	-version-info 0:0:0
