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
*	\file		client.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CLIENT_HPP_
#define CAEN_INCLUDE_CLIENT_HPP_

#include <cstdarg>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/ip/address.hpp>
#include <boost/core/noncopyable.hpp>

#include <CAEN_FELib.h>

#include "cpp-utility/optional.hpp"
#include "endpoints/endpoint.hpp"
#include "library_logger.hpp"
#include "lib_definitions.hpp"

namespace caen {

namespace dig2 {

struct url_data {

	// Standard URI components (RFC 2396)
	std::string _scheme;
	std::string _authority;
	std::string _path;
	std::string _query;
	std::string _fragment;

	// Custom query fields
	caen::optional<bool> _monitor;
	caen::optional<spdlog::level::level_enum> _log_level;
	caen::optional<std::string> _pid;
	caen::optional<int> _keepalive;
	caen::optional<int> _rcvbuf;
	caen::optional<int> _receiver_thread_affinity;

};

url_data parse_url(const std::string& url);

struct client : private boost::noncopyable {

	client(const url_data& data);

	~client();

	std::string get_device_tree(handle::internal_handle_t handle);
	std::vector<handle::internal_handle_t> get_child_handles(handle::internal_handle_t handle, const std::string& path);
	handle::internal_handle_t get_handle(handle::internal_handle_t handle, const std::string& path);
	handle::internal_handle_t get_parent_handle(handle::internal_handle_t handle, const std::string& path);
	std::string get_path(handle::internal_handle_t handle);
	std::pair<std::string, ::CAEN_FELib_NodeType_t> get_node_properties(handle::internal_handle_t handle, const std::string& path);
	std::string get_value(handle::internal_handle_t handle, const std::string& path, const std::string& arg = std::string{});
	void set_value(handle::internal_handle_t handle, const std::string& path, const std::string& value);
	void send_command(handle::internal_handle_t handle, const std::string& path);
	std::uint32_t get_user_register(handle::internal_handle_t handle, std::uint32_t address);
	void set_user_register(handle::internal_handle_t handle, std::uint32_t address, std::uint32_t value);
	void set_data_format(handle::internal_handle_t handle, const std::string& format);
	void read_data(handle::internal_handle_t handle, int timeout, std::va_list* args);
	void has_data(handle::internal_handle_t handle, int timeout);
	bool is_monitor() const noexcept;

	const url_data& get_url_data() const noexcept;
	const boost::asio::ip::address& get_address() const noexcept;
	const boost::asio::ip::address& get_endpoint_address() const noexcept;
	void register_endpoint(std::shared_ptr<ep::endpoint> ep);
	const std::list<std::shared_ptr<ep::endpoint>>& get_endpoint_list() const noexcept;
	handle::internal_handle_t get_digitizer_internal_handle() const noexcept;
	bool is_server_version_aligned() const noexcept;
	std::size_t get_n_channels() const noexcept;
	double get_sampling_period_ns() const noexcept;

private:

	struct client_impl; // forward declaration
	std::unique_ptr<client_impl> _pimpl;

};

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_CLIENT_HPP_ */
