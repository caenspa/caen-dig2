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
*	\file		api.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_API_HPP_
#define CAEN_INCLUDE_API_HPP_

#include <cstdarg>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <CAEN_FELib.h>

namespace caen {

namespace dig2 {

std::string get_lib_info();

std::string get_lib_version();

std::string device_discovery(int timeout);

void open(const std::string& url, std::uint32_t& user_handle);

void close(std::uint32_t handle);

std::string get_device_tree(std::uint32_t handle);

std::vector<std::uint32_t> get_child_handles(std::uint32_t handle, const std::string& path);

std::uint32_t get_handle(std::uint32_t handle, const std::string& path);

std::uint32_t get_parent_handle(std::uint32_t handle, const std::string& path);

std::string get_path(std::uint32_t handle);

std::pair<std::string, ::CAEN_FELib_NodeType_t> get_node_properties(std::uint32_t handle, const std::string& path);

std::string get_value(std::uint32_t handle, const std::string& path, const std::string& arg = std::string{});

void set_value(std::uint32_t handle, const std::string& path, const std::string& value);

void send_command(std::uint32_t handle, const std::string& path);

std::uint32_t get_user_register(std::uint32_t handle, std::uint32_t address);

void set_user_register(std::uint32_t handle, std::uint32_t address, std::uint32_t value);

void set_data_format(std::uint32_t handle, const std::string& format);

void read_data(uint32_t handle, int timeout, va_list* args);

void has_data(uint32_t handle, int timeout);

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_API_HPP_ */
