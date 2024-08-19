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
*	\file		dpp_probe_types.hpp
*	\brief		Enumerators for common DPP probe type codes
*	\author		Bruno Angelucci, Giovanni Cerretani, Alberto Potenza
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_DPP_PROBE_TYPES_HPP_
#define CAEN_INCLUDE_ENDPOINTS_DPP_PROBE_TYPES_HPP_

#include <cstdint>

namespace caen {

namespace dig2 {

namespace ep {

enum struct dpp_digital_probe_type : std::uint8_t {
	unknown							= 0xff,
	// common (PHA firmware values)
	trigger							= 0b00000,
	time_filter_armed				= 0b00001,
	re_trigger_guard				= 0b00010,
	energy_filter_baseline_freeze	= 0b00011,
	event_pile_up					= 0b00111,
	// PHA specific (PHA firmware values)
	energy_filter_peaking			= 0b00100,
	energy_filter_peak_ready		= 0b00101,
	energy_filter_pile_up_guard		= 0b00110,
	adc_saturation					= 0b01000,
	adc_saturation_protection		= 0b01001,
	post_saturation_event			= 0b01010,
	energy_filter_saturation		= 0b01011,
	signal_inhibit					= 0b01100,
	// PSD specific (PSD firmware values with offset 0b10000)
	over_threshold					= 0b10100,
	charge_ready					= 0b10101,
	long_gate						= 0b10110,
	short_gate						= 0b11000,
	input_saturation				= 0b11001,
	charge_over_range				= 0b11010,
	negative_over_threshold			= 0b11011,
};

enum struct dpp_analog_probe_type : std::uint8_t {
	unknown							= 0xff,
	// common (PHA firmware values)
	adc_input						= 0b0000,
	// PHA specific (PHA firmware values)
	time_filter						= 0b0001,
	energy_filter					= 0b0010,
	energy_filter_baseline			= 0b0011,
	energy_filter_minus_baseline	= 0b0100,
	// PSD specific (PSD firmware values with offset 0b1000)
	baseline						= 0b1001,
	cfd								= 0b1010,
};

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_DPP_PROBE_TYPES_HPP_ */
