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
*	\file		aggregate_endpoint.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "endpoints/aggregate_endpoint.hpp"

#include <atomic>

#include <boost/assert.hpp>
#include <boost/core/ignore_unused.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/serdes.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct aggregate_endpoint::aggregate_endpoint_impl {

	aggregate_endpoint_impl()
		: _clear_flag{}
		, _last_aggregate_header{} {

		/*
		 * Required because until C++20 default std::atomic_flag constructor leaves flag
		 * in uninitialized status and, since C++20, default state is clear.
		 * Nevertheless, it does not makes any difference at runtime, since then test_and_set
		 * (in decode) is always called after a clear (in clear_data)
		 */
#if __cplusplus < 202002L
		_clear_flag.clear();
#endif
	}

	void require_clear() noexcept {
		_clear_flag.clear();
	}

	bool is_clear_required_and_reset() noexcept {
		return !_clear_flag.test_and_set();
	}

	auto& last_aggregate_header() noexcept {
		return _last_aggregate_header;
	}

	const auto& last_aggregate_header() const noexcept {
		return _last_aggregate_header;
	}

private:

	std::atomic_flag _clear_flag;
	dpp_aggregate_header _last_aggregate_header;

};

aggregate_endpoint::aggregate_endpoint(client& client, handle::internal_handle_t endpoint_handle)
	: sw_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<aggregate_endpoint_impl>()} {}

aggregate_endpoint::~aggregate_endpoint() = default;

bool aggregate_endpoint::decode_aggregate_header(const caen::byte*& p) noexcept {

	const auto p_begin = p;
	const auto p_end = p_begin + dpp_aggregate_header::aggregate_header_size;

	word_t word;

	// 1st aggregate word (mask_and_left_shift is slower but is used here to decode format first)
	caen::serdes::deserialize(p, word);

	// decode format on separated variable to avoid dereference _pimpl if format is not individual_trigger_mode
	const auto format = caen::bit::mask_and_left_shift<dpp_aggregate_header::s::format, evt_header::format>(word);
	if (format != evt_header::format::individual_trigger_mode)
		return false;

	auto& agg = _pimpl->last_aggregate_header();

	// reuse format already decoded
	agg._format = format;

	// continue 1st aggregate word
	caen::bit::mask_and_left_shift<dpp_aggregate_header::s::flush>(word, agg._flush);
	caen::bit::left_shift<dpp_aggregate_header::s::tbd_1>(word);
	caen::bit::mask_and_left_shift<dpp_aggregate_header::s::board_fail>(word, agg._board_fail);
	caen::bit::mask_and_left_shift<dpp_aggregate_header::s::aggregate_counter>(word, agg._aggregate_counter);
	caen::bit::mask_and_left_shift<dpp_aggregate_header::s::n_words>(word, agg._n_words);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	BOOST_ASSERT_MSG(p == p_end, "inconsistent decoding");
	boost::ignore_unused(p_end);

	return true;
}

const aggregate_endpoint::dpp_aggregate_header& aggregate_endpoint::last_aggregate_header() const noexcept {
	// const added manually to call const overload, because const-ness is not propagated on pimpl idiom
	const auto& impl = *_pimpl;
	return impl.last_aggregate_header();
}

bool aggregate_endpoint::is_clear_required_and_reset() noexcept {
	return _pimpl->is_clear_required_and_reset();
}

void aggregate_endpoint::require_clear() noexcept {
	_pimpl->require_clear();
}

} // namespace ep

} // namespace dig2

} // namespace caen
