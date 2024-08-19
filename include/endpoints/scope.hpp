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
*	\file		scope.hpp
*	\brief		Decoded endpoint for Scope
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_SCOPE_HPP_
#define CAEN_INCLUDE_ENDPOINTS_SCOPE_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "cpp-utility/integer.hpp"
#include "cpp-utility/vector.hpp"
#include "endpoints/sw_endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct scope final : public sw_endpoint {

	enum class names { // overrides endpoint::names
		UNKNOWN,
		TIMESTAMP,
		TIMESTAMP_NS,
		TRIGGER_ID,
		WAVEFORM,
		WAVEFORM_SIZE,
		FLAGS,
		SAMPLES_OVERLAPPED,
		BOARD_FAIL,
		EVENT_SIZE,
	};

	using args_list_t = utility::args_list_t<names, types>;

	scope(client& client, handle::internal_handle_t endpoint_handle);
	~scope();

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
	struct scope_evt {
		struct s {
			// 1st word
			static constexpr std::size_t format{evt_header::s::format};
			static constexpr std::size_t tbd_1{3};
			static constexpr std::size_t board_fail{1};
			static constexpr std::size_t trigger_id{24};
			static constexpr std::size_t n_words{evt_header::s::n_words};
			static_assert(tbd_1 + board_fail + trigger_id == evt_header::s::implementation_defined, "invalid sizes");
			// 2nd word
			static constexpr std::size_t flags{13};
			static constexpr std::size_t samples_overlapped{3};
			static constexpr std::size_t timestamp{48};
			// 3rd word
			static constexpr std::size_t ch_mask{64};
			// waveform word
			static constexpr std::size_t sample{16};
		};
		// constants
		static constexpr std::size_t evt_header_words{3};
		static constexpr std::size_t evt_header_size{evt_header_words * word_size};
		static constexpr std::size_t samples_per_word{word_bit_size / s::sample};
		// fields
		evt_header::format _format;
		// - tbd_1 not saved into event
		bool _board_fail;
		caen::uint_t<s::trigger_id>::fast _trigger_id;
		caen::uint_t<s::n_words>::fast _n_words;
		caen::uint_t<s::flags>::fast _flags;
		caen::uint_t<s::samples_overlapped>::fast _samples_overlapped;
		caen::uint_t<s::timestamp>::fast _timestamp;
		caen::uint_t<s::ch_mask>::fast _ch_mask;
		caen::vector<caen::vector<caen::uint_t<s::sample>::least>> _waveforms;
		std::size_t _event_size;
		// software fields
		bool _fake_stop_event;
		// consistency assertions
		static_assert(std::is_same<decltype(_waveforms)::value_type::value_type, std::uint16_t>::value, "documented to be handled as std::uint16_t");
	};

	struct endpoint_impl; // forward declaration
	std::unique_ptr<endpoint_impl> _pimpl;

};

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(scope::names, {
	{ scope::names::UNKNOWN,			nullptr					},
	{ scope::names::TIMESTAMP,			"TIMESTAMP"s			},
	{ scope::names::TIMESTAMP_NS,		"TIMESTAMP_NS"s			},
	{ scope::names::TRIGGER_ID,			"TRIGGER_ID"s			},
	{ scope::names::WAVEFORM,			"WAVEFORM"s				},
	{ scope::names::WAVEFORM_SIZE,		"WAVEFORM_SIZE"s		},
	{ scope::names::FLAGS,				"FLAGS"s				},
	{ scope::names::SAMPLES_OVERLAPPED,	"SAMPLES_OVERLAPPED"s	},
	{ scope::names::BOARD_FAIL,			"BOARD_FAIL"s			},
	{ scope::names::EVENT_SIZE,			"EVENT_SIZE"s			},
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_SCOPE_HPP_ */
