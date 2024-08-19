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
*	\file		opendpp.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "endpoints/opendpp.hpp"

#include <algorithm>
#include <cstring>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <spdlog/fmt/fmt.h>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/circular_buffer.hpp"
#include "cpp-utility/counting_range.hpp"
#include "cpp-utility/scope_exit.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "cpp-utility/to_address.hpp"
#include "client.hpp"
#include "data_format_utils.hpp"
#include "lib_error.hpp"
#include "library_logger.hpp"

using namespace std::literals;
using namespace caen::literals;

namespace caen {

namespace dig2 {

namespace ep {

struct opendpp::endpoint_impl {

	endpoint_impl()
		: _logger{library_logger::create_logger("opendpp_ep"s)}
		, _buffer()
		, _args_list{opendpp::default_data_format()} {
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<opendpp>::parse_data_format(_args_list, json_format);
	}

	static constexpr std::size_t circular_buffer_size{4096};

	std::shared_ptr<spdlog::logger> _logger;
	caen::circular_buffer<hit_evt, circular_buffer_size> _buffer;
	args_list_t _args_list;

};

opendpp::opendpp(client& client, handle::internal_handle_t endpoint_handle)
	: aggregate_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>()} {

}

opendpp::~opendpp() = default;

void opendpp::resize() {

	if (is_decode_disabled()) {

		// free space if not enabled
		_pimpl->_buffer.apply_all([](hit_evt& evt) {
			caen::reset(evt._user_info);
			caen::reset(evt._waveform);
		});

	} else {

		auto& client = get_client();
		const auto n_channels = client.get_n_channels();

		// chenable can be set in run: in case, memory could be allocated by caen::resize during run

		const auto is_enabled = [&client](auto i) {
			const auto enabled_s = client.get_value(client.get_digitizer_internal_handle(), fmt::format("/ch/{}/par/chenable", i));
			return caen::string::iequals(enabled_s, "true"_sv);
		};

		const auto ch_enabled = caen::counting_range(n_channels) | boost::adaptors::filtered(is_enabled);

		const auto is_any_ch_enabled = !ch_enabled.empty();

		// reserve here to avoid allocations during run
		_pimpl->_buffer.apply_all([is_any_ch_enabled](hit_evt& evt) {
			if (BOOST_LIKELY(is_any_ch_enabled)) {
				caen::reserve(evt._user_info, hit_evt::max_user_info_words);
				caen::reserve(evt._waveform, hit_evt::max_waveform_samples);
			} else {
				caen::reset(evt._user_info);
				caen::reset(evt._waveform);
			}
		});

	}

	// resize is called at arm just after a clear data: call test_and_set to reset any previous flags
	is_clear_required_and_reset();
}

void opendpp::decode(const caen::byte* p, std::size_t size) {

	const auto p_begin = p;
	const auto p_end = p_begin + size;

	if (!decode_aggregate_header(p))
		return;

	BOOST_ASSERT_MSG(size == last_aggregate_header()._n_words * word_size, "inconsistent size");

	auto& buffer = _pimpl->_buffer;

	// force notify at the end of the aggregate or in case of is_clear_required_and_reset
	caen::scope_exit se_notify([&buffer] { buffer.notify(); });

	while (p < p_end) {

		if (is_clear_required_and_reset())
			return;

		decode_hit(p);

	}

	BOOST_ASSERT_MSG(p == p_end, "inconsistent decoding");
}

void opendpp::decode_hit(const caen::byte*& p) {

	auto& buffer = _pimpl->_buffer;

	const auto bw = buffer.get_buffer_write();
	caen::scope_exit se_abort([&buffer] { buffer.abort_writing(); });

	auto& evt = *bw;

	// fields from aggregate header
	auto& agg = last_aggregate_header();
	evt._board_fail = agg._board_fail;
	evt._flush = agg._flush;
	evt._aggregate_counter = agg._aggregate_counter;

	// software fields
	evt._fake_stop_event = false;

	// reset user info vector in any case
	caen::clear(evt._user_info);

	const auto compute_event_size = [p_event_begin = p](auto p_curr) { return p_curr - p_event_begin; };

	word_t word;

	bool is_last_word;

	// declare fields not saved into event
	bool has_waveform;

	// 1st word (mask_and_left_shift is slower but is used here to decode is_last_word first)
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_left_shift<hit_evt::s::last_word>(word, is_last_word);
	caen::bit::mask_and_left_shift<hit_evt::s::channel>(word, evt._channel);

	if (is_last_word) {

		// single word event

		// continue 1st word
		caen::bit::mask_and_left_shift<hit_evt::s::flags_a>(word, evt._flags_a);
		caen::bit::mask_and_left_shift<hit_evt::s::timestamp_reduced>(word, evt._timestamp); // directly into _timestamp
		caen::bit::mask_and_left_shift<hit_evt::s::energy>(word, evt._energy);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

		// set manually to use the final part of the decode
		has_waveform = false;

	} else {

		// standard event

		// continue 1st word
		caen::bit::mask_and_left_shift<hit_evt::s::special_event>(word, evt._special_event);
		caen::bit::mask_and_left_shift<hit_evt::s::info>(word, evt._info);
		caen::bit::mask_and_left_shift<hit_evt::s::timestamp>(word, evt._timestamp);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

		// 2nd word
		caen::serdes::deserialize(p, word);
		caen::bit::mask_and_right_shift<hit_evt::s::energy>(word, evt._energy);
		caen::bit::mask_and_right_shift<hit_evt::s::fine_timestamp>(word, evt._fine_timestamp);
		caen::bit::mask_and_right_shift<hit_evt::s::psd>(word, evt._psd);
		caen::bit::mask_and_right_shift<hit_evt::s::flags_a>(word, evt._flags_a);
		caen::bit::mask_and_right_shift<hit_evt::s::flags_b>(word, evt._flags_b);
		caen::bit::mask_and_right_shift<hit_evt::s::has_waveform>(word, has_waveform);
		caen::bit::mask_and_right_shift<hit_evt::s::last_word>(word, is_last_word);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

		// additional user words
		while (!is_last_word) {
			caen::serdes::deserialize(p, word);
			caen::bit::mask_and_right_shift<hit_evt::s::user_info>(word, caen::emplace_back(evt._user_info));
			caen::bit::mask_and_right_shift<hit_evt::s::last_word>(word, is_last_word);
			BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
		}
	}

	if (has_waveform) {

		decode_hit_waveform(p, evt._waveform, evt._truncated);

	} else {

		// clear (no deallocation)
		caen::clear(evt._waveform);

	}

	evt._event_size = compute_event_size(p);

	se_abort.release();
	buffer.end_writing_relaxed();
}

void opendpp::decode_hit_waveform(const caen::byte*& p, hit_evt::waveform_t& waveform, bool& truncated) {

	word_t word;

	// waveform size word
	caen::serdes::deserialize(p, word);
	const auto waveform_n_words = caen::bit::mask_and_right_shift<hit_evt::s::waveform_n_words>(word);
	caen::bit::right_shift<hit_evt::s::tbd_1>(word);
	caen::bit::mask_and_right_shift<hit_evt::s::truncated>(word, truncated);
	BOOST_ASSERT_MSG(!word, "inconsistent waveform header word decoding");

	// numeric cast throws if result overflows size_t
	const auto n_samples = boost::numeric_cast<std::size_t>(waveform_n_words * hit_evt::samples_per_word);

	// resize (no allocation)
	caen::resize(waveform, n_samples); // memory already reserved: no allocation should be made

	for (auto it = waveform.begin(); it != waveform.end(); it += hit_evt::samples_per_word) {

		BOOST_ASSERT_MSG(it + hit_evt::samples_per_word <= waveform.end(), "inconsistent waveform size");

		caen::serdes::deserialize(p, word);

		/*
		 * In little-endian systems the loop can be replaced with `std::memcpy`:
		 * it is faster, but it is not portable on big-endian systems.
		 * The loop version works also on big-endian systems. Recent compilers
		 * (LLVM >= 12 and GCC >= 8) generate the same code of `std::memcpy` in
		 * case of a little-endian system. MSVC does not optimize it, as of version 1933.
		 */
		if /* constexpr */ (caen::endian::native == caen::endian::little) {
			std::memcpy(caen::to_address(it), &word, word_size);
		} else {
			for (auto i : caen::counting_range(hit_evt::samples_per_word))
				caen::bit::mask_and_right_shift<hit_evt::s::sample>(word, *(it + i));
			BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
		}

	}

}

void opendpp::stop() {
	auto& buffer = _pimpl->_buffer;
	const auto bw = buffer.get_buffer_write();
	auto& evt = *bw;
	evt._fake_stop_event = true;
	buffer.end_writing();
}

opendpp::args_list_t opendpp::default_data_format() {
	using vt = data_format_utils<opendpp>::args_type;
	return {{
			vt{names::CHANNEL,			types::U8,	0	},
			vt{names::TIMESTAMP,		types::U64, 0	},
			vt{names::FINE_TIMESTAMP,	types::U16, 0	},
			vt{names::ENERGY,			types::U16, 0	},
	}};
}

std::size_t opendpp::data_format_dimension(names name) {
	switch (name) {
	case names::CHANNEL:
	case names::TIMESTAMP:
	case names::TIMESTAMP_NS:
	case names::FINE_TIMESTAMP:
	case names::ENERGY:
	case names::FLAGS_B:
	case names::FLAGS_A:
	case names::PSD:
	case names::SPECIAL_EVENT:
	case names::USER_INFO_SIZE:
	case names::TRUNCATED:
	case names::WAVEFORM_SIZE:
	case names::BOARD_FAIL:
	case names::AGGREGATE_COUNTER:
	case names::FLUSH:
	case names::EVENT_SIZE:
		return 0;
	case names::USER_INFO:
	case names::WAVEFORM:
		return 1;
	default:
		throw "unsupported name"_ex;
	}
}

void opendpp::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void opendpp::read_data(timeout_t timeout, std::va_list* args) {

	auto& buffer = _pimpl->_buffer;

	const auto br = buffer.get_buffer_read(timeout);

	if (br == nullptr)
		throw ex::timeout();

	caen::scope_exit se([&buffer] { buffer.abort_reading(); });

	auto& evt = *br;

	if (evt._fake_stop_event) {
		se.release();
		buffer.end_reading();
		throw ex::stop();
	}

	for (const auto& arg : _pimpl->_args_list) {
		const auto name = std::get<0>(arg);
		const auto type = std::get<1>(arg);
		switch (name) {
		case names::CHANNEL:
			utility::put_argument(args, type, evt._channel);
			break;
		case names::TIMESTAMP:
			utility::put_argument(args, type, evt._timestamp);
			break;
		case names::TIMESTAMP_NS:
			utility::put_argument(args, type, timestamp_to_ns<hit_evt::s::timestamp>{}(evt._timestamp));
			break;
		case names::FINE_TIMESTAMP:
			utility::put_argument(args, type, evt._fine_timestamp);
			break;
		case names::ENERGY:
			utility::put_argument(args, type, evt._energy);
			break;
		case names::FLAGS_B:
			utility::put_argument(args, type, evt._flags_b);
			break;
		case names::FLAGS_A:
			utility::put_argument(args, type, evt._flags_a);
			break;
		case names::PSD:
			utility::put_argument(args, type, evt._psd);
			break;
		case names::SPECIAL_EVENT:
			utility::put_argument(args, type, evt._special_event);
			break;
		case names::USER_INFO:
			utility::put_argument_array(args, type, evt._user_info);
			break;
		case names::USER_INFO_SIZE:
			utility::put_argument(args, type, evt._user_info.size());
			break;
		case names::TRUNCATED:
			utility::put_argument(args, type, evt._truncated);
			break;
		case names::WAVEFORM:
			utility::put_argument_array(args, type, evt._waveform);
			break;
		case names::WAVEFORM_SIZE:
			utility::put_argument(args, type, evt._waveform.size());
			break;
		case names::BOARD_FAIL:
			utility::put_argument(args, type, evt._board_fail);
			break;
		case names::AGGREGATE_COUNTER:
			utility::put_argument(args, type, evt._aggregate_counter);
			break;
		case names::FLUSH:
			utility::put_argument(args, type, evt._flush);
			break;
		case names::EVENT_SIZE:
			utility::put_argument(args, type, evt._event_size);
			break;
		default:
			throw "unsupported data type"_ex;
		}
	}

	se.release();
	buffer.end_reading_relaxed();
}

void opendpp::has_data(timeout_t timeout) {

	auto& buffer = _pimpl->_buffer;

	const auto br = buffer.get_buffer_read(timeout);

	if (br == nullptr)
		throw ex::timeout();

	caen::scope_exit se([&buffer] { buffer.abort_reading(); });

	auto& evt = *br;

	if (evt._fake_stop_event)
		throw ex::stop();
}

void opendpp::clear_data() {
	require_clear();
	_pimpl->_buffer.invalidate_buffers();
}

} // namespace ep

} // namespace dig2

} // namespace caen
