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
*	\file		variant.hpp
*	\brief		`std::variant` replacement
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_VARIANT_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_VARIANT_HPP_

#include <type_traits>

#if __cplusplus >= 201703L
#include <variant>
#endif

#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>
#include <boost/version.hpp>

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_CLANG, <=, 7, 0, 0)
/**
 * @brief Workaround for Clang bug 33222.
 *
 * Workaround for clang compilation error for std::variant using GCC's 7.1 libstdc++:
 * if needed, use the pre-C++17 fallback.
 * This also requires to rename pre_cxx17 namespace to something else, because
 * somewhere else pre_cxx17 could be inline.
 * @sa https://bugs.llvm.org/show_bug.cgi?id=33222
 */
#define CAEN_VARIANT_WORKAROUND_CLANG_BUG_33222
#endif

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_CLANG, >=, 16, 0, 0)
/**
 * @brief Workaround for Boost.MPL issue 69.
 *
 * Exclude **Boost.Variant** from clang 16 because of a bug in **Boost.MPL**,
 * apparently already workarounded in Boost.Variant 1.81.
 * This patch can be removed when changing minimum Boost version to at least
 * 1.71.0, dropping **Boost.Variant**.
 * This workaround removes the legacy fallback if Boost < 1.81 and compiling
 * with -std=c++14 (that would be weird on clang 16).
 * @sa https://github.com/boostorg/mpl/issues/69
 */
#define CAEN_VARIANT_WORKAROUND_CLANG_16
#if __cplusplus < 201703L && BOOST_VERSION < 108100
#error On clang > 16 you need either C++17 or Boost >= 1.81.0
#endif
#endif

#ifndef CAEN_VARIANT_WORKAROUND_CLANG_16
#include <boost/variant.hpp>
#endif

/*
 * **Boost.Variant2** is provided since Boost 1.71. Unlike **Boost.Variant**,
 * its interface is compatible with `std::variant`, except, like **Boost.Variant**,
 * it provides the "never-empty" guarantee.
 */
#if BOOST_VERSION >= 107100
#include <boost/variant2/variant.hpp>
#define CAEN_VARIANT_USE_BOOST_VARIANT2
#endif

namespace caen {

#ifndef CAEN_VARIANT_WORKAROUND_CLANG_16 // don't compile if not available

#if (__cplusplus < 201703L || defined(CAEN_VARIANT_WORKAROUND_CLANG_BUG_33222)) && !defined(CAEN_VARIANT_USE_BOOST_VARIANT2)
inline
#endif
namespace pre_cxx17_or_clang_7_boost_variant {

/** 
 * @brief Replacement for `std::variant` using **Boost.Variant**.
 *
 * The main differences are:
 * - `boost::variant` allocated on heap
 * - `boost::variant` provides the "never-empty" guarantee
 * - `boost::variant` has no constexpr support
 * - some methods are missing (e.g. `std::holds_alternative` and `std::variant::valueless_by_exception`)
 * - have a different name (e.g. `std::visit`, `std::get_if` and `std::variant::index`)
 *
 * We try to implement the missing stuff, except for `std::get` and emplace() overloads that
 * takes numeric index as template argument instead of a type.
 */
template <typename... T>
class variant : private boost::variant<T...> {
public:

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_CLANG, <, 3, 9, 0)
	/// Base constructor is fine (workaround for a known bug in clang < 3.9)
	template <typename... Args>
	variant(Args&&... args) noexcept(std::is_nothrow_constructible<boost::variant<T...>, Args...>::value)
		: boost::variant<T...>::variant(std::forward<Args>(args)...) {
	}
#else
	/// Base constructor is fine.
	using boost::variant<T...>::variant;
#endif

	/// `boost::variant::operator=` are fine.
	using boost::variant<T...>::operator=;

	/// `boost::variant::swap` is fine but we cannot simply use "using" because of different argument type.
	void swap(variant& rhs) {
		boost::variant<T...>::swap(rhs);
	}
	
	/// `boost::variant::which` is the same of `std::variant::index` except for the return type.
	std::size_t index() const noexcept {
		return static_cast<std::size_t>(this->which());
	}
	
	/// `boost::variant` provides the "never-empty" guarantee, cannot be valueless.
	constexpr bool valueless_by_exception() const noexcept {
		return false;
	}
	
	/// `boost::variant::emplace` is missing and not easy to be implemented; this does almost the same thing.
	template <typename TIn, typename... Args>
	decltype(auto) emplace(Args&&... args) {
		*this = TIn(std::forward<Args>(args)...);
		return boost::get<TIn>(*this);
	}

	/// Not standard but required by `boost::apply_visitor`.
	using typename boost::variant<T...>::types;

