/******************************************************************************
*
*	CAEN SpA - Software Division
*	Via Vetraia, 11 - 55049 - Viareggio ITALY
*	+39 0594 388 398 - www.caen.it
*
*******************************************************************************
*
*	Copyright (C) 2020-2023 CAEN SpA
*
*	This file is part of the CAEN Back-end Server.
*
*	The CAEN Back-end Server is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	TheCAEN Back-end Server is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Back-end Server; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		server_definitions.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_SERVER_DEFINITIONS_HPP_
#define CAEN_INCLUDE_SERVER_DEFINITIONS_HPP_

#define BACKEND_SERVER_STR_HELPER(S)	#S
#define BACKEND_SERVER_STR(S)			BACKEND_SERVER_STR_HELPER(S)

#define BACKEND_SERVER_VERSION_MAJOR	1
#define BACKEND_SERVER_VERSION_MINOR	4
#define BACKEND_SERVER_VERSION_PATCH	2
#define BACKEND_SERVER_VERSION			(BACKEND_SERVER_VERSION_MAJOR * 10000) + (BACKEND_SERVER_VERSION_MINOR * 100) + (BACKEND_SERVER_VERSION_PATCH)
#define BACKEND_SERVER_VERSION_STRING	BACKEND_SERVER_STR(BACKEND_SERVER_VERSION_MAJOR) "." BACKEND_SERVER_STR(BACKEND_SERVER_VERSION_MINOR) "." BACKEND_SERVER_STR(BACKEND_SERVER_VERSION_PATCH)

#ifdef __cplusplus // to expose previous code also in C

#include <cstddef>

namespace server_definitions {

constexpr unsigned int version{BACKEND_SERVER_VERSION};
constexpr std::size_t header_size{1 << 5};
constexpr unsigned short command_port{0xcae0};
constexpr unsigned short rest_port{0xcae2};
constexpr unsigned short udp_port{0xcae4};

} // namespace server_definitions

#endif

#endif /* CAEN_INCLUDE_SERVER_DEFINITIONS_HPP_ */
