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
*	\file		vector.hpp
*	\brief		`std::vector` with default initialization allocator
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_VECTOR_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_VECTOR_HPP_

#include <memory>
#include <type_traits>
#include <vector>

#include <boost/config.hpp>
#include <boost/range/algorithm/fill.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION >= 107100
#include <boost/core/noinit_adaptor.hpp>
#define CAEN_USE_BOOST_NOINIT_ADAPTOR
#endif

#include <spdlog/spdlog.h>

namespace caen {

#ifndef CAEN_USE_BOOST_NOINIT_ADAPTOR
inline
#endif
namespace caen_noinit_adaptor {

/**
 * @brief Custom allocator to reduce resize complexity of `std::vector` of POD data.
 * 
 * Allocator adaptor that interposes construct() calls to convert value initialization
 * into default initialization. Used by default @ref caen::vector.
 * @tparam Allocator	The allocator (e.g. `std::allocator<T>`)
 * @sa https://stackoverflow.com/a/21028912/3287591
 */
template <typename Allocator>
struct noinit_adaptor : public Allocator {

private:
	using allocator_traits = std::allocator_traits<Allocator>;

public:
	template <typename U>
	struct rebind {
		using other = noinit_adaptor<typename allocator_traits::template rebind_alloc<U>>;
	};

	using Allocator::Allocator;

	/**
	 * @brief Perform default initialization.
	 * 
	 * If no argument is provided, use this convenience overload to perform
	 * default initialization (note `U` without braces): it is a no-op on POD
	 * and the default constructor in case of class type.
	 * @sa https://en.cppreference.com/w/cpp/language/default_initialization
	 * @sa https://en.cppreference.com/w/cpp/memory/allocator/construct
	 * @note This is the core of this class.
	 * @note Implemented just like `std::allocator<T>::construct` without braces.
	 */
	template <typename U>
	void construct(U* ptr) noexcept(std::is_nothrow_default_constructible<U>::value) {
		::new (const_cast<void*>(static_cast<const volatile void*>(ptr))) U;
	}

};

} // namespace caen_noinit_adaptor

#ifdef CAEN_USE_BOOST_NOINIT_ADAPTOR
inline namespace boost_noinit_adaptor {

using boost::noinit_adaptor;

} // namespace boost_noinit_adaptor
#endif

/**
 * @brief Same of `std::vector`, with an allocator designed to improve performance on POD types.
 *
 * If the type of elements is a POD, then `resize` is much faster
 * since is done with default constructor instead of value initialization used by
 * `std::allocator<T>` (i.e. memory is not filled to zero on vector::resize()).
 * @tparam T			the type of the elements
 * @tparam Allocator	the allocator, set to @ref caen::noinit_adaptor by default
 */
template <typename T, typename Allocator = noinit_adaptor<std::allocator<T>>>
using vector = std::vector<T, Allocator>;

/**
 * @brief Clear and release memory allocated by a vector.
 *
 * `vector::clear()` does not modify vector capacity unless we call
 * `vector::shrink_to_fit()`, that however is not required to release
 * memory. This approach, instead, actually releases the memory.
 * Just for completeness, until C++11 the copy assignment did not
 * release memory, and a `vector::swap()` approach was required.
 * @tparam T			type of the elements
 * @tparam Allocator	allocator
 * @param v				vector
 */
template <typename T, typename Allocator>
void reset(vector<T, Allocator>& v) noexcept {
	v = vector<T, Allocator>();
}

/**
 * @brief Clear vector and set new capacity, releasing unnecessary memory.
 *
 * Since `std::allocator<T>` uses `new`/`delete`, that are never implemented with
 * `realloc`, every reallocation is performed with `malloc`/`memcpy`/`free`.
 * So, in case we need a different capacity, either bigger or smaller,
 * we just recreate a new vector, to be sure to optimize memory usage.
 * @tparam Vector		type of the vector
 * @param v				vector
 * @param new_capacity	new capacity
 */
template <typename T, typename Allocator>
void reserve(vector<T, Allocator>& v, typename vector<T, Allocator>::size_type new_capacity) {
	if (new_capacity != v.capacity()) {
		reset(v);
		v.reserve(new_capacity);
	} else {
		v.clear();
	}
}

/**
 * @brief Clear vector.
 *
 * @tparam T			type of the elements
 * @tparam Allocator	allocator
 * @param v				vector
 */
template <typename T, typename Allocator>
void clear(vector<T, Allocator>& v) noexcept {
	v.clear();
}

/**
 * @brief Resize vector with debug log message if requires reallocation.
 *
 * @tparam T			type of the elements
 * @tparam Allocator	allocator
 * @param v				vector
 * @param new_size		new size
 */
template <typename T, typename Allocator>
void resize(vector<T, Allocator>& v, typename vector<T, Allocator>::size_type new_size) {
	if (BOOST_UNLIKELY(v.capacity() < new_size))
		SPDLOG_DEBUG("need to reallocate memory (current capacity: {}, needed: {})", v.capacity(), new_size);
	v.resize(new_size);
}

/**
 * @brief Set all values to `typename vector<T, Allocator>::value_type{}`.
 *
 * @tparam T			type of the elements
 * @tparam Allocator	allocator
 * @param v				vector
 */
template <typename T, typename Allocator>
void set_default(vector<T, Allocator>& v) noexcept {
	boost::fill(v, typename vector<T, Allocator>::value_type{});
}

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

/**
 * @brief Wrapper to port returning behavior of `emplace_back` on C++14.
 *
 * @tparam T			type of the elements
 * @tparam Allocator	allocator
 * @param v				vector
 * @param args			emplace_back arguments
 */
template <typename T, typename Allocator, typename... Args>
decltype(auto) emplace_back(vector<T, Allocator>& v, Args&& ...args) {
	return v.emplace_back(std::forward<Args>(args)...), v.back();
}

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

/**
 * @brief Wrapper to port returning behavior of `emplace_back` on C++14.
 *
 * @tparam T			the type of the elements
 * @tparam Allocator	the allocator
 * @param v				the vector
 * @param args			emplace_back arguments
 */
template <typename T, typename Allocator, typename... Args>
decltype(auto) emplace_back(vector<T, Allocator>& v, Args&& ...args) {
	return v.emplace_back(std::forward<Args>(args)...);
}

} // namespace cxx17
#endif

} // namespace caen

#undef CAEN_USE_BOOST_NOINIT_ADAPTOR

#endif /* CAEN_INCLUDE_CPP_UTILITY_VECTOR_HPP_ */