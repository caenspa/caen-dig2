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
*	\file		to_underlying.hpp
*	\brief		`std::to_underlying` replacement
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_TO_UNDERLYING_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_TO_UNDERLYING_HPP_

#include <type_traits>

#if defined(__has_include) && __has_include(<version>)
#include <version> // C++20
#endif

#ifdef __cpp_lib_to_underlying
#include <utility>
#endif

namespace caen {

#ifndef __cpp_lib_to_underlying
inline
#endif
namespace no_std_to_underlying {

/**
 * @brief Replacement for `std::to_underlying`.
 *
 * @tparam Enum		enumerator type
 * @param value		enumerator value
 * @return			the value, static-casted to its underlying type
 */
template <typename Enum>
constexpr auto to_underlying(Enum value) noexcept {
	using underlying_type = std::underlying_type_t<Enum>;
	return static_cast<underlying_type>(value);
}

} // namespace no_std_to_underlying

#ifdef __cpp_lib_to_underlying
inline namespace std_to_underlying {

using std::to_underlying;

} // namespace std_to_underlying
#endif

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_TO_UNDERLYING_HPP_ */
