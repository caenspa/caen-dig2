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
*	\file		string.hpp
*	\brief		String utilities
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_STRING_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_STRING_HPP_

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <locale>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/assert.hpp>
#include <boost/static_assert.hpp>

#include "counting_range.hpp"
#include "math.hpp"
#include "type_traits.hpp"

namespace caen {

namespace string {

namespace detail {

template<class T, typename = void>
struct has_size : std::false_type {};

template <typename T>
struct has_size<T, void_t<decltype(std::declval<const T&>().size())>> : std::true_type {};

template <typename... Types>
struct all_have_size : conjunction<has_size<Types>...> {};

template<typename Range1T, typename Range2T, std::enable_if_t<all_have_size<Range1T, Range2T>::value, int> = 0>
bool iequals(const Range1T& input, const Range2T& test) {
	return (test.size() == input.size()) && boost::iequals<Range1T, Range2T>(input, test);
}

template<typename Range1T, typename Range2T, std::enable_if_t<!all_have_size<Range1T, Range2T>::value, int> = 0>
bool iequals(const Range1T& input, const Range2T& test) {
	return boost::iequals<Range1T, Range2T>(input, test);
}

template <typename T, int Base, typename String, std::enable_if_t<is_type_any_of<T, bool, unsigned char, unsigned short, unsigned>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	const auto temp_result = std::stoul(str, &sz, Base);
	if (std::numeric_limits<T>::max() < temp_result)
		throw std::out_of_range("stob/stouc/stous/stou");
	return static_cast<T>(temp_result);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, unsigned long>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	return std::stoul(str, &sz, Base);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, unsigned long long>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	return std::stoull(str, &sz, Base);
}
template <typename T, int Base, typename String, std::enable_if_t<is_type_any_of<T, signed char, short>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	const auto temp_result = std::stoi(str, &sz, Base);
	if (temp_result < std::numeric_limits<T>::lowest() || std::numeric_limits<T>::max() < temp_result)
		throw std::out_of_range("stosc/stos");
	return static_cast<T>(temp_result);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, int>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	return std::stoi(str, &sz, Base);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, long>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	return std::stol(str, &sz, Base);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, long long>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	return std::stoll(str, &sz, Base);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, float>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	static_assert(Base == 0, "Base template argument is unused, kept for consistency in the template signature. Must be zero.");
	return std::stof(str, &sz);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, double>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	static_assert(Base == 0, "Base template argument is unused, kept for consistency in the template signature. Must be zero.");
	return std::stod(str, &sz);
}
template <typename T, int Base, typename String, std::enable_if_t<std::is_same<T, long double>::value, int> = 0>
T string_to_number_impl(const String& str, std::size_t& sz) {
	static_assert(Base == 0, "Base template argument is unused, kept for consistency in the template signature. Must be zero.");
	return std::stold(str, &sz);
}

template <typename T, int Base, typename String>
T string_to_number(const String& str) {
	std::size_t sz;
	const T result = string_to_number_impl<T, Base>(str, sz);
	if (sz < str.size())
		throw std::invalid_argument("string_to_number: unexpected characters");
	return result;
}

template <typename T, typename Float, std::enable_if_t<std::is_integral<T>::value, int> = 0>
T float_to_number(Float value) {
	return caen::math::round<T>(value);
}
template <typename T, typename Float, std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
T float_to_number(Float value) {
	if (value < std::numeric_limits<T>::lowest() || std::numeric_limits<T>::max() < value)
		throw std::out_of_range("value cannot be represented");
	return static_cast<T>(value);
}

template <typename T, typename Float = double, typename String>
T string_to_number_safe(const String& str) {
	static_assert(std::is_floating_point<Float>::value, "intermediate type must be floating point number");
	// first convert to floating point, then convert it to desired type
	return float_to_number<T>(string_to_number<Float, 0>(str));
}

template <typename Char, std::enable_if_t<std::is_same<Char, char>::value, int> = 0>
bool is_space(Char c) {
	// unsigned char cast needed, provided by to_int_type (https://en.cppreference.com/w/cpp/string/byte/isspace)
	return std::isspace(std::char_traits<char>::to_int_type(c));
}
template <typename Char, std::enable_if_t<std::is_same<Char, wchar_t>::value, int> = 0>
bool is_space(Char c) {
	// see comment on char version
	return std::iswspace(std::char_traits<wchar_t>::to_int_type(c));
}
template <typename Char, std::enable_if_t<!is_type_any_of<Char, char, wchar_t>::value, int> = 0>
bool is_space(Char c) {
	// generic version, slower since depends on std::locale
	return std::isspace(c, std::locale{});
}

template <typename Char, std::enable_if_t<std::is_same<Char, char>::value, int> = 0>
bool is_printable(Char c) {
	// see commend on is_space
	return std::isprint(std::char_traits<char>::to_int_type(c)) != 0;
}

template <typename Char, std::enable_if_t<std::is_same<Char, wchar_t>::value, int> = 0>
bool is_printable(Char c) {
	// see commend on is_space
	return std::iswprint(std::char_traits<wchar_t>::to_int_type(c)) != 0;
}

template <typename Char, std::enable_if_t<!is_type_any_of<Char, char, wchar_t>::value, int> = 0>
bool is_printable(Char c) {
	// see commend on is_space
	return std::isprint(c, std::locale());
}

template <typename Char>
struct null_terminator : std::integral_constant<Char, std::char_traits<Char>::to_char_type(0)> {};

template <typename Char, typename String>
String pointer_to_string_safe(const Char* ptr, typename String::size_type max_len) {
	BOOST_ASSERT_MSG(!is_printable(null_terminator<Char>::value), "invalid implementation");
	if (ptr == nullptr)
		return String{};
	const auto range = caen::counting_range(max_len);
	const auto it = std::find_if_not(range.begin(), range.end(), [ptr](auto i) { return is_printable(ptr[i]); });
	if (it == range.end() || ptr[*it] != null_terminator<Char>::value)
		return String{};
	return String(ptr, *it);
}

template <typename Char, typename String>
void string_to_pointer_safe(Char* dst, const String& src, typename String::size_type max_size) {
	if (dst == nullptr)
		return;
	if (src.size() >= max_size)
		throw std::runtime_error("string too long to be copied");
	const auto n = src.copy(dst, max_size - 1);
	dst[n] = null_terminator<Char>::value;
}

namespace sanity_checks {

constexpr bool test_null_terminator() noexcept {
	bool ret{true};
	ret &= (null_terminator<char>::value == *"");
	ret &= (null_terminator<wchar_t>::value == *L"");
	ret &= (null_terminator<char16_t>::value == *u"");
	ret &= (null_terminator<char32_t>::value == *U"");
	return ret;
}

BOOST_STATIC_ASSERT(test_null_terminator());

} // namespace sanity_checks

} // namespace detail

