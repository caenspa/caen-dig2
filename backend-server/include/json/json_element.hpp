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
*	This file is part of the CAEN Back-end Server.
*
*	The CAEN Back-end Server is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	The CAEN Back-end Server is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Back-end Server; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		json_element.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_ELEMENT_HPP_
#define CAEN_INCLUDE_JSON_JSON_ELEMENT_HPP_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "json/json_utilities.hpp"
#include "json/json_element_fwd.hpp"

using namespace std::literals;

struct json_element {

	json_element()
	: _name{}
	, _is_visible{}
	, _description{}
	, _node_type{}
	, _access_mode{}
	, _level{}
	, _data_type{}
	, _index_string{}
	, _index{}
	, _default_value{}
	, _min_value{}
	, _max_value{}
	, _increment{}
	, _multiple_value{}
	, _allowed_values{}
	, _uom{}
	, _exp_uom{}
	, _set_in_run{} {
	};

	/**
	 * Convert JSON to json_element
	 * @param args any input of nlohmann::json::parse representing a JSON
	 * @return an instance of json_cmd parsing the input content
	 */
	template <typename... Args>
	static json_element marshal(Args&& ...args) {
		return nlohmann::json::parse(std::forward<Args>(args)...).template get<json_element>();
	}

	/**
	 * Convert json_element to JSON
	 * @return the JSON with no indentation
	 */
	nlohmann::json::string_t unmarshal() const {
		return nlohmann::json(*this).dump();
	}

	static inline constexpr auto key_name = "name"sv;
	static inline constexpr auto key_description = "description"sv;
	static inline constexpr auto key_node_type = "nodeType"sv;
	static inline constexpr auto key_access_mode = "accessMode"sv;
	static inline constexpr auto key_level = "level"sv;
	static inline constexpr auto key_data_type = "dataType"sv;
	static inline constexpr auto key_index_string = "indexString"sv;
	static inline constexpr auto key_index = "index"sv;
	static inline constexpr auto key_default_value = "defaultValue"sv;
	static inline constexpr auto key_min_value = "minValue"sv;
	static inline constexpr auto key_max_value = "maxValue"sv;
	static inline constexpr auto key_increment = "increment"sv;
	static inline constexpr auto key_multiple_value = "multipleValue"sv;
	static inline constexpr auto key_allowed_values = "allowedValues"sv;
	static inline constexpr auto key_uom = "UOM"sv;
	static inline constexpr auto key_exp_uom = "ExpUOM"sv;
	static inline constexpr auto key_set_in_run = "setInRun"sv;
	static inline constexpr auto key_is_visible = "isVisible"sv;
	static inline constexpr auto key_arg_in_get = "argInGet"sv;

	friend void from_json(const nlohmann::json& j, json_element& e) {
		caen::json::get(j, key_name, e._name);
		caen::json::get(j, key_is_visible, e._is_visible);
		caen::json::get_if_not_null(j, key_description, e._description);
		caen::json::get_if_not_null(j, key_node_type, e._node_type);
		caen::json::get_if_not_null(j, key_access_mode, e._access_mode);
		caen::json::get_if_not_null(j, key_level, e._level);
		caen::json::get_if_not_null(j, key_data_type, e._data_type);
		caen::json::get_if_not_null(j, key_index_string, e._index_string);
		caen::json::get_if_not_null(j, key_index, e._index);
		caen::json::get_if_not_null(j, key_default_value, e._default_value);
		caen::json::get_if_not_null(j, key_min_value, e._min_value);
		caen::json::get_if_not_null(j, key_max_value, e._max_value);
		caen::json::get_if_not_null(j, key_increment, e._increment);
		caen::json::get_if_not_null(j, key_allowed_values, e._allowed_values);
		caen::json::get_if_not_null(j, key_multiple_value, e._multiple_value);
		caen::json::get_if_not_null(j, key_uom, e._uom);
		caen::json::get_if_not_null(j, key_exp_uom, e._exp_uom);
		caen::json::get_if_not_null(j, key_set_in_run, e._set_in_run);
		caen::json::get_if_not_null(j, key_arg_in_get, e._arg_in_get);
	}

