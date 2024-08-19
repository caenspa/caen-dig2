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
*	\file		dpppha.cpp
*	\brief
*	\author		Giovanni Cerretani, Alberto Potenza
*
******************************************************************************/

#include "endpoints/dpppha.hpp"

#include <mutex>
#include <vector>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/transform.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/circular_buffer.hpp"
#include "cpp-utility/counting_range.hpp"
#include "cpp-utility/lexical_cast.hpp"
#include "cpp-utility/optional.hpp"
#include "cpp-utility/scope_exit.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "cpp-utility/to_underlying.hpp"
#include "client.hpp"
#include "data_format_utils.hpp"
#include "lib_error.hpp"
#include "library_logger.hpp"

using namespace std::literals;
using namespace caen::literals;

namespace caen {

namespace dig2 {

namespace {

template <typename Func, typename WaveDataInfo>
void apply_all_probes(WaveDataInfo& wave_data_info, const Func&& func) {
	for (auto& probe : wave_data_info._analog_probes) {
		func(probe._data);
		func(probe._decoded_data);
	}
	for (auto& probe : wave_data_info._digital_probes) {
		func(probe._data);
	}
}

} // unnamed namespace

namespace ep {

struct dpppha::endpoint_impl {

	endpoint_impl(double sampling_period_ns)
		: _logger{library_logger::create_logger("dpppha_ep"s)}
		, _buffer()
		, _args_list{dpppha::default_data_format()}
		, _sampling_period_ns{sampling_period_ns} {
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<dpppha>::parse_data_format(_args_list, json_format);
	}

	static constexpr std::size_t circular_buffer_size{4096};