/**
 * @brief Same of `boost::iequals`, with possible performance improvement based on string sizes.
 *
 * `boost::iequals` only compares strings by iterating character by character,
 * but if input types have `size()` method with complexity O(1) this could
 * provide a faster check without any dereferences. We could use the generic
 * `boost::size`/`std::size` to extend this approach also when input is a char
 * array, but it returns the size of the array including the null terminator.
 * We could subtract 1 to this value, but seems to become too elaborated.
 * Using `std::char_traits<Char>::length` would require iterating the array,
 * and then vanishing any advantage of this approach. So, the approach of
 * `size()` is used only if both input types provides a `size()` method,
 * assuming that it has complexity O(1).
 * @tparam Range1T		input string type
 * @tparam Range2T		test string type
 * @param[in] input		input string
 * @param[in] test		test string
 * @return true if strings are equal (case insensitive)
 */
template<typename Range1T, typename Range2T>
bool iequals(const Range1T& input, const Range2T& test) {
	return detail::iequals<Range1T, Range2T>(input, test);
}

/**
 * @brief Convert string to any arithmetic type, with intermediate conversion to double.
 *
 * We cannot use directly `std::sto*` functions when using strings generated
 * by converting `double` to string, that could also have exponential
 * representation. So, we first convert string to `double`, then trying
 * to cast intermediate `double` to desired type.
 * @tparam T			output numeric type
 * @tparam String		input string type
 * @param[in] value		input string
 * @return the result of conversion
 */