	friend void to_json(nlohmann::json& j, const json_element& e) {
		caen::json::set(j, key_name, e._name);
		caen::json::set(j, key_is_visible, e._is_visible);
		caen::json::set(j, key_description, e._description);
		caen::json::set(j, key_node_type, e._node_type);
		caen::json::set(j, key_access_mode, e._access_mode);
		caen::json::set(j, key_level, e._level);
		caen::json::set(j, key_data_type, e._data_type);
		caen::json::set(j, key_index_string, e._index_string);
		caen::json::set(j, key_index, e._index);
		caen::json::set(j, key_default_value, e._default_value);
		caen::json::set(j, key_min_value, e._min_value);
		caen::json::set(j, key_max_value, e._max_value);
		caen::json::set(j, key_increment, e._increment);
		caen::json::set(j, key_multiple_value, e._multiple_value);
		caen::json::set(j, key_allowed_values, e._allowed_values);
		caen::json::set(j, key_uom, e._uom);
		caen::json::set(j, key_exp_uom, e._exp_uom);
		caen::json::set(j, key_set_in_run, e._set_in_run);
		caen::json::set(j, key_arg_in_get, e._arg_in_get);
	}

	// Getters
	auto& get_name() const noexcept { return _name; }
	auto get_is_visible() const noexcept { return _is_visible; }
	auto& get_description() const noexcept { return _description; }
	auto& get_node_type() const noexcept { return _node_type; }
	auto& get_access_mode() const noexcept { return _access_mode; }
	auto& get_level() const noexcept { return _level; }
	auto& get_data_type() const noexcept { return _data_type; }
	auto& get_index() const noexcept { return _index; }
	auto& get_index_string() const noexcept { return _index_string; }
	auto& get_default_value() const noexcept { return _default_value; }
	auto& get_min_value() const noexcept { return _min_value; }
	auto& get_max_value() const noexcept { return _max_value; }
	auto& get_increment() const noexcept { return _increment; }
	auto& get_multiple_value() const noexcept { return _multiple_value; }
	auto& get_allowed_values() const noexcept { return _allowed_values; }
	auto& get_uom() const noexcept { return _uom; }
	auto& get_exp_uom() const noexcept { return _exp_uom; }
	auto& get_set_in_run() const noexcept { return _set_in_run; }
	auto& get_arg_in_get() const noexcept { return _arg_in_get; }

private:

	std::string _name;
	bool _is_visible;

	std::optional<std::string> _description;
	std::optional<element::node_type> _node_type;
	std::optional<element::access_mode> _access_mode;
	std::optional<element::level> _level;
	std::optional<element::data_type> _data_type;
	std::optional<std::vector<std::string>> _index_string;
	std::optional<std::vector<int>> _index;
	std::optional<std::string> _default_value;
	std::optional<double> _min_value;
	std::optional<double> _max_value;
	std::optional<double> _increment;
	std::optional<bool> _multiple_value;
	std::optional<std::vector<std::string>> _allowed_values;
	std::optional<std::string> _uom;
	std::optional<int> _exp_uom;
	std::optional<bool> _set_in_run;
	std::optional<bool> _arg_in_get;

};

namespace element {

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(node_type, {
	{ node_type::UNKNOWN, 			nullptr				},
	{ node_type::PARAMETER,			"PARAMETER"s		},
	{ node_type::CMD,				"CMD"s				},
	{ node_type::ENDPOINT,			"ENDPOINT"s			},
	{ node_type::FEATURE,			"FEATURE"s			},
})

NLOHMANN_JSON_SERIALIZE_ENUM(data_type, {
	{ data_type::UNKNOWN, 			nullptr				},
	{ data_type::STRING,			"STRING"s			},
	{ data_type::NUMBER,			"NUMBER"s			},
})

NLOHMANN_JSON_SERIALIZE_ENUM(access_mode, {
	{ access_mode::UNKNOWN, 		nullptr				},
	{ access_mode::READ_ONLY,		"READ_ONLY"s		},
	{ access_mode::WRITE_ONLY,		"WRITE_ONLY"s		},
	{ access_mode::READ_WRITE,		"READ_WRITE"s		},
})

NLOHMANN_JSON_SERIALIZE_ENUM(level, {
	{ level::UNKNOWN, 				nullptr				},
	{ level::DIGITIZER,				"DIG"s				},
	{ level::CHANNEL,				"CH"s				},
	{ level::LVDS,					"LVDS"s				},
	{ level::VGA,					"VGA"s				},
	{ level::ENDPOINT,				"ENDPOINT"s			},
	{ level::FOLDER,				"FOLDER"s			},
	{ level::GROUP,					"GROUP"s			},
})

} // namespace element

#endif /* CAEN_INCLUDE_JSON_JSON_ELEMENT_HPP_ */
