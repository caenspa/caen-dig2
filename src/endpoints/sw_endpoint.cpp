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
*	\file		sw_endpoint.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "endpoints/sw_endpoint.hpp"

#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "client.hpp"

using namespace caen::literals;
using namespace std::literals;

namespace caen {

namespace dig2 {

namespace ep {

struct sw_endpoint::endpoint_impl {
	explicit endpoint_impl(handle::internal_handle_t active_endpoint_handle) noexcept
		: _active_endpoint_handle{active_endpoint_handle} {
	}
	const handle::internal_handle_t _active_endpoint_handle;
};

sw_endpoint::sw_endpoint(client& client, handle::internal_handle_t endpoint_handle)
	: endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>(get_client().get_handle(get_client().get_digitizer_internal_handle(), "/endpoint/par/activeendpoint"s))} {}

sw_endpoint::~sw_endpoint() = default;

bool sw_endpoint::is_decode_disabled() {
	const auto active_endpoint_s = get_client().get_value(_pimpl->_active_endpoint_handle, std::string{});
	return caen::string::iequals(active_endpoint_s, "raw"_sv);
}

} // namespace ep

} // namespace dig2

} // namespace caen