template <typename T, typename String>
T to_number_safe(const String& value) {
	static_assert(std::is_arithmetic<T>::value, "output type must be arithmetic");
	return detail::string_to_number_safe<T>(value);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <typename T, typename String>
void to_number_safe(T& v, const String& value) {
	v = to_number_safe<T>(value);
}

/**
 * @brief Convert string to any arithmetic type.
 *
 * Safe wrapper to the `std::sto*` functions. Similar also to the functions
 * provided by `caen::conversion::lexical_cast()`. Except for the underlying
 * implementation, the main difference is that this can also be used to parse
 * values with base auto-detection (like "0x", "0X" or "0"), not supported by
 * the `caen::conversion::lexical_cast()`.
 * @warning The behavior of this function depends on the current C locale (that could be different from the global C++ locale).
 * @tparam T			output numeric type
 * @tparam Base			base argument passed to `std::sto*` functions.
 * @tparam String		input string type
 * @param[in] value		input string
 * @return the result of conversion
 */
template <typename T, int Base = 0, typename String>
T to_number(const String& value) {
	static_assert(std::is_arithmetic<T>::value, "output type must be arithmetic");
	return detail::string_to_number<T, Base>(value);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * Template argument order is changed to allow base specification with
 * the deduction of the other template arguments.
 * @overload
 */
template <int Base = 0, typename T, typename String>
void to_number(T& v, const String& value) {
	v = to_number<T, Base>(value);
}

/**
 * @brief Remove space characters from string
 *
 * @tparam String		input string type
 * @param[in] value		input string
 * @return a string with any space character removed
 */
template <typename String>
decltype(auto) remove_spaces(String&& value) {
	auto res = std::forward<String>(value); // copy
	auto it = std::remove_if(res.begin(), res.end(), [](auto c) { return detail::is_space(c); });
	res.erase(it, res.end());
	return res;
}

/**
 * @brief Remove space characters from string
 *
 * @tparam String		input string type
 * @tparam Range		range type
 * @tparam Vector		output type
 * @param[in] value		input string
 * @param[in] range		a set of characters to be recognized as delimiters
 * @return split input string as a container
 */
template <typename String, typename Range, typename Vector = std::vector<remove_cvref_t<String>>>
decltype(auto) split_string(String&& value, Range&& range) {
	Vector ret;
	boost::split(ret, std::forward<String>(value), boost::is_any_of(std::forward<Range>(range)));
	return ret;
}

/**
 * @brief Convenience overload with "|" as delimiter.
 *
 * @overload
 */
template <typename String>
decltype(auto) split_string(String&& value) {
	return split_string(std::forward<String>(value), "|");
}

/**
 * @brief Inverse of split_string overload for "|" as delimiter.
 *
 * @tparam Container	container type
 * @param[in] value		input container
 * @return a concatenated string
 */
template <typename Container>
decltype(auto) join_string(Container&& value) {
	return boost::join(std::forward<Container>(value), "|");
}

template <typename Char, typename String = std::basic_string<Char>>
String pointer_to_string_safe(const Char* src, typename String::size_type max_size) {
	BOOST_STATIC_ASSERT(std::is_same<Char, typename String::value_type>::value);
	return detail::pointer_to_string_safe<Char, String>(src, max_size);
}

template <typename Char, typename String>
void string_to_pointer_safe(Char* dst, const String& src, typename String::size_type max_size) {
	BOOST_STATIC_ASSERT(std::is_same<Char, typename String::value_type>::value);
	detail::string_to_pointer_safe<Char, String>(dst, src, max_size);
}

/**
 * @brief Split a string like "0x10=24" in (0x10, 24)
 *
 * @tparam String		string type
 * @tparam NumberLeft	left number type
 * @tparam NumberRight	right number type
 * @param[in] value		input string
 * @param[out] lh		left number
 * @param[out] rh		right number
 */
template <typename String, typename NumberLeft, typename NumberRight>
void parse_values_with_equal(String&& value, NumberLeft& lh, NumberRight& rh) {
	const auto v = split_string(std::forward<String>(value), "=");
	if (v.size() != 2)
		throw std::invalid_argument(value);
	to_number(lh, v[0]);
	to_number(rh, v[1]);
}

} // namespace string

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_STRING_HPP_ */
