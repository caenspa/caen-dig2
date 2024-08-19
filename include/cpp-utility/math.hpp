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
*	\file		math.hpp
*	\brief		Math utils
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_MATH_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_MATH_HPP_

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include <boost/core/ignore_unused.hpp>
#include <boost/predef/compiler.h>
#include <boost/predef/version_number.h>
#include <boost/static_assert.hpp>

#include "type_traits.hpp"

namespace caen {

namespace math {

namespace detail {

template <typename Int, typename Float, std::enable_if_t<std::is_unsigned<Int>::value, int> = 0>
Int round_impl(Float v) noexcept {
	return static_cast<Int>(v + Float{0.5});
}
template <typename Int, typename Float, std::enable_if_t<is_type_any_of<Int, signed char, short, int>::value, int> = 0>
Int round_impl(Float v) {
	return static_cast<Int>(std::lround(v));
}
template <typename Int, typename Float, std::enable_if_t<std::is_same<Int, long>::value, int> = 0>
Int round_impl(Float v) {
	return std::lround(v);
}
template <typename Int, typename Float, std::enable_if_t<std::is_same<Int, long long>::value, int> = 0>
Int round_impl(Float v) {
	return std::llround(v);
}

template <typename Int, typename Float>
Int round(Float v) {
	if (v < std::numeric_limits<Int>::lowest() || v > std::numeric_limits<Int>::max())
		throw std::out_of_range("invalid range");
	return round_impl<Int, Float>(v);
}

} // namespace detail

/*
 * cmath constexpr functions are a GCC extension.
 * Moreover, its support is limited on GCC 5 (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67371)
 */
#if BOOST_COMP_GNUC >= BOOST_VERSION_NUMBER(6, 1, 0)
#define CAEN_MATH_CONSTEXPR_SUPPORTED
#define CAEN_MATH_CONSTEXPR constexpr
#else
#define CAEN_MATH_CONSTEXPR
#endif

// generic case for floating point
template <typename T, std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
CAEN_MATH_CONSTEXPR bool is_zero(T v) noexcept {
	return std::abs(v) < std::numeric_limits<T>::epsilon() * T{1e5}; // 1e5 is an arbitrary tolerance factor
}

// special case for integers where epsilon() == 0
template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
constexpr bool is_zero(T v) noexcept {
	return v == T{};
}

template <typename T, std::enable_if_t<std::is_signed<T>::value, int> = 0>
constexpr bool is_negative(T v) noexcept {
	return v < T{};
}

template <typename T, std::enable_if_t<std::is_unsigned<T>::value, int> = 0>
constexpr bool is_negative(T v) noexcept {
	boost::ignore_unused(v);
	return false;
}

template <typename T>
CAEN_MATH_CONSTEXPR bool is_aligned(T v, T step) noexcept {
	return is_zero(std::remainder(v, step));
}

template <typename T>
CAEN_MATH_CONSTEXPR T distance_from_nearest_unit(T v) noexcept {
	static_assert(std::is_floating_point<T>::value, "input type must be floating point number");
	return std::abs(std::remainder(v, 1.));
}

template <typename T>
CAEN_MATH_CONSTEXPR unsigned int digits_after_decimal_point(T v, unsigned int base = 10, T tolerance = 4.) {
	static_assert(std::is_floating_point<T>::value, "input type must be floating point number");
	switch (std::fpclassify(v)) {
	case FP_ZERO:
		return 0;
	case FP_NORMAL: {
		unsigned int n{};
		T eps{tolerance * std::abs(v) * std::numeric_limits<T>::epsilon()};
		while (distance_from_nearest_unit(v) > eps) {
			v *= base;
			eps *= base;
			++n;
		}
		return n;
	}
	case FP_SUBNORMAL:
	case FP_NAN:
	case FP_INFINITE:
	default:
		throw std::domain_error("invalid value");
	}
}

template <typename T>
CAEN_MATH_CONSTEXPR T round_to_nearest_multiple_of(T v, T multiple) {
	if (is_negative(multiple)) {
		throw std::domain_error("multiple cannot be negative");
	} else if (is_zero(multiple)) {
		return v;
	}
	const auto ratio = static_cast<double>(v) / multiple;
	const auto iratio = static_cast<T>(std::lround(ratio));
	return iratio * multiple;
}

template <typename SignedT, typename UnsignedT = std::make_unsigned_t<SignedT>>
constexpr UnsignedT abs(SignedT v) noexcept {
	static_assert(std::is_integral<SignedT>::value, "input type must be integer");
	static_assert(std::is_signed<SignedT>::value, "input type must be signed");
	// see https://stackoverflow.com/a/16101699/3287591
#if BOOST_COMP_MSVC
// Ignoring "unary minus operator applied to unsigned type, result still unsigned"
#pragma warning(push)
#pragma warning(disable: 4146)
#endif
	return v >= 0 ? v : -static_cast<UnsignedT>(v);
#if BOOST_COMP_MSVC
#pragma warning(pop)
#endif
}

/**
 * @brief Safe round floating point numbers to integers.
 *
 * @tparam Int			output integer type
 * @tparam Float		input floating point type
 * @param[in] value		input floating point
 * @return the result of rounding
 */
template <typename Int, typename Float>
Int round(Float value) {
	static_assert(std::is_integral<Int>::value, "output type must be integer");
	static_assert(std::is_floating_point<Float>::value, "input type must be integer");
	return detail::round<Int, Float>(value);
}

/**
 * @brief Convenience function for template argument deduction.
 *
 * @overload
 */
template <typename Int, typename Float>
void round(Int& v, Float value) {
	v = round<Int, Float>(value);
}

namespace sanity_checks {

constexpr bool test_abs() noexcept {
	bool ret{true};
	ret &= (abs(100) == 100U);
	ret &= (abs(0) == 0U);
	ret &= (abs(-100) == 100U);
	return ret;
}

BOOST_STATIC_ASSERT(test_abs());

#ifdef CAEN_MATH_CONSTEXPR_SUPPORTED

constexpr bool test_is_aligned() noexcept {
	bool ret{true};
	// tests for integral values
	ret &= (is_aligned(1, 1));
	ret &= (is_aligned(2, 1));
	ret &= (!is_aligned(1, 2));
	// test for floating point values
	ret &= (is_aligned(0., 1.));
	ret &= (is_aligned(1e0, 1.));
	ret &= (is_aligned(1e10, 1.));
	ret &= (is_aligned(-1e0, 1.));
	ret &= (is_aligned(-1e10, 1.));
	ret &= (!is_aligned(1e-1, 1.));
	ret &= (!is_aligned(1e-10, 1.));
	ret &= (is_aligned(1e0, 1e-3));
	ret &= (is_aligned(1e2, 1e-3));
	ret &= (is_aligned(-1e-3, 1e-3));
	ret &= (is_aligned(-1e2, 1e-3));
	ret &= (!is_aligned(1e-4, 1e-3));
	ret &= (!is_aligned(1e-7, 1e-3));
	ret &= (is_aligned(7e0, 7.));
	ret &= (is_aligned(7e10, 7.));
	ret &= (is_aligned(-7e0, 7.));
	ret &= (is_aligned(-7e10, 7.));
	ret &= (!is_aligned(7e-1, 7.));
	ret &= (!is_aligned(7e-10, 7.));
	ret &= (is_aligned(7e-1, 7e-2));
	ret &= (is_aligned(7e-10, 7e-20));
	ret &= (!is_aligned(0.5, 1.));
	ret &= (!is_aligned(std::numeric_limits<double>::epsilon() * 1e5, 1.));
	// expected failures
	ret &= (is_aligned(std::numeric_limits<double>::min(), 1.)); // within tolerance
	return ret;
}

constexpr bool test_round_to_nearest_multiple_of() noexcept {
	bool ret{true};
	ret &= (round_to_nearest_multiple_of(1, 3) == 0);
	ret &= (round_to_nearest_multiple_of(3, 3) == 3);
	ret &= (round_to_nearest_multiple_of(1, 2) == 2);
	ret &= (round_to_nearest_multiple_of(1, 0) == 1);
	ret &= (round_to_nearest_multiple_of(-1, 3) == 0);
	ret &= (round_to_nearest_multiple_of(-3, 3) == -3);
	ret &= (round_to_nearest_multiple_of(-1, 2) == -2);
	ret &= (round_to_nearest_multiple_of(-1, 0) == -1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
	ret &= (round_to_nearest_multiple_of(1., 0.) == 1.);
	ret &= (round_to_nearest_multiple_of(10., 9.) == 9.);
	ret &= (round_to_nearest_multiple_of(100., 9.) == 99.);
#pragma GCC diagnostic pop
	return ret;
}

constexpr bool test_digits_after_decimal_point() noexcept {
	bool ret{true};
	ret &= (digits_after_decimal_point(0.) == 0);
	ret &= (digits_after_decimal_point(1e-1) == 1);
	ret &= (digits_after_decimal_point(1e-2) == 2);
	ret &= (digits_after_decimal_point(1e-3) == 3);
	ret &= (digits_after_decimal_point(1e-4) == 4);
	ret &= (digits_after_decimal_point(1e-5) == 5);
	ret &= (digits_after_decimal_point(1e-6) == 6);
	ret &= (digits_after_decimal_point(1e-7) == 7);
	ret &= (digits_after_decimal_point(1e-8) == 8);
	ret &= (digits_after_decimal_point(1e-9) == 9);
	ret &= (digits_after_decimal_point(1e-10) == 10);
	ret &= (digits_after_decimal_point(1e-11) == 11);
	ret &= (digits_after_decimal_point(1e-12) == 12);
	ret &= (digits_after_decimal_point(1e-13) == 13);
	ret &= (digits_after_decimal_point(1e-14) == 14);
	ret &= (digits_after_decimal_point(1e-15) == 15);
	ret &= (digits_after_decimal_point(1e-16) == 16);
	ret &= (digits_after_decimal_point(1e-17) == 17);
	ret &= (digits_after_decimal_point(1e-18) == 18);
	ret &= (digits_after_decimal_point(1e-19) == 19);
	ret &= (digits_after_decimal_point(1e-20) == 20);
	ret &= (digits_after_decimal_point(1e-100) == 100);
	ret &= (digits_after_decimal_point(1e-200) == 200);
	ret &= (digits_after_decimal_point(1e-300) == 300);
	ret &= (digits_after_decimal_point(-1e-1) == 1);
	ret &= (digits_after_decimal_point(-1e-2) == 2);
	ret &= (digits_after_decimal_point(-1e-3) == 3);
	ret &= (digits_after_decimal_point(-1e-4) == 4);
	ret &= (digits_after_decimal_point(-1e-5) == 5);
	ret &= (digits_after_decimal_point(-1e-6) == 6);
	ret &= (digits_after_decimal_point(-1e-7) == 7);
	ret &= (digits_after_decimal_point(-1e-8) == 8);
	ret &= (digits_after_decimal_point(-1e-9) == 9);
	ret &= (digits_after_decimal_point(-1e-10) == 10);
	ret &= (digits_after_decimal_point(-1e-11) == 11);
	ret &= (digits_after_decimal_point(-1e-12) == 12);
	ret &= (digits_after_decimal_point(-1e-13) == 13);
	ret &= (digits_after_decimal_point(-1e-14) == 14);
	ret &= (digits_after_decimal_point(-1e-15) == 15);
	ret &= (digits_after_decimal_point(-1e-16) == 16);
	ret &= (digits_after_decimal_point(-1e-17) == 17);
	ret &= (digits_after_decimal_point(-1e-18) == 18);
	ret &= (digits_after_decimal_point(-1e-19) == 19);
	ret &= (digits_after_decimal_point(-1e-20) == 20);
	ret &= (digits_after_decimal_point(-1e-100) == 100);
	ret &= (digits_after_decimal_point(-1e-200) == 200);
	ret &= (digits_after_decimal_point(-1e-300) == 300);
	ret &= (digits_after_decimal_point(1e0) == 0);
	ret &= (digits_after_decimal_point(1e1) == 0);
	ret &= (digits_after_decimal_point(1e2) == 0);
	ret &= (digits_after_decimal_point(1e3) == 0);
	ret &= (digits_after_decimal_point(1e4) == 0);
	ret &= (digits_after_decimal_point(1e5) == 0);
	ret &= (digits_after_decimal_point(1e6) == 0);
	ret &= (digits_after_decimal_point(1e7) == 0);
	ret &= (digits_after_decimal_point(1e8) == 0);
	ret &= (digits_after_decimal_point(1e9) == 0);
	ret &= (digits_after_decimal_point(1e10) == 0);
	ret &= (digits_after_decimal_point(1e11) == 0);
	ret &= (digits_after_decimal_point(1e12) == 0);
	ret &= (digits_after_decimal_point(1e13) == 0);
	ret &= (digits_after_decimal_point(1e14) == 0);
	ret &= (digits_after_decimal_point(1e15) == 0);
	ret &= (digits_after_decimal_point(1e16) == 0);
	ret &= (digits_after_decimal_point(1e100) == 0);
	ret &= (digits_after_decimal_point(1e200) == 0);
	ret &= (digits_after_decimal_point(1e300) == 0);
	ret &= (digits_after_decimal_point(-1e0) == 0);
	ret &= (digits_after_decimal_point(-1e1) == 0);
	ret &= (digits_after_decimal_point(-1e2) == 0);
	ret &= (digits_after_decimal_point(-1e3) == 0);
	ret &= (digits_after_decimal_point(-1e4) == 0);
	ret &= (digits_after_decimal_point(-1e5) == 0);
	ret &= (digits_after_decimal_point(-1e6) == 0);
	ret &= (digits_after_decimal_point(-1e7) == 0);
	ret &= (digits_after_decimal_point(-1e8) == 0);
	ret &= (digits_after_decimal_point(-1e9) == 0);
	ret &= (digits_after_decimal_point(-1e10) == 0);
	ret &= (digits_after_decimal_point(-1e11) == 0);
	ret &= (digits_after_decimal_point(-1e12) == 0);
	ret &= (digits_after_decimal_point(-1e13) == 0);
	ret &= (digits_after_decimal_point(-1e14) == 0);
	ret &= (digits_after_decimal_point(-1e15) == 0);
	ret &= (digits_after_decimal_point(-1e16) == 0);
	ret &= (digits_after_decimal_point(-1e100) == 0);
	ret &= (digits_after_decimal_point(-1e200) == 0);
	ret &= (digits_after_decimal_point(-1e300) == 0);
	ret &= (digits_after_decimal_point(2.0) == 0);
	ret &= (digits_after_decimal_point(2.1) == 1);
	ret &= (digits_after_decimal_point(2.01) == 2);
	ret &= (digits_after_decimal_point(2.001) == 3);
	ret &= (digits_after_decimal_point(2.0001) == 4);
	ret &= (digits_after_decimal_point(2.00001) == 5);
	ret &= (digits_after_decimal_point(2.000001) == 6);
	ret &= (digits_after_decimal_point(2.0000001) == 7);
	ret &= (digits_after_decimal_point(-2.2) == 1);
	ret &= (digits_after_decimal_point(-2.03) == 2);
	ret &= (digits_after_decimal_point(-2.004) == 3);
	ret &= (digits_after_decimal_point(-2.0005) == 4);
	ret &= (digits_after_decimal_point(-2.00006) == 5);
	ret &= (digits_after_decimal_point(-2.000007) == 6);
	ret &= (digits_after_decimal_point(-2.0000008) == 7);
	ret &= (digits_after_decimal_point(1.2) == 1);
	ret &= (digits_after_decimal_point(1.03) == 2);
	ret &= (digits_after_decimal_point(1.004) == 3);
	ret &= (digits_after_decimal_point(1.0005) == 4);
	ret &= (digits_after_decimal_point(1.00006) == 5);
	ret &= (digits_after_decimal_point(1.000007) == 6);
	ret &= (digits_after_decimal_point(1.0000008) == 7);
	ret &= (digits_after_decimal_point(-0.2) == 1);
	ret &= (digits_after_decimal_point(-0.03) == 2);
	ret &= (digits_after_decimal_point(-0.004) == 3);
	ret &= (digits_after_decimal_point(-0.0005) == 4);
	ret &= (digits_after_decimal_point(-0.00006) == 5);
	ret &= (digits_after_decimal_point(-0.000007) == 6);
	ret &= (digits_after_decimal_point(-0.0000008) == 7);
	ret &= (digits_after_decimal_point(0.200) == 1);
	ret &= (digits_after_decimal_point(0.2300) == 2);
	ret &= (digits_after_decimal_point(0.20400) == 3);
	ret &= (digits_after_decimal_point(0.200500) == 4);
	ret &= (digits_after_decimal_point(0.2000600) == 5);
	ret &= (digits_after_decimal_point(0.20000700) == 6);
	ret &= (digits_after_decimal_point(0.200000800) == 7);
	ret &= (digits_after_decimal_point(1. + 1e-1) == 1);
	ret &= (digits_after_decimal_point(1. + 1e-2) == 2);
	ret &= (digits_after_decimal_point(1. + 1e-3) == 3);
	ret &= (digits_after_decimal_point(1. + 1e-4) == 4);
	ret &= (digits_after_decimal_point(1. + 1e-5) == 5);
	ret &= (digits_after_decimal_point(1. + 1e-6) == 6);
	ret &= (digits_after_decimal_point(1. + 1e-7) == 7);
	ret &= (digits_after_decimal_point(1. + 1e-8) == 8);
	ret &= (digits_after_decimal_point(1. + 1e-9) == 9);
	ret &= (digits_after_decimal_point(1. + 1e-10) == 10);
	ret &= (digits_after_decimal_point(1. + 1e-11) == 11);
	ret &= (digits_after_decimal_point(1. + 1e-12) == 12);
	ret &= (digits_after_decimal_point(1. + 1e-13) == 13);
	ret &= (digits_after_decimal_point(1. + 1e-14) == 14);
	ret &= (digits_after_decimal_point(1. + 1e-15) == 15);
	// base 2 tests
	ret &= (digits_after_decimal_point(0.0, 2) == 0);
	ret &= (digits_after_decimal_point(1.0, 2) == 0);
	ret &= (digits_after_decimal_point(0.5, 2) == 1);
	ret &= (digits_after_decimal_point(1.5, 2) == 1);
	ret &= (digits_after_decimal_point(0.25, 2) == 2);
	ret &= (digits_after_decimal_point(1.25, 2) == 2);
	ret &= (digits_after_decimal_point(0.125, 2) == 3);
	ret &= (digits_after_decimal_point(1.125, 2) == 3);
	// expected failures
	ret &= (digits_after_decimal_point(1. + 1e-16) == 0); // 1. + 1e-16 == 1.
	ret &= (digits_after_decimal_point(1. + 2e-16) == 0); // 1. + 2e-16 != 1. but within tolerance
	return ret;
}

BOOST_STATIC_ASSERT(test_is_aligned());
BOOST_STATIC_ASSERT(test_round_to_nearest_multiple_of());
BOOST_STATIC_ASSERT(test_digits_after_decimal_point());

#endif

} // namespace sanity_checks

} // namespace math

} // namespace caen

#undef CAEN_MATH_CONSTEXPR_SUPPORTED

#endif /* CAEN_INCLUDE_CPP_UTILITY_MATH_HPP_ */
