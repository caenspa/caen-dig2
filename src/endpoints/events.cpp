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
*	\file		events.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "endpoints/events.hpp"

#include <boost/assert.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/to_underlying.hpp"
#include "endpoints/hw_endpoint.hpp"
#include "lib_definitions.hpp"
#include "lib_error.hpp"

using namespace std::literals;

namespace caen {

namespace dig2 {

namespace ep {

events::events(client& client, hw_endpoint& hw_endpoint)
	: sw_endpoint(client, handle::invalid_server_handle)
	, _logger{library_logger::create_logger("evt_ep"s)}
	, _hw_endpoint{hw_endpoint} {
}

events::~events() = default;

void events::set_data_format(const std::string &json_format) {
	boost::ignore_unused(json_format);
	throw ex::not_yet_implemented(__func__);
}

void events::read_data(timeout_t timeout, std::va_list* args) {
	boost::ignore_unused(timeout, args);
	throw ex::not_yet_implemented(__func__);
}

void events::has_data(timeout_t timeout) {
	boost::ignore_unused(timeout);
	throw ex::not_yet_implemented(__func__);
}

void events::clear_data() {
}

void events::resize() {
}

void events::decode(const caen::byte* p, std::size_t size) {

	const auto p_begin = p;
	const auto p_end = p_begin + size;

	special_evt evt;

	word_t word;

	// 1st word (mask_and_left_shift is slower but is used here to decode format first)
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_left_shift<special_evt::s::format>(word, evt._format);
	if (evt._format != evt_header::format::special_event)
		return;

	caen::bit::mask_and_left_shift<special_evt::s::event_id>(word, evt._event_id);
	caen::bit::left_shift<special_evt::s::tdb_1>(word);
	caen::bit::mask_and_left_shift<special_evt::s::n_additional_headers>(word, evt._n_additional_headers);
	caen::bit::mask_and_left_shift<special_evt::s::n_words>(word, evt._n_words);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	BOOST_ASSERT_MSG(size == evt._n_words * word_size, "inconsistent size");
	BOOST_ASSERT_MSG(evt._n_additional_headers <= evt._n_words - 1, "inconsistent number of additional headers");

	const std::size_t additional_header_size{evt._n_additional_headers * word_size};
	const auto p_end_additional_headers = p + additional_header_size;

	// numeric cast throws if result overflows size_t
	const auto n_additional_headers = boost::numeric_cast<std::size_t>(evt._n_additional_headers);

	// additional words
	evt._additional_headers.resize(n_additional_headers); // caen::resize() useless here
	for (auto& ah : evt._additional_headers) {
		caen::serdes::deserialize(p, word);
		caen::bit::mask_and_right_shift<special_evt::s::additional_header_data>(word, ah._data);
		caen::bit::mask_and_right_shift<special_evt::s::additional_header_type>(word, ah._type);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
	}

	BOOST_ASSERT_MSG(p == p_end_additional_headers, "inconsistent header decoding");
	boost::ignore_unused(p_end_additional_headers);

	SPDLOG_LOGGER_TRACE(_logger, "event (id={})", caen::to_underlying(evt._event_id));

	// process event
	switch (evt._event_id) {
	case special_evt::event_id_type::start: {

		auto& ah = evt._event_data.emplace<special_evt::start_event_data>(); // initialize variant

		using s_ah = std::remove_reference_t<decltype(ah)>;
		BOOST_ASSERT_MSG(evt._event_id == s_ah::event_id, "inconsistent event id");

		if (BOOST_UNLIKELY(evt._n_additional_headers != s_ah::n_additional_headers))
			throw ex::runtime_error(fmt::format("inconsistent number of additional headers (n_additional_headers={})", evt._n_additional_headers));

		auto ah_type = evt._additional_headers[0]._type;
		auto ah_data = evt._additional_headers[0]._data;
		BOOST_ASSERT_MSG(ah_type == special_evt::additional_header_type_type::acq_width, "unexpected additional header type");
		boost::ignore_unused(ah_type);
		caen::bit::mask_and_right_shift<s_ah::s::acq_width>(ah_data, ah._acq_width);
		caen::bit::mask_and_right_shift<s_ah::s::n_traces>(ah_data, ah._n_traces);
		caen::bit::mask_and_right_shift<s_ah::s::decimation_factor_log2>(ah_data, ah._decimation_factor_log2);
		caen::bit::right_shift<s_ah::s::tbd_1>(ah_data);
		BOOST_ASSERT_MSG(!ah_data, "inconsistent word decoding");

		ah_type = evt._additional_headers[1]._type;
		ah_data = evt._additional_headers[1]._data;
		BOOST_ASSERT_MSG(ah_type == special_evt::additional_header_type_type::size_32, "unexpected additional header type");
		boost::ignore_unused(ah_type);
		caen::bit::mask_and_right_shift<s_ah::s::ch_mask_31_0>(ah_data, ah._ch_mask_31_0);
		caen::bit::right_shift<s_ah::s::tbd_2>(ah_data);
		BOOST_ASSERT_MSG(!ah_data, "inconsistent word decoding");

		ah_type = evt._additional_headers[2]._type;
		ah_data = evt._additional_headers[2]._data;
		BOOST_ASSERT_MSG(ah_type == special_evt::additional_header_type_type::size_32, "unexpected additional header type");
		boost::ignore_unused(ah_type);
		caen::bit::mask_and_right_shift<s_ah::s::ch_mask_63_32>(ah_data, ah._ch_mask_63_32);
		caen::bit::right_shift<s_ah::s::tbd_3>(ah_data);
		BOOST_ASSERT_MSG(!ah_data, "inconsistent word decoding");

		_hw_endpoint.event_start();

		break;
	}
	case special_evt::event_id_type::stop: {

		auto& ah = evt._event_data.emplace<special_evt::stop_event_data>(); // initialize variant

		using s_ah = std::remove_reference_t<decltype(ah)>;
		BOOST_ASSERT_MSG(evt._event_id == s_ah::event_id, "inconsistent event id");

		if (BOOST_UNLIKELY(evt._n_additional_headers != s_ah::n_additional_headers))
			throw ex::runtime_error(fmt::format("inconsistent number of additional headers (n_additional_headers={})", evt._n_additional_headers));

		auto ah_type = evt._additional_headers[0]._type;
		auto ah_data = evt._additional_headers[0]._data;
		BOOST_ASSERT_MSG(ah_type == special_evt::additional_header_type_type::size_48, "unexpected additional header type");
		boost::ignore_unused(ah_type);
		caen::bit::mask_and_right_shift<s_ah::s::evt_time_tag>(ah_data, ah._evt_time_tag);
		caen::bit::right_shift<s_ah::s::tbd_1>(ah_data);
		BOOST_ASSERT_MSG(!ah_data, "inconsistent word decoding");

		ah_type = evt._additional_headers[1]._type;
		ah_data = evt._additional_headers[1]._data;
		BOOST_ASSERT_MSG(ah_type == special_evt::additional_header_type_type::size_32, "unexpected additional header type");
		boost::ignore_unused(ah_type);
		caen::bit::mask_and_right_shift<s_ah::s::dead_time>(ah_data, ah._dead_time);
		caen::bit::right_shift<s_ah::s::tbd_2>(ah_data);
		BOOST_ASSERT_MSG(!ah_data, "inconsistent word decoding");

		_hw_endpoint.event_stop();

		break;
	}
	default:
		_logger->warn("unsupported event id {}", caen::to_underlying(evt._event_id));
		break;
	}

	BOOST_ASSERT_MSG(p == p_end, "inconsistent decoding");
	boost::ignore_unused(p_end);
}

void events::stop() {
	// no-op
}

} // namespace ep

} // namespace dig2

} // namespace caen