	/// Not standard but required by `boost::apply_visitor`.
	using boost::variant<T...>::apply_visitor;

	/// Trick to reuse `boost::get` using private inheritance. Should not be used directly.
	template <typename TOut>
	decltype(auto) _impl_get() const & {
		return boost::get<TOut>(*this);
	}

	/// Trick to reuse `boost::get` using private inheritance. Should not be used directly.
	template <typename TOut>
	decltype(auto) _impl_get() & {
		return boost::get<TOut>(*this);
	}

	/// Trick to reuse `boost::get` using private inheritance. Should not be used directly.
	template <typename TOut>
	decltype(auto) _impl_get() && {
		return boost::get<TOut>(std::move(*this));
	}

	/// Trick to reuse `boost::get` using private inheritance. Should not be used directly.
	template <typename TOut>
	decltype(auto) _impl_get_if() noexcept {
		return boost::get<TOut>(this);
	}

	/// Trick to reuse `boost::get` using private inheritance. Should not be used directly.
	template <typename TOut>
	decltype(auto) _impl_get_if() const noexcept {
		return boost::get<TOut>(this);
	}

};

/**
 * @brief Replacement for `std::monostate`, an empty struct used as placeholder.
 */
struct monostate {};

/**
 * @brief `boost::apply_visitor` is almost identical to `std::visit`, except for the different name.
 */
template <typename... Args>
decltype(auto) visit(Args&& ...args) {
	return boost::apply_visitor(std::forward<Args>(args)...);
}

/**
 * @brief `boost::get` is just fine to replace `std::get`.
 *
 * We use a workaround to avoid public inheritance from `boost::variant`: being a class without
 * virtual destructor, it is not a good idea to inherit from it.
 */
template <typename T, typename Variant>
decltype(auto) get(Variant&& v) {
	return std::forward<Variant>(v).template _impl_get<T>();
}

/**
 * @brief `boost::get` overloads with pointers as argument are renamed `std::get_if` in the standard implementation.
 *
 * In the standard implementation, overloads of `boost::get` with pointers as argument are not overloads
 * but are renamed `std::get_if`.
 * We use a workaround to avoid public inheritance from `boost::variant`: being a class without
 * virtual destructor, it is not a good idea to inherit from it.
 */
template <typename T, typename VariantPtr>
decltype(auto) get_if(VariantPtr* vptr) noexcept {
	return vptr != nullptr ? vptr->template _impl_get_if<T>() : nullptr;
}

/**
 * @brief `boost::variant` has nothing like `std::holds_alternative`, but it can be implemented in terms of get_if().
 */
template<typename TTest, typename... T>
bool holds_alternative(const variant<T...>& v) noexcept {
	return (get_if<TTest>(&v) != nullptr);
}

template <typename T>
struct variant_size;
template <typename T>
struct variant_size<const T> : variant_size<T>::type {};
template <typename T>
struct variant_size<volatile T> : variant_size<T>::type {};
template <typename T>
struct variant_size<const volatile T> : variant_size<T>::type {};

/**
 * @brief `boost::variant` has nothing like `std::variant_size`, but it can be implemented easily.
 */
template <typename... T>
struct variant_size<variant<T...>> : std::integral_constant<std::size_t, sizeof...(T)> {};

} // namespace pre_cxx17_or_clang_7_boost_variant

#endif

#ifdef CAEN_VARIANT_USE_BOOST_VARIANT2 // don't compile if not available
#if (__cplusplus < 201703L || defined(CAEN_VARIANT_WORKAROUND_CLANG_BUG_33222))
inline
#endif
namespace pre_cxx17_or_clang_7_boost_variant2 {

using boost::variant2::variant;
using boost::variant2::monostate;
using boost::variant2::get;
using boost::variant2::get_if;
using boost::variant2::visit;
using boost::variant2::holds_alternative;
using boost::variant2::variant_size;

} // namespace pre_cxx17_or_clang_7_boost_variant2
#endif

#if __cplusplus >= 201703L && !defined(CAEN_VARIANT_WORKAROUND_CLANG_BUG_33222)
inline namespace cxx17 {

using std::variant;
using std::monostate;
using std::get;
using std::get_if;
using std::visit;
using std::holds_alternative;
using std::variant_size;

} // namespace cxx17
#endif

} // namespace caen

#undef CAEN_VARIANT_USE_BOOST_VARIANT2
#undef CAEN_VARIANT_WORKAROUND_CLANG_16
#undef CAEN_VARIANT_WORKAROUND_CLANG_BUG_33222

#endif /* CAEN_INCLUDE_CPP_UTILITY_VARIANT_HPP_ */
