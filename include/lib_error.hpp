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
*	\file		lib_error.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_LIB_ERROR_HPP_
#define CAEN_INCLUDE_LIB_ERROR_HPP_

#include <exception>
#include <stdexcept>
#include <string>

#include "lib_definitions.hpp"

namespace caen {

namespace dig2 {

namespace ex {

using namespace std::string_literals;

struct runtime_error : public std::runtime_error {
	using std::runtime_error::runtime_error;
};

struct not_enabled : public std::logic_error {
	not_enabled() : logic_error("not enabled"s) {}
};

struct timeout : public ex::runtime_error {
	timeout() : runtime_error("timeout"s) {}
};

struct stop : public ex::runtime_error {
	stop() : runtime_error("stop"s) {}
};

struct not_yet_implemented : public runtime_error {
	using ex::runtime_error::runtime_error;
};

struct invalid_argument : public std::invalid_argument {
	using std::invalid_argument::invalid_argument;
};

struct domain_error : public std::domain_error {
	using std::domain_error::domain_error;
};

struct invalid_handle : public std::invalid_argument {
	explicit invalid_handle(handle::client_handle_t handle) : invalid_argument(std::to_string(handle)), _handle{handle} {}
	handle::client_handle_t handle() const noexcept { return _handle; }
private:
	const handle::client_handle_t _handle;
};

struct command_error : public ex::runtime_error {
	using ex::runtime_error::runtime_error;
};

struct communication_error : public ex::runtime_error {
	using ex::runtime_error::runtime_error;
};

struct device_not_found : public ex::runtime_error {
	using ex::runtime_error::runtime_error;
};

struct too_many_devices : public ex::runtime_error {
	using ex::runtime_error::runtime_error;
};

struct bad_library_version : public ex::runtime_error {
	using ex::runtime_error::runtime_error;
};

} // namespace ex

/**
 * @brief UDL to generate ex::runtime_error with compile-time defined message.
 * 
 * Example:
 * @code
 * throw "generic error"_ex;
 * @endcode
 */
inline auto operator""_ex(const char* str, std::size_t len) {
	return ex::runtime_error(std::string(str, len));
}

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_LIB_ERROR_HPP_ */
