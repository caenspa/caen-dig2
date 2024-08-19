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
*	\file		integer.hpp
*	\brief		Boost.Integer with fast type specialization from cstdint
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_INTEGER_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_INTEGER_HPP_

#include <cstdint>

#include <boost/integer.hpp>

#ifndef CAEN_NO_BOOST_INT_SPECIALIZATION

/*
 * From `boost::int_fast_t` documentation:
 *
 * > By default, the output type is identical to the input type. Eventually,
 * > this code's implementation should be customized for each platform to give
 * > accurate mappings between the built-in types and the easiest-to-manipulate
 * > built-in types. Also, there is no guarantee that the output type actually
 * > is easier to manipulate than the input type.
 *
 * We implement those specializations relying on cstdint types. Usually, main
 * differences are that 16-bit types are replaced by larger types and, at least
 * on Linux, 32-bit types are replaced by 64-bit types, if compiling for 64-bit
 * architectures.
 *
 * Important:
 * Template specializations must be defined before any instantiation. Some Boost
 * libraries, like Boost.Lexical_Cast, use Boost.Integer: compilation fails if
 * their headers are included before this header. This is why we provide a way
 * to compile out this part in case you are fine with the default implementation
 * of `boost::int_fast_t`: define CAEN_NO_BOOST_INT_SPECIALIZATION at compile
 * time. In this case, the default implementation will be used.
 */
namespace boost {

template<> struct int_fast_t<std::uint8_t>	{ using type = std::uint_fast8_t;	};
template<> struct int_fast_t<std::uint16_t>	{ using type = std::uint_fast16_t;	};
template<> struct int_fast_t<std::uint32_t>	{ using type = std::uint_fast32_t;	};
template<> struct int_fast_t<std::uint64_t>	{ using type = std::uint_fast64_t;	};
template<> struct int_fast_t<std::int8_t>	{ using type = std::int_fast8_t;	};
template<> struct int_fast_t<std::int16_t>	{ using type = std::int_fast16_t;	};
template<> struct int_fast_t<std::int32_t>	{ using type = std::int_fast32_t;	};
template<> struct int_fast_t<std::int64_t>	{ using type = std::int_fast64_t;	};

} // namespace boost

#endif

namespace caen {

// prefer caen namespace to be sure specializations in this header are used

using boost::uint_t;
using boost::int_t;
using boost::int_min_value_t;
using boost::int_max_value_t;
using boost::uint_value_t;

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_INTEGER_HPP_ */
