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
*	\file		hw_endpoint.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_HW_ENDPOINT_HPP_
#define CAEN_INCLUDE_ENDPOINTS_HW_ENDPOINT_HPP_

#include <memory>

#include "endpoints/endpoint.hpp"

namespace caen {

namespace dig2 {

struct client; // forward declaration

namespace ep {

struct sw_endpoint; // forward declaration

struct hw_endpoint : public endpoint {

	hw_endpoint(client& client, handle::internal_handle_t endpoint_handle);
	~hw_endpoint();

	virtual void register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) = 0;

	virtual void arm_acquisition() = 0;
	virtual void disarm_acquisition() = 0;

	virtual void event_start() = 0;
	virtual void event_stop() = 0;

private:

	struct endpoint_impl;
	std::unique_ptr<endpoint_impl> _pimpl;

};

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_HW_ENDPOINT_HPP_ */
