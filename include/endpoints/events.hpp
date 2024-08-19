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
*	\file		events.hpp
*	\brief		Special event endpoint
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_EVENTS_HPP_
#define CAEN_INCLUDE_ENDPOINTS_EVENTS_HPP_

#include <memory>
#include <list>
#include <vector>
#include <utility>

#include "cpp-utility/variant.hpp"
#include "cpp-utility/vector.hpp"
#include "endpoints/sw_endpoint.hpp"
#include "library_logger.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct hw_endpoint; // forward declaration

struct events final : public sw_endpoint {

	enum class names { UNKNOWN }; // overrides endpoint::names

	events(client& client, hw_endpoint& parent_endpoint);
	~events();

	void resize() override;
	void decode(const caen::byte* p, std::size_t size) override;
	void stop() override;
	void set_data_format(const std::string &json_format) override;
	void read_data(timeout_t timeout, std::va_list* args) override;
	void has_data(timeout_t timeout) override;
	void clear_data() override;

private:
	struct special_evt {
		struct s {
			// 1st header
			static constexpr std::size_t format{evt_header::s::format};
			static constexpr std::size_t event_id{4};
			static constexpr std::size_t tdb_1{16};
			static constexpr std::size_t n_additional_headers{8};
			static constexpr std::size_t n_words{evt_header::s::n_words};
			static_assert(event_id + tdb_1 + n_additional_headers == evt_header::s::implementation_defined, "invalid sizes");
			// other headers
			static constexpr std::size_t additional_header_type{8};
			static constexpr std::size_t additional_header_data{56};
		};
		enum struct event_id_type : caen::uint_t<s::event_id>::fast {
			start	= 0b0000,
			stop	= 0b0010,
		};
		enum struct additional_header_type_type : caen::uint_t<s::additional_header_type>::fast {
			size_48		= 0b00000000,
			size_32		= 0b00000001,
			acq_width	= 0b00000010,
		};
		struct additional_header {
			additional_header_type_type _type;
			caen::uint_t<s::additional_header_data>::fast _data;
		};
		struct start_event_data {
			static constexpr event_id_type event_id{event_id_type::start};
			static constexpr std::size_t n_additional_headers{3};
			struct s {
				static constexpr std::size_t tbd_1{24};
				static constexpr std::size_t decimation_factor_log2{5};
				static constexpr std::size_t n_traces{2};
				static constexpr std::size_t acq_width{25};
				static constexpr std::size_t tbd_2{24};
				static constexpr std::size_t ch_mask_31_0{32};
				static constexpr std::size_t tbd_3{24};
				static constexpr std::size_t ch_mask_63_32{32};
			};
			// - tbd_1 not saved into event
			caen::uint_t<s::decimation_factor_log2>::fast _decimation_factor_log2;
			caen::uint_t<s::n_traces>::fast _n_traces;
			caen::uint_t<s::acq_width>::fast _acq_width;
			// - tbd_2 not saved into event
			caen::uint_t<s::ch_mask_31_0>::fast _ch_mask_31_0;
			// - tbd_3 not saved into event
			caen::uint_t<s::ch_mask_63_32>::fast _ch_mask_63_32;
		};
		struct stop_event_data {
			static constexpr event_id_type event_id{event_id_type::stop};
			static constexpr std::size_t n_additional_headers{2};
			struct s {
				static constexpr std::size_t tbd_1{8};
				static constexpr std::size_t evt_time_tag{48};
				static constexpr std::size_t tbd_2{24};
				static constexpr std::size_t dead_time{32};
			};
			// - tbd_1 not saved into event
			caen::uint_t<s::evt_time_tag>::fast _evt_time_tag;
			// - tbd_2 not saved into event
			caen::uint_t<s::dead_time>::fast _dead_time;
		};
		evt_header::format _format;
		event_id_type _event_id;
		// - tbd_1 not saved into event
		caen::uint_t<s::n_additional_headers>::fast _n_additional_headers;
		caen::uint_t<s::n_words>::fast _n_words;
		caen::vector<additional_header> _additional_headers;
		caen::variant<caen::monostate, start_event_data, stop_event_data> _event_data;
	};

	std::shared_ptr<spdlog::logger> _logger;
	hw_endpoint& _hw_endpoint;
	std::list<std::pair<names, types>> _args_list;
};

NLOHMANN_JSON_SERIALIZE_ENUM(events::names, {
	{ events::names::UNKNOWN, 			nullptr }
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_EVENTS_HPP_ */
