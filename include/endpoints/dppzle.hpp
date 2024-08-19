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
*	\file		dppzle.hpp
*	\brief		Decoded endpoint for DPP-ZLE
*	\author		Giovanni Cerretani, Stefano Venditti
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_DPPZLE_HPP_
#define CAEN_INCLUDE_ENDPOINTS_DPPZLE_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "cpp-utility/integer.hpp"
#include "cpp-utility/vector.hpp"
#include "endpoints/aggregate_endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct dppzle final : public aggregate_endpoint {

	enum class names { // overrides endpoint::names
		UNKNOWN,
		TIMESTAMP,
		TIMESTAMP_NS,
		RECORD_LENGTH,
		TRUNCATE_WAVE,
		TRUNCATE_PARAM,
		WAVEFORM_DEFVALUE,
		CHUNK_NUMBER,
		CHUNK_TIME,
		CHUNK_SIZE,
		CHUNK_BEGIN,
		WAVEFORM,
		RECONSTRUCTED_WAVEFORM,
		SAMPLE_TYPE,
		RECONSTRUCTED_WAVEFORM_SIZE,
		BOARD_FAIL,
		AGGREGATE_COUNTER,
		FLUSH,
		EVENT_SIZE,
	};

	using args_list_t = utility::args_list_t<names, types>;

	dppzle(client& client, handle::internal_handle_t endpoint_handle);
	~dppzle();

	void resize() override;
	void decode(const caen::byte* p, std::size_t size) override;
	void stop() override;
	void set_data_format(const std::string &json_format) override;
	void read_data(timeout_t timeout, std::va_list* args) override;
	void has_data(timeout_t timeout) override;
	void clear_data() override;

	static args_list_t default_data_format();
	static std::size_t data_format_dimension(names name);

