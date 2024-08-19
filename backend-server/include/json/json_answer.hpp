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
*	\file		json_answer.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_ANSWER_HPP_
#define CAEN_INCLUDE_JSON_JSON_ANSWER_HPP_

#include <string>
#include <exception>
#include <tuple>

#include <nlohmann/json.hpp>

#include "json/json_utilities.hpp"
#include "json/json_common.hpp"

struct json_answer {

	// Utility function to accept both single and multiple value in the same interface
	/**
	 * Generator of json_answer with result set to true
	 * @param cmd a copy of the command
	 * @param strategy a flag, an exception or a functor with no parameters returning either a string or a vector of strings
	 * @return an instance of json_answer
	 */
	template <typename T>
	static json_answer build_error(cmd::command cmd, const T& strategy) {
		return build<false>(cmd, strategy);
	}

	/**
	 * Generator of json_answer with result set to false
	 * @param cmd a copy of the command
	 * @param strategy a flag, an exception or a functor with no parameters returning either a string or a vector of strings
	 * @return an instance of json_answer
	 */
	// Utility function to accept both single and multiple value in the same interface
	template <typename T>
	static json_answer build_success(cmd::command cmd, const T& strategy) {
		return build<true>(cmd, strategy);
	}

	/**
	 * Default constructor needed by nlohmann's JSON.
	 */
	json_answer()
		: _cmd{cmd::command::UNKNOWN}
		, _result{}
		, _flag{answer::flag::UNKNOWN}
		, _value{} {
	}

	/**
	 * Convert JSON to json_answer
	 * @param args any input of nlohmann::json::parse representing a JSON
	 * @return an instance of json_answer parsing the input content
	 */
	template <typename... Args>
	static json_answer marshal(Args&& ...args) {
		return nlohmann::json::parse(std::forward<Args>(args)...).template get<json_answer>();
	}

	/**
	 * Convert json_answer to JSON
	 * @return the JSON with no indentation
	 */
	nlohmann::json::string_t unmarshal() const {
		return nlohmann::json(*this).dump();
	}

	cmd::command get_cmd() const noexcept { return _cmd; }
	answer::flag get_flag() const noexcept { return _flag; }
	bool get_result() const noexcept { return _result; }
	const answer::value_t& get_value() const noexcept { return _value; }

	static constexpr auto& key_cmd() noexcept { return "cmd"; }
	static constexpr auto& key_flag() noexcept { return "flag"; }
	static constexpr auto& key_result() noexcept { return "result"; }
	static constexpr auto& key_value() noexcept { return "value"; }

	friend void from_json(const nlohmann::json& j, json_answer& e) {
		caen::json::get_if_not_null(j, key_cmd(), e._cmd);
		caen::json::get_if_not_null(j, key_result(), e._result);
		caen::json::get_if_not_null(j, key_flag(), e._flag);
		caen::json::get_if_not_null(j, key_value(), e._value);
	}

	friend void to_json(nlohmann::json& j, const json_answer& e) {
		caen::json::set(j, key_cmd(), e._cmd);
		caen::json::set(j, key_result(), e._result);
		caen::json::set(j, key_flag(), e._flag);
		caen::json::set(j, key_value(), e._value);
	}

private:

	template <bool T>
	static json_answer build_partial(cmd::command cmd) {
		auto r = json_answer();
		r._cmd = cmd;
		r._result = T;
		return r;
	}

	template <bool T>
	static json_answer build(cmd::command cmd, const std::exception& ex) {
		auto r = build_partial<T>(cmd);
		r._value.emplace_back(ex.what());
		return r;
	}

	template <bool T>
	static json_answer build(cmd::command cmd, const answer::flag_value_provider& strategy) {
		auto r = build_partial<T>(cmd);
		if (strategy)
			std::tie(r._flag, r._value) = strategy();
		return r;
	}

	template <bool T>
	static json_answer build(cmd::command cmd, const answer::value_provider& strategy) {
		auto r = build_partial<T>(cmd);
		if (strategy) // provider could be empty
			r._value = strategy();
		return r;
	}

	template <bool T>
	static json_answer build(cmd::command cmd, const answer::single_value_provider& strategy) {
		auto r = build_partial<T>(cmd);
		if (strategy) // provider could be empty
			r._value.push_back(strategy());
		return r;
	}

	cmd::command _cmd;
	bool _result;
	answer::flag _flag;
	answer::value_t _value;

};

namespace answer {

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(flag, {
	{ flag::UNKNOWN, 			nullptr 		},
	{ flag::ARM,				"ARM"s			},
	{ flag::DISARM,				"DISARM"s		},
	{ flag::CLEAR,				"CLEAR"s		},
	{ flag::RESET,				"RESET"s		},
})

} // namespace answer

#endif /* CAEN_INCLUDE_JSON_JSON_ANSWER_HPP_ */
