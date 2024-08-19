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
*	\file		aggregate_endpoint.hpp
*	\brief		Aggregate event base endpoint
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_AGGREGATE_ENDPOINT_HPP_
#define CAEN_INCLUDE_ENDPOINTS_AGGREGATE_ENDPOINT_HPP_

#include <memory>

#include "cpp-utility/integer.hpp"
#include "endpoints/sw_endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct aggregate_endpoint : public sw_endpoint {

	aggregate_endpoint(client& client, handle::internal_handle_t endpoint_handle);
	~aggregate_endpoint();

protected:
	struct dpp_aggregate_header {
		struct s {
			static constexpr std::size_t format{evt_header::s::format};
			static constexpr std::size_t flush{1};
			static constexpr std::size_t tbd_1{2};
			static constexpr std::size_t board_fail{1};
			static constexpr std::size_t aggregate_counter{24};
			static constexpr std::size_t n_words{evt_header::s::n_words};
			static_assert(flush + tbd_1 + board_fail + aggregate_counter == evt_header::s::implementation_defined, "invalid sizes");
		};
		// constants
		static constexpr std::size_t aggregate_header_words{1};
		static constexpr std::size_t aggregate_header_size{aggregate_header_words * word_size};
		// fields
		evt_header::format _format;
		bool _flush;
		// - tbd_1 not saved into event
		bool _board_fail;
		caen::uint_t<s::aggregate_counter>::fast _aggregate_counter;
		caen::uint_t<s::n_words>::fast _n_words;
	};

	bool decode_aggregate_header(const caen::byte*& p) noexcept;
	const dpp_aggregate_header& last_aggregate_header() const noexcept;

	/*
	 * Utility functions to handle clear in aggregate endpoints: if a clear() is sent while
	 * the decode() is in the middle of a loop of an aggregate, we require an asynchronous
	 * flag to interrupt the decode and discard remaining events of the current aggregate.
	 */

	// shall be called by clear_data()
	void require_clear() noexcept;

	// shall be invoked by decode() before beginning a new hit
	bool is_clear_required_and_reset() noexcept;

private:
	struct aggregate_endpoint_impl; // forward declaration
	std::unique_ptr<aggregate_endpoint_impl> _pimpl;
};

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_AGGREGATE_ENDPOINT_HPP_ */
