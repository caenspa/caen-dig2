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
*	\file		lexical_cast.hpp
*	\brief		Utilities related to `boost::lexical_cast`
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_LEXICAL_CAST_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_LEXICAL_CAST_HPP_

#include <cstddef>
#include <type_traits>

// see comment on integer.hpp for rationale of this useless include
#include "integer.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/static_assert.hpp>

#include "type_traits.hpp"

namespace caen {

namespace conversion {

namespace detail {

template <typename Type>
struct is_char : is_type_any_of<Type, signed char, unsigned char> {};

template <typename Target, typename Source>
struct no_char : negation<disjunction<is_char<Target>, is_char<Source>>> {};

template <typename Target, typename Source>
struct is_char_target : conjunction<is_char<Target>, negation<is_char<Source>>> {};

template <typename Target, typename Source>
struct is_char_source : conjunction<negation<is_char<Target>>, is_char<Source>> {};

template <typename Target, typename Source>
struct is_char_both : conjunction<is_char<Target>, is_char<Source>> {};

template <typename> struct intermediate_impl {}; // default case (compile time error)
template <> struct intermediate_impl<signed char> { using type = signed int; };
template <> struct intermediate_impl<unsigned char> { using type = unsigned int; };

template <typename Type>
struct intermediate : intermediate_impl<std::remove_cv_t<Type>> {};

template <typename Type>
using intermediate_t = typename intermediate<Type>::type;

namespace sanity_check {

constexpr bool test_is_char() noexcept {
	bool ret{true};
	ret &= (!is_char<char>::value);
	ret &= (is_char<signed char>::value);
	ret &= (is_char<unsigned char>::value);
	ret &= (!is_char<const char>::value);
	ret &= (is_char<const signed char>::value);
	ret &= (is_char<const unsigned char>::value);
	ret &= (!is_char<volatile char>::value);
	ret &= (is_char<volatile signed char>::value);
	ret &= (is_char<volatile unsigned char>::value);
	ret &= (!is_char<const volatile char>::value);
	ret &= (is_char<const volatile signed char>::value);
	ret &= (is_char<const volatile unsigned char>::value);
	return ret;
}

constexpr bool test_is_char_target_or_source() noexcept {
	bool ret{true};
	ret &= (no_char<char, char>::value);
	ret &= (!no_char<unsigned char, char>::value);
	ret &= (is_char_target<signed char, char>::value);
	ret &= (!is_char_target<signed char, signed char>::value);
	ret &= (is_char_source<char, signed char>::value);
	ret &= (!is_char_source<signed char, signed char>::value);
	ret &= (is_char_both<signed char, unsigned char>::value);
	ret &= (!is_char_both<char, signed char>::value);
	return ret;
}

constexpr bool test_intermediate() noexcept {
	bool ret{true};
	ret &= (std::is_same<intermediate_t<signed char>, signed int>::value);
	ret &= (std::is_same<intermediate_t<unsigned char>, unsigned int>::value);
	return ret;
}

BOOST_STATIC_ASSERT(test_is_char());
BOOST_STATIC_ASSERT(test_is_char_target_or_source());
BOOST_STATIC_ASSERT(test_intermediate());

} // namespace sanity_check

template <typename Target, typename Source, std::enable_if_t<no_char<Target, Source>::value, int> = 0>
decltype(auto) lexical_cast(const Source& arg) {
	// standard case
	return boost::lexical_cast<Target, Source>(arg);
}

template <typename Target, typename Source, std::enable_if_t<is_char_target<Target, Source>::value, int> = 0>
decltype(auto) lexical_cast(const Source& arg) {
	// intermediate result to int, then numeric_cast to throw if overflows target
	const auto int_res = boost::lexical_cast<intermediate_t<Target>>(arg);
	return boost::numeric_cast<Target>(int_res);
}

template <typename Target, typename Source, std::enable_if_t<is_char_source<Target, Source>::value, int> = 0>
decltype(auto) lexical_cast(const Source& arg) {
	// intermediate source to int: would fail at compile time only if intermediate_t<Source>{arg} would be a narrowing conversion
	return boost::lexical_cast<Target>(intermediate_t<Source>{arg});
}

template <typename Target, typename Source, std::enable_if_t<is_char_both<Target, Source>::value, int> = 0>
decltype(auto) lexical_cast(const Source& arg) {
	// strange case, a mix of the two previous cases: numeric_cast could be sufficient, lexical_cast kept for consistency
	const auto int_res = boost::lexical_cast<intermediate_t<Target>>(intermediate_t<Source>{arg});
	return boost::numeric_cast<Target>(arg);
}

template <typename Target, typename CharT, std::enable_if_t<!is_char<Target>::value, int> = 0>
decltype(auto) lexical_cast(const CharT* size, std::size_t count) {
	// standard case
	return boost::lexical_cast<Target>(size, count);
}

template <typename Target, typename CharT, std::enable_if_t<is_char<Target>::value, int> = 0>
decltype(auto) lexical_cast(const CharT* size, std::size_t count) {
	// intermediate result to int, then numeric_cast to throw if overflows target
	const auto int_res = boost::lexical_cast<intermediate_t<Target>>(size, count);
	return boost::numeric_cast<Target>(int_res);
}

} // namespace detail

/**
 * @brief Same of `boost::lexical_cast`, except that `signed char` and `unsigned char` are treated as integer types.
 *
 * `boost::lexical_cast` treats `signed char` and `unsigned char` just like
 * `char`, both as input and output type. This could be undesirable and not
 * intuitive at all: we add a wrapper to add intermediate conversions to integers
 * when dealing with `signed char` and `unsigned char`, using `boost::numeric_cast`
 * to throw in case of overflows.
 * Note that this hack has effect also on `std::int8_t` and `std::uint8_t`,
 * typically implemented as typedef to these types.
 * @warning String to number with base detection is not supported. Use `caen::string::to_number()` if required.
 * @warning The behavior of this function depends on the current global C++ locale.
 * @sa https://www.boost.org/doc/libs/1_67_0/doc/html/boost_lexical_cast/frequently_asked_questions.html
 * @tparam Target		output type
 * @tparam Args			parameter pack forwarded to `boost::lexical_cast`
 * @param[in] args		parameter pack name
 * @return a variable of Target type
 */
template <typename Target, typename... Args>
decltype(auto) lexical_cast(Args&& ...args) {
	return detail::lexical_cast<Target>(std::forward<Args>(args)...);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @tparam Target		the output type, that can be deduced from argument
 * @tparam Args			parameter pack forwarded to `caen::conversion::lexical_cast()`
 * @param[out] result	the output
 * @param[in] args		parameter pack name
 */
template <typename Target, typename... Args>
void lexical_cast_to(Target& result, Args&& ...args) {
	result = lexical_cast<Target>(std::forward<Args>(args)...);
}

} // namespace conversion

// import also into caen namespace
using conversion::lexical_cast;
using conversion::lexical_cast_to;

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_LEXICAL_CAST_HPP_ */
