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
*	This file is part of the CAEN Dig2 Library.
*
*	The CAEN Dig2 Library is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	The CAEN Dig2 Library is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Dig2 Library; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		lib_definitions.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_LIB_DEFINITIONS_HPP_
#define CAEN_INCLUDE_LIB_DEFINITIONS_HPP_

#include <cstdint>
#include <cstdlib>
#include <limits>

#include <json/json_common.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/integer.hpp"

namespace caen {

namespace dig2 {

namespace handle {

static constexpr std::size_t server_handle_bits{cmd::handle_bits}; // from json_common.hpp

using client_handle_t = cmd::handle_t;
using internal_handle_t = caen::uint_t<server_handle_bits>::fast;

static constexpr std::size_t board_bits{caen::bit::bit_size<client_handle_t>::value - server_handle_bits};
static constexpr internal_handle_t invalid_server_handle{cmd::max_handle};

} // namespace handle

namespace max_size {

static constexpr std::size_t devices{1 << handle::board_bits};

namespace str {

static constexpr std::size_t version{16};
static constexpr std::size_t error_name{32};
static constexpr std::size_t error_description{256};
static constexpr std::size_t last_error_description{1024};
static constexpr std::size_t node_name{32};
static constexpr std::size_t value{256};
static constexpr std::size_t path{256};

} // namespace str

} // namespace max_size

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_LIB_DEFINITIONS_HPP_ */
