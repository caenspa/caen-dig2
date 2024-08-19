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
*	\file		dependent_false.hpp
*	\brief		Dependent false, utility for if-constexpr
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_DEPENDENT_FALSE_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_DEPENDENT_FALSE_HPP_

#include <type_traits>

namespace caen {

/*
 * @brief Utility to use static_assert on if-constexpr.
 *
 * @sa https://stackoverflow.com/a/14637534/3287591
 */
template<typename T>
struct dependent_false : std::false_type {};

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_DEPENDENT_FALSE_HPP_ */
