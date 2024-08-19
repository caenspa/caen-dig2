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
*	\file		socket_option.hpp
*	\brief		Platform independent extension to Boost.ASIO socket options
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_SOCKET_OPTION_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_SOCKET_OPTION_HPP_

#include <boost/asio.hpp>
#include <boost/predef/os.h>

#if BOOST_OS_WINDOWS
#include <winsock2.h>
#elif BOOST_OS_MACOS || BOOST_OS_LINUX
#include <netinet/tcp.h>
#else
#error unsupported operating system
#endif

namespace caen {

namespace socket_option {

using keep_interval = boost::asio::detail::socket_option::integer<BOOST_ASIO_OS_DEF(IPPROTO_TCP), TCP_KEEPINTVL>;
using keep_cnt = boost::asio::detail::socket_option::integer<BOOST_ASIO_OS_DEF(IPPROTO_TCP), TCP_KEEPCNT>;
#if BOOST_OS_MACOS
using keep_idle = boost::asio::detail::socket_option::integer<BOOST_ASIO_OS_DEF(IPPROTO_TCP), TCP_KEEPALIVE>;
#else
using keep_idle = boost::asio::detail::socket_option::integer<BOOST_ASIO_OS_DEF(IPPROTO_TCP), TCP_KEEPIDLE>;
#endif

} // namespace socket_option

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_SOCKET_OPTION_HPP_ */
