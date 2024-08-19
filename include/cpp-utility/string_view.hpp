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
*	\file		string_view.hpp
*	\brief		`std::string_view` replacement
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_STRING_VIEW_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_STRING_VIEW_HPP_

#if __cplusplus >= 201703L
#include <string_view>
#endif

#include <boost/utility/string_view.hpp>
#include <boost/config.hpp>

namespace caen {

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

using boost::basic_string_view;							//!< Use boost::basic_string_view on pre C++17.

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

using std::basic_string_view;							//!< Use std::basic_string_view on C++17 and after.

} // namespace cxx17
#endif

/**
* @defgroup StringViewDefines String view definitions
* @brief Definitions of drop-in replacement for `std::string_view` (and relative types for wider strings).
* 
* Drop-in replacements to be used instead of `std::string_view` when building
*   with standard pre C++17.
* 
* @{ */
using string_view = basic_string_view<char>;
using wstring_view = basic_string_view<wchar_t>;
using u16string_view = basic_string_view<char16_t>;
using u32string_view = basic_string_view<char32_t>;
/** @} */

inline namespace literals {

/**
* @defgroup StringViewLiterals String view literals
* @brief Definitions of UDL `operator""_sv`.
* 
* Drop-in replacements to be used instead of standard UDL `operator""sv`
* when building with standard pre C++17.
* 
* @{ */
constexpr auto operator""_sv(const char* str, std::size_t len) noexcept {
	return basic_string_view<char>{str, len};
}
constexpr auto operator""_sv(const wchar_t* str, std::size_t len) noexcept {
	return basic_string_view<wchar_t>{str, len};
}
constexpr auto operator""_sv(const char16_t* str, std::size_t len) noexcept {
	return basic_string_view<char16_t>{str, len};
}
constexpr auto operator""_sv(const char32_t* str, std::size_t len) noexcept {
	return basic_string_view<char32_t>{str, len};
}
/** @} */

} // namespace literals

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_STRING_VIEW_HPP_ */
