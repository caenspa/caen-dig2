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
*	\file		library_logger.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_LIBRARY_LOGGER_HPP_
#define CAEN_INCLUDE_LIBRARY_LOGGER_HPP_

#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h> // required to print custom objects

#include "cpp-utility/optional.hpp"

namespace caen {

namespace dig2 {

namespace library_logger {

/**
 * Initialize the logger stuff and print version of included libraries on the main sink
 */
void init();

/**
 * Create a new logger to print on the main sink
 * @param name		the logger name
 * @return a new logger instance
 */
std::shared_ptr<spdlog::logger> create_logger(const std::string& name);

/**
 * Create a new logger to print on the main sink with custom optional level
 * @param name		the logger name
 * @param level		optional logger level to override default level
 * @return a new logger instance
 */
std::shared_ptr<spdlog::logger> create_logger(const std::string& name, const caen::optional<spdlog::level::level_enum>& level);

} // namespace library_logger

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_LIBRARY_LOGGER_HPP_ */
