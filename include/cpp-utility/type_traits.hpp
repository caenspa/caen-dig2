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
*	\file		type_traits.hpp
*	\brief		replacement for some C++17 traits
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_TYPE_TRAITS_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_TYPE_TRAITS_HPP_

#include <type_traits>
#include <functional>

#if defined(__has_include) && __has_include(<version>)
#include <version> // C++20
#endif

#include <boost/type_traits/make_void.hpp>
#include <boost/static_assert.hpp>

namespace caen {

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

using boost::void_t;

template <bool Val>
struct bool_constant : std::integral_constant<bool, Val> {};

template <typename...>
struct disjunction : std::false_type {};
template<typename B1, typename... Bs>
struct disjunction<B1, Bs...> : std::conditional_t<B1::value, std::true_type, disjunction<Bs...>> {};

template <typename...>
struct conjunction : std::true_type {};
template<typename B1, typename... Bs>
struct conjunction<B1, Bs...> : std::conditional_t<B1::value, conjunction<Bs...>, std::false_type> {};

template <typename B>
struct negation : bool_constant<!bool(B::value)> {};

// `boost::callable_traits::is_invocable` not working with function with empty arguments list
// Implementation from https://stackoverflow.com/a/51188325/3287591
template <typename F, typename... Args>
struct is_invocable :
	std::is_constructible<
		std::function<void(Args...)>,
		std::reference_wrapper<typename std::remove_reference<F>::type>
	> {};

template <typename R, typename F, typename... Args>
struct is_invocable_r :
	std::is_constructible<
		std::function<R(Args...)>,
		std::reference_wrapper<typename std::remove_reference<F>::type>
	> {};

namespace sanity_checks {

constexpr bool test_type_traits() noexcept {
	bool ret{true};
	ret &= (std::is_void<void_t<int, void(int), bool, void>>::value);
	ret &= (bool_constant<true>::value);
	ret &= (!bool_constant<false>::value);
	ret &= (conjunction<std::true_type, std::true_type>::value);
	ret &= (!conjunction<std::false_type, std::true_type>::value);
	ret &= (disjunction<std::false_type, std::true_type>::value);
	ret &= (!disjunction<std::false_type, std::false_type>::value);
	ret &= (negation<std::false_type>::value);
	ret &= (!negation<std::true_type>::value);
	ret &= (is_invocable<void(int), int>::value);
	ret &= (!is_invocable<void(int), int, int>::value);
	ret &= (is_invocable_r<int, int()>::value);
	ret &= (!is_invocable_r<int, void(int), int>::value);
	return ret;
}

BOOST_STATIC_ASSERT(test_type_traits());

} // namespace sanity_checks

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

using std::void_t;
using std::bool_constant;
using std::conjunction;
using std::disjunction;
using std::negation;
using std::is_invocable;
using std::is_invocable_r;

} // namespace cxx17
#endif

#ifndef __cpp_lib_remove_cvref
inline
#endif
namespace no_std_remove_cvref {

template <typename T>
struct remove_cvref {
	typedef std::remove_cv_t<std::remove_reference_t<T>> type;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

} // namespace no_std_remove_cvref

#ifdef __cpp_lib_remove_cvref
inline namespace std_remove_cvref {

using std::remove_cvref;
using std::remove_cvref_t;

} // namespace std_remove_cvref
#endif

template <typename Type, typename... Types>
struct is_type_any_of : disjunction<std::is_same<std::remove_cv_t<Type>, Types>...> {};

namespace sanity_checks {

constexpr bool test_is_type_any_of() noexcept {
	bool ret{true};
	ret &= is_type_any_of<int, char, int>::value;
	ret &= !is_type_any_of<int&, char, int>::value;
	ret &= !is_type_any_of<char, signed char, unsigned char>::value;
	return ret;
}

BOOST_STATIC_ASSERT(test_is_type_any_of());

} // namespace sanity_checks

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_TYPE_TRAITS_HPP_ */
