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
*	\file		hw_endpoint.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "endpoints/hw_endpoint.hpp"

#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "client.hpp"

using namespace caen::literals;
using namespace std::literals;

namespace caen {

namespace dig2 {

namespace ep {

struct hw_endpoint::endpoint_impl {
	explicit endpoint_impl() noexcept {
	}
};

hw_endpoint::hw_endpoint(client& client, handle::internal_handle_t endpoint_handle)
	: endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>()} {}

hw_endpoint::~hw_endpoint() = default;

} // namespace ep

} // namespace dig2

} // namespace caen
