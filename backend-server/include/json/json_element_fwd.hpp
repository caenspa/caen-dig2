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
*	This file is part of the CAEN Back-end Server.
*
*	The CAEN Back-end Server is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	TheCAEN Back-end Server is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Back-end Server; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		json_element_fwd.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_ELEMENT_FWD_HPP_
#define CAEN_INCLUDE_JSON_JSON_ELEMENT_FWD_HPP_

struct json_element;

namespace element {

enum class node_type {
	UNKNOWN,
	PARAMETER,
	FEATURE,
	ENDPOINT,
	CMD,
};

enum class data_type {
	UNKNOWN,
	STRING,
	NUMBER,
};

enum class access_mode {
	UNKNOWN,
	READ_ONLY,
	WRITE_ONLY,
	READ_WRITE,
};

enum class level {
	UNKNOWN,
	DIGITIZER,
	CHANNEL,
	LVDS,
	VGA,
	ENDPOINT,
	FOLDER,
	GROUP,
};

} // namespace element

#endif /* CAEN_INCLUDE_JSON_JSON_ELEMENT_FWD_HPP_ */
