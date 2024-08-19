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
*	\file		opendpp.hpp
*	\brief		Decoded endpoint for Open DPP
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_OPENDPP_HPP_
#define CAEN_INCLUDE_ENDPOINTS_OPENDPP_HPP_

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

struct opendpp final : public aggregate_endpoint {

	enum class names { // overrides endpoint::names
		UNKNOWN,
		CHANNEL,
		TIMESTAMP,
		TIMESTAMP_NS,
		FINE_TIMESTAMP,
		ENERGY,
		FLAGS_B,
		FLAGS_A,
		PSD,
		SPECIAL_EVENT,
		USER_INFO,
		USER_INFO_SIZE,
		TRUNCATED,
		WAVEFORM,
		WAVEFORM_SIZE,
		BOARD_FAIL,
		AGGREGATE_COUNTER,
		FLUSH,
		EVENT_SIZE,
	};

	using args_list_t = utility::args_list_t<names, types>;

	opendpp(client& client, handle::internal_handle_t endpoint_handle);
	~opendpp();

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
	struct hit_evt {
		struct s {
			// 1st word
			static constexpr std::size_t last_word{1}; // first bit of each word
			static constexpr std::size_t channel{7};
			static constexpr std::size_t special_event{1};
			static constexpr std::size_t info{7};
			static constexpr std::size_t timestamp{48};
			static constexpr std::size_t timestamp_reduced{32};
			// 2nd word
			static constexpr std::size_t has_waveform{1};
			static constexpr std::size_t flags_b{12};
			static constexpr std::size_t flags_a{8};
			static constexpr std::size_t psd{16};
			static constexpr std::size_t fine_timestamp{10};
			static constexpr std::size_t energy{16};
			// nth word (user info)
			static constexpr std::size_t user_info{63};
			// waveform size word
			static constexpr std::size_t truncated{1};
			static constexpr std::size_t tbd_1{51};
			static constexpr std::size_t waveform_n_words{12};
			// waveform
			static constexpr std::size_t sample{16};
		};
		// constants
		static constexpr std::size_t samples_per_word{word_bit_size / s::sample};
		static constexpr std::size_t max_user_info_words{4};
		static constexpr std::size_t max_waveform_words{4095};
		static constexpr std::size_t max_waveform_samples{max_waveform_words * samples_per_word};
		// typedefs
		using waveform_t = caen::vector<caen::uint_t<s::sample>::least>;
		// fields
		caen::uint_t<s::channel>::fast _channel;
		bool _special_event;
		caen::uint_t<s::info>::fast _info;
		caen::uint_t<s::timestamp>::fast _timestamp;
		// - has_waveform not saved into event
		caen::uint_t<s::flags_b>::fast _flags_b;
		caen::uint_t<s::flags_a>::fast _flags_a;
		caen::uint_t<s::psd>::fast _psd;
		caen::uint_t<s::fine_timestamp>::fast _fine_timestamp;
		caen::uint_t<s::energy>::fast _energy;
		caen::vector<caen::uint_t<s::user_info>::fast> _user_info;
		// - tbd_1 not saved into event
		bool _truncated;
		// - waveform_n_words not saved into event
		waveform_t _waveform;
		std::size_t _event_size;
		// fields from aggregate header
		bool _board_fail;
		bool _flush;
		caen::uint_t<dpp_aggregate_header::s::aggregate_counter>::fast _aggregate_counter;
		// software fields
		bool _fake_stop_event;
		// consistency assertions
		static_assert(std::is_same<decltype(_waveform)::value_type, std::uint16_t>::value, "documented to be handled as std::uint16_t");
	};

	// decode internal implementation
	void decode_hit(const caen::byte*& p);
	void decode_hit_waveform(const caen::byte*& p, hit_evt::waveform_t& waveform, bool& truncated);

	struct endpoint_impl;
	std::unique_ptr<endpoint_impl> _pimpl;

};

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(opendpp::names, {
	{ opendpp::names::UNKNOWN,				nullptr					},
	{ opendpp::names::CHANNEL,				"CHANNEL"s				},
	{ opendpp::names::TIMESTAMP,			"TIMESTAMP"s			},
	{ opendpp::names::TIMESTAMP_NS,			"TIMESTAMP_NS"s			},
	{ opendpp::names::FINE_TIMESTAMP,		"FINE_TIMESTAMP"s		},
	{ opendpp::names::ENERGY,				"ENERGY"s				},
	{ opendpp::names::FLAGS_B,				"FLAGS_B"s				},
	{ opendpp::names::FLAGS_A,				"FLAGS_A"s				},
	{ opendpp::names::PSD,					"PSD"s					},
	{ opendpp::names::SPECIAL_EVENT,		"SPECIAL_EVENT"s		},
	{ opendpp::names::USER_INFO,			"USER_INFO"s			},
	{ opendpp::names::USER_INFO_SIZE,		"USER_INFO_SIZE"s		},
	{ opendpp::names::TRUNCATED,			"TRUNCATED"s			},
	{ opendpp::names::WAVEFORM,				"WAVEFORM"s				},
	{ opendpp::names::WAVEFORM_SIZE,		"WAVEFORM_SIZE"s		},
	{ opendpp::names::BOARD_FAIL,			"BOARD_FAIL"s			},
	{ opendpp::names::AGGREGATE_COUNTER,	"AGGREGATE_COUNTER"s	},
	{ opendpp::names::FLUSH,				"FLUSH"s				},
	{ opendpp::names::EVENT_SIZE,			"EVENT_SIZE"s			},
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_OPENDPP_HPP_ */
