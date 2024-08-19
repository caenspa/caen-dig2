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
*	\file		dppzle.cpp
*	\brief
*	\author		Giovanni Cerretani, Stefano Venditti
*
******************************************************************************/

#include "endpoints/dppzle.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include <boost/assert.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/combine.hpp>
#include <spdlog/fmt/fmt.h>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/circular_buffer.hpp"
#include "cpp-utility/counting_range.hpp"
#include "cpp-utility/lexical_cast.hpp"
#include "cpp-utility/scope_exit.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "cpp-utility/to_address.hpp"
#include "cpp-utility/to_underlying.hpp"
#include "client.hpp"
#include "data_format_utils.hpp"
#include "lib_error.hpp"
#include "library_logger.hpp"

using namespace std::literals;
using namespace caen::literals;

namespace caen {

namespace dig2 {

namespace ep {

struct dppzle::endpoint_impl {

	endpoint_impl()
		: _logger{library_logger::create_logger("dppzle_ep"s)}
		, _buffer()
		, _args_list{dppzle::default_data_format()}
		, _new_event{true} {
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<dppzle>::parse_data_format(_args_list, json_format);
	}

	static constexpr std::size_t circular_buffer_size{4};

	std::shared_ptr<spdlog::logger> _logger;
	caen::circular_buffer<zle_evt, circular_buffer_size> _buffer;
	args_list_t _args_list;
	bool _new_event; // software flag to handle decode of multiple events into a single event

};

dppzle::dppzle(client& client, handle::internal_handle_t endpoint_handle)
	: aggregate_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>()} {

	// resize buffer elements that depends on number of channels
	_pimpl->_buffer.apply_all([n_channels = get_client().get_n_channels()](zle_evt& evt) {
		evt._channel_data.resize(n_channels); // caen::resize() useless here
	});
}

dppzle::~dppzle() = default;

void dppzle::resize() {

	if (is_decode_disabled()) {

		// free space if not enabled
		_pimpl->_buffer.apply_all([](zle_evt& evt) {
			caen::reset(evt._counters);
			for (auto& cd : evt._channel_data) {
				caen::reset(cd._chunk_time);
				caen::reset(cd._chunk_size);
				caen::reset(cd._chunk_begin);
				caen::reset(cd._waveform);
				caen::reset(cd._reconstructed_waveform);
				caen::reset(cd._sample_type);
			}
		});

	} else {

		auto& client = get_client();
		const auto n_channels = client.get_n_channels();

		const auto is_enabled = [&client](auto i) {
			const auto enabled_s = client.get_value(client.get_digitizer_internal_handle(), fmt::format("/ch/{}/par/chenable", i));
			return caen::string::iequals(enabled_s, "true"_sv);
		};

		const auto ch_enabled = caen::counting_range(n_channels) | boost::adaptors::transformed(is_enabled);

		// store values in a container to avoid call get_value for every event in the buffer (avoid std::vector<bool>)
		const caen::vector<std::uint8_t> ch_enabled_v(ch_enabled.begin(), ch_enabled.end());

		const auto record_length_s = client.get_value(client.get_digitizer_internal_handle(), "/par/recordlengths"s);
		const auto record_length = caen::lexical_cast<std::size_t>(record_length_s);

		// reserve here to avoid allocations during run
		_pimpl->_buffer.apply_all([&ch_enabled_v, record_length](zle_evt& evt) {
			evt._record_length = record_length; // record length is read from parameter because cannot be deduced from event
			caen::reserve(evt._counters, zle_evt::max_n_counters);
			for (auto&& c : boost::combine(evt._channel_data, ch_enabled_v)) {
				auto& cd = c.get<0>();
				const auto enabled = c.get<1>();
				if (enabled) {
					caen::reserve(cd._chunk_time, zle_evt::max_n_counters / 2 + 1);
					caen::reserve(cd._chunk_size, zle_evt::max_n_counters / 2 + 1);
					caen::reserve(cd._chunk_begin, zle_evt::max_n_counters / 2 + 1);
					caen::reserve(cd._waveform, zle_evt::max_waveform_samples);
					caen::reserve(cd._reconstructed_waveform, record_length);
					caen::reserve(cd._sample_type, record_length);
				} else {
					caen::reset(cd._chunk_time);
					caen::reset(cd._chunk_size);
					caen::reset(cd._chunk_begin);
					caen::reset(cd._waveform);
					caen::reset(cd._reconstructed_waveform);
					caen::reset(cd._sample_type);
				}
			}
		});

	}

	// resize is called at arm just after a clear data: call test_and_set to reset any previous flags
	is_clear_required_and_reset();
}

void dppzle::decode(const caen::byte* p, std::size_t size) {

	const auto p_begin = p;
	const auto p_end = p_begin + size;

	if (!decode_aggregate_header(p))
		return;

	BOOST_ASSERT_MSG(size == last_aggregate_header()._n_words * word_size, "inconsistent size");

	auto& buffer = _pimpl->_buffer;

	// force notify at the end of the aggregate or in case of is_clear_required_and_reset
	// even if redundant at the end of the aggregate (notified at the last channel) it is kept for consistency
	caen::scope_exit se_notify([&buffer] { buffer.notify(); });

	while (p < p_end) {

		if (is_clear_required_and_reset())
			return;

		decode_hit(p);

	}

	BOOST_ASSERT_MSG(p == p_end, "inconsistent decoding");
}

void dppzle::decode_hit(const caen::byte*& p) {

	auto& buffer = _pimpl->_buffer;
	auto& new_event = _pimpl->_new_event;

	const auto bw = buffer.get_buffer_write();
	caen::scope_exit se_abort([&buffer] { buffer.abort_writing(); });

	auto& evt = *bw;
	auto& agg = last_aggregate_header();

	if (std::exchange(new_event, false)) {
		// clear vectors of all channels to mark non participating channels
		for (auto& cd : evt._channel_data) {
			caen::clear(cd._chunk_time);
			caen::clear(cd._chunk_size);
			caen::clear(cd._chunk_begin);
			caen::clear(cd._waveform);
			caen::clear(cd._reconstructed_waveform);
			caen::clear(cd._sample_type);
		}

		// reset fields that are accumulated over channels
		evt._board_fail = false;
		evt._flush = false;
		evt._event_size = 0;

		// events could arrive split in more aggregates: these fields are taken from the first participating aggregate fields
		evt._aggregate_counter = agg._aggregate_counter;

		// software fields
		evt._fake_stop_event = false;
	}

	// events could arrive split in more aggregates: these fields are the or of all participating aggregates fields
	evt._board_fail |= agg._board_fail;
	evt._flush |= agg._flush;

	const auto compute_event_size = [p_event_begin = p](auto p_curr) { return p_curr - p_event_begin; };

	// there is always at least the first counter
	caen::resize(evt._counters, 1);

	word_t word;

	bool is_last_word;

	// 1st word
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_right_shift<zle_evt::s::timestamp>(word, evt._timestamp); // always the same, overwritten for each channel
	caen::bit::right_shift<zle_evt::s::tbd_1>(word);
	const auto last_channel = caen::bit::mask_and_right_shift<zle_evt::s::last_channel, bool>(word);
	const auto channel = caen::bit::mask_and_right_shift<zle_evt::s::channel>(word);
	caen::bit::mask_and_right_shift<zle_evt::s::last_word>(word, is_last_word);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	BOOST_ASSERT_MSG(!is_last_word, "unexpected is_last_word in first word");

	auto& channel_data = evt._channel_data[channel];

	auto& first_counter = evt._counters.front();

	// 2nd word
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_right_shift<zle_evt::counter::s::size>(word, first_counter._size);
	caen::bit::mask_and_right_shift<zle_evt::counter::s::counters_truncated>(word, first_counter._counters_truncated);
	caen::bit::mask_and_right_shift<zle_evt::counter::s::wave_truncated>(word, first_counter._wave_truncated);
	caen::bit::mask_and_right_shift<zle_evt::counter::s::last>(word, first_counter._last);
	caen::bit::right_shift<zle_evt::counter::s::tbd_1>(word);
	const auto even_counters_good = caen::bit::mask_and_right_shift<zle_evt::s::even_counters_good, bool>(word);
	caen::bit::right_shift<zle_evt::s::tbd_3>(word);
	caen::bit::mask_and_right_shift<zle_evt::s::waveform_defvalue>(word, channel_data._waveform_defvalue);
	caen::bit::right_shift<zle_evt::s::tbd_2>(word);
	const auto has_waveform = caen::bit::mask_and_right_shift<zle_evt::s::has_waveform, bool>(word);
	caen::bit::mask_and_right_shift<zle_evt::s::last_word>(word, is_last_word);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	first_counter._is_good = even_counters_good; // first counter is even (0th)

	while (!is_last_word) {

		// additional counters words
		caen::serdes::deserialize(p, word);

		auto& counter_low = caen::emplace_back(evt._counters);
		counter_low._is_good = !even_counters_good; // low counters are odd (1st, 3rd, ...)
		caen::bit::mask_and_right_shift<zle_evt::counter::s::size>(word, counter_low._size);
		caen::bit::mask_and_right_shift<zle_evt::counter::s::counters_truncated>(word, counter_low._counters_truncated);
		caen::bit::mask_and_right_shift<zle_evt::counter::s::wave_truncated>(word, counter_low._wave_truncated);
		caen::bit::mask_and_right_shift<zle_evt::counter::s::last>(word, counter_low._last);
		caen::bit::right_shift<zle_evt::counter::s::tbd_1>(word);
		if (counter_low._last) {
			caen::bit::right_shift<zle_evt::s::tbd_4>(word); // unused half word
		} else {
			auto& counter_high = caen::emplace_back(evt._counters);
			counter_high._is_good = even_counters_good; // high counters are even (2nd, 4th, ...)
			caen::bit::mask_and_right_shift<zle_evt::counter::s::size>(word, counter_high._size);
			caen::bit::mask_and_right_shift<zle_evt::counter::s::counters_truncated>(word, counter_high._counters_truncated);
			caen::bit::mask_and_right_shift<zle_evt::counter::s::wave_truncated>(word, counter_high._wave_truncated);
			caen::bit::mask_and_right_shift<zle_evt::counter::s::last>(word, counter_high._last);
		}
		caen::bit::mask_and_right_shift<zle_evt::s::last_word>(word, is_last_word);

		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
		BOOST_ASSERT_MSG(is_last_word == evt._counters.back()._last, "inconsistent event content");
	}

	// truncated flags, if any, must be set in the last counter
	const auto last_counter = evt._counters.back();
	channel_data._truncate_wave = last_counter._wave_truncated;
	channel_data._truncate_param = last_counter._counters_truncated;

	// consistency checks
	BOOST_ASSERT_MSG(!evt._counters.empty(), "inconsistent number of counter");
	BOOST_ASSERT_MSG(has_waveform == (evt._counters.size() > 1 || evt._counters.front()._is_good), "inconsistent event content");

	// fill waveform
	if (has_waveform) {

		decode_hit_waveform(p, channel_data._waveform);

	} else {

		// waveform already cleared on new_event, nothing to do
		BOOST_ASSERT_MSG(channel_data._waveform.empty(), "inconsistent decoding");

	}

	// fill chunk stuff and reconstructed waveform even if there is no waveform in the event

	// resize (no allocation)
	caen::resize(channel_data._reconstructed_waveform, evt._record_length);
	caen::resize(channel_data._sample_type, evt._record_length);

	auto waveform_it = channel_data._waveform.cbegin();
	auto reconstructed_waveform_it = channel_data._reconstructed_waveform.begin();
	auto sample_type_it = channel_data._sample_type.begin();

	std::size_t accumulated_chunk_time{};
	std::size_t accumulated_chunk_begin{};

	for (const auto& counter : evt._counters) {
		enum struct sample_type : decltype(sample_type_it)::value_type {
			bad = 0b0,
			good = 0b1,
		};
		sample_type type;
		const std::size_t chunk_size{counter._size};
		if (counter._is_good) {
			channel_data._chunk_size.push_back(chunk_size);
			channel_data._chunk_time.push_back(accumulated_chunk_time);
			channel_data._chunk_begin.push_back(accumulated_chunk_begin);
			reconstructed_waveform_it = std::copy_n(waveform_it, chunk_size, reconstructed_waveform_it);
			waveform_it += chunk_size;
			accumulated_chunk_begin += chunk_size;
			type = counter._counters_truncated ? sample_type::bad : sample_type::good;
		} else {
			reconstructed_waveform_it = std::fill_n(reconstructed_waveform_it, chunk_size, channel_data._waveform_defvalue);
			type = sample_type::bad;
		}
		accumulated_chunk_time += chunk_size;
		// sample_type vector type could also use this scoped enumerator
		// but caen::args::put_argument_matrix is restricted only to arithmetic types by design
		sample_type_it = std::fill_n(sample_type_it, chunk_size, caen::to_underlying(type));
	}

	// delete eventual unused samples at the end if size is not multiple of zle_evt::samples_per_word
	BOOST_ASSERT_MSG(channel_data._waveform.size() - accumulated_chunk_begin < zle_evt::samples_per_word, "inconsistent event content");
	caen::resize(channel_data._waveform, accumulated_chunk_begin);

	// delete eventual unused reconstructed samples at the end if wave is truncated
	BOOST_ASSERT_MSG(accumulated_chunk_time <= channel_data._reconstructed_waveform.size(), "inconsistent event content");
	BOOST_ASSERT_MSG(channel_data._truncate_wave == (accumulated_chunk_time < channel_data._reconstructed_waveform.size()), "inconsistent event content");
	if (channel_data._truncate_wave)
		caen::resize(channel_data._reconstructed_waveform, accumulated_chunk_time);

	evt._event_size += compute_event_size(p);

	if (last_channel) {
		se_abort.release();
		buffer.end_writing();
		new_event = true;		// flag to reset waveforms at the next event
	}

}

void dppzle::decode_hit_waveform(const caen::byte*& p, zle_evt::channel_data::waveform_t& waveform) {

	word_t word;

	// waveform size word
	caen::serdes::deserialize(p, word);
	const auto waveform_n_words = caen::bit::mask_and_right_shift<zle_evt::s::waveform_n_words>(word);
	caen::bit::right_shift<zle_evt::s::tbd_5>(word);
	const auto truncated = caen::bit::mask_and_right_shift<zle_evt::s::truncated, bool>(word);
	BOOST_ASSERT_MSG(!word, "inconsistent waveform header word decoding");

	if (BOOST_UNLIKELY(truncated))
		_pimpl->_logger->warn("unexpected truncated waveform");

	// numeric cast throws if result overflows size_t
	const auto n_samples = boost::numeric_cast<std::size_t>(waveform_n_words * zle_evt::samples_per_word);

	// resize waveform (no allocation)
	caen::resize(waveform, n_samples);

	for (auto it = waveform.begin(); it != waveform.end(); it += zle_evt::samples_per_word) {

		BOOST_ASSERT_MSG(it + zle_evt::samples_per_word <= waveform.end(), "inconsistent waveform size");

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
			for (auto i : caen::counting_range(zle_evt::samples_per_word))
				caen::bit::mask_and_right_shift<zle_evt::s::sample>(word, *(it + i));
			BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
		}

	}

}

void dppzle::stop() {
	auto& buffer = _pimpl->_buffer;
	const auto bw = buffer.get_buffer_write();
	auto& evt = *bw;
	evt._fake_stop_event = true;
	buffer.end_writing();
}

dppzle::args_list_t dppzle::default_data_format() {
	using vt = data_format_utils<dppzle>::args_type;
	return {{
			vt{names::TIMESTAMP,					types::U64,		0	},
			vt{names::RECONSTRUCTED_WAVEFORM,		types::U16,		2	},
			vt{names::RECONSTRUCTED_WAVEFORM_SIZE,	types::SIZE_T,	1	}
	}};
}

std::size_t dppzle::data_format_dimension(names name) {
	switch (name) {
	case names::TIMESTAMP:
	case names::TIMESTAMP_NS:
	case names::RECORD_LENGTH:
	case names::BOARD_FAIL:
	case names::AGGREGATE_COUNTER:
	case names::FLUSH:
	case names::EVENT_SIZE:
		return 0;
	case names::TRUNCATE_WAVE:
	case names::TRUNCATE_PARAM:
	case names::WAVEFORM_DEFVALUE:
	case names::CHUNK_NUMBER:
	case names::RECONSTRUCTED_WAVEFORM_SIZE:
		return 1;
	case names::CHUNK_TIME:
	case names::CHUNK_SIZE:
	case names::CHUNK_BEGIN:
	case names::WAVEFORM:
	case names::RECONSTRUCTED_WAVEFORM:
	case names::SAMPLE_TYPE:
		return 2;
	default:
		throw "unsupported name"_ex;
	}
}

void dppzle::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void dppzle::read_data(timeout_t timeout, std::va_list* args) {

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
		case names::TIMESTAMP:
			utility::put_argument(args, type, evt._timestamp);
			break;
		case names::TIMESTAMP_NS:
			utility::put_argument(args, type, timestamp_to_ns<zle_evt::s::timestamp>{}(evt._timestamp));
			break;
		case names::RECORD_LENGTH:
			utility::put_argument(args, type, evt._record_length);
			break;
		case names::TRUNCATE_WAVE:
			utility::put_argument_array(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept { return cd._truncate_wave; }));
			break;
		case names::TRUNCATE_PARAM:
			utility::put_argument_array(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept { return cd._truncate_param; }));
			break;
		case names::WAVEFORM_DEFVALUE:
			utility::put_argument_array(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept { return cd._waveform_defvalue; }));
			break;
		case names::CHUNK_NUMBER:
			utility::put_argument_array(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept { return cd._chunk_size.size(); }));
			break;
		case names::CHUNK_TIME:
			utility::put_argument_matrix(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept -> const auto& { return cd._chunk_time; })); // const auto& required to avoid copy
			break;
		case names::CHUNK_SIZE:
			utility::put_argument_matrix(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept -> const auto& { return cd._chunk_size; })); // idem
			break;
		case names::CHUNK_BEGIN:
			utility::put_argument_matrix(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept -> const auto& { return cd._chunk_begin; })); // idem
			break;
		case names::WAVEFORM:
			utility::put_argument_matrix(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept -> const auto& { return cd._waveform; })); // idem
			break;
		case names::RECONSTRUCTED_WAVEFORM:
			utility::put_argument_matrix(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept -> const auto& { return cd._reconstructed_waveform; })); // idem
			break;
		case names::SAMPLE_TYPE:
			utility::put_argument_matrix(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept -> const auto& { return cd._sample_type; })); // idem
			break;
		case names::RECONSTRUCTED_WAVEFORM_SIZE:
			utility::put_argument_array(args, type, evt._channel_data | boost::adaptors::transformed([](const auto& cd) noexcept { return cd._reconstructed_waveform.size(); }));
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
	buffer.end_reading();
}

void dppzle::has_data(timeout_t timeout) {

	auto& buffer = _pimpl->_buffer;

	const auto br = buffer.get_buffer_read(timeout);

	if (br == nullptr)
		throw ex::timeout();

	caen::scope_exit se([&buffer] { buffer.abort_reading(); });

	auto& evt = *br;

	if (evt._fake_stop_event)
		throw ex::stop();
}

void dppzle::clear_data() {
	require_clear();
	_pimpl->_new_event = true;
	_pimpl->_buffer.invalidate_buffers();
}

} // namespace ep

} // namespace dig2

} // namespace caen
