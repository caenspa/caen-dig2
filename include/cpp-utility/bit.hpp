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
*	\file		bit.hpp
*	\brief		Bit manipulation utilities
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_BIT_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_BIT_HPP_

#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

#if defined(__has_include) && __has_include(<version>)
#include <version> // C++20
#endif

#ifdef __cpp_lib_bit_cast
#include <bit>
#endif

#include <boost/predef/compiler.h>
#include <boost/predef/other/endian.h>
#include <boost/predef/other/workaround.h>
#include <boost/static_assert.hpp>

#include "integer.hpp"

namespace caen {

namespace bit {

namespace detail {

template <typename T>
struct bit_size : std::integral_constant<std::size_t, (sizeof(T) * CHAR_BIT)> {};

template <typename TOut>
constexpr TOut get_bit_unsafe(std::size_t pos) noexcept {
	return TOut{1} << pos;
}

template <typename TOut>
constexpr TOut get_bit(std::size_t pos) {
	if (pos >= bit_size<TOut>::value)
		throw std::range_error("get_bit type is too small");
	return get_bit_unsafe<TOut>(pos);
}

template <typename TOut>
constexpr TOut get_mask_unsafe(std::size_t nbits) noexcept {
	return (nbits < bit_size<TOut>::value) ? (get_bit_unsafe<TOut>(nbits) - TOut{1}) : std::numeric_limits<TOut>::max();
}

template <typename TOut>
constexpr TOut get_mask(std::size_t nbits) {
	if (nbits > bit_size<TOut>::value)
		throw std::range_error("get_mask type is too small");
	return get_mask_unsafe<TOut>(nbits);
}

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_GNUC, <, 6, 1, 0)

// avoid never executed "throw" in constexpr function (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67371)

template <typename TOut, std::size_t Pos, std::enable_if_t<(Pos < bit_size<TOut>::value), int> = 0>
constexpr TOut get_bit() noexcept {
	return get_bit_unsafe<TOut>(Pos);
}

template <typename TOut, std::size_t NBits, std::enable_if_t<(NBits <= bit_size<TOut>::value), int> = 0>
constexpr TOut get_mask() noexcept {
	return get_mask_unsafe<TOut>(NBits);
}

#else

template <typename TOut, std::size_t Pos>
constexpr TOut get_bit() noexcept {
	return get_bit<TOut>(Pos);
}

template <typename TOut, std::size_t NBits>
constexpr TOut get_mask() noexcept {
	return get_mask<TOut>(NBits);
}

#endif

#ifndef __cpp_lib_bit_cast
inline
#endif
namespace no_std_bit_cast {

// cannot be done `constexpr` because it requires compiler support. (https://en.cppreference.com/w/cpp/numeric/bit_cast)
template <typename TOut, typename TIn>
TOut bit_cast(const TIn& src) noexcept {
	TOut dst;
	std::memcpy(&dst, &src, sizeof(dst));
	return dst;
}

// corner case when all scalar types have sizeof equal to 1 is not supported. (https://en.cppreference.com/w/cpp/types/endian)
enum class endian {
	little = 1234,
	big = 4321,

#if BOOST_ENDIAN_BIG_BYTE
#ifdef CAEN_BIT_ENDIAN_DETECTED
#error endian already defined
#endif
#define CAEN_BIT_ENDIAN_DETECTED
	native = little,
#endif

#if BOOST_ENDIAN_BIG_WORD
#ifdef CAEN_BIT_ENDIAN_DETECTED
#error endian already defined
#endif
#define CAEN_BIT_ENDIAN_DETECTED
	native = 2143,
#endif

#if BOOST_ENDIAN_LITTLE_BYTE
#ifdef CAEN_BIT_ENDIAN_DETECTED
#error endian already defined
#endif
#define CAEN_BIT_ENDIAN_DETECTED
	native = little,
#endif

#if BOOST_ENDIAN_LITTLE_WORD
#ifdef CAEN_BIT_ENDIAN_DETECTED
#error endian already defined
#endif
#define CAEN_BIT_ENDIAN_DETECTED
	native = 3412,
#endif

#ifndef CAEN_BIT_ENDIAN_DETECTED
	native = 0,
#endif
#undef CAEN_BIT_ENDIAN_DETECTED
};

template <std::size_t NBits, typename UnsignedInt>
constexpr UnsignedInt sign_extend(UnsignedInt v) noexcept {
	static_assert(-1 == ~0, "sign_extend requires two's complement architecture");
	constexpr auto mask = get_bit<UnsignedInt, (NBits - 1)>();
	return (v ^ mask) - mask;
}

// cannot be done `constexpr` because it requires bit_cast.
template <std::size_t NBits, typename UnsignedInt, typename SignedInt>
SignedInt sign_extend_cast(UnsignedInt v) noexcept {
	return bit_cast<SignedInt>(sign_extend<NBits>(v));
}

} // namespace no_std_bit_cast

#ifdef __cpp_lib_bit_cast
inline namespace std_bit_cast {

using std::bit_cast;
using std::endian;

template <std::size_t NBits, typename UnsignedInt>
constexpr UnsignedInt sign_extend(UnsignedInt v) noexcept {
	// two's complement is mandatory since C++20
	constexpr auto mask = get_bit<UnsignedInt, (NBits - 1)>();
	return (v ^ mask) - mask;
}

// static_cast is sufficient since C++20 and can be `constexpr` (https://stackoverflow.com/a/57947296/3287591)
template <std::size_t NBits, typename UnsignedInt, typename SignedInt>
constexpr SignedInt sign_extend_cast(UnsignedInt v) noexcept {
	return static_cast<SignedInt>(sign_extend<NBits>(v));
}

} // namespace std_bit_cast
#endif

#ifndef __cpp_lib_bit_cast
#define CAEN_BIT_CXX20_COSTEXPR
#else
#define CAEN_BIT_CXX20_COSTEXPR		constexpr
#endif

template <typename T, typename = void>
struct is_unsigned : std::is_unsigned<T> {};

template <typename T>
struct is_unsigned<T, std::enable_if_t<std::is_enum<T>::value>> : is_unsigned<std::underlying_type_t<T>> {};

namespace sanity_checks {

#if BOOST_COMP_MSVC || defined(BOOST_COMP_MSVC_EMULATED)
/*
 * In versions of Visual Studio before Visual Studio 2022 version 17.4, the C++ compiler didn't
 * correctly determine the underlying type of an unscoped enumeration with no fixed base type.
 * The compiler also didn't correctly model the types of enumerators. It could assume an incorrect
 * type in enumerations without a fixed underlying type before the closing brace of the enumeration.
 * Under /Zc:enumTypes, the compiler correctly implements the standard behavior.
 *
 * Unfortunately, /Zc:enumTypes is not enabled by default because it is a potential source and
 * binary breaking change.
 *
 * If /Zc:enumTypes would be enabled, we could also replace BOOST_COMP_MSVC with the classic
 * BOOST_PREDEF_WORKAROUND(BOOST_COMP_MSVC, <, 19, 34, 0). BOOST_COMP_MSVC_EMULATED is a workaround
 * for IntelliSense, that is still broken.
 *
 * See:
 * - https://learn.microsoft.com/en-us/cpp/overview/cpp-conformance-improvements?view=msvc-170
 * - https://developercommunity.visualstudio.com/t/underlying-type-of-an-unscoped-enum/524018
 */
#define CAEN_SKIP_ENUM_AUTO_UNSIGNED_TEST
#endif

using signed_t = signed char;
using unsigned_t = unsigned char;

// enum my_enum_opaque; bad defined
enum my_enum_opaque_signed : signed_t;
enum my_enum_opaque_unsigned : unsigned_t;
enum my_enum {};
enum my_enum_auto_signed { _vsm = -1 };
#ifndef CAEN_SKIP_ENUM_AUTO_UNSIGNED_TEST
enum my_enum_auto_unsigned { _vum = std::numeric_limits<unsigned int>::max() };
#endif
enum my_enum_signed : signed_t {};
enum my_enum_unsigned : unsigned_t {};
enum struct my_enum_opaque_scoped;
enum struct my_enum_opaque_scoped_signed : signed_t;
enum struct my_enum_opaque_scoped_unsigned : unsigned_t;
enum struct my_enum_scoped {};
enum struct my_enum_scoped_signed : signed_t {};
enum struct my_enum_scoped_unsigned : unsigned_t {};

constexpr bool test_is_unsigned() noexcept {
	bool ret{true};
	ret &= (is_unsigned<bool>::value);
	ret &= (is_unsigned<char>::value == (CHAR_MIN != SCHAR_MIN));	// implementation defined: signed on gcc and clang (settable with -funsigned-char), signed on MSVC (settable with /J)
	ret &= (is_unsigned<unsigned char>::value);
	ret &= (!is_unsigned<signed char>::value);
	ret &= (is_unsigned<unsigned int>::value);
	ret &= (!is_unsigned<float>::value);
	ret &= (!is_unsigned<double>::value);
	ret &= (!is_unsigned<long double>::value);
	ret &= (!is_unsigned<my_enum_opaque_signed>::value);
	ret &= (is_unsigned<my_enum_opaque_unsigned>::value);
	ret &= (is_unsigned<my_enum>::value || true);					// implementation defined: unsigned on gcc and clang, signed on MSVC
	ret &= (!is_unsigned<my_enum_auto_signed>::value);
#ifndef CAEN_SKIP_ENUM_AUTO_UNSIGNED_TEST
	ret &= (is_unsigned<my_enum_auto_unsigned>::value);				// implementation broken on MSCV
#endif
	ret &= (!is_unsigned<my_enum_signed>::value);
	ret &= (is_unsigned<my_enum_unsigned>::value);
	ret &= (!is_unsigned<my_enum_opaque_scoped>::value);
	ret &= (!is_unsigned<my_enum_opaque_scoped_signed>::value);
	ret &= (is_unsigned<my_enum_opaque_scoped_unsigned>::value);
	ret &= (!is_unsigned<my_enum_scoped>::value);
	ret &= (!is_unsigned<my_enum_scoped_signed>::value);
	ret &= (is_unsigned< my_enum_scoped_unsigned>::value);
	return ret;
}

BOOST_STATIC_ASSERT(test_is_unsigned());

} // namespace sanity_checks

template <std::size_t NBits, std::size_t Lsb, typename TOut, typename TIn>
constexpr TOut mask_at(TIn v) noexcept {
	return static_cast<TOut>((v >> Lsb) & get_mask<TIn, NBits>());
}

template <std::size_t NBits, typename TIn, std::enable_if_t<(NBits < bit_size<TIn>::value), int> = 0>
constexpr void right_shift(TIn& v) noexcept {
	v >>= NBits;
}

template <std::size_t NBits, typename TIn, std::enable_if_t<(NBits == bit_size<TIn>::value), int> = 0>
constexpr void right_shift(TIn& v) noexcept {
	v = TIn{}; // set to zero
}

template <std::size_t NBits, typename TOut, typename TIn>
constexpr TOut mask_and_right_shift(TIn& v) noexcept {
	const TOut res = mask_at<NBits, 0, TOut, TIn>(v);
	right_shift<NBits, TIn>(v);
	return res;
}

template <std::size_t NBits, typename TIn, std::enable_if_t<(NBits < bit_size<TIn>::value), int> = 0>
constexpr void left_shift(TIn& v) noexcept {
	v <<= NBits;
}

template <std::size_t NBits, typename TIn, std::enable_if_t<(NBits == bit_size<TIn>::value), int> = 0>
constexpr void left_shift(TIn& v) noexcept {
	v = TIn{}; // set to zero
}

template <std::size_t NBits, typename TOut, typename TIn>
constexpr TOut mask_and_left_shift(TIn &v) noexcept {
	constexpr std::size_t offset = bit_size<TIn>::value - NBits;
	const TOut res = mask_at<NBits, offset, TOut, TIn>(v);
	left_shift<NBits, TIn>(v);
	return res;
}

template <std::size_t Pos, typename TIn>
constexpr bool test(TIn v) noexcept {
	return (get_bit<TIn, Pos>() & v) != 0;
}

template <std::size_t Pos, typename TIn>
constexpr void set(TIn& v) noexcept {
	v |= get_bit<TIn, Pos>();
}

} // namespace detail

/**
 * @brief Get number of bits of a type at compile time.
 */
using detail::bit_size;

/**
 * @brief Get an integer with given bit set.
 */
using detail::get_bit;

/**
 * @brief Get an integer with all first given number of bit set.
 */
using detail::get_mask;

/**
 * @brief Replacement for `std::endian` on pre C++20.
 */
using detail::endian;

/**
 * @brief Replacement for `std::bit_cast` on pre C++20.
 *
 * Useful because `reinterpret_cast` generate a undefined behavior for strict aliasing rule.
 * @note `constexpr` requires C++20.
 * @sa https://en.cppreference.com/w/cpp/numeric/bit_cast
 * @tparam TOut		output type
 * @tparam TIn		input type
 * @param v			input value
 * @return			result
 */
template <typename TOut, typename TIn>
CAEN_BIT_CXX20_COSTEXPR TOut bit_cast(const TIn& v) noexcept {
	static_assert(sizeof(TIn) == sizeof(TOut), "input type and output type must have the same size");
	static_assert(std::is_trivially_copyable<TIn>::value, "invalid input type");
	static_assert(std::is_trivially_copyable<TOut>::value, "invalid output type");
	static_assert(std::is_trivially_constructible<TOut>::value, "invalid output type");
	return detail::bit_cast<TOut, TIn>(v);
}

/**
 * @brief Extend the sign bit in a unsigned type value, returning the same unsigned type.
 *
 * @warning	Does not work if any bit above NBits is set on @arg v.
 * @tparam NBits		number of significant bits in input value @arg v
 * @tparam UnsignedInt	input type (unsigned, must contain at least NBits bits)
 * @param[in] v			input value
 * @return				result
 */
template <std::size_t NBits, typename UnsignedInt>
constexpr UnsignedInt sign_extend(UnsignedInt v) noexcept {
	static_assert(std::is_unsigned<UnsignedInt>::value, "input type must be unsigned (enumerators not supported)");
	static_assert(NBits != 0, "NBits cannot be zero");
	static_assert(NBits != 0 && NBits <= bit_size<UnsignedInt>::value, "type is too small");
	return detail::sign_extend<NBits, UnsignedInt>(v);
}

/**
 * @brief Sign extend with automatic bit cast to corresponding signed type.
 *
 * @note `constexpr` requires C++20.
 * @tparam NBits		width of input value
 * @tparam UnsignedInt	input type
 * @tparam SignedInt	output type
 * @param v				input value
 * @return the result
 */
template <std::size_t NBits, typename UnsignedInt, typename SignedInt = std::make_signed_t<UnsignedInt>>
CAEN_BIT_CXX20_COSTEXPR SignedInt sign_extend_cast(UnsignedInt v) noexcept {
	static_assert(std::is_unsigned<UnsignedInt>::value, "input type must be unsigned (enumerators not supported)");
	static_assert(std::is_signed<SignedInt>::value, "output type must be signed (enumerators not supported)");
	return detail::sign_extend_cast<NBits, UnsignedInt, SignedInt>(v);
}

/**
 * @brief Right shift the argument by NBits.
 *
 * @tparam NBits	number of bits
 * @tparam TIn		input type (unsigned, must contain at least NBits bits)
 * @param v			the input value that will be shifted
 */
template <std::size_t NBits, typename TIn>
constexpr void right_shift(TIn& v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(NBits <= bit_size<TIn>::value, "input type is too small");
	detail::right_shift<NBits, TIn>(v);
}

/**
 * @brief Left shift the argument by NBits.
 *
 * @tparam NBits	number of bits
 * @tparam TIn		input type (unsigned, must contain at least NBits bits)
 * @param v			the input value that will be shifted
 */
template <std::size_t NBits, typename TIn>
constexpr void left_shift(TIn& v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(NBits <= bit_size<TIn>::value, "input type is too small");
	detail::left_shift<NBits, TIn>(v);
}

/**
 * @brief Get the lowest NBits from argument, and right shift the argument.
 *
 * @tparam NBits	number of bits
 * @tparam TOut		output type (unsigned, must contain at least NBits bits)
 * @tparam TIn		input type (unsigned, must contain at least NBits bits)
 * @param v			the input value that will be shifted
 * @return the value stored in the lowest NBits of v
 */
template <std::size_t NBits, typename TOut = typename uint_t<NBits>::fast, typename TIn>
constexpr TOut mask_and_right_shift(TIn& v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(detail::is_unsigned<TOut>::value, "result type must be unsigned");
	static_assert(NBits <= bit_size<TIn>::value, "input type is too small");
	static_assert(NBits <= bit_size<TOut>::value, "result type is too small");
	static_assert(!std::is_same<TOut, bool>::value || (NBits == 1), "bool requires NBits == 1");
	static_assert(!std::is_same<TIn, bool>::value || (NBits == 1), "bool requires NBits == 1");
	return detail::mask_and_right_shift<NBits, TOut, TIn>(v);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <std::size_t NBits, typename TOut, typename TIn>
constexpr void mask_and_right_shift(TIn& v, TOut& res) noexcept {
	res = mask_and_right_shift<NBits, TOut, TIn>(v);
}

/**
 * @brief Get the highest NBits from argument, and left shift the argument.
 *
 * @warning Slower than mask_and_right_shift, requiring 2 shifts instead of 1.
 * @tparam NBits	number of bits
 * @tparam TOut		output type (unsigned, must contain at least NBits bits)
 * @tparam TIn		input type (unsigned, must contain at least NBits)
 * @param v			the input value that will be shifted
 * @return the value stored in the highest NBits of v
 */
template <std::size_t NBits, typename TOut = typename uint_t<NBits>::fast, typename TIn>
constexpr TOut mask_and_left_shift(TIn& v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(detail::is_unsigned<TOut>::value, "result type must be unsigned");
	static_assert(NBits <= bit_size<TIn>::value, "input type is too small");
	static_assert(NBits <= bit_size<TOut>::value, "result type is too small");
	static_assert(!std::is_same<TOut, bool>::value || (NBits == 1), "bool requires NBits == 1");
	static_assert(!std::is_same<TIn, bool>::value || (NBits == 1), "bool requires NBits == 1");
	return detail::mask_and_left_shift<NBits, TOut, TIn>(v);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <std::size_t NBits, typename TOut, typename TIn>
constexpr void mask_and_left_shift(TIn& v, TOut& res) noexcept {
	res = mask_and_left_shift<NBits, TOut, TIn>(v);
}

/**
 * @brief Get a value masking argument at given position.
 *
 * @tparam NBits	number of bits
 * @tparam Lsb		start position
 * @tparam TOut		output type (unsigned, must contain at least NBits bits)
 * @tparam TIn		input type (unsigned, must contain at least NBits + Lsb bits)
 * @param v			the input value
 * @return the value stored in the NBits of v starting at Lsb
 */
template <std::size_t NBits, std::size_t Lsb = 0, typename TOut = typename uint_t<NBits>::fast, typename TIn>
constexpr TOut mask_at(TIn v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(detail::is_unsigned<TOut>::value, "result type must be unsigned");
	static_assert(NBits + Lsb <= bit_size<TIn>::value, "input type is too small");
	static_assert(NBits <= bit_size<TOut>::value, "result type is too small");
	static_assert(!std::is_same<TOut, bool>::value || (NBits == 1), "bool requires NBits == 1");
	static_assert(!std::is_same<TIn, bool>::value || ((NBits == 1) && (Lsb == 0)), "bool requires NBits == 1 and Lsb == 0");
	return detail::mask_at<NBits, Lsb, TOut, TIn>(v);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <std::size_t NBits, std::size_t Lsb = 0, typename TOut, typename TIn>
constexpr void mask_at(const TIn v, TOut& res) noexcept {
	res = mask_at<NBits, Lsb, TOut, TIn>(v);
}

/**
 * @brief Test if a bit is set.
 *
 * @tparam Pos		bit index
 * @tparam TIn		input type (unsigned, must contain at least Pos + 1 bits)
 * @param v			the input value
 * @return true if selected bit is set
 */
template <std::size_t Pos, typename TIn>
constexpr bool test(TIn v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(Pos < bit_size<TIn>::value, "input type is too small");
	static_assert(!std::is_same<TIn, bool>::value || (Pos == 0), "bool requires Pos == 0");
	return detail::test<Pos, TIn>(v);
}

/**
 * @brief Set a bit.
 *
 * @tparam Pos		bit index
 * @tparam TIn		input type (unsigned, must contain at least Pos + 1 bits)
 * @param v			the input value
 */
template <std::size_t Pos, typename TIn>
constexpr void set(TIn& v) noexcept {
	static_assert(detail::is_unsigned<TIn>::value, "input type must be unsigned");
	static_assert(Pos < bit_size<TIn>::value, "input type is too small");
	static_assert(!std::is_same<TIn, bool>::value || (Pos == 0), "bool requires Pos == 0");
	detail::set<Pos, TIn>(v);
}

// sanity checks
namespace sanity_checks {

constexpr bool test_bit_size() noexcept {
	bool ret{true};
	ret &= (bit_size<char>::value == CHAR_BIT);
	ret &= (bit_size<std::int8_t>::value == 8);
	ret &= (bit_size<std::int16_t>::value == 16);
	ret &= (bit_size<std::int32_t>::value == 32);
	ret &= (bit_size<std::int64_t>::value == 64);
	ret &= (bit_size<std::uint8_t>::value == 8);
	ret &= (bit_size<std::uint16_t>::value == 16);
	ret &= (bit_size<std::uint32_t>::value == 32);
	ret &= (bit_size<std::uint64_t>::value == 64);
	ret &= (bit_size<std::uint_least8_t>::value >= 8);
	ret &= (bit_size<std::uint_least16_t>::value >= 16);
	ret &= (bit_size<std::uint_least32_t>::value >= 32);
	ret &= (bit_size<std::uint_least64_t>::value >= 64);
	return ret;
}

constexpr bool test_sign_extend() noexcept {
	bool ret{true};
	ret &= (sign_extend<4, std::uint8_t>(0b0000'1111) == 0b1111'1111);
	ret &= (sign_extend<5, std::uint8_t>(0b0000'1111) == 0b0000'1111);
	ret &= (sign_extend<4, std::uint8_t>(0b0000'0111) == 0b0000'0111);
	ret &= (sign_extend<5, std::uint16_t>(0b0000'0000'0001'0001) == 0b1111'1111'1111'0001);
	ret &= (sign_extend<6, std::uint16_t>(0b0000'0000'0001'0001) == 0b0000'0000'0001'0001);
	return ret;
}

constexpr bool test_sign_extend_bad() noexcept {
	bool ret{true};
	// not working if input value bits above NBits are not zero
	ret &= (sign_extend<4, std::uint8_t>(0b1100'1111) != 0b1111'1111);
	ret &= (sign_extend<5, std::uint8_t>(0b1100'1111) != 0b0000'1111);
	return ret;
}

constexpr bool test_sign_extend_cast() noexcept {
	bool ret{true};
#ifdef __cpp_lib_bit_cast
	ret &= (sign_extend_cast<4, std::uint8_t>(0b0000'1111) == std::int8_t{-1});
	ret &= (sign_extend_cast<5, std::uint8_t>(0b0000'1111) == std::int8_t{15});
	ret &= (sign_extend_cast<4, std::uint8_t>(0b0000'0111) == std::int8_t{7});
	ret &= (sign_extend_cast<5, std::uint16_t>(0b0000'0000'0001'0001) == std::int16_t{-15});
	ret &= (sign_extend_cast<6, std::uint16_t>(0b0000'0000'0001'0001) == std::int16_t{17});
#endif
	return ret;
}

constexpr bool test_mask_at() noexcept {
	const std::uint32_t b{0x01234567};
	bool ret{true};
	ret &= (mask_at<4, 0>(b) == uint_t<4>::fast{0x7});
	ret &= (mask_at<4, 8>(b) == uint_t<4>::fast{0x5});
	ret &= (mask_at<12, 20>(b) == uint_t<12>::fast{0x012});
	ret &= (mask_at<32, 0>(b) == uint_t<32>::fast{0x01234567});
	ret &= (mask_at<0, 27>(b) == uint_t<0>::fast{0x0});
	return ret;
}

constexpr bool test_right_shift() noexcept {
	std::uint32_t b{0x01234567};
	bool ret{true};
	right_shift<24>(b);
	ret &= (b == std::uint32_t{0x01});
	return ret;
}

constexpr bool test_left_shift() noexcept {
	std::uint32_t b{0x01234567};
	bool ret{true};
	left_shift<24>(b);
	ret &= (b == std::uint32_t{0x67000000});
	return ret;
}

constexpr bool test_mask_and_right_shift() noexcept {
	std::uint32_t b{0x01234567};
	bool ret{true};
	ret &= (mask_and_right_shift<16>(b) == uint_t<16>::fast{0x4567});
	ret &= (mask_and_right_shift<16>(b) == uint_t<16>::fast{0x0123});
	ret &= (b == 0);
	return ret;
}

constexpr bool test_mask_and_left_shift() noexcept {
	std::uint32_t b{0x01234567};
	bool ret{true};
	ret &= (mask_and_left_shift<16>(b) == uint_t<16>::fast{0x0123});
	ret &= (mask_and_left_shift<16>(b) == uint_t<16>::fast{0x4567});
	ret &= (b == 0);
	return ret;
}

constexpr bool test_mask_and_shift_max() noexcept {
	std::uint64_t b1{0x0123456789abcdef};
	auto b2{b1};
	const auto b_copy{b1};
	bool ret{true};
	ret &= (mask_and_right_shift<64, std::uint64_t>(b1) == b_copy);
	ret &= (mask_and_left_shift<64, std::uint64_t>(b2) == b_copy);
	ret &= (b1 == 0);
	ret &= (b2 == 0);
	return ret;
}

constexpr bool test_test() noexcept {
	std::uint16_t b{0b101};
	bool ret{true};
	ret &= (test<0>(b));
	ret &= (!test<1>(b));
	ret &= (test<2>(b));
	ret &= (!test<3>(b));
	ret &= (!test<4>(b));
	return ret;
}

constexpr bool test_set() noexcept {
	std::uint16_t b{};
	set<0>(b);
	set<2>(b);
	set<3>(b);
	bool ret{true};
	ret &= (b == std::uint16_t{0b1101});
	return ret;
}

BOOST_STATIC_ASSERT(test_bit_size());
BOOST_STATIC_ASSERT(test_sign_extend());
BOOST_STATIC_ASSERT(test_sign_extend_bad());
BOOST_STATIC_ASSERT(test_sign_extend_cast());
BOOST_STATIC_ASSERT(test_mask_at());
BOOST_STATIC_ASSERT(test_right_shift());
BOOST_STATIC_ASSERT(test_left_shift());
BOOST_STATIC_ASSERT(test_mask_and_right_shift());
BOOST_STATIC_ASSERT(test_mask_and_left_shift());
BOOST_STATIC_ASSERT(test_mask_and_shift_max());
BOOST_STATIC_ASSERT(test_test());
BOOST_STATIC_ASSERT(test_set());

} // namespace sanity_checks

} // namespace bit

// import also into caen namespace (for consistency with std)
using bit::bit_cast;
using bit::endian;

} // namespace caen

#undef CAEN_SKIP_ENUM_AUTO_UNSIGNED_TEST

#endif /* CAEN_INCLUDE_CPP_UTILITY_BIT_HPP_ */
