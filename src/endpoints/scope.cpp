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
*	\file		scope.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "endpoints/scope.hpp"

#include <bitset>
#include <cstring>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/range/adaptor/indexed.hpp>
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
#include "client.hpp"
#include "data_format_utils.hpp"
#include "lib_error.hpp"
#include "library_logger.hpp"

using namespace std::literals;
using namespace caen::literals;

namespace caen {

namespace dig2 {

namespace ep {

struct scope::endpoint_impl {

	endpoint_impl()
		: _logger{library_logger::create_logger("scope_ep"s)}
		, _buffer()
		, _args_list{scope::default_data_format()} {
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<scope>::parse_data_format(_args_list, json_format);
	}

	static constexpr std::size_t circular_buffer_size{4};

	std::shared_ptr<spdlog::logger> _logger;
	caen::circular_buffer<scope_evt, circular_buffer_size> _buffer;
	args_list_t _args_list;

};

scope::scope(client& client, handle::internal_handle_t endpoint_handle)
	: sw_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>()} {

	// resize buffer elements that depends on number of channels
	_pimpl->_buffer.apply_all([n_channels = get_client().get_n_channels()](scope_evt& evt) {
		evt._waveforms.resize(n_channels); // caen::resize() useless here
	});
}

scope::~scope() = default;

void scope::resize() {

	if (is_decode_disabled()) {

		// free space if not enabled
		_pimpl->_buffer.apply_all([](scope_evt& evt) {
			for (auto& waveform : evt._waveforms)
				caen::reset(waveform);
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
		const caen::vector<char> ch_enabled_v(ch_enabled.begin(), ch_enabled.end());

		const auto record_length_s = client.get_value(client.get_digitizer_internal_handle(), "/par/recordlengths"s);
		const auto record_length = caen::lexical_cast<std::size_t>(record_length_s);

		// reserve here to avoid allocations during run
		_pimpl->_buffer.apply_all([&ch_enabled_v, record_length](scope_evt& evt) {
			for (auto&& w : boost::combine(evt._waveforms, ch_enabled_v)) {
				auto& waveform = w.get<0>();
				const auto enabled = w.get<1>();
				if (enabled)
					caen::reserve(waveform, record_length);
				else
					caen::reset(waveform);
			}
		});

	}
}

namespace {

// utility double iterator
template <std::size_t SamplesPerWord, typename ChList>
struct waveform_word_iterator {

	waveform_word_iterator(const ChList& ch_list, std::size_t n_samples) noexcept
		: _ch_list_begin{ch_list.cbegin()}
		, _ch_list_end{ch_list.cend()}
		, _ch_list_it{_ch_list_begin}
		, _n_samples{n_samples}
		, _first_sample{} {
	}

	auto first_sample() const noexcept { return _first_sample; }
	auto channel() const noexcept { return *_ch_list_it; }
	operator bool() const noexcept { return _first_sample != _n_samples; }

	waveform_word_iterator& operator++() noexcept {
		if (++_ch_list_it == _ch_list_end) {
			_ch_list_it = _ch_list_begin;
			_first_sample += SamplesPerWord;
		}
		return *this;
	}

private:

	const typename ChList::const_iterator _ch_list_begin;
	const typename ChList::const_iterator _ch_list_end;
	typename ChList::const_iterator _ch_list_it;
	const std::size_t _n_samples;
	std::size_t _first_sample;

};

template <std::size_t SamplesPerWord, typename ChList>
decltype(auto) make_waveform_word_iterator(const ChList& ch_list, std::size_t n_samples) noexcept {
	return waveform_word_iterator<SamplesPerWord, ChList>(ch_list, n_samples);
}

} // unnamed namespace

void scope::decode(const caen::byte* p, std::size_t size) {

	const auto p_begin = p;
	const auto p_end = p_begin + size;

	word_t word;

	// 1st word (mask_and_left_shift is slower but is used here to decode format first)
	caen::serdes::deserialize(p, word);

	// decode format on separated variable to avoid the mutex-guarded call to get_buffer_write if format is not common_trigger_mode
	const auto format = caen::bit::mask_and_left_shift<scope_evt::s::format, evt_header::format>(word);
	if (format != evt_header::format::common_trigger_mode)
		return;

	if (BOOST_UNLIKELY(size < scope_evt::evt_header_size))
		throw ex::runtime_error(fmt::format("scope event too small (size={})", size));

	auto& buffer = _pimpl->_buffer;

	const auto bw = buffer.get_buffer_write();
	caen::scope_exit se_abort([&buffer] { buffer.abort_writing(); });

	auto& evt = *bw;

	// software fields
	evt._fake_stop_event = false;

	// reuse format already decoded
	evt._format = format;

	// continue 1st word
	caen::bit::left_shift<scope_evt::s::tbd_1>(word);
	caen::bit::mask_and_left_shift<scope_evt::s::board_fail>(word, evt._board_fail);
	caen::bit::mask_and_left_shift<scope_evt::s::trigger_id>(word, evt._trigger_id);
	caen::bit::mask_and_left_shift<scope_evt::s::n_words>(word, evt._n_words);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	BOOST_ASSERT_MSG(size == evt._n_words * word_size, "inconsistent size");

	// 2nd word
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_right_shift<scope_evt::s::timestamp>(word, evt._timestamp);
	caen::bit::mask_and_right_shift<scope_evt::s::samples_overlapped>(word, evt._samples_overlapped);
	caen::bit::mask_and_right_shift<scope_evt::s::flags>(word, evt._flags);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	// 3rd word
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_right_shift<scope_evt::s::ch_mask>(word, evt._ch_mask);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

	BOOST_ASSERT_MSG(p == p_begin + scope_evt::evt_header_size, "inconsistent header decoding");

	// create list of channels and fill waveforms_size
	const std::bitset<scope_evt::s::ch_mask> ch_mask(evt._ch_mask);
	const auto n_participating_channels = ch_mask.count();

	BOOST_ASSERT_MSG(evt._waveforms.size() >= n_participating_channels, "unexpected number of participating channels");

	const auto n_samples = [evt_n_words = evt._n_words, n_participating_channels]() -> std::size_t {
		if (BOOST_UNLIKELY(n_participating_channels == 0))
			return 0;
		const auto waveform_n_words = evt_n_words - scope_evt::evt_header_words;
		const auto total_n_samples = waveform_n_words * scope_evt::samples_per_word;
		if (BOOST_UNLIKELY(total_n_samples % n_participating_channels != 0))
			throw ex::runtime_error(fmt::format("unexpected waveform size (total_n_samples={}, n_participating_channels={})", total_n_samples, n_participating_channels));
		const auto n_samples = total_n_samples / n_participating_channels;
		// numeric cast throws if result overflows size_t
		return boost::numeric_cast<std::size_t>(n_samples);
	}();

	caen::vector<std::size_t> ch_list;
	caen::reserve(ch_list, n_participating_channels);

	for (auto&& w : evt._waveforms | boost::adaptors::indexed()) {
		auto& waveform = w.value();
		const auto i = w.index();
		const auto participate = ch_mask[i];
		if (participate) {
			// resize (no allocation)
			caen::resize(waveform, n_samples);
			ch_list.emplace_back(i);
		} else {
			// clear (no deallocation)
			caen::clear(waveform);
		}
	}

	for (auto wave_it = make_waveform_word_iterator<scope_evt::samples_per_word>(ch_list, n_samples); wave_it; ++wave_it) {

		auto& waveform = evt._waveforms[wave_it.channel()];

		BOOST_ASSERT_MSG(wave_it.first_sample() + scope_evt::samples_per_word <= waveform.size(), "inconsistent waveform size");

		const auto it = waveform.begin() + wave_it.first_sample();

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
			for (auto i : caen::counting_range(scope_evt::samples_per_word))
				caen::bit::mask_and_right_shift<scope_evt::s::sample>(word, *(it + i));
			BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
		}
	}

	BOOST_ASSERT_MSG(p == p_end, "inconsistent decoding");
	boost::ignore_unused(p_end);

	evt._event_size = size;

	se_abort.release();
	buffer.end_writing();
}

void scope::stop() {
	auto& buffer = _pimpl->_buffer;
	const auto bw = buffer.get_buffer_write();
	auto& evt = *bw;
	evt._fake_stop_event = true;
	buffer.end_writing();
}

scope::args_list_t scope::default_data_format() {
	using vt = data_format_utils<scope>::args_type;
	return {{
			vt{names::TIMESTAMP,		types::U64,		0	},
			vt{names::TRIGGER_ID,		types::U32,		0	},
			vt{names::WAVEFORM,			types::U16,		2	},
			vt{names::WAVEFORM_SIZE,	types::U32,		1	}
	}};
}

std::size_t scope::data_format_dimension(names name) {
	switch (name) {
	case names::TIMESTAMP:
	case names::TIMESTAMP_NS:
	case names::TRIGGER_ID:
	case names::SAMPLES_OVERLAPPED:
	case names::FLAGS:
	case names::BOARD_FAIL:
	case names::EVENT_SIZE:
		return 0;
	case names::WAVEFORM_SIZE:
		return 1;
	case names::WAVEFORM:
		return 2;
	default:
		throw "unsupported name"_ex;
	}
}

void scope::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void scope::read_data(timeout_t timeout, std::va_list* args) {

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
			utility::put_argument(args, type, timestamp_to_ns<scope_evt::s::timestamp>{}(evt._timestamp));
			break;
		case names::TRIGGER_ID:
			utility::put_argument(args, type, evt._trigger_id);
			break;
		case names::WAVEFORM:
			utility::put_argument_matrix(args, type, evt._waveforms);
			break;
		case names::WAVEFORM_SIZE:
			utility::put_argument_array(args, type, evt._waveforms | boost::adaptors::transformed([](const auto& w) noexcept { return w.size(); }));
			break;
		case names::SAMPLES_OVERLAPPED:
			utility::put_argument(args, type, evt._samples_overlapped);
			break;
		case names::FLAGS:
			utility::put_argument(args, type, evt._flags);
			break;
		case names::BOARD_FAIL:
			utility::put_argument(args, type, evt._board_fail);
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

void scope::has_data(timeout_t timeout) {

	auto& buffer = _pimpl->_buffer;

	const auto br = buffer.get_buffer_read(timeout);

	if (br == nullptr)
		throw ex::timeout();

	caen::scope_exit se([&buffer] { buffer.abort_reading(); });

	auto& evt = *br;

	if (evt._fake_stop_event)
		throw ex::stop();
}

void scope::clear_data() {
	_pimpl->_buffer.invalidate_buffers();
}

} // namespace ep

} // namespace dig2

} // namespace caen
