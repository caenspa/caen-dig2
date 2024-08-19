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
*	\file		args.hpp
*	\brief		Utilities to copy data structures to `std::va_list`
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_ARGS_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_ARGS_HPP_

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#include <boost/static_assert.hpp>

#include "type_traits.hpp"

namespace caen {

namespace args {

namespace detail {

template<class T, typename = void>
struct container_traits {};

template <typename Container>
struct container_traits<Container, caen::void_t<typename Container::value_type>> {
	using value_type = typename Container::value_type;
};

template <typename T, std::size_t N>
struct container_traits<T[N]> {
	using value_type = T;
};

/*
 * @brief Wrapper of to get value_type from containers, including C arrays.
 *
 * Since we are just looking for narrowing conversions, std::remove_cv_t removes qualifiers to simplify checks.
 */
template <typename Container>
using container_value_type_t = std::remove_cv_t<typename container_traits<Container>::value_type>;

/*
 * @brief Wrapper to get iterator value_type.
 *
 * Since we are just looking for narrowing conversions, std::remove_cv_t removes qualifiers to simplify checks.
 * This also to prevent issues related to LWG issue 2952.
 * @sa https://stackoverflow.com/q/71829984/3287591.
 */
template <typename It>
using iterator_value_type_t = std::remove_cv_t<typename std::iterator_traits<It>::value_type>;

/*
 * @brief Metafunction to detect if a conversion is narrowing.
 *
 * List-initialization limits the allowed implicit conversions by prohibiting the narrowing conversions.
 * This metafunction should not be used with enumerators because the direct-list-initialization behavior
 * from int to enumerator has been changed since C++17. Moreover, the metafunction is broken on some
 * compilers:
 * - MSCV: narrowing conversion not detected from scoped enums to underlying type integer (fixed on MSVC 19.31)
 * - GCC: narrowing conversion not detected from unscoped enums, whose underlying type is not fixed,
 *     to integers (bug #105255, not fixed as of GCC 11)
 * @sa https://en.cppreference.com/w/cpp/language/list_initialization.
 * @tparam TOut		output type
 * @tparam TIn		input type
 */
template <typename TOut, typename TIn, typename = void>
struct is_narrowing_conversion : std::true_type {};

template <typename TOut, typename TIn>
struct is_narrowing_conversion<TOut, TIn, caen::void_t<decltype(TOut{std::declval<TIn>()})>> : std::false_type {};

/*
 * @brief Same of @ref is_narrowing_conversion, with type conversion.
 *
 * @tparam It			output iterator type
 * @tparam Container	input container type
 */
template <typename It, typename Container, typename Iv = iterator_value_type_t<It>, typename Cv = container_value_type_t<Container>>
struct use_explicit_cast : is_narrowing_conversion<Iv, Cv> {};

/*
 * @brief Metafunction to restrict to arithmetic types (i.e. no enumerators and other stuff).
 *
 * @tparam It			output iterator type
 * @tparam Container	input container type
 */
template <typename It, typename Container, typename Iv = iterator_value_type_t<It>, typename Cv = container_value_type_t<Container>>
struct are_arithmetic_types : caen::conjunction<
	std::is_arithmetic<Iv>,
	std::is_arithmetic<Cv>
> {};

/*
 * @brief Metafuncion returning true if explicit cast is not required and if parameters are arithmetic types.
 *
 * @tparam It			output iterator type
 * @tparam Container	input container type
 */
template <typename It, typename Container>
struct use_copy : caen::conjunction<
	caen::negation<use_explicit_cast<It, Container>>,
	are_arithmetic_types<It, Container>
> {};

/*
 * @brief Metafuncion returning true if explicit cast is required and if parameters are arithmetic types.
 *
 * @tparam It			output iterator type
 * @tparam Container	input container type
 */
template <typename It, typename Container>
struct use_transform : caen::conjunction<
	use_explicit_cast<It, Container>,
	are_arithmetic_types<It, Container>
> {};

/*
 * @brief Functor that provides explicit static_cast.
 *
 * @tparam It	output iterator type
 */
template <typename It>
struct cast_into {

	using result_type = iterator_value_type_t<It>;