private:
	struct zle_evt {
		struct s {
			// 1st word
			static constexpr std::size_t last_word{1}; // first bit of each word
			static constexpr std::size_t channel{7};
			static constexpr std::size_t last_channel{1};
			static constexpr std::size_t tbd_1{7};
			static constexpr std::size_t timestamp{48};
			// 2nd word (half word)
			static constexpr std::size_t has_waveform{1};
			static constexpr std::size_t tbd_2{10};
			static constexpr std::size_t waveform_defvalue{16};
			static constexpr std::size_t tbd_3{3};
			static constexpr std::size_t even_counters_good{1};
			// tbd_4 is the unused half word (last high half word in case of even counters)
			static constexpr std::size_t tbd_4{31};
			// waveform size word
			static constexpr std::size_t truncated{1};
			static constexpr std::size_t tbd_5{51};
			static constexpr std::size_t waveform_n_words{12};
			// waveform
			static constexpr std::size_t sample{16};
		};
		struct counter {
			struct s {
				// counter (half word)
				static constexpr std::size_t tbd_1{1}; // bit occupied by last_word on even counters
				static constexpr std::size_t last{1};
				static constexpr std::size_t wave_truncated{1};
				static constexpr std::size_t counters_truncated{1};
				static constexpr std::size_t size{28};
			};
			// - tbd_1 not saved into event
			bool _last;
			bool _wave_truncated;
			bool _counters_truncated;
			caen::uint_t<s::size>::fast _size;
			bool _is_good;
		};
		struct channel_data {
			// typedefs
			using waveform_t = caen::vector<caen::uint_t<s::sample>::least>;
			// fields
			bool _truncate_wave;
			bool _truncate_param;
			caen::uint_t<s::waveform_defvalue>::fast _waveform_defvalue; // saved for each channel
			caen::vector<std::size_t> _chunk_time;
			caen::vector<std::size_t> _chunk_size;
			caen::vector<std::size_t> _chunk_begin;
			waveform_t _waveform;
			waveform_t _reconstructed_waveform;
			caen::vector<caen::uint_t<s::even_counters_good>::least> _sample_type; // software probe (better to avoid vector<bool>)
		};
		// constants
		static constexpr std::size_t samples_per_word{word_bit_size / s::sample};
		static constexpr std::size_t max_n_counters{1023};
		static constexpr std::size_t max_waveform_words{4095};
		static constexpr std::size_t max_waveform_samples{max_waveform_words * samples_per_word};
		// fields
		// - channel not saved into event
		// - last_channel not saved into event
		// - tbd_1 not saved into event
		caen::uint_t<s::timestamp>::fast _timestamp;
		// - has_waveform not saved into event
		// - tbd_2 not saved into event
		// - waveform_defvalue saved per channel
		// - tbd_3 not saved into event
		// - even_counters_good not saved into event
		caen::vector<counter> _counters;
		// - tbd_4 not saved into event
		// - truncated not saved into event
		// - tbd_5 not saved into event
		// - waveform_n_words not saved into event
		std::size_t _record_length;
		caen::vector<channel_data> _channel_data;
		std::size_t _event_size;
		// fields from aggregate header
		bool _board_fail;
		bool _flush;
		caen::uint_t<dpp_aggregate_header::s::aggregate_counter>::fast _aggregate_counter;
		// software fields
		bool _fake_stop_event;
		// consistency assertions
		static_assert(std::is_same<decltype(channel_data::_reconstructed_waveform)::value_type, std::uint16_t>::value, "documented to be handled as std::uint16_t");
		static_assert(std::is_same<decltype(channel_data::_waveform)::value_type, std::uint16_t>::value, "documented to be handled as std::uint16_t");
		static_assert(std::is_same<decltype(channel_data::_sample_type)::value_type, std::uint8_t>::value, "documented to be handled as std::uint8_t");
	};

	// decode internal implementation
	void decode_hit(const caen::byte*& p);
	void decode_hit_waveform(const caen::byte*& p, zle_evt::channel_data::waveform_t& waveform);

	struct endpoint_impl; // forward declaration
	std::unique_ptr<endpoint_impl> _pimpl;

};

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(dppzle::names, {
	{ dppzle::names::UNKNOWN,						nullptr							},
	{ dppzle::names::TIMESTAMP,						"TIMESTAMP"s					},
	{ dppzle::names::TIMESTAMP_NS,					"TIMESTAMP_NS"s					},
	{ dppzle::names::RECORD_LENGTH,					"RECORD_LENGTH"s				},
	{ dppzle::names::TRUNCATE_WAVE,					"TRUNCATE_WAVE"s				},
	{ dppzle::names::TRUNCATE_PARAM,				"TRUNCATE_PARAM"s				},
	{ dppzle::names::WAVEFORM_DEFVALUE,				"WAVEFORM_DEFVALUE"s			},
	{ dppzle::names::CHUNK_NUMBER,					"CHUNK_NUMBER"s					},
	{ dppzle::names::CHUNK_TIME,					"CHUNK_TIME"s					},
	{ dppzle::names::CHUNK_SIZE,					"CHUNK_SIZE"s					},
	{ dppzle::names::CHUNK_BEGIN,					"CHUNK_BEGIN"s					},
	{ dppzle::names::WAVEFORM,						"WAVEFORM"s						},
	{ dppzle::names::RECONSTRUCTED_WAVEFORM,		"RECONSTRUCTED_WAVEFORM"s		},
	{ dppzle::names::SAMPLE_TYPE,					"SAMPLE_TYPE"s					},
	{ dppzle::names::RECONSTRUCTED_WAVEFORM_SIZE,	"RECONSTRUCTED_WAVEFORM_SIZE"s	},
	{ dppzle::names::BOARD_FAIL,					"BOARD_FAIL"s					},
	{ dppzle::names::AGGREGATE_COUNTER,				"AGGREGATE_COUNTER"s			},
	{ dppzle::names::FLUSH,							"FLUSH"s						},
	{ dppzle::names::EVENT_SIZE,					"EVENT_SIZE"s					},
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_SCOPE_HPP_ */
