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
*	This file is part of the CAEN C++ Utility.
*
*	The CAEN C++ Utility is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	The CAEN C++ Utility is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN C++ Utility; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		optional.hpp
*	\brief		`std::optional` replacement for C++14
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_OPTIONAL_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_OPTIONAL_HPP_

#include <utility>

#if __cplusplus >= 201703L
#include <optional>
#endif

#include <boost/optional.hpp>

namespace caen {

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

using boost::optional;
using boost::make_optional;
using nullopt_t = boost::none_t;
const nullopt_t nullopt = boost::none;

// to emulate returning emplace also on pre_cxx17
template <typename T, typename... Args>
decltype(auto) emplace(optional<T>& target, Args&& ...args) {
	return target.emplace(std::forward<Args>(args)...), target.value();
}

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

using std::optional;
using std::make_optional;
using std::nullopt_t;
using std::nullopt;

template <typename T, typename... Args>
decltype(auto) emplace(optional<T>& target, Args&& ...args) {
	return target.emplace(std::forward<Args>(args)...);
}

} // namespace cxx17
#endif

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_OPTIONAL_HPP_ */
