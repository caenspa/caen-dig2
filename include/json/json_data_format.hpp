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
*	\file		json_data_format.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_DATA_FORMAT_HPP_
#define CAEN_INCLUDE_JSON_JSON_DATA_FORMAT_HPP_

#include <type_traits>

#include <nlohmann/json.hpp>

#include <json/json_utilities.hpp>

#include "endpoints/endpoint.hpp"

namespace caen {

namespace dig2 {

using namespace caen::literals;

template <typename Endpoint>
struct json_data_format {

	/**
	 * Default constructor needed by nlohmann's JSON.
	 */
	json_data_format()
		: _name{names_type::UNKNOWN}
		, _type{types_type::UNKNOWN}
		, _dim{} {
	}

	/**
	 * Convert JSON to json_data_format
	 * @param args any input of nlohmann::json::parse representing a JSON
	 * @return an instance of json_cmd parsing the input content
	 */
	template <typename... Args>
	static json_data_format marshal(Args&& ...args) {
		return nlohmann::json::parse(std::forward<Args>(args)...).template get<json_data_format>();
	}

	/**
	 * Convert json_data_format to JSON
	 * @return the JSON with no indentation
	 */
	nlohmann::json::string_t unmarshal() const {
		return nlohmann::json(*this).dump();
	}

	auto get_name() const noexcept { return _name; }
	auto get_type() const noexcept { return _type; }
	auto get_dim() const noexcept { return _dim; }

	static constexpr auto& key_name() noexcept { return "name"; }
	static constexpr auto& key_type() noexcept { return "type"; }
	static constexpr auto& key_dim() noexcept { return "dim"; }

	friend void from_json(const nlohmann::json& j, json_data_format& e) {
		caen::json::get_if_not_null(j, key_name(), e._name);
		caen::json::get_if_not_null(j, key_type(), e._type);
		caen::json::get_if_not_null(j, key_dim(), e._dim);
	}

	friend void to_json(nlohmann::json& j, const json_data_format& e) {
		caen::json::set(j, key_name(), e._name);
		caen::json::set(j, key_type(), e._type);
		caen::json::set(j, key_dim(), e._dim);
	}

private:

	using traits = ep::utility::endpoint_traits<Endpoint>;
	using names_type = typename traits::names_type;
	using types_type = typename traits::types_type;

	names_type _name;
	types_type _type;
	std::size_t _dim;

};

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_JSON_JSON_DATA_FORMAT_HPP_ */
