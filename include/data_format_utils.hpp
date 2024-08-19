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
*	\file		data_format_utils.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_DATA_FORMAT_UTILS_HPP_
#define CAEN_INCLUDE_DATA_FORMAT_UTILS_HPP_

#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/range/algorithm/transform.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/fmt/fmt.h>

#include "cpp-utility/type_traits.hpp"
#include "json/json_data_format.hpp"
#include "endpoints/endpoint.hpp"
#include "lib_error.hpp"

namespace caen {

namespace dig2 {

namespace ep {

namespace detail {

template <typename T, typename List, typename = void>
struct is_default_data_format_defined
	: std::false_type {};

template <typename T, typename List>
struct is_default_data_format_defined<T, List, caen::void_t<decltype(T::default_data_format)>>
	: caen::is_invocable_r<List, decltype(T::default_data_format)> {};

template <typename T, typename = void>
struct is_data_format_dimension_defined
	: std::false_type {};

template <typename T>
struct is_data_format_dimension_defined<T, caen::void_t<decltype(T::data_format_dimension)>>
	: caen::is_invocable_r<std::size_t, decltype(T::data_format_dimension), typename utility::endpoint_traits<T>::names_type> {};

} // namespace detail

/*
 * Utility struct to reuse the logic of the endpoint::set_data_format implementation in each endpoint.
 * Being "names" type different in each endpoint, it cannot implement it in base class.
 */
template <typename Endpoint>
struct data_format_utils {

	using traits = utility::endpoint_traits<Endpoint>;
	using args_list_t = typename traits::args_list_type;
	using args_type = typename args_list_t::value_type;

	static_assert(detail::is_data_format_dimension_defined<Endpoint>::value, "endpoint must define data_format_dimension function");
	static_assert(detail::is_default_data_format_defined<Endpoint, args_list_t>::value, "endpoint must define a default_data_format function returning a type convertible to args_list_t");

	template <typename String>
	static void parse_data_format(args_list_t& list, String&& json_format) {
		if (json_format.empty()) {
			list = Endpoint::default_data_format();
		} else {
			const auto j = nlohmann::json::parse(std::forward<String>(json_format));
			args_list_t new_list;
			new_list.reserve(j.size());
			boost::transform(j, std::back_inserter(new_list), [](const nlohmann::json& element) {
				const auto format = element.get<json_data_format<Endpoint>>(); // json to object
				if (traits::is_unknown(format.get_name()))
					throw ex::invalid_argument(fmt::format("invalid name in {}", element.dump()));
				if (traits::is_unknown(format.get_type()))
					throw ex::invalid_argument(fmt::format("invalid type in {}", element.dump()));
				const auto expected_dim = Endpoint::data_format_dimension(format.get_name());
				if (expected_dim != format.get_dim())
					throw ex::invalid_argument(fmt::format("invalid dim in {} (must be {})", element.dump(), expected_dim));
				return args_type{format.get_name(), format.get_type(), format.get_dim()};
			});
			list = std::move(new_list);
		}
	}

};

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_DATA_FORMAT_UTILS_HPP_ */