	std::shared_ptr<spdlog::logger> _logger;
	caen::circular_buffer<hit_evt, circular_buffer_size> _buffer;
	args_list_t _args_list;
	const double _sampling_period_ns;

};

dpppha::dpppha(client& client, handle::internal_handle_t endpoint_handle)
	: aggregate_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>(client.get_sampling_period_ns())}
	, _stats_ep{std::make_shared<stats>(client, client.get_handle(endpoint_handle, "/stats"))} {

	get_client().register_endpoint(_stats_ep);

}

dpppha::~dpppha() = default;

void dpppha::resize() {

	if (is_decode_disabled()) {

		// free space if not enabled
		_pimpl->_buffer.apply_all([](hit_evt& evt) {
			apply_all_probes(evt._wave_info_data, [](auto& data) { caen::reset(data); });
		});

	} else {

		auto& client = get_client();
		const auto n_channels = client.get_n_channels();

		// chenable and wavetriggersource can be set in run: in case, memory could be allocated by caen::resize during run

		const auto is_enabled = [&client](auto i) {
			const auto enabled_s = client.get_value(client.get_digitizer_internal_handle(), fmt::format("/ch/{}/par/chenable", i));
			return caen::string::iequals(enabled_s, "true"_sv);
		};

		const auto is_wave_trg_enabled = [&client](auto i) {
			const auto wavetriggersource_s = client.get_value(client.get_digitizer_internal_handle(), fmt::format("/ch/{}/par/wavetriggersource", i));
			return !caen::string::iequals(wavetriggersource_s, "disabled"_sv);
		};

		const auto get_record_length = [&client](auto i) {
			const auto ch_record_length_s = client.get_value(client.get_digitizer_internal_handle(), fmt::format("/ch/{}/par/chrecordlengths", i));
			return caen::lexical_cast<std::size_t>(ch_record_length_s);
		};

		namespace ba = boost::adaptors;
		const auto ch_record_length = caen::counting_range(n_channels) | ba::filtered(is_enabled) | ba::filtered(is_wave_trg_enabled) | ba::transformed(get_record_length);

		// store values in a container to avoid call get_value twice per each boost::max_element cycle
		const caen::vector<std::size_t> ch_record_length_v(ch_record_length.begin(), ch_record_length.end());

		const auto it = boost::max_element(ch_record_length_v);
		const std::size_t max_record_length{BOOST_LIKELY(it != ch_record_length_v.end()) ? *it : 0};

		BOOST_ASSERT_MSG(max_record_length <= hit_evt::wave_info_data::max_waveform_samples, "unexpected record length");

		// reserve here to avoid allocations during run
		_pimpl->_buffer.apply_all([rl = max_record_length](hit_evt& evt) {
			apply_all_probes(evt._wave_info_data, [rl](auto& data) { caen::reserve(data, rl); });
		});

	}

	// resize is called at arm just after a clear data: call test_and_set to reset any previous flags
	is_clear_required_and_reset();
}

void dpppha::decode(const caen::byte* p, std::size_t size) {

	const auto p_begin = p;
	const auto p_end = p_begin + size;

	if (!decode_aggregate_header(p))
		return;

	BOOST_ASSERT_MSG(size == last_aggregate_header()._n_words * word_size, "inconsistent size");

	auto& buffer = _pimpl->_buffer;

	// force notify at the end of the aggregate and on is_clear_required_and_reset
	caen::scope_exit se_notify([&buffer] { buffer.notify(); });

	while (p < p_end) {

		if (is_clear_required_and_reset())
			return;

		decode_hit(p);

	}

	BOOST_ASSERT_MSG(p == p_end, "inconsistent decoding");

}

void dpppha::decode_hit(const caen::byte*& p) {

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

	const auto compute_event_size = [p_event_begin = p](auto p_curr) { return p_curr - p_event_begin; };

	word_t word;

	bool is_last_word;

	// declare fields not saved into event
	bool special_event;
	bool has_waveform;
	caen::optional<stats::time_info> stats_time_info;
	caen::optional<stats::counter_info> stats_counter_info;

	// 1st word (mask_and_left_shift is slower but is used here to decode is_last_word first)
	caen::serdes::deserialize(p, word);
	caen::bit::mask_and_left_shift<hit_evt::s::last_word>(word, is_last_word);
	caen::bit::mask_and_left_shift<hit_evt::s::channel>(word, evt._channel);

	if (is_last_word) {

		// single word event

		// continue 1st word
		caen::bit::mask_and_left_shift<hit_evt::s::flag_high_priority>(word, evt._flag_high_priority);
		caen::bit::mask_and_left_shift<hit_evt::s::timestamp_reduced>(word, evt._timestamp); // directly into _timestamp
		caen::bit::mask_and_left_shift<hit_evt::s::energy>(word, evt._energy);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

		// set manually to use the final part of the decode
		special_event = false;
		has_waveform = false;

	} else {

		// standard event

		// continue 1st word
		caen::bit::mask_and_left_shift<hit_evt::s::special_event>(word, special_event);
		caen::bit::left_shift<hit_evt::s::tbd_1>(word);
		caen::bit::mask_and_left_shift<hit_evt::s::timestamp>(word, evt._timestamp);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

		// 2nd word
		caen::serdes::deserialize(p, word);
		caen::bit::mask_and_right_shift<hit_evt::s::energy>(word, evt._energy);
		caen::bit::mask_and_right_shift<hit_evt::s::fine_timestamp>(word, evt._fine_timestamp);
		caen::bit::right_shift<hit_evt::s::tbd_2>(word);
		caen::bit::mask_and_right_shift<hit_evt::s::flag_high_priority>(word, evt._flag_high_priority);
		caen::bit::mask_and_right_shift<hit_evt::s::flag_low_priority>(word, evt._flag_low_priority);
		caen::bit::mask_and_right_shift<hit_evt::s::has_waveform>(word, has_waveform);
		caen::bit::mask_and_right_shift<hit_evt::s::last_word>(word, is_last_word);
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

		BOOST_ASSERT_MSG(!(is_last_word && (has_waveform || special_event)), "inconsistent event content");

		while (!is_last_word) {

			// declare fields not saved into event
			hit_evt::extra_type extra_type;
			caen::uint_t<hit_evt::s::extra_data>::fast extra_data;

			// extra word
			caen::serdes::deserialize(p, word);
			caen::bit::mask_and_right_shift<hit_evt::s::extra_data>(word, extra_data);
			caen::bit::mask_and_right_shift<hit_evt::s::extra_type>(word, extra_type);
			caen::bit::mask_and_right_shift<hit_evt::s::last_word>(word, is_last_word);
			BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

			// parse extra word
			switch (extra_type) {
			case hit_evt::extra_type::wave_info: {

				using s_ed = hit_evt::wave_info_data;

				BOOST_ASSERT_MSG(extra_type == s_ed::extra_id, "inconsistent extra type");
				BOOST_ASSERT_MSG(!special_event, "special_event flag should not be set");
				BOOST_ASSERT_MSG(has_waveform, "has_waveform flag should be set");

				s_ed& ed = evt._wave_info_data;

				// decode info in extra word data
				for (auto& probe : ed._analog_probes) {
					caen::bit::mask_and_right_shift<s_ed::analog_probe::s::type>(extra_data, probe._type);
					probe._decoded_type = [](auto t) noexcept {
						switch (t) {
						case s_ed::analog_probe::type::adc_input:						return dpp_analog_probe_type::adc_input;
						case s_ed::analog_probe::type::time_filter:						return dpp_analog_probe_type::time_filter;
						case s_ed::analog_probe::type::energy_filter:					return dpp_analog_probe_type::energy_filter;
						case s_ed::analog_probe::type::energy_filter_baseline:			return dpp_analog_probe_type::energy_filter_baseline;
						case s_ed::analog_probe::type::energy_filter_minus_baseline:	return dpp_analog_probe_type::energy_filter_minus_baseline;
						default:														return dpp_analog_probe_type::unknown;
						}
					}(probe._type);
					caen::bit::mask_and_right_shift<s_ed::analog_probe::s::is_signed>(extra_data, probe._is_signed);
					caen::bit::mask_and_right_shift<s_ed::analog_probe::s::mul_factor>(extra_data, probe._mul_factor);
					probe._decoded_mul_factor = [](auto mf) noexcept {
						switch (mf) {
						case s_ed::analog_probe::mul_factor::factor_1:					return 1;
						case s_ed::analog_probe::mul_factor::factor_4:					return 4;
						case s_ed::analog_probe::mul_factor::factor_8:					return 8;
						case s_ed::analog_probe::mul_factor::factor_16:					return 16;
						default:														return 0; // default
						}
					}(probe._mul_factor);
				}
				for (auto& probe : ed._digital_probes) {
					caen::bit::mask_and_right_shift<s_ed::digital_probe::s::type>(extra_data, probe._type);
					probe._decoded_type = [](auto t) noexcept {
						switch (t) {
						case s_ed::digital_probe::type::trigger:						return dpp_digital_probe_type::trigger;
						case s_ed::digital_probe::type::time_filter_armed:				return dpp_digital_probe_type::time_filter_armed;
						case s_ed::digital_probe::type::re_trigger_guard:				return dpp_digital_probe_type::re_trigger_guard;
						case s_ed::digital_probe::type::energy_filter_baseline_freeze:	return dpp_digital_probe_type::energy_filter_baseline_freeze;
						case s_ed::digital_probe::type::energy_filter_peaking:			return dpp_digital_probe_type::energy_filter_peaking;
						case s_ed::digital_probe::type::energy_filter_peak_ready:		return dpp_digital_probe_type::energy_filter_peak_ready;
						case s_ed::digital_probe::type::energy_filter_pile_up_guard:	return dpp_digital_probe_type::energy_filter_pile_up_guard;
						case s_ed::digital_probe::type::event_pile_up:					return dpp_digital_probe_type::event_pile_up;
						case s_ed::digital_probe::type::adc_saturation:					return dpp_digital_probe_type::adc_saturation;
						case s_ed::digital_probe::type::adc_saturation_protection:		return dpp_digital_probe_type::adc_saturation_protection;
						case s_ed::digital_probe::type::post_saturation_event:			return dpp_digital_probe_type::post_saturation_event;
						case s_ed::digital_probe::type::energy_filter_saturation:		return dpp_digital_probe_type::energy_filter_saturation;
						case s_ed::digital_probe::type::signal_inhibit:					return dpp_digital_probe_type::signal_inhibit;
						default:														return dpp_digital_probe_type::unknown;
						}
					}(probe._type);
				}
				caen::bit::mask_and_right_shift<s_ed::s::trigger_thr>(extra_data, ed._trigger_thr);
				caen::bit::mask_and_right_shift<s_ed::s::time_resolution>(extra_data, ed._time_resolution);
				caen::bit::right_shift<s_ed::s::tbd_1>(extra_data);
				BOOST_ASSERT_MSG(!extra_data, "inconsistent word decoding");

				break;
			}
			case hit_evt::extra_type::time_info: {

				using s_ed = hit_evt::time_info_data;

				BOOST_ASSERT_MSG(extra_type == s_ed::extra_id, "inconsistent extra type");
				BOOST_ASSERT_MSG(special_event, "special_event flag should be set");
				BOOST_ASSERT_MSG(!has_waveform, "has_waveform flag should not be set");
				BOOST_ASSERT_MSG(!stats_time_info, "stats info data should not be already initialized");

				s_ed ed;

				caen::bit::mask_and_right_shift<s_ed::s::dead_time>(extra_data, ed._dead_time);
				caen::bit::right_shift<s_ed::s::tbd_1>(extra_data);
				BOOST_ASSERT_MSG(!extra_data, "inconsistent word decoding");

				stats_time_info.emplace();
				stats_time_info->_dead_time = ed._dead_time;

				break;
			}
			case hit_evt::extra_type::counter_info: {

				using s_ed = hit_evt::counter_info_data;

				BOOST_ASSERT_MSG(extra_type == s_ed::extra_id, "inconsistent extra type");
				BOOST_ASSERT_MSG(special_event, "special_event flag should be set");
				BOOST_ASSERT_MSG(!has_waveform, "has_waveform flag should not be set");
				BOOST_ASSERT_MSG(!stats_counter_info, "stats info data should not be already initialized");

				s_ed ed;

				caen::bit::mask_and_right_shift<s_ed::s::saved_event_cnt>(extra_data, ed._saved_event_cnt);
				caen::bit::mask_and_right_shift<s_ed::s::trigger_cnt>(extra_data, ed._trigger_cnt);
				caen::bit::right_shift<s_ed::s::tbd_1>(extra_data);
				BOOST_ASSERT_MSG(!extra_data, "inconsistent word decoding");

				stats_counter_info.emplace();
				stats_counter_info->_saved_event_cnt = ed._saved_event_cnt;
				stats_counter_info->_trigger_cnt = ed._trigger_cnt;

				break;
			}
			default:
				_pimpl->_logger->warn("unsupported event id {:d}", caen::to_underlying(extra_type));
				break;
			}
		}
	}

	if (has_waveform) {

		decode_hit_waveform(p, evt._wave_info_data);

	} else {

		// clear (no deallocation)
		apply_all_probes(evt._wave_info_data, [](auto& data) { caen::clear(data); });

	}

	evt._event_size = compute_event_size(p);

	if (special_event) {

		_stats_ep->update(evt._channel, evt._timestamp, std::move(stats_time_info), std::move(stats_counter_info));

		// special events are not propagated to user
		return;

	}

	se_abort.release();
	buffer.end_writing_relaxed();

}

void dpppha::decode_hit_waveform(const caen::byte*& p, hit_evt::wave_info_data& ed) {

	using s_ed = hit_evt::wave_info_data;

	word_t word;

	// waveform size word
	caen::serdes::deserialize(p, word);
	const auto waveform_n_words = caen::bit::mask_and_right_shift<s_ed::s::waveform_n_words>(word);
	caen::bit::right_shift<s_ed::s::tbd_2>(word);
	const auto truncated = caen::bit::mask_and_right_shift<s_ed::s::truncated, bool>(word);
	BOOST_ASSERT_MSG(!word, "inconsistent waveform header word decoding");

	if (BOOST_UNLIKELY(truncated))
		_pimpl->_logger->warn("unexpected truncated waveform");

	// numeric cast throws if result overflows size_t
	const auto n_samples = boost::numeric_cast<std::size_t>(waveform_n_words * s_ed::samples_per_word);

	// resize (no allocation)
	apply_all_probes(ed, [n_samples](auto& data) { caen::resize(data, n_samples); });

	for (auto w : caen::counting_range(waveform_n_words)) {
		caen::serdes::deserialize(p, word);
		for (auto i : caen::counting_range(s_ed::samples_per_word)) {
			const auto s = (w * s_ed::samples_per_word) + i;
			const auto sth_sample = [s](auto& probe) noexcept -> auto& { return probe._data[s]; };
			caen::bit::mask_and_right_shift<s_ed::analog_probe::s::sample>(word, sth_sample(std::get<0>(ed._analog_probes)));
			caen::bit::mask_and_right_shift<s_ed::digital_probe::s::sample>(word, sth_sample(std::get<0>(ed._digital_probes)));
			caen::bit::mask_and_right_shift<s_ed::digital_probe::s::sample>(word, sth_sample(std::get<1>(ed._digital_probes)));
			caen::bit::mask_and_right_shift<s_ed::analog_probe::s::sample>(word, sth_sample(std::get<1>(ed._analog_probes)));
			caen::bit::mask_and_right_shift<s_ed::digital_probe::s::sample>(word, sth_sample(std::get<2>(ed._digital_probes)));
			caen::bit::mask_and_right_shift<s_ed::digital_probe::s::sample>(word, sth_sample(std::get<3>(ed._digital_probes)));
		}
		BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
	}

	// compute decoded data
	for (auto& probe : ed._analog_probes) {
		auto decode_analog_probe = [is = probe._is_signed, dmf = probe._decoded_mul_factor](auto d) noexcept {
			return (is ? caen::bit::sign_extend_cast<s_ed::analog_probe::s::sample>(d) : d) * dmf;
		};
		boost::transform(probe._data, probe._decoded_data.begin(), decode_analog_probe);
	}

}

void dpppha::stop() {
	auto& buffer = _pimpl->_buffer;
	const auto bw = buffer.get_buffer_write();
	auto& evt = *bw;
	evt._fake_stop_event = true;
	buffer.end_writing();
}

dpppha::args_list_t dpppha::default_data_format() {
	using vt = data_format_utils<dpppha>::args_type;
	return {{
			vt{names::CHANNEL,			types::U8,		0	},
			vt{names::TIMESTAMP,		types::U64,		0	},
			vt{names::FINE_TIMESTAMP,	types::U16,		0	},
			vt{names::ENERGY,			types::U16,		0	},
			vt{names::ANALOG_PROBE_1,	types::I32,		1	},
			vt{names::ANALOG_PROBE_2,	types::I32,		1	},
			vt{names::DIGITAL_PROBE_1,	types::U8,		1	},
			vt{names::DIGITAL_PROBE_2,	types::U8,		1	},
			vt{names::DIGITAL_PROBE_3,	types::U8,		1	},
			vt{names::DIGITAL_PROBE_4,	types::U8,		1	},
			vt{names::WAVEFORM_SIZE,	types::SIZE_T,	0	},
	}};
}

std::size_t dpppha::data_format_dimension(names name) {
	switch (name) {
	case names::CHANNEL:
	case names::TIMESTAMP:
	case names::TIMESTAMP_NS:
	case names::FINE_TIMESTAMP:
	case names::ENERGY:
	case names::FLAGS_LOW_PRIORITY:
	case names::FLAGS_HIGH_PRIORITY:
	case names::TRIGGER_THR:
	case names::TIME_RESOLUTION:
	case names::ANALOG_PROBE_1_TYPE:
	case names::ANALOG_PROBE_2_TYPE:
	case names::DIGITAL_PROBE_1_TYPE:
	case names::DIGITAL_PROBE_2_TYPE:
	case names::DIGITAL_PROBE_3_TYPE:
	case names::DIGITAL_PROBE_4_TYPE:
	case names::WAVEFORM_SIZE:
	case names::BOARD_FAIL:
	case names::AGGREGATE_COUNTER:
	case names::FLUSH:
	case names::EVENT_SIZE:
		return 0;
	case names::ANALOG_PROBE_1:
	case names::ANALOG_PROBE_2:
	case names::DIGITAL_PROBE_1:
	case names::DIGITAL_PROBE_2:
	case names::DIGITAL_PROBE_3:
	case names::DIGITAL_PROBE_4:
		return 1;
	default:
		throw "unsupported name"_ex;
	}
}

void dpppha::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void dpppha::read_data(timeout_t timeout, std::va_list* args) {

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
			// this U64 to DOUBLE conversion is always precise, assuming sampling period in ns is a power of 2 (see v1.5.9 changelog)
			utility::put_argument(args, type, evt._timestamp * _pimpl->_sampling_period_ns);
			break;
		case names::FINE_TIMESTAMP:
			utility::put_argument(args, type, evt._fine_timestamp);
			break;
		case names::ENERGY:
			utility::put_argument(args, type, evt._energy);
			break;
		case names::FLAGS_LOW_PRIORITY:
			utility::put_argument(args, type, evt._flag_low_priority);
			break;
		case names::FLAGS_HIGH_PRIORITY:
			utility::put_argument(args, type, evt._flag_high_priority);
			break;
		case names::TRIGGER_THR:
			utility::put_argument(args, type, evt._wave_info_data._trigger_thr);
			break;
		case names::TIME_RESOLUTION:
			utility::put_argument(args, type, caen::to_underlying(evt._wave_info_data._time_resolution));
			break;
		case names::ANALOG_PROBE_1:
			utility::put_argument_array(args, type, std::get<0>(evt._wave_info_data._analog_probes)._decoded_data);
			break;
		case names::ANALOG_PROBE_1_TYPE:
			utility::put_argument(args, type, caen::to_underlying(std::get<0>(evt._wave_info_data._analog_probes)._decoded_type));
			break;
		case names::ANALOG_PROBE_2:
			utility::put_argument_array(args, type, std::get<1>(evt._wave_info_data._analog_probes)._decoded_data);
			break;
		case names::ANALOG_PROBE_2_TYPE:
			utility::put_argument(args, type, caen::to_underlying(std::get<1>(evt._wave_info_data._analog_probes)._decoded_type));
			break;
		case names::DIGITAL_PROBE_1:
			utility::put_argument_array(args, type, std::get<0>(evt._wave_info_data._digital_probes)._data);
			break;
		case names::DIGITAL_PROBE_1_TYPE:
			utility::put_argument(args, type, caen::to_underlying(std::get<0>(evt._wave_info_data._digital_probes)._decoded_type));
			break;
		case names::DIGITAL_PROBE_2:
			utility::put_argument_array(args, type, std::get<1>(evt._wave_info_data._digital_probes)._data);
			break;
		case names::DIGITAL_PROBE_2_TYPE:
			utility::put_argument(args, type, caen::to_underlying(std::get<1>(evt._wave_info_data._digital_probes)._decoded_type));
			break;
		case names::DIGITAL_PROBE_3:
			utility::put_argument_array(args, type, std::get<2>(evt._wave_info_data._digital_probes)._data);
			break;
		case names::DIGITAL_PROBE_3_TYPE:
			utility::put_argument(args, type, caen::to_underlying(std::get<2>(evt._wave_info_data._digital_probes)._decoded_type));
			break;
		case names::DIGITAL_PROBE_4:
			utility::put_argument_array(args, type, std::get<3>(evt._wave_info_data._digital_probes)._data);
			break;
		case names::DIGITAL_PROBE_4_TYPE:
			utility::put_argument(args, type, caen::to_underlying(std::get<3>(evt._wave_info_data._digital_probes)._decoded_type));
			break;
		case names::WAVEFORM_SIZE:
			// assuming all probes have the same size
			utility::put_argument(args, type, std::get<0>(evt._wave_info_data._analog_probes)._data.size());
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

void dpppha::has_data(timeout_t timeout) {

	auto& buffer = _pimpl->_buffer;

	const auto br = buffer.get_buffer_read(timeout);

	if (br == nullptr)
		throw ex::timeout();

	caen::scope_exit se([&buffer] { buffer.abort_reading(); });

	auto& evt = *br;

	if (evt._fake_stop_event)
		throw ex::stop();
}

void dpppha::clear_data() {
	require_clear();
	_pimpl->_buffer.invalidate_buffers();
	_stats_ep->clear_data();
}

struct dpppha::stats::endpoint_impl {

	endpoint_impl(double sampling_period_ns)
		: _logger{library_logger::create_logger("dpppha_stats_ep"s)}
		, _data{}
		, _args_list{dpppha::stats::default_data_format()}
		, _sampling_period_ns{sampling_period_ns} {
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<dpppha::stats>::parse_data_format(_args_list, json_format);
	}

	struct data {
		// std::vector here to clear on resize
		std::vector<time_info::type> _real_time;
		std::vector<time_info::type> _dead_time;
		std::vector<time_info::type> _live_time;
		std::vector<counter_info::type> _trigger_cnt;
		std::vector<counter_info::type> _saved_event_cnt;
	};

	std::shared_ptr<spdlog::logger> _logger;
	data _data;
	args_list_t _args_list;
	const double _sampling_period_ns;
	std::mutex _mtx;

};

dpppha::stats::stats(client& client, handle::internal_handle_t endpoint_handle)
	: endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>(client.get_sampling_period_ns())} {

	// resize buffer elements that depends on number of channels
	const auto n_channels = get_client().get_n_channels();
	auto& data = _pimpl->_data;
	data._real_time.resize(n_channels);
	data._dead_time.resize(n_channels);
	data._live_time.resize(n_channels);
	data._trigger_cnt.resize(n_channels);
	data._saved_event_cnt.resize(n_channels);
}

dpppha::stats::~stats() = default;

dpppha::stats::args_list_t dpppha::stats::default_data_format() {
	using vt = data_format_utils<dpppha::stats>::args_type;
	return {{
			vt{names::REAL_TIME,		types::U64,		1	},
			vt{names::DEAD_TIME,		types::U64,		1	},
	}};
}

std::size_t dpppha::stats::data_format_dimension(names name) {
	switch (name) {
	case names::REAL_TIME:
	case names::REAL_TIME_NS:
	case names::DEAD_TIME:
	case names::DEAD_TIME_NS:
	case names::LIVE_TIME:
	case names::LIVE_TIME_NS:
	case names::TRIGGER_CNT:
	case names::SAVED_EVENT_CNT:
		return 1;
	default:
		throw "unsupported name"_ex;
	}
}

void dpppha::stats::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void dpppha::stats::read_data(timeout_t timeout, std::va_list* args) {

	boost::ignore_unused(timeout);

	// make a local copy to unlock the mutex as soon as possible.
	const auto data = [this] {
		std::lock_guard<std::mutex> l{_pimpl->_mtx};
		return _pimpl->_data;
	}();

	for (const auto& arg : _pimpl->_args_list) {
		const auto name = std::get<0>(arg);
		const auto type = std::get<1>(arg);
		switch (name) {
		case names::REAL_TIME:
			utility::put_argument_array(args, type, data._real_time);
			break;
		case names::REAL_TIME_NS:
			utility::put_argument_array(args, type, data._real_time | boost::adaptors::transformed([sp = _pimpl->_sampling_period_ns](auto v) { return v * sp; }));
			break;
		case names::DEAD_TIME:
			utility::put_argument_array(args, type, data._dead_time);
			break;
		case names::DEAD_TIME_NS:
			utility::put_argument_array(args, type, data._dead_time | boost::adaptors::transformed([sp = _pimpl->_sampling_period_ns](auto v) { return v * sp; }));
			break;
		case names::LIVE_TIME:
			utility::put_argument_array(args, type, data._live_time);
			break;
		case names::LIVE_TIME_NS:
			utility::put_argument_array(args, type, data._live_time | boost::adaptors::transformed([sp = _pimpl->_sampling_period_ns](auto v) { return v * sp; }));
			break;
		case names::TRIGGER_CNT:
			utility::put_argument_array(args, type, data._trigger_cnt);
			break;
		case names::SAVED_EVENT_CNT:
			utility::put_argument_array(args, type, data._saved_event_cnt);
			break;
		default:
			throw "unsupported data type"_ex;
		}
	}
}

void dpppha::stats::has_data(timeout_t timeout) {
	boost::ignore_unused(timeout);
}

void dpppha::stats::clear_data() {
	std::lock_guard<std::mutex> l{_pimpl->_mtx};
	auto& data = _pimpl->_data;
	caen::set_default(data._real_time);
	caen::set_default(data._dead_time);
	caen::set_default(data._live_time);
	caen::set_default(data._trigger_cnt);
	caen::set_default(data._saved_event_cnt);
}

void dpppha::stats::update(std::size_t channel, time_info::type timestamp, caen::optional<time_info> time_info, caen::optional<counter_info> counter_info) {
	std::lock_guard<std::mutex> l{_pimpl->_mtx};
	auto& data = _pimpl->_data;
	data._real_time[channel] = timestamp;
	if (time_info) {
		data._dead_time[channel] = time_info->_dead_time;
		data._live_time[channel] = timestamp - time_info->_dead_time;
	}
	if (counter_info) {
		data._trigger_cnt[channel] = counter_info->_trigger_cnt;
		data._saved_event_cnt[channel] = counter_info->_saved_event_cnt;
	}
}

} // namespace ep

} // namespace dig2

} // namespace caen
