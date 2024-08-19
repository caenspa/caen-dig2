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
*	\file		dpppha.hpp
*	\brief		Decoded endpoint for DPP-PHA
*	\author		Giovanni Cerretani, Alberto Potenza
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_DPPPHA_HPP_
#define CAEN_INCLUDE_ENDPOINTS_DPPPHA_HPP_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "cpp-utility/integer.hpp"
#include "cpp-utility/optional.hpp"
#include "cpp-utility/vector.hpp"
#include "endpoints/aggregate_endpoint.hpp"
#include "endpoints/dpp_probe_types.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct dpppha final : public aggregate_endpoint {

	enum class names { // overrides endpoint::names
		UNKNOWN,
		CHANNEL,
		TIMESTAMP,
		TIMESTAMP_NS,
		FINE_TIMESTAMP,
		ENERGY,
		FLAGS_LOW_PRIORITY,
		FLAGS_HIGH_PRIORITY,
		TRIGGER_THR,
		TIME_RESOLUTION,
		ANALOG_PROBE_1,
		ANALOG_PROBE_1_TYPE,
		ANALOG_PROBE_2,
		ANALOG_PROBE_2_TYPE,
		DIGITAL_PROBE_1,
		DIGITAL_PROBE_1_TYPE,
		DIGITAL_PROBE_2,
		DIGITAL_PROBE_2_TYPE,
		DIGITAL_PROBE_3,
		DIGITAL_PROBE_3_TYPE,
		DIGITAL_PROBE_4,
		DIGITAL_PROBE_4_TYPE,
		WAVEFORM_SIZE,
		BOARD_FAIL,
		AGGREGATE_COUNTER,
		FLUSH,
		EVENT_SIZE,
	};

	using args_list_t = utility::args_list_t<names, types>;

	dpppha(client& client, handle::internal_handle_t endpoint_handle);
	~dpppha();

	void resize() override;
	void decode(const caen::byte* p, std::size_t size) override;
	void stop() override;
	void set_data_format(const std::string &json_format) override;
	void read_data(timeout_t timeout, std::va_list* args) override;
	void has_data(timeout_t timeout) override;
	void clear_data() override;

	static args_list_t default_data_format();
	static std::size_t data_format_dimension(names name);

	struct stats final : public endpoint {

		enum class names { // overrides endpoint::names
			UNKNOWN,
			REAL_TIME,
			REAL_TIME_NS,
			DEAD_TIME,
			DEAD_TIME_NS,
			LIVE_TIME,
			LIVE_TIME_NS,
			TRIGGER_CNT,
			SAVED_EVENT_CNT,
		};

		using args_list_t = utility::args_list_t<names, types>;
		struct time_info {
			using type = std::uint64_t;
			type _dead_time;
		};
		struct counter_info {
			using type = std::uint32_t;
			type _trigger_cnt;
			type _saved_event_cnt;
		};

		stats(client& client, handle::internal_handle_t endpoint_handle);
		~stats();

		void set_data_format(const std::string& json_format) override;
		void read_data(timeout_t timeout, std::va_list* args) override;
		void has_data(timeout_t timeout) override;
		void clear_data() override;

		void update(std::size_t channel, time_info::type timestamp, caen::optional<time_info> time_info, caen::optional<counter_info> counter_info);

		static args_list_t default_data_format();
		static std::size_t data_format_dimension(names name);

	private:

		struct endpoint_impl; // forward declaration
		std::unique_ptr<endpoint_impl> _pimpl;

	};

