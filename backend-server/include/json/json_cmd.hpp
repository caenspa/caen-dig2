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
*	\file		json_cmd.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_CMD_HPP_
#define CAEN_INCLUDE_JSON_JSON_CMD_HPP_

#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "cpp-utility/serdes.hpp"
#include "cpp-utility/vector.hpp"
#include "json/json_common.hpp"
#include "json/json_utilities.hpp"

using namespace std::literals;

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
	 * Generator of a MULTIPLE json_cmd carrying a batch of sub-commands executed in order.
	 * Each sub-command carries its own handle; the envelope handle is unused by the backend.
	 * The backend runs every sub-command (best-effort) and returns one answer per sub-command.
	 * @param handle envelope handle (kept for symmetry with build())
	 * @param subs sub-commands to execute, in submitted order
	 * @return an instance of json_cmd of type MULTIPLE
	 */
	static json_cmd build_multiple(cmd::handle_t handle, std::vector<json_cmd> subs) {
		json_cmd r;
		r._cmd = cmd::command::MULTIPLE;
		r._handle = handle;
		r._multiple = std::move(subs);
		return r;
	}

	/**
	 * Default constructor needed by nlohmann's JSON.
	 */
	json_cmd()
		: _cmd{cmd::command::UNKNOWN}
		, _handle{}
		, _query{}
		, _value{}
		, _multiple{} {
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

	static inline constexpr std::size_t binary_header_size = sizeof(cmd::command) + sizeof(cmd::handle_t) + 2 * sizeof(std::uint64_t);

	static json_cmd from_binary(const caen::vector<std::byte>& data) {
		auto b_it = data.begin();
		// decode header
		const auto cmd = caen::serdes::deserialize<cmd::command>(b_it);
		const auto handle = caen::serdes::deserialize<cmd::handle_t>(b_it);
		const auto query_size = caen::serdes::deserialize<std::uint64_t>(b_it);
		const auto value_size = caen::serdes::deserialize<std::uint64_t>(b_it);
		BOOST_ASSERT(b_it == data.begin() + binary_header_size);
		// decode payload
		cmd::query_t query;
		query.reserve(query_size);
		std::transform(b_it, b_it + query_size, std::back_inserter(query), [](const auto& c) { return static_cast<char>(c); });
		b_it += query_size;
		cmd::value_t value;
		value.reserve(value_size);
		std::transform(b_it, b_it + value_size, std::back_inserter(value), [](const auto& c) { return static_cast<char>(c); });
		b_it += value_size;
		auto r = build(cmd, handle, std::move(query), std::move(value));
		// decode nested MULTIPLE sub-commands
		const auto multiple_count = caen::serdes::deserialize<std::uint64_t>(b_it);
		r._multiple.reserve(multiple_count);
		for (std::uint64_t i = 0; i < multiple_count; ++i) {
			const auto child_size = caen::serdes::deserialize<std::uint64_t>(b_it);
			caen::vector<std::byte> child_data(child_size);
			std::copy(b_it, b_it + child_size, child_data.begin());
			b_it += child_size;
			r._multiple.push_back(from_binary(child_data));
		}
		BOOST_ASSERT(b_it == data.end());
		return r;
	}

	caen::vector<std::byte> to_binary() const {
		// serialize the nested MULTIPLE sub-commands first, to know their sizes
		std::vector<caen::vector<std::byte>> children;
		children.reserve(_multiple.size());
		std::size_t children_size = 0;
		for (const auto& child : _multiple) {
			auto child_data = child.to_binary();
			children_size += sizeof(std::uint64_t) + child_data.size();
			children.push_back(std::move(child_data));
		}
		// create header
		const auto size = binary_header_size + _query.size() + _value.size() + sizeof(std::uint64_t) + children_size;
		caen::vector<std::byte> res(size);
		auto b_it = res.begin();
		caen::serdes::serialize<cmd::command>(b_it, _cmd);
		caen::serdes::serialize<cmd::handle_t>(b_it, _handle);
		caen::serdes::serialize<std::uint64_t>(b_it, _query.size());
		caen::serdes::serialize<std::uint64_t>(b_it, _value.size());
		BOOST_ASSERT(b_it == res.begin() + binary_header_size);
		// add payload
		b_it = std::transform(_query.begin(), _query.end(), b_it, [](const auto& c) { return static_cast<std::byte>(c); });
		b_it = std::transform(_value.begin(), _value.end(), b_it, [](const auto& c) { return static_cast<std::byte>(c); });
		// add nested MULTIPLE sub-commands
		caen::serdes::serialize<std::uint64_t>(b_it, _multiple.size());
		for (const auto& child_data : children) {
			caen::serdes::serialize<std::uint64_t>(b_it, child_data.size());
			b_it = std::copy(child_data.begin(), child_data.end(), b_it);
		}
		BOOST_ASSERT(b_it == res.end());
		return res;
	}

	cmd::command get_cmd() const noexcept { return _cmd; }
	cmd::handle_t get_handle() const noexcept { return _handle; }
	const cmd::query_t& get_query() const noexcept { return _query; }
	const cmd::value_t& get_value() const noexcept { return _value; }
	const std::vector<json_cmd>& get_multiple() const noexcept { return _multiple; }

	friend void from_json(const nlohmann::json& j, json_cmd& e) {
		caen::json::get_if_not_null(j, key_cmd, e._cmd);
		caen::json::get_if_not_null(j, key_handle, e._handle);
		caen::json::get_if_not_null(j, key_query, e._query);
		caen::json::get_if_not_null(j, key_value, e._value);
		caen::json::get_if_not_null(j, key_multiple, e._multiple);
	}

	friend void to_json(nlohmann::json& j, const json_cmd& e) {
		caen::json::set(j, key_cmd, e._cmd);
		caen::json::set(j, key_handle, e._handle);
		caen::json::set(j, key_query, e._query);
		caen::json::set(j, key_value, e._value);
		caen::json::set(j, key_multiple, e._multiple);
	}

private:

	static constexpr auto key_cmd = "cmd"sv;
	static constexpr auto key_handle = "handle"sv;
	static constexpr auto key_query = "query"sv;
	static constexpr auto key_value = "value"sv;
	static constexpr auto key_multiple = "multiple"sv;

	cmd::command _cmd;
	cmd::handle_t _handle;
	cmd::query_t _query;
	cmd::value_t _value;
	std::vector<json_cmd> _multiple; // requires C++17

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
	{ command::SET_VALUE,			"setValue"s				},
	{ command::SEND_COMMAND,		"sendCommand"s			},
	{ command::MULTIPLE,			"multiple"s				},
})

} // namespace cmd

#endif /* CAEN_INCLUDE_JSON_JSON_CMD_HPP_ */
