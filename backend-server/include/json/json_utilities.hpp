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
*	\file		json_utilities.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_UTILITIES_HPP_
#define CAEN_INCLUDE_JSON_JSON_UTILITIES_HPP_

#include <nlohmann/json.hpp>

#include <boost/algorithm/string/case_conv.hpp>

#include "cpp-utility/optional.hpp"

// partial specialization for caen::optional
namespace nlohmann {

template <typename T>
struct adl_serializer<caen::optional<T>> {
	static void to_json(json& j, const caen::optional<T>& opt) {
		if (!opt)
			j = nullptr;
		else
			j = *opt;
	}
	static void from_json(const json& j, caen::optional<T>& opt) {
		if (j.is_null())
			opt = caen::nullopt;
		else
			opt = j.get<T>();
	}
};

} // namespace nlohmann

namespace caen {

namespace json {

template <typename BasicJsonType, typename T, typename TKey>
void get(const BasicJsonType& j, TKey&& key, T& value) {
	j.at(std::forward<TKey>(key)).get_to(value); // may throw
}

template <typename BasicJsonType, typename T, typename TKey>
void get_if_not_null(const BasicJsonType& j, TKey&& key, T& value) {
	const auto it = j.find(std::forward<TKey>(key));
	if (it != j.end() && !it->is_null())
		it->get_to(value);
}

template <typename BasicJsonType, typename T, typename TKey>
void set(BasicJsonType& j, TKey&& key, T&& value) {
	j[std::forward<TKey>(key)] = std::forward<T>(value);
}

/**
 * Convert a type (enum, in particular), to string version, using to_json
 * @tparam T	input type
 * @param v		value
 * @return		a string that can be converted back to enum from_json
 */
template <typename T, typename String = std::string>
String to_json_string(T&& v) {
	return nlohmann::json(std::forward<T>(v)).get<String>();
}

/**
 * Non-throwing version of to_json_string(), useful for error logging
 * @tparam T	input type
 * @param v		value
 * @return		same of to_json_string(), empty string if conversion cannot be performed.
 */
template <typename T, typename String = std::string>
String to_json_string_safe(T&& v) noexcept try {
	return to_json_string<T, String>(std::forward<T>(v));
}
catch (...) {
	return String{};
}

/**
 * Utility function to recursively iterate across all element of a nlohmann::json
 * @sa https://stackoverflow.com/q/45934851/3287591
*/
template <typename BasicJsonType, typename UnaryFunction>
void json_recursive_for_each(BasicJsonType& j, UnaryFunction f) noexcept(noexcept(f)) {
	for (auto it = std::begin(j); it != std::end(j); ++it) {
		if (it->is_structured())
			json_recursive_for_each(*it, f);
		f(it);
	}
}

/**
 * Utility function to recursively iterate across all element of a nlohmann::json
 * @sa https://stackoverflow.com/q/45934851/3287591
*/
template <typename BasicJsonType, typename UnaryFunction>
void json_recursive_erase_if(BasicJsonType& j, UnaryFunction f) {
	for (auto it = std::begin(j); it != std::end(j);) {
		if (it->is_structured())
			json_recursive_erase_if(*it, f);
		if (f(it))
			it = j.erase(it);
		else
			++it;
	}
}

} // namespace json

inline namespace literals {

/**
 * Version of _json_pointer that force the input to lower case
*/
inline nlohmann::json::json_pointer operator""_json_pointer_lowercase(const char* s, std::size_t n) {
	std::string path(s, n);
	boost::to_lower(path);
	return nlohmann::json::json_pointer(path);
}

} // namespace literals

} // namespace caen

#endif /* CAEN_INCLUDE_JSON_JSON_UTILITIES_HPP_ */
