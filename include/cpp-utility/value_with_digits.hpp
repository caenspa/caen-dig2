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
*	\file		value_with_digits.hpp
*	\brief		Floating point value with number of digits
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_VALUE_WITH_DIGITS_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_VALUE_WITH_DIGITS_HPP_

#include <type_traits>

#include <boost/static_assert.hpp>

#include <spdlog/fmt/fmt.h>

namespace caen {

template <typename T>
struct value_with_digits {

	BOOST_STATIC_ASSERT(std::is_floating_point<T>::value);

	constexpr value_with_digits(T value, unsigned int ndigits) noexcept
	: _value{value}
	, _ndigits{ndigits} {}

	constexpr auto get_value() const noexcept { return _value; }
	constexpr auto get_ndigits() const noexcept { return _ndigits; }

	std::string to_string() const {
		return fmt::format("{:.{}f}", _value, _ndigits);
	}

private:
	T _value;
	unsigned int _ndigits;
};

using double_with_digits = value_with_digits<double>;
using float_with_digits = value_with_digits<float>;

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_VALUE_WITH_DIGITS_HPP_ */
