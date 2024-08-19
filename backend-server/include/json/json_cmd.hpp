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
*	TheCAEN Back-end Server is distributed in the hope that it will be useful,
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
*	\file		json_cmd.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_CMD_HPP_
#define CAEN_INCLUDE_JSON_JSON_CMD_HPP_

#include <nlohmann/json.hpp>

#include <string>

#include "json/json_common.hpp"
#include "json/json_utilities.hpp"

struct json_cmd {

	/**
	 * Generator of json_cmd
	 * @param cmd
	 * @param handle
	 * @param query
	 * @param value
	 * @return an instance of json_cmd
	 */
	template <typename QueryT = cmd::query_t, typename ValueT = cmd::value_t>
	static json_cmd build(cmd::command cmd, cmd::handle_t handle, QueryT&& query = QueryT{}, ValueT&& value = ValueT{}) {
		json_cmd r;
		r._cmd = cmd;
		r._handle = handle;
		r._query = std::forward<QueryT>(query);
		r._value = std::forward<ValueT>(value);
		return r;
	}

	/**
	 * Default constructor needed by nlohmann's JSON.
	 */
	json_cmd()
	: _cmd{cmd::command::UNKNOWN}
	, _handle{}
	, _query{}
	, _multiple_query{}
	, _value{}
	, _multiple_value{} {
	}

	/**
	 * Convert JSON to json_cmd
	 * @param args any input of nlohmann::json::parse representing a JSON
	 * @return an instance of json_cmd parsing the input content
	 */
	template <typename... Args>
	static json_cmd marshal(Args&& ...args) {
		return nlohmann::json::parse(std::forward<Args>(args)...).template get<json_cmd>();
	}

	/**
	 * Convert json_cmd to JSON
	 * @return the JSON with no indentation
	 */
	nlohmann::json::string_t unmarshal() const {
		return nlohmann::json(*this).dump();
	}

	cmd::command get_cmd() const noexcept { return _cmd; }
	cmd::handle_t get_handle() const noexcept { return _handle; }
	const cmd::query_t& get_query() const noexcept { return _query; }
	const cmd::multiple_query_t& get_multiple_query() const noexcept { return _multiple_query; }
	const cmd::value_t& get_value() const noexcept { return _value; }
	const cmd::multiple_value_t& get_multiple_value() const noexcept { return _multiple_value; }

	static constexpr auto& key_cmd() noexcept { return "cmd"; }
	static constexpr auto& key_handle() noexcept { return "handle"; }
	static constexpr auto& key_query() noexcept { return "query"; }
	static constexpr auto& key_multiple_query() noexcept { return "multipleQuery"; }
	static constexpr auto& key_value() noexcept { return "value"; }
	static constexpr auto& key_multiple_value() noexcept { return "multipleValue"; }

	friend void from_json(const nlohmann::json& j, json_cmd& e) {
		caen::json::get_if_not_null(j, key_cmd(), e._cmd);
		caen::json::get_if_not_null(j, key_handle(), e._handle);
		caen::json::get_if_not_null(j, key_query(), e._query);
		caen::json::get_if_not_null(j, key_multiple_query(), e._multiple_query);
		caen::json::get_if_not_null(j, key_value(), e._value);
		caen::json::get_if_not_null(j, key_multiple_value(), e._multiple_value);
	}

	friend void to_json(nlohmann::json& j, const json_cmd& e) {
		caen::json::set(j, key_cmd(), e._cmd);
		caen::json::set(j, key_handle(), e._handle);
		caen::json::set(j, key_query(), e._query);
		caen::json::set(j, key_multiple_query(), e._multiple_query);
		caen::json::set(j, key_value(), e._value);
		caen::json::set(j, key_multiple_value(), e._multiple_value);
	}

private:

	cmd::command _cmd;
	cmd::handle_t _handle;
	cmd::query_t _query;
	cmd::multiple_query_t _multiple_query;
	cmd::value_t _value;
	cmd::multiple_value_t _multiple_value;

};

namespace cmd {

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(command, {
	{ command::UNKNOWN, 			nullptr 				},
	{ command::CONNECT,				"connect"s				},
	{ command::GET_DEVICE_TREE,		"getDeviceTree"s		},
	{ command::GET_CHILD_HANDLES,	"getChildHandles"s		},
	{ command::GET_HANDLE,			"getHandle"s			},
	{ command::GET_PARENT_HANDLE,	"getParentHandle"s		},
	{ command::GET_PATH,			"getPath"s				},
	{ command::GET_NODE_PROPERTIES,	"getNodeProperties"s	},
	{ command::GET_VALUE,			"getValue"s				},
	{ command::MULTI_GET_VALUE,		"multiGetValue"s		},
	{ command::SET_VALUE,			"setValue"s				},
	{ command::MULTI_SET_VALUE,		"multiSetValue"s		},
	{ command::SEND_COMMAND,		"sendCommand"s			},
})

} // namespace cmd

#endif /* CAEN_INCLUDE_JSON_JSON_CMD_HPP_ */
