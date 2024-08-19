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
*	\file		json_common.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_COMMON_HPP_
#define CAEN_INCLUDE_JSON_JSON_COMMON_HPP_

struct json_cmd; // forward declaration
struct json_answer; // forward declaration

#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <utility>
#include <climits>

#include <CAEN_FELib.h>

namespace cmd {

enum class command {
	UNKNOWN,
	CONNECT,
	GET_DEVICE_TREE,
	GET_HANDLE,
	GET_CHILD_HANDLES,
	GET_PARENT_HANDLE,
	GET_PATH,
	GET_NODE_PROPERTIES,
	GET_VALUE,
	MULTI_GET_VALUE,
	SET_VALUE,
	MULTI_SET_VALUE,
	SEND_COMMAND,
};

using handle_t = std::uint32_t;
using query_t = std::string;
using multiple_query_t = std::vector<query_t>;
using value_t = std::string;
using multiple_value_t = std::vector<value_t>;

static constexpr std::size_t handle_bits{24};
static constexpr handle_t max_handle{(handle_t{1} << handle_bits) - 1}; // reserved use

static_assert(handle_bits < sizeof(handle_t) * CHAR_BIT, "handle_t is too small to store handle_bits");

} // namespace cmd

namespace answer {

enum class flag {
	UNKNOWN,
	ARM,
	DISARM,
	CLEAR,
	RESET,
};

using single_value_t = std::string;
using single_value_provider = std::function<single_value_t()>;
using value_t = std::vector<single_value_t>;
using value_provider = std::function<value_t()>;
using flag_value_t = std::pair<flag, value_t>;
using flag_value_provider = std::function<flag_value_t()>;

} // namespace answer

namespace nt {

using node_type = ::CAEN_FELib_NodeType_t;

} // namespace nt

#endif /* CAEN_INCLUDE_JSON_JSON_COMMON_HPP_ */