private:
	struct hit_evt {
		struct s {
			// 1st word
			static constexpr std::size_t last_word{1}; // first bit of each word
			static constexpr std::size_t channel{7};
			static constexpr std::size_t special_event{1};
			static constexpr std::size_t tbd_1{7};
			static constexpr std::size_t timestamp{48};
			static constexpr std::size_t timestamp_reduced{32};
			// 2nd word
			static constexpr std::size_t has_waveform{1};
			static constexpr std::size_t flag_low_priority{12};
			static constexpr std::size_t flag_high_priority{8};
			static constexpr std::size_t tbd_2{16};
			static constexpr std::size_t fine_timestamp{10};
			static constexpr std::size_t energy{16};
			// 3rd word
			static constexpr std::size_t extra_type{3};
			static constexpr std::size_t extra_data{60};
		};
		enum struct extra_type : caen::uint_t<s::extra_type>::fast {
			wave_info								= 0b000,
			time_info								= 0b001,
			counter_info							= 0b010,
		};
		struct wave_info_data {
			struct digital_probe {
				struct s {
					// part of 3rd word (extra)
					static constexpr std::size_t type{4};
					// part of waveform word
					static constexpr std::size_t sample{1};
				};
				enum struct type : caen::uint_t<s::type>::fast {
					trigger							= 0b0000,
					time_filter_armed				= 0b0001,
					re_trigger_guard				= 0b0010,
					energy_filter_baseline_freeze	= 0b0011,
					energy_filter_peaking			= 0b0100,
					energy_filter_peak_ready		= 0b0101,
					energy_filter_pile_up_guard		= 0b0110,
					event_pile_up					= 0b0111,
					adc_saturation					= 0b1000,
					adc_saturation_protection		= 0b1001,
					post_saturation_event			= 0b1010,
					energy_filter_saturation		= 0b1011,
					signal_inhibit					= 0b1100,
				};
				// fields
				type _type;
				dpp_digital_probe_type _decoded_type; // decoded common type
				caen::vector<caen::uint_t<s::sample>::least> _data;
			};
			struct analog_probe {
				struct s {
					// part of 3rd word (extra)
					static constexpr std::size_t mul_factor{2};
					static constexpr std::size_t is_signed{1};
					static constexpr std::size_t type{3};
					// part of waveform word
					static constexpr std::size_t sample{14};
					static constexpr std::size_t decoded_sample{sample + 4}; // 4 is factor_16
				};
				enum struct mul_factor : caen::uint_t<s::mul_factor>::fast {
					factor_1						= 0b00,
					factor_4						= 0b01,
					factor_8						= 0b10,
					factor_16						= 0b11,
				};
				enum struct type : caen::uint_t<s::type>::fast {
					adc_input						= 0b000,
					time_filter						= 0b001,
					energy_filter					= 0b010,
					energy_filter_baseline			= 0b011,
					energy_filter_minus_baseline	= 0b100,
				};
				// fields
				mul_factor _mul_factor;
				bool _is_signed;
				type _type;
				dpp_analog_probe_type _decoded_type; // decoded common type
				caen::vector<caen::uint_t<s::sample>::least> _data;
				caen::vector<caen::int_t<s::decoded_sample>::least> _decoded_data;
				decltype(_decoded_data)::value_type _decoded_mul_factor;
			};
			struct s {
				// 3rd word (extra), other fields described in analog_probe::s and digital_probe::s
				static constexpr std::size_t tbd_1{14};
				static constexpr std::size_t time_resolution{2};
				static constexpr std::size_t trigger_thr{16};
				// waveform size word
				static constexpr std::size_t truncated{1};
				static constexpr std::size_t tbd_2{51};
				static constexpr std::size_t waveform_n_words{12};
				// waveform word
				static constexpr std::size_t n_digital_probes{4};
				static constexpr std::size_t n_analog_probes{2};
				static constexpr std::size_t sample{n_analog_probes * analog_probe::s::sample + n_digital_probes * digital_probe::s::sample};
			};
			enum struct time_resolution : caen::uint_t<s::time_resolution>::fast {
				no_downsampling						= 0b00,
				downsampling_x2						= 0b01,
				downsampling_x4						= 0b10,
				downsampling_x8						= 0b11,
			};
			// constants
			static constexpr extra_type extra_id{extra_type::wave_info};
			static constexpr std::size_t samples_per_word{word_bit_size / s::sample};
			static constexpr std::size_t max_waveform_words{4095};
			static constexpr std::size_t max_waveform_samples{max_waveform_words * samples_per_word};
			// fields
			// - tbd_1 not saved into event
			time_resolution _time_resolution;
			caen::uint_t<s::trigger_thr>::fast _trigger_thr;
			std::array<digital_probe, s::n_digital_probes> _digital_probes;
			std::array<analog_probe, s::n_analog_probes> _analog_probes;
			// - truncated not saved into event
			// - tbd_2 not saved into event
			// - waveform_n_words not saved into event
		};
		struct time_info_data {
			struct s {
				static constexpr std::size_t tbd_1{12};
				static constexpr std::size_t dead_time{48};
			};
			// constants
			static constexpr extra_type extra_id{extra_type::time_info};
			// fields
			// - tbd_1 not saved into event
			caen::uint_t<s::dead_time>::fast _dead_time;
		};
		struct counter_info_data {
			struct s {
				static constexpr std::size_t tbd_1{12};
				static constexpr std::size_t trigger_cnt{24};
				static constexpr std::size_t saved_event_cnt{24};
			};
			// constants
			static constexpr extra_type extra_id{extra_type::counter_info};
			// fields
			// - tbd_1 not saved into event
			caen::uint_t<s::trigger_cnt>::fast _trigger_cnt;
			caen::uint_t<s::saved_event_cnt>::fast _saved_event_cnt;
		};
		// fields
		caen::uint_t<s::channel>::fast _channel;
		// - special_event not saved into event
		// - tbd_1 not saved into event
		caen::uint_t<s::timestamp>::fast _timestamp;
		// - has_waveform not saved into event
		caen::uint_t<s::flag_low_priority>::fast _flag_low_priority;
		caen::uint_t<s::flag_high_priority>::fast _flag_high_priority;
		// - tbd_2 not saved into event
		caen::uint_t<s::fine_timestamp>::fast _fine_timestamp;
		caen::uint_t<s::energy>::fast _energy;
		// - extra_type not saved into event
		// - extra_data not saved into event
		wave_info_data _wave_info_data;
		std::size_t _event_size;
		// fields from aggregate header
		bool _board_fail;
		bool _flush;
		caen::uint_t<dpp_aggregate_header::s::aggregate_counter>::fast _aggregate_counter;
		// software fields
		bool _fake_stop_event;
		// consistency assertions
		static_assert(std::is_same<decltype(wave_info_data::digital_probe::_data)::value_type, std::uint8_t>::value, "documented to be handled as std::uint8_t");
		static_assert(std::is_same<decltype(wave_info_data::analog_probe::_data)::value_type, std::uint16_t>::value, "documented to be handled as std::uint16_t");
		static_assert(std::is_same<decltype(wave_info_data::analog_probe::_decoded_data)::value_type, std::int32_t>::value, "documented to be handled as std::int32_t");
	};

	// decode internal implementation
	void decode_hit(const caen::byte*& p);
	void decode_hit_waveform(const caen::byte*& p, hit_evt::wave_info_data& ed);

	struct endpoint_impl; // forward declaration
	std::unique_ptr<endpoint_impl> _pimpl;

	std::shared_ptr<stats> _stats_ep;

};

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(dpppha::names, {
	{ dpppha::names::UNKNOWN,					nullptr						},
	{ dpppha::names::CHANNEL,					"CHANNEL"s					},
	{ dpppha::names::TIMESTAMP,					"TIMESTAMP"s				},
	{ dpppha::names::TIMESTAMP_NS,				"TIMESTAMP_NS"s				},
	{ dpppha::names::FINE_TIMESTAMP,			"FINE_TIMESTAMP"s			},
	{ dpppha::names::ENERGY,					"ENERGY"s					},
	{ dpppha::names::FLAGS_LOW_PRIORITY,		"FLAGS_LOW_PRIORITY"s		},
	{ dpppha::names::FLAGS_HIGH_PRIORITY,		"FLAGS_HIGH_PRIORITY"s		},
	{ dpppha::names::TRIGGER_THR,				"TRIGGER_THR"s				},
	{ dpppha::names::TIME_RESOLUTION,			"TIME_RESOLUTION"s			},
	{ dpppha::names::ANALOG_PROBE_1,			"ANALOG_PROBE_1"s			},
	{ dpppha::names::ANALOG_PROBE_1_TYPE,		"ANALOG_PROBE_1_TYPE"s		},
	{ dpppha::names::ANALOG_PROBE_2,			"ANALOG_PROBE_2"s			},
	{ dpppha::names::ANALOG_PROBE_2_TYPE,		"ANALOG_PROBE_2_TYPE"s		},
	{ dpppha::names::DIGITAL_PROBE_1,			"DIGITAL_PROBE_1"s			},
	{ dpppha::names::DIGITAL_PROBE_1_TYPE,		"DIGITAL_PROBE_1_TYPE"s		},
	{ dpppha::names::DIGITAL_PROBE_2,			"DIGITAL_PROBE_2"s			},
	{ dpppha::names::DIGITAL_PROBE_2_TYPE,		"DIGITAL_PROBE_2_TYPE"s		},
	{ dpppha::names::DIGITAL_PROBE_3,			"DIGITAL_PROBE_3"s			},
	{ dpppha::names::DIGITAL_PROBE_3_TYPE,		"DIGITAL_PROBE_3_TYPE"s		},
	{ dpppha::names::DIGITAL_PROBE_4,			"DIGITAL_PROBE_4"s			},
	{ dpppha::names::DIGITAL_PROBE_4_TYPE,		"DIGITAL_PROBE_4_TYPE"s		},
	{ dpppha::names::WAVEFORM_SIZE,				"WAVEFORM_SIZE"s			},
	{ dpppha::names::BOARD_FAIL,				"BOARD_FAIL"s				},
	{ dpppha::names::AGGREGATE_COUNTER,			"AGGREGATE_COUNTER"s		},
	{ dpppha::names::FLUSH,						"FLUSH"s					},
	{ dpppha::names::EVENT_SIZE,				"EVENT_SIZE"s				},
})

NLOHMANN_JSON_SERIALIZE_ENUM(dpppha::stats::names, {
	{ dpppha::stats::names::UNKNOWN,			nullptr						},
	{ dpppha::stats::names::REAL_TIME,			"REAL_TIME"s				},
	{ dpppha::stats::names::REAL_TIME_NS,		"REAL_TIME_NS"s				},
	{ dpppha::stats::names::DEAD_TIME,			"DEAD_TIME"s				},
	{ dpppha::stats::names::DEAD_TIME_NS,		"DEAD_TIME_NS"s				},
	{ dpppha::stats::names::LIVE_TIME,			"LIVE_TIME"s				},
	{ dpppha::stats::names::LIVE_TIME_NS,		"LIVE_TIME_NS"s				},
	{ dpppha::stats::names::TRIGGER_CNT,		"TRIGGER_CNT"s				},
	{ dpppha::stats::names::SAVED_EVENT_CNT,	"SAVED_EVENT_CNT"s			},
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_DPPPHA_HPP_ */
