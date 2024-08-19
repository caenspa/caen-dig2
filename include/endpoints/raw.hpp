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
*	\file		raw.hpp
*	\brief		Raw endpoint
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_RAW_HPP_
#define CAEN_INCLUDE_ENDPOINTS_RAW_HPP_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "endpoints/hw_endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct sw_endpoint; // forward declaration

struct raw final : public hw_endpoint {

	enum class names { // overrides endpoint::names
		UNKNOWN,
		DATA,
		SIZE,
		N_EVENTS,
	};

	using args_list_t = utility::args_list_t<names, types>;

	raw(client& client, handle::internal_handle_t endpoint_handle);
	~raw();

	void set_max_size_getter(std::function<std::size_t()> f);
	void set_is_decoded_getter(std::function<bool()> f);

	void register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) override;
	void set_data_format(const std::string& json_format) override;
	void read_data(timeout_t timeout, std::va_list* args) override;
	void has_data(timeout_t timeout) override;
	void clear_data() override;
	void arm_acquisition() override;
	void disarm_acquisition() override;

	void event_start() override;
	void event_stop() override;

	static args_list_t default_data_format();
	static std::size_t data_format_dimension(names name);

private:

	struct endpoint_impl;
	std::unique_ptr<endpoint_impl> _pimpl;

};

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(raw::names, {
	{ raw::names::UNKNOWN, 		nullptr		},
	{ raw::names::DATA, 		"DATA"s		},
	{ raw::names::SIZE, 		"SIZE"s		},
	{ raw::names::N_EVENTS,		"N_EVENTS"s	},
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_RAW_HPP_ */