	template <typename TIn>
	constexpr result_type operator()(TIn&& value) const noexcept {
		static_assert(std::is_nothrow_constructible<result_type, decltype(value)>::value, "it is better to restrict the use to noexcept conversion");
		return static_cast<result_type>(std::forward<TIn>(value));
	}

};

namespace sanity_checks {

enum struct my_scoped_enum : int {};

constexpr bool test_container_value_type_t() noexcept {
	bool ret{true};
	ret &= (std::is_same<container_value_type_t<char[1]>, char>::value);
	ret &= (std::is_same<container_value_type_t<const char[1]>, char>::value);
	ret &= (std::is_same<container_value_type_t<volatile char[1]>, char>::value);
	ret &= (std::is_same<container_value_type_t<const volatile char[1]>, char>::value);
	ret &= (!std::is_same<container_value_type_t<char[1]>, const char>::value);
	return ret;
}

constexpr bool test_iterator_value_type_t() noexcept {
	bool ret{true};
	ret &= (std::is_same<iterator_value_type_t<char*>, char>::value);
	ret &= (std::is_same<iterator_value_type_t<const char*>, char>::value);
	ret &= (std::is_same<iterator_value_type_t<volatile char*>, char>::value);
	ret &= (std::is_same<iterator_value_type_t<const volatile char*>, char>::value);
	ret &= (!std::is_same<iterator_value_type_t<char*>, const char>::value);
	return ret;
}

constexpr bool test_use_explicit_cast() noexcept {
	bool ret{true};
	// cstdint (std::int32_t)
	ret &= (!use_explicit_cast<std::int32_t*, std::uint8_t[1]>::value);
	ret &= (!use_explicit_cast<std::int32_t*, std::int8_t[1]>::value);
	ret &= (!use_explicit_cast<std::int32_t*, std::uint16_t[1]>::value);
	ret &= (!use_explicit_cast<std::int32_t*, std::int16_t[1]>::value);
	ret &= (use_explicit_cast<std::int32_t*, std::uint32_t[1]>::value);
	ret &= (!use_explicit_cast<std::int32_t*, std::int32_t[1]>::value);
	ret &= (use_explicit_cast<std::int32_t*, std::uint64_t[1]>::value);
	ret &= (use_explicit_cast<std::int32_t*, std::int64_t[1]>::value);
	// cstdint (std::uint32_t)
	ret &= (!use_explicit_cast<std::uint32_t*, std::uint8_t[1]>::value);
	ret &= (use_explicit_cast<std::uint32_t*, std::int8_t[1]>::value);
	ret &= (!use_explicit_cast<std::uint32_t*, std::uint16_t[1]>::value);
	ret &= (use_explicit_cast<std::uint32_t*, std::int16_t[1]>::value);
	ret &= (!use_explicit_cast<std::uint32_t*, std::uint32_t[1]>::value);
	ret &= (use_explicit_cast<std::uint32_t*, std::int32_t[1]>::value);
	ret &= (use_explicit_cast<std::uint32_t*, std::uint64_t[1]>::value);
	ret &= (use_explicit_cast<std::uint32_t*, std::int64_t[1]>::value);
	// const / volatile
	ret &= (!use_explicit_cast<int*, int[1]>::value);
	ret &= (!use_explicit_cast<int*, const int[1]>::value);
	ret &= (!use_explicit_cast<int*, volatile int[1]>::value);
	ret &= (!use_explicit_cast<int*, const volatile int[1]>::value);
	// floating point
	ret &= (!use_explicit_cast<double*, double[1]>::value);
	ret &= (!use_explicit_cast<double*, float[1]>::value);
	ret &= (use_explicit_cast<float*, double[1]>::value);
	ret &= (use_explicit_cast<int*, float[1]>::value);
	ret &= (use_explicit_cast<float*, int[1]>::value);
	// no check for enumerators (see comments above)
	return ret;
}

constexpr bool test_are_arithmetic_types() noexcept {
	bool ret{true};
	// numeric types
	ret &= (are_arithmetic_types<bool*, char[1]>::value);
	ret &= (are_arithmetic_types<std::uint16_t*, std::uint64_t[1]>::value);
	// enumerators (always false)
	ret &= (!are_arithmetic_types<my_scoped_enum*, std::uint64_t[1]>::value);
	ret &= (!are_arithmetic_types<std::uint64_t*, my_scoped_enum[1]>::value);
	ret &= (!are_arithmetic_types<my_scoped_enum*, my_scoped_enum[1]>::value);
	return ret;
}

constexpr bool test_use_copy() noexcept {
	bool ret{true};
	// numeric types
	ret &= (use_copy<bool*, bool[1]>::value);
	ret &= (use_copy<std::uint8_t*, bool[1]>::value);
	ret &= (use_copy<std::uint8_t*, std::uint8_t[1]>::value);
	// enumerators (always false)
	ret &= (!use_copy<my_scoped_enum*, int[1]>::value);
	ret &= (!use_copy<int*, my_scoped_enum[1]>::value);
	ret &= (!use_copy<my_scoped_enum*, my_scoped_enum[1]>::value);
	return ret;
}

constexpr bool test_use_transform() noexcept {
	bool ret{true};
	// numeric types
	ret &= (use_transform<bool*, std::uint8_t[1]>::value);
	ret &= (use_transform<std::uint8_t*, std::uint16_t[1]>::value);
	// enumerators (always false)
	ret &= (!use_transform<my_scoped_enum*, int[1]>::value);
	ret &= (!use_transform<int*, my_scoped_enum[1]>::value);
	ret &= (!use_transform<my_scoped_enum*, my_scoped_enum[1]>::value);
	return ret;
}

BOOST_STATIC_ASSERT(test_container_value_type_t());
BOOST_STATIC_ASSERT(test_iterator_value_type_t());
BOOST_STATIC_ASSERT(test_use_explicit_cast());
BOOST_STATIC_ASSERT(test_are_arithmetic_types());
BOOST_STATIC_ASSERT(test_use_copy());
BOOST_STATIC_ASSERT(test_use_transform());

} // namespace sanity_checks

/*
 * @brief Standard version with explicit cast to silent narrowing conversion warnings.
 */
template <typename It, typename TIn>
void insert_value(It p, TIn&& value) noexcept {
	*p = cast_into<It>{}(std::forward<TIn>(value));
}

/*
 * @brief Standard version with `std::copy`, optimized to `std::memmove` when possible.
 *
 * `std::copy` is perfect: it is implemented in terms of a simple value assignment, and can be optimized
 * with `std::memmove` when appropriate. Unfortunately, since the implicit conversion could be a narrowing
 * conversions, compilers emits warnings like -Wconversion (gcc/clang) or C4244 (MSVC).
 */
template <typename It, typename Container, std::enable_if_t<use_copy<It, Container>::value, int> = 0>
void insert_array(It p, const Container& value) noexcept {
	std::copy(std::cbegin(value), std::cend(value), p);
}

/*
 * @brief Fallback version with `std::transform` and explicit cast to avoid warnings about possible loss of data.
 *
 * Indeed, since the behavior is the same of `std::copy`, we could also use always `std::transform`.
 * Unfortunately, no compiler seems to be able to optimize this with `std::memmove` when the type is the same,
 * so we explictly use `std::copy` in that case.
 */
template <typename It, typename Container, std::enable_if_t<use_transform<It, Container>::value, int> = 0>
void insert_array(It p, const Container& value) noexcept {
	std::transform(std::cbegin(value), std::cend(value), p, cast_into<It>{});
}

} // namespace detail

/**
 * @brief Copy a variable in a `std::va_list`.
 * 
 * @pre Not restricted to arithmetic types (enum accepted both as input and output).
 * @tparam TOut			output type
 * @tparam TIn			input type (must be convertible to @p TOut with no exception)
 * @param args			a pointer to initialized `std::va_list`
 * @param value			the value to be copied
 */
template <typename TOut, typename TIn>
void insert_value(std::va_list* args, TIn&& value) noexcept {
	const auto p = va_arg(*args, TOut*);
	detail::insert_value(p, std::forward<TIn>(value));
}

/**
 * @brief Copy raw data in a `std::va_list`.
 * 
 * It is always optimized with `std::memmove`.
 * @tparam TOut			output type
 * @tparam TIn			input type (must be the same of @p TOut, except for constness)
 * @param args			a pointer to initialized `std::va_list`
 * @param p_in			a pointer to source data
 * @param size			number of elements to copy
 */
template <typename TOut, typename TIn>
void insert_raw_data(std::va_list* args, const TIn* p_in, std::size_t size) noexcept {
	static_assert(std::is_same<TOut, std::remove_const_t<TIn>>::value, "type must be the same");
	static_assert(std::is_trivially_copyable<TIn>::value, "is_trivially_copyable is required for std::memmove");
	const auto p = va_arg(*args, TOut*);
	std::copy_n(p_in, size, p);
}

/**
 * @brief Copy an array in a `std::va_list`.
 *
 * It is optimized with `std::memmove` when possible.
 * @pre Only arithmetic types are accepted (no enum, struct, ...).
 * @tparam TOut			output type
 * @tparam Container	container type (value type must be convertible to @p TOut with no exception)
 * @param args			a pointer to initialized `std::va_list`
 * @param value			the container
 */
template <typename TOut, typename Container>
void insert_array(std::va_list* args, const Container& value) noexcept {
	const auto p = va_arg(*args, TOut*);
	detail::insert_array(p, value);
}

/**
 * @brief Copy a matrix in a `std::va_list`.
 * 
 * It is optimized with `std::memmove` when possible.
 * @pre Only arithmetic types are accepted (no enum, struct, ...).
 * @tparam TOut			output type
 * @tparam Container	container type, that must be a container of containers (value type must be convertible to @p TOut with no exception)
 * @param args			a pointer to initialized `std::va_list`
 * @param value			the container matrix
 */
template <typename TOut, typename Container>
void insert_matrix(std::va_list* args, const Container& value) noexcept {
	auto p = va_arg(*args, TOut**);
	for (auto&& v : value)
		detail::insert_array(*p++, std::forward<decltype(v)>(v));
}

} // namespace args

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_ARGS_HPP_ */
