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
*	\file		serdes.hpp
*	\brief		Network serialization of integers and enums.
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_SERDES_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_SERDES_HPP_

#include <cstring>
#include <iterator>
#include <type_traits>

#include <boost/endian/conversion.hpp>
#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>
#include <boost/static_assert.hpp>
#include <boost/version.hpp>

#include "bit.hpp"
#include "byte.hpp"
#include "to_address.hpp"
#include "type_traits.hpp"

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_MSVC, <, 19, 32, 0)
/*
 * Visual Studio fails to detect `std::contiguous_iterator` on pointers to volatile types;
 * fixed on MSVC2022 17.2 (_MSC_VER == 1932).
 * See https://developercommunity.visualstudio.com/t/C20-iterator-concepts-fails-on-pointer/10034979
 */
#define CAEN_SERDES_BROKEN_STD_CONTIGUOUS_ITERATOR
#endif

namespace caen {

namespace serdes {

namespace detail {

/*
 * There could be containers that provide random access iterators but not contiguous storage,
 * like the infamous `std::deque` in the STL.
 *
 * The main difference between contiguous iterators and random access iterators is that
 * the former allows, given a pointer to an value_type from an iterator, to perform pointer
 * arithmetic on that pointer, which shall work in exactly the same way as performing the
 * same arithmetic on the corresponding iterators.
 *
 * So, `std::contiguous_iterator_tag` would be correct here, but is C++20, and actually the
 * `std::iterator_traits` specializations for pointers, `std::vector`, `std::array` and
 * `std::string` still still return `std::random_access_iterator_tag` for backward compatibility:
 * there is no way to distinguish iterator traits of `std::deque` from those of contiguous
 * containers. For more details, see https://stackoverflow.com/a/42855677/3287591.
 *
 * Moreover, it is not easy to check if the iterator points to a contiguous container,
 * before C++20, as discussed at https://stackoverflow.com/q/35004633/3287591.
 *
 * The problem is that this checks for `std::random_access_iterator_tag`, and does not filter
 * containers like `std::deque`: in these cases it would be illegal to use reinterpret_cast
 * from char* to other larger types.
 *
 * A correct implementation for C++20, that rejects `std::deque`, is provided. If compiling with
 * pre C++20, it is up to the users of these functions to avoid containers like `std::deque`.
 */
#if __cplusplus < 202002L || defined(CAEN_SERDES_BROKEN_STD_CONTIGUOUS_ITERATOR)
inline
#endif
namespace pre_cxx20_or_broken_std_contiguous_iterator {

template <typename It>
struct is_contiguous_iterator : std::is_convertible<
	typename std::iterator_traits<It>::iterator_category,
	std::random_access_iterator_tag
> {};

} // namespace pre_cxx20_or_broken_std_contiguous_iterator

#if __cplusplus >= 202002L && !defined(CAEN_SERDES_BROKEN_STD_CONTIGUOUS_ITERATOR)
inline namespace cxx20_and_not_broken_std_contiguous_iterator {

template <typename It>
struct is_contiguous_iterator : std::bool_constant<std::contiguous_iterator<It>> {};

} // namespace cxx20_and_not_broken_std_contiguous_iterator
#endif

/*
 * @brief Wrapper to get iterator value_type.
 *
 * Since we are just looking for narrowing conversions, `std::remove_cv_t` removes qualifiers to simplify checks.
 * This also to prevent issues related to LWG issue 2952.
 * @sa https://cplusplus.github.io/LWG/issue2952
 * @sa https://stackoverflow.com/q/71829984/3287591
 * @tparam It		iterator type
 */
template <typename It>
using iterator_value_type_t = std::remove_cv_t<typename std::iterator_traits<It>::value_type>;

/*
 * @brief Metafunction that checks if the iterator value type is any of those provided as argument.
 *
 * @tparam It		iterator type
 * @tparam Types	types to be checked against
 */
template <typename It, typename... Types>
struct is_value_type_any_of : is_type_any_of<iterator_value_type_t<It>, Types...> {};


/*
 * @brief Metafunction that checks if the iterator type is suitable to be used on functions in this header.
 *
 * Compile-time check if iterator is a contiguous iterator and if container data type is a type that supports
 * reinterpret_cast to any other type(`char`, `unsigned char`, `caen::byte` without breaking the strict aliasing rule.
 * An alternative approach would be to use `std::memcpy` instead of reinterpret_cast, that is the safe way to deal with
 * strict aliasing rule: in our case compilers should emit the same code for `reinterpret_cast` and `std::memcpy`.
 * All things considered, we prefer `std::memcpy` because it is safe also for types that have alignment requirements,
 * like float on 32-bit ARM: only in these specific cases compilers should emit different code for `reinterpret_cast`
 * and `std::memcpy`.
 * @note `caen::pre_cxx17::byte` is added in case compiling with C++17 and still using the C++14 portable version.
 * @tparam It		iterator type
 */
template <typename It>
struct is_valid_iterator : caen::conjunction<
	is_contiguous_iterator<It>,
	is_value_type_any_of<
		It,
		char,
		unsigned char,
		caen::byte,
		caen::pre_cxx17::byte
	>
> {};

namespace sanity_checks {

constexpr bool test_is_contiguous_iterator() noexcept {
	bool ret{true};
	ret &= (is_contiguous_iterator<int*>::value);
	ret &= (is_contiguous_iterator<const volatile int*>::value);
	return ret;
}

constexpr bool test_is_value_type_any_of() noexcept {
	bool ret{true};
	ret &= (is_value_type_any_of<int*, bool, char, unsigned char, int>::value);
	ret &= (is_value_type_any_of<const int*, int>::value);
	ret &= (is_value_type_any_of<volatile int*, int>::value);
	ret &= (is_value_type_any_of<const volatile int*, int>::value);
	ret &= (!is_value_type_any_of<short*, bool, char, unsigned char, int>::value);
	ret &= (!is_value_type_any_of<char*, unsigned char, signed char>::value);
	ret &= (!is_value_type_any_of<unsigned char*, char, signed char>::value);
	return ret;
}

constexpr bool test_is_valid_iterator() noexcept {
	bool ret{true};
	ret &= (is_valid_iterator<char*>::value);
	ret &= (is_valid_iterator<unsigned char*>::value);
	ret &= (is_valid_iterator<caen::byte*>::value);
	ret &= (is_valid_iterator<caen::pre_cxx17::byte*>::value);
	ret &= (is_valid_iterator<const char*>::value);
	ret &= (is_valid_iterator<const unsigned char*>::value);
	ret &= (is_valid_iterator<const caen::byte*>::value);
	ret &= (is_valid_iterator<const caen::pre_cxx17::byte*>::value);
	ret &= (is_valid_iterator<volatile char*>::value);
	ret &= (is_valid_iterator<volatile unsigned char*>::value);
	ret &= (is_valid_iterator<volatile caen::byte*>::value);
	ret &= (is_valid_iterator<volatile caen::pre_cxx17::byte*>::value);
	ret &= (is_valid_iterator<const volatile char*>::value);
	ret &= (is_valid_iterator<const volatile unsigned char*>::value);
	ret &= (is_valid_iterator<const volatile caen::byte*>::value);
	ret &= (is_valid_iterator<const volatile caen::pre_cxx17::byte*>::value);
	ret &= (!is_valid_iterator<signed char*>::value);
	ret &= (!is_valid_iterator<const signed char*>::value);
	ret &= (!is_valid_iterator<bool*>::value);
	ret &= (!is_valid_iterator<volatile int*>::value);
	return ret;
}

BOOST_STATIC_ASSERT(test_is_contiguous_iterator());
BOOST_STATIC_ASSERT(test_is_value_type_any_of());
BOOST_STATIC_ASSERT(test_is_valid_iterator());

} // namespace sanity_checks

#if 107000 < BOOST_VERSION && BOOST_VERSION < 107400
/*
 * Since Boost 1.74, Boost.Endian conversion supports boolean and floating point types only with the inplace
 * functions. Floating point types worked by chance also on Boost <= 1.70, but only if source and target endianness
 * were equal, and booleans worked fine on the inplace function provided by <= 1.70, while its support has been
 * broken on 1.71 and fixed again on 1.74.
 *
 * Note also that, according to C++ standard:
 * - sizeof(bool) could be greater than 1
 * - floating point types could be not compliant with IEEE 754
 *
 * For these reason, these types are not portable between different architectures, just like int, long, etc., and
 * should be avoided when transmitting information over a network (prefer cstdint integer types, for floating
 * point types you may add a static assertion on std::numeric_limits<T>::is_iec559).
 *
 * For this reason, support for those types is limited and depends on the Boost version:
 * - BOOST_VERSION <= 107000:
 *   - bool: supported
 *   - float/double/long double: partially supported (only with same endianness)
 * - 107000 < BOOST_VERSION && BOOST_VERSION < 107400:
 *   - bool: not supported
 *   - float/double/long double: not supported
 * - 107400 <= BOOST_VERSION:
 *   - bool: supported
 *   - float/double/long double: supported
 */
template <typename T>
struct is_serdes_supported : caen::conjunction<std::is_integral<T>, caen::negation<std::is_same<T, bool>>> {};
#else
template <typename T>
struct is_serdes_supported : std::is_arithmetic<T> {};
#endif

template <caen::endian> struct boost_endian {}; // default case (compile time error)
template <> struct boost_endian<caen::endian::big>		{ static constexpr auto order = boost::endian::order::big; };
template <> struct boost_endian<caen::endian::little>	{ static constexpr auto order = boost::endian::order::little; };

template <boost::endian::order Endian, typename T, typename It, std::enable_if_t<(is_serdes_supported<T>::value), int> = 0>
T deserialize(It& it) noexcept {
	T v;
	std::memcpy(&v, caen::to_address(it), sizeof(T));
	it += sizeof(T);
	boost::endian::conditional_reverse_inplace<Endian, boost::endian::order::native>(v);
	return v;
}

template <boost::endian::order Endian, typename T, typename It, std::enable_if_t<(std::is_enum<T>::value), int> = 0>
T deserialize(It& it) noexcept {
	return static_cast<T>(deserialize<Endian, std::underlying_type_t<T>>(it));
}

template <boost::endian::order Endian, typename T, typename It, std::enable_if_t<(is_serdes_supported<T>::value), int> = 0>
void serialize(It& it, T v) noexcept {
	boost::endian::conditional_reverse_inplace<boost::endian::order::native, Endian>(v);
	std::memcpy(caen::to_address(it), &v, sizeof(T));
	it += sizeof(T);
}

template <boost::endian::order Endian, typename T, typename It, std::enable_if_t<(std::is_enum<T>::value), int> = 0>
void serialize(It& it, T v) noexcept {
	serialize<Endian>(it, static_cast<std::underlying_type_t<T>>(v));
}

} // namespace detail

/**
 * @brief Decode a value of a given type from a raw buffer, increasing the input iterator.
 *
 * @tparam Endian	the source endianness
 * @tparam TOut		the output type (must be trivially copyable)
 * @tparam It		a pointer or an iterator type to a raw buffer (value type must be char, unsigned char or caen::byte)
 * @param it		the iterator that will be increased by sizeof(T)
 * @return the value
 */
template <caen::endian Endian, typename TOut, typename It>
TOut deserialize_endian(It& it) noexcept {
	static_assert(std::is_trivially_copyable<TOut>::value, "output type must be trivially copyable");
	static_assert(detail::is_valid_iterator<It>::value, "invalid iterator value type");
	return detail::deserialize<detail::boost_endian<Endian>::order, TOut, It>(it);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <caen::endian Endian, typename TOut, typename It>
void deserialize_endian(It& it, TOut& res) noexcept {
	res = deserialize_endian<Endian, TOut, It>(it);
}

/**
 * @brief Encode a value of a given type into a raw buffer, increasing the input iterator.
 *
 * @tparam Endian	the target endianness (big is the default for network)
 * @tparam TIn		the input type (must be trivially copyable)
 * @tparam It		a pointer or an iterator type to a raw buffer (value type must be char, unsigned char or caen::byte)
 * @param it		the iterator that will be increased by sizeof(T)
 * @param v			the value
 */
template <caen::endian Endian, typename TIn, typename It>
void serialize_endian(It& it, TIn v) noexcept {
	static_assert(std::is_trivially_copyable<TIn>::value, "input type must be trivially copyable");
	static_assert(detail::is_valid_iterator<It>::value, "invalid iterator value type");
	detail::serialize<detail::boost_endian<Endian>::order, TIn, It>(it, v);
}

/**
 * @brief Decode a value of a given type from a big-endian raw buffer, increasing the input iterator.
 *
 * @tparam TOut		the output type (must be trivially copyable)
 * @tparam It		a pointer or an iterator type to a raw buffer (value type must be char, unsigned char or caen::byte)
 * @param it		the iterator that will be increased by sizeof(T)
 * @return the value
 */
template <typename TOut, typename It>
TOut deserialize(It& it) noexcept {
	return deserialize_endian<caen::endian::big, TOut, It>(it);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <typename TOut, typename It>
void deserialize(It& it, TOut& res) noexcept {
	res = deserialize<TOut, It>(it);
}

/**
 * @brief Encode a value of a given type into a big-endian raw buffer, increasing the input iterator.
 *
 * @tparam TIn		the input type (must be trivially copyable)
 * @tparam It		a pointer or an iterator type to a raw buffer (value type must be char, unsigned char or caen::byte)
 * @param it		the iterator that will be increased by sizeof(T)
 * @param v			the value
 */
template <typename TIn, typename It>
void serialize(It& it, TIn v) noexcept {
	serialize_endian<caen::endian::big, TIn, It>(it, v);
}

/**
 * @brief Decode a value of a given type from a little-endian raw buffer, increasing the input iterator.
 *
 * @tparam TOut		the output type (must be trivially copyable)
 * @tparam It		a pointer or an iterator type to a raw buffer (value type must be char, unsigned char or caen::byte)
 * @param it		the iterator that will be increased by sizeof(T)
 * @return the value
 */
template <typename TOut, typename It>
TOut deserialize_little(It& it) noexcept {
	return deserialize_endian<caen::endian::little, TOut, It>(it);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <typename TOut, typename It>
void deserialize_little(It& it, TOut& res) noexcept {
	res = deserialize_little<TOut, It>(it);
}

/**
 * @brief Encode a value of a given type into a little-endian raw buffer, increasing the input iterator.
 *
 * @tparam TIn		the input type (must be trivially copyable)
 * @tparam It		a pointer or an iterator type to a raw buffer (value type must be char, unsigned char or caen::byte)
 * @param it		the iterator that will be increased by sizeof(T)
 * @param v			the value
 */
template <typename TIn, typename It>
void serialize_little(It& it, TIn v) noexcept {
	serialize_endian<caen::endian::little, TIn, It>(it, v);
}

} // namespace serdes

// import also into caen namespace
using serdes::deserialize_endian;
using serdes::serialize_endian;
using serdes::deserialize;
using serdes::serialize;
using serdes::deserialize_little;
using serdes::serialize_little;

} // namespace caen

#undef CAEN_SERDES_BROKEN_STD_CONTIGUOUS_ITERATOR

#endif /* CAEN_INCLUDE_CPP_UTILITY_SERDES_HPP_ */
