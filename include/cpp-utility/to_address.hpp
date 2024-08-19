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
*	\file		to_address.hpp
*	\brief		`std::to_address` basic replacement
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_TO_ADDRESS_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_TO_ADDRESS_HPP_

#include <type_traits>

namespace caen {

/**
 * @brief Replacement of `std::to_address` for raw pointers.
 *
 * @tparam T	any type, except function
 * @param p		raw pointer
 * @return		p unmodified
 */
template <typename T>
constexpr T* to_address(T* p) noexcept {
	static_assert(!std::is_function<T>::value, "function type not supported");
	return p;
}

/**
 * @brief Basic replacement of `std::to_address` for fancy pointers.
 *
 * This class works like `std::to_address`, except that the support for
 * overloaded `std::pointer_traits<Ptr>::to_address` for fancy pointers is
 * deliberately missing.
 *
 * We could use Boost.Core `boost::to_address` on pre C++20, identical but
 * relying pointer traits on `boost` namespace insted of those on `std`.
 * Moreover, the support for `std::array` iterator is broken on <= Boost 1.79
 * when compiling with Visual Studio 2019.
 *
 * In light of all this, we provide a basic support just to convert iterators
 * to the underlying address.
 *
 * @tparam Ptr	fancy pointer type (e.g. an iterator type)
 * @param p		fancy pointer
 * @return		the value, static-casted to its underlying type
 */
template <typename Ptr>
constexpr auto to_address(const Ptr& p) noexcept {
	return caen::to_address(p.operator->());
}

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_TO_ADDRESS_HPP_ */
