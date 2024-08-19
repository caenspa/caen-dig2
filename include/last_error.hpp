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
*	This file is part of the CAEN Dig2 Library.
*
*	The CAEN Dig2 Library is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	The CAEN Dig2 Library is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Dig2 Library; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		last_error.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_LAST_ERROR_HPP_
#define CAEN_INCLUDE_LAST_ERROR_HPP_

#include <string>

#include <boost/current_function.hpp>

#include "cpp-utility/string_view.hpp"

namespace caen {

namespace dig2 {

namespace last_error {

std::string& instance() noexcept(noexcept(std::string()));

int _handle_exception(caen::string_view func) noexcept;

} // namespace last_error

} // namespace dig2

} // namespace caen

// macro to automatic put function name
#define handle_exception() ::caen::dig2::last_error::_handle_exception(BOOST_CURRENT_FUNCTION)

#endif /* CAEN_INCLUDE_LAST_ERROR_HPP_ */
