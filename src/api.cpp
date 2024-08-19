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
*	\file		api.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "api.hpp"

#include <iterator>
#include <chrono>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/static_assert.hpp>
#include <spdlog/fmt/fmt.h>

#include "cpp-utility/scope_exit.hpp"
#include "cpp-utility/string_view.hpp"
#include "CAENDig2.h"
#include "client.hpp"
#include "discovery.hpp"
#include "global.hpp"
#include "handle.hpp"
#include "lib_definitions.hpp"
#include "lib_error.hpp"

using namespace std::literals;
using namespace caen::literals;

namespace caen {

namespace dig2 {

constexpr auto version_string = CAEN_DIG2_VERSION_STRING ""_sv;
BOOST_STATIC_ASSERT(version_string.size() < max_size::str::version); // equal is not fine due to null terminator character

std::string get_lib_info() {
	throw ex::not_yet_implemented(__func__);
}

std::string get_lib_version() {
	return std::string(version_string);
}

std::string device_discovery(int timeout) {
	const std::chrono::seconds timeout_s(timeout);
	return ssdp::get_ssdp_devices(timeout_s).dump();
}

void open(const std::string& url, std::uint32_t& user_handle) {

	const auto url_data = parse_url(url);

	const auto& clients = global::get_instance().get_clients();

	const auto it = boost::find(clients, nullptr);
	if (BOOST_UNLIKELY(it == clients.end()))
		throw ex::too_many_devices(fmt::format("unable to open {}: library can handle only {} devices", url_data._authority, clients.size()));

	const auto board = static_cast<handle::lib::board_t>(std::distance(clients.begin(), it));

	global::get_instance().create_client(board, url_data);

	// Now client can be accessed from global array. Arm destroy routine in case of error.
	scope_exit cleanup_routine([board] { global::get_instance().destroy_client(board); });

	const auto& client_ptr = global::get_instance().get_client(board);

	BOOST_ASSERT_MSG(client_ptr != nullptr, "invalid client");

	user_handle = handle::lib(board, client_ptr->get_digitizer_internal_handle()).client_handle();

	// Connection successful: release now the destroy routine because ex::bad_library_version is not an error
	cleanup_routine.release();

	if (!client_ptr->is_server_version_aligned())
		throw ex::bad_library_version("open succeeded but dig2-lib is old: there could be undefined behaviors. please update it."s);

}

void close(std::uint32_t handle) {
	auto h = handle::lib::get_if_used(handle);

	if (h.internal_handle() != h.get_client().get_digitizer_internal_handle())
		throw ex::invalid_argument("close allowed only on digitizer handle"s);

	global::get_instance().destroy_client(h.board());
}

std::string get_device_tree(std::uint32_t handle) {
	const auto h = handle::lib::get_if_used(handle);
	return h.get_client().get_device_tree(h.internal_handle());
}

std::vector<std::uint32_t> get_child_handles(std::uint32_t handle, const std::string& path) {
	const auto h = handle::lib::get_if_used(handle);
	const auto child_internal_handles = h.get_client().get_child_handles(h.internal_handle(), path);
	std::vector<std::uint32_t> res;
	boost::transform(child_internal_handles, std::back_inserter(res), [b = h.board()](auto internal_handle) {
		return handle::lib(b, internal_handle).client_handle();
	});
	return res;
}

std::uint32_t get_handle(std::uint32_t handle, const std::string& path) {
	const auto h = handle::lib::get_if_used(handle);
	const auto internal_handle = h.get_client().get_handle(h.internal_handle(), path);
	return handle::lib(h.board(), internal_handle).client_handle();
}

std::uint32_t get_parent_handle(std::uint32_t handle, const std::string& path) {
	const auto h = handle::lib::get_if_used(handle);
	const auto internal_handle = h.get_client().get_parent_handle(h.internal_handle(), path);
	return handle::lib(h.board(), internal_handle).client_handle();
}

std::string get_path(std::uint32_t handle) {
	const auto h = handle::lib::get_if_used(handle);
	return h.get_client().get_path(h.internal_handle());
}

std::pair<std::string, ::CAEN_FELib_NodeType_t> get_node_properties(std::uint32_t handle, const std::string& path) {
	const auto h = handle::lib::get_if_used(handle);
	return h.get_client().get_node_properties(h.internal_handle(), path);
}

std::string get_value(std::uint32_t handle, const std::string& path, const std::string& arg) {
	const auto h = handle::lib::get_if_used(handle);
	return h.get_client().get_value(h.internal_handle(), path, arg);
}

void set_value(std::uint32_t handle, const std::string& path, const std::string& value) {
	const auto h = handle::lib::get_if_used(handle);
	h.get_client().set_value(h.internal_handle(), path, value);
}

void send_command(std::uint32_t handle, const std::string& path) {
	const auto h = handle::lib::get_if_used(handle);
	h.get_client().send_command(h.internal_handle(), path);
}

std::uint32_t get_user_register(std::uint32_t handle, std::uint32_t address) {
	const auto h = handle::lib::get_if_used(handle);
	return h.get_client().get_user_register(h.internal_handle(), address);
}

void set_user_register(std::uint32_t handle, std::uint32_t address, std::uint32_t value) {
	const auto h = handle::lib::get_if_used(handle);
	h.get_client().set_user_register(h.internal_handle(), address, value);
}

void set_data_format(std::uint32_t handle, const std::string& format) {
	const auto h = handle::lib::get_if_used(handle);
	h.get_client().set_data_format(h.internal_handle(), format);
}

void read_data(std::uint32_t handle, int timeout, std::va_list* args) {
	const auto h = handle::lib::get_if_used(handle);
	h.get_client().read_data(h.internal_handle(), timeout, args);
}

void has_data(uint32_t handle, int timeout) {
	const auto h = handle::lib::get_if_used(handle);
	h.get_client().has_data(h.internal_handle(), timeout);
}

} // namespace dig2

} // namespace caen
