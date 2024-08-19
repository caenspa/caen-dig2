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
*	\file		byte.hpp
*	\brief		`std::byte` replacement
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_BYTE_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_BYTE_HPP_

#include <type_traits>

#if __cplusplus >= 201703L
#include <cstddef>
#endif

#include <boost/config.hpp>
#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>

namespace caen {

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

/**
 * @brief Replacement for `std::byte`.
 *
 * Scoped enum is the best choice to emulate `std::byte`. The main difference is that on
 * C++14 it is not possible to use direct-list-initialization on enumeration types. So,
 * something like `caen::byte{1};`, legal in C++17, is not legal on C++14. This is a
 * language feature, cannot be implemented in terms of code, unless byte is implemented
 * as a typedef, like `using byte = unsigned char;`.
 * @warning On C++14 this breaks the strict aliasing rule, even if it seems to work properly.
 * @sa https://stackoverflow.com/a/44126731/3287591
 * @sa https://en.cppreference.com/w/cpp/types/byte
 */
#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_GNUC, <, 6, 0, 0)
// workaround for GCC bug #43407
enum class byte : unsigned char BOOST_MAY_ALIAS {};
#else
enum class BOOST_MAY_ALIAS byte : unsigned char {};
#endif

template <class IntegerType, std::enable_if_t<std::is_integral<IntegerType>::value, int> = 0>
constexpr byte operator<<(byte b, IntegerType shift) noexcept {
	return byte(static_cast<unsigned int>(b) << shift);
}
template <class IntegerType, std::enable_if_t<std::is_integral<IntegerType>::value, int> = 0>
constexpr byte& operator<<=(byte& b, IntegerType shift) noexcept {
	return b = b << shift;
}
template <class IntegerType, std::enable_if_t<std::is_integral<IntegerType>::value, int> = 0>
constexpr byte operator>>(byte b, IntegerType shift) noexcept {
	return byte(static_cast<unsigned int>(b) >> shift);
}
template <class IntegerType, std::enable_if_t<std::is_integral<IntegerType>::value, int> = 0>
constexpr byte& operator>>=(byte& b, IntegerType shift) noexcept {
	return b = b >> shift;
}
constexpr byte operator|(byte l, byte r) noexcept {
	return byte(static_cast<unsigned int>(l) | static_cast<unsigned int>(r));
}
constexpr byte& operator|=(byte& l, byte r) noexcept {
	return l = l | r;
}
constexpr byte operator&(byte l, byte r) noexcept {
	return byte(static_cast<unsigned int>(l) & static_cast<unsigned int>(r));
}
constexpr byte& operator&=(byte& l, byte r) noexcept {
	return l = l & r;
}
constexpr byte operator^(byte l, byte r) noexcept {
	return byte(static_cast<unsigned int>(l) ^ static_cast<unsigned int>(r));
}
constexpr byte& operator^=(byte& l, byte r) noexcept {
	return l = l ^ r;
}
constexpr byte operator~(byte b) noexcept {
	return byte(~static_cast<unsigned int>(b));
}
template <class IntegerType, std::enable_if_t<std::is_integral<IntegerType>::value, int> = 0>
constexpr IntegerType to_integer(byte b) noexcept {
	return IntegerType(b);
}

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

/**
 * @brief Fallback to standard version std::byte, if available.
 */
using std::byte;
using std::to_integer;

} // namespace cxx17
#endif

namespace sanity_checks {

static_assert(sizeof(byte) == sizeof(unsigned char), "byte size must be 1");

// requirements for fast vector allocation with std::memset
static_assert(std::is_standard_layout<byte>::value, "is_standard_layout required for safe pointer conversion with reinterpret_cast");

// requirements for fast vector copy with std::memcpy (C++14@[basic.types], https://stackoverflow.com/a/7624942/3287591)
static_assert(std::is_trivially_copyable<byte>::value, "is_trivially_copyable required for fast vector copy");

// requirements for fast vector allocation on MSVC debug
static_assert(std::is_scalar<byte>::value, "is_scalar required for fast assignment in MSVC debug");
static_assert(!std::is_volatile<byte>::value, "!is_volatile required for fast assignment in MSVC debug");
static_assert(!std::is_member_pointer<byte>::value, "!is_member_pointer required for fast assignment in MSVC debug");

static_assert(to_integer<int>(byte{}) == int{}, "invalid to_integer implementation");

} // namespace sanity_checks

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_BYTE_HPP_ */
