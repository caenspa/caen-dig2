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
*	\file		is_in.hpp
*	\brief		Utilities to compare groups of values
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_IS_IN_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_IS_IN_HPP_

#include <boost/static_assert.hpp>

namespace caen {

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

/**
 * @brief Check if a variable is equal to all variables in a given set.
 *
 * @tparam T		value type
 * @tparam Args		parameter pack
 * @param value		the value
 * @param args		values to be compared with value
 * @return			true if value is equal to all in args
 */
template <typename T, typename... Args>
constexpr bool are_equals(const T& value, const Args&... args) noexcept {
	static_assert(sizeof...(args) > 0, "at least one value is needed");
	bool ret = true;
	using unused = bool[];
	(void)(unused{ false, ret &= value == args... });
	return ret;
}

/**
 * @brief Check if a variable is equal to any variables in a given set.
 *
 * @tparam T		value type
 * @tparam Args		parameter pack
 * @param value		the value
 * @param args		values to be compared with value
 * @return			true if value is equal to any in args
 */
template <typename T, typename... Args>
constexpr bool is_in(const T& value, const Args&... args) noexcept {
	static_assert(sizeof...(args) > 0, "at least one value is needed");
	bool ret = false;
	using unused = bool[];
	(void)(unused{ false, ret |= value == args... });
	return ret;
}

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

/**
 * @brief Check if a variable is equal to any variables in a given set.
 *
 * @tparam T		value type
 * @tparam Args		parameter pack
 * @param value		the value
 * @param args		values to be compared with value
 * @return			true if value is equal to any in args
 */
template <typename T, typename... Args>
constexpr bool is_in(const T& value, const Args&... args) noexcept {
	static_assert(sizeof...(args) > 0, "at least one value is needed");
	return ((value == args) || ...);
}


/**
 * @brief Check if a variable is equal to all variables in a given set.
 *
 * @tparam T		value type
 * @tparam Args		parameter pack
 * @param value		the value
 * @param args		values to be compared with value
 * @return			true if value is equal to all in args
 */
template <typename T, typename... Args>
constexpr bool are_equals(const T& value, const Args&... args) {
	static_assert(sizeof...(args) > 0, "at least one value is needed");
	return ((value == args) && ...);
}

} // namespace cxx17

#endif

namespace sanity_checks {

constexpr bool test_is_in() noexcept {
	bool ret{true};
	ret &= is_in(1, 1);
	ret &= !is_in(0, 1);
	ret &= is_in(1, 1, 2, 3, 4);
	ret &= !is_in(1, 10);
	return ret;
}

constexpr bool test_are_equals() noexcept {
	bool ret{true};
	ret &= are_equals(1, 1);
	ret &= !are_equals(0, 1);
	ret &= are_equals(1, 1, 1, 1, 1);
	ret &= !are_equals(1, 10);
	return ret;
}

BOOST_STATIC_ASSERT(test_is_in());
BOOST_STATIC_ASSERT(test_are_equals());

} // namespace sanity_checks

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_IS_IN_HPP_ */
