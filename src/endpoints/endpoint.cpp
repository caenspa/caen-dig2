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
*	\file		endpoint.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "endpoints/endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct endpoint::endpoint_impl {
	endpoint_impl(client& client, handle::internal_handle_t endpoint_server_handle) noexcept
		: _client{client}
		, _endpoint_server_handle{endpoint_server_handle} {}
	client& _client;
	const handle::internal_handle_t _endpoint_server_handle;
};

endpoint::endpoint(client& client, handle::internal_handle_t endpoint_server_handle)
	: _pimpl{std::make_unique<endpoint_impl>(client, endpoint_server_handle)} {}

endpoint::~endpoint() = default;

handle::internal_handle_t endpoint::get_endpoint_server_handle() const noexcept {
	return _pimpl->_endpoint_server_handle;
}

client& endpoint::get_client() const noexcept {
	return _pimpl->_client;
}

} // namespace ep

} // namespace dig2

} // namespace caen
