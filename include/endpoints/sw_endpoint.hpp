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
*	\file		sw_endpoint.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_SW_ENDPOINT_HPP_
#define CAEN_INCLUDE_ENDPOINTS_SW_ENDPOINT_HPP_

#include <cstddef>
#include <memory>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/byte.hpp"
#include "cpp-utility/integer.hpp"
#include "endpoints/endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct sw_endpoint : public endpoint {

	sw_endpoint(client& client, handle::internal_handle_t endpoint_handle);
	~sw_endpoint();

	virtual void resize() = 0;
	virtual void decode(const caen::byte* p, std::size_t size) = 0;
	virtual void stop() = 0;

protected:
	// common stuff used to decode
	using word_t = std::uint64_t;
	using half_word_t = std::uint32_t;
	static constexpr std::size_t word_size{sizeof(word_t)}; // in bytes
	static constexpr std::size_t half_word_size{sizeof(half_word_t)}; // in bytes
	static constexpr std::size_t word_bit_size{caen::bit::bit_size<word_t>::value}; // in bit
	static constexpr std::size_t half_word_bit_size{caen::bit::bit_size<half_word_t>::value}; // in bit
	static constexpr unsigned int sampling_period_log2{3}; // ns
	static constexpr unsigned int sampling_period{1U << sampling_period_log2}; // ns

	struct evt_header {
		struct s {
			static constexpr std::size_t format{4};
			static constexpr std::size_t implementation_defined{28};
			static constexpr std::size_t n_words{32};
			static_assert(format + implementation_defined + n_words == word_bit_size, "invalid sizes");
		};
		enum struct format : caen::uint_t<s::format>::fast {
			unused					= 0b0000,
			common_trigger_mode		= 0b0001,
			individual_trigger_mode	= 0b0010,
			special_event			= 0b0011,
			special_time_event		= 0b0100,
		};
		format _format;
		// - implementation_defined not saved into event
		caen::uint_t<s::n_words>::fast _n_words;
	};

	template <std::size_t NBits>
	struct timestamp_to_ns {
		using type = typename caen::uint_t<NBits + sampling_period_log2>::fast;
		constexpr type operator()(type v) const noexcept { return v * sampling_period; }
	};

	// decode thread needs to decode event header
	friend struct raw;
	friend struct rawudp;

	bool is_decode_disabled();

private:

	struct endpoint_impl; // forward declaration
	std::unique_ptr<endpoint_impl> _pimpl;

};

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_SW_ENDPOINT_HPP_ */
