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
*	\file		rawudp.hpp
*	\brief		Raw UDP endpoint
*	\author		Giovanni Cerretani, Alberto Potenza
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_RAWUDP_HPP_
#define CAEN_INCLUDE_ENDPOINTS_RAWUDP_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "endpoints/hw_endpoint.hpp"

namespace caen {

namespace dig2 {

namespace ep {

struct sw_endpoint; // forward declaration

struct rawudp final : public hw_endpoint {

public:

	enum class names { // overrides endpoint::names
		UNKNOWN,
		DATA,
		SIZE,
		BUFFER_ID,
		FLUSH,
	};

	using args_list_t = utility::args_list_t<names, types>;

	rawudp(client& client, handle::internal_handle_t endpoint_handle);
	~rawudp();

	void set_max_size_getter(std::function<std::size_t()> f);
	void set_is_decoded_getter(std::function<bool()> f);

	void register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) override;
	void set_data_format(const std::string& json_format) override;
	void read_data(timeout_t timeout, std::va_list* args) override;
	void has_data(timeout_t timeout) override;
	void clear_data() override;
	void arm_acquisition() override;
	void disarm_acquisition() override;

	void event_start() override;
	void event_stop() override;

	static args_list_t default_data_format();
	static std::size_t data_format_dimension(names name);

	struct stats final : public endpoint {

		enum class names { // overrides endpoint::names
			UNKNOWN,
			RECEIVED_DATAGRAMS,
			DISCARDED_DATAGRAMS,
			RECEIVED_BYTES,
			EMITTED_BYTES,
			COMPLETED_BUFFERS,
			FLUSHED_BUFFERS,
			INCOMPLETE_BUFFERS,
			ESTIMATED_LOST_BUFFERS,
			RECOVERED_BYTES,
			DISCARDED_BYTES,
		};

		using args_list_t = utility::args_list_t<names, types>;

		stats(client& client, handle::internal_handle_t endpoint_handle);
		~stats();

		void set_data_format(const std::string& json_format) override;
		void read_data(timeout_t timeout, std::va_list* args) override;
		void has_data(timeout_t timeout) override;
		void clear_data() override;

		struct counters {
			std::uint64_t _received_datagrams{}; // Non-empty UDP datagrams received, including discarded datagrams.
			std::uint64_t _discarded_datagrams{}; // Received UDP datagrams that could not be used.
			std::uint64_t _received_bytes{}; // UDP bytes received, including payload, padding, and footer.
			std::uint64_t _emitted_bytes{}; // Data bytes emitted to the raw buffer, excluding padding and footer.
			std::uint64_t _completed_buffers{}; // Full buffers emitted after receiving a datagram with last flag.
			std::uint64_t _flushed_buffers{}; // Partial aligned prefixes emitted with flush flag.
			std::uint64_t _incomplete_buffers{}; // Buffers known to have ended without full reconstruction.
			std::uint64_t _estimated_lost_buffers{}; // Whole buffers conservatively estimated lost from buffer_id gaps.
			std::uint64_t _recovered_bytes{}; // Data bytes emitted from recovered partial aligned prefixes.
			std::uint64_t _discarded_bytes{}; // Known data bytes discarded locally, excluding unknown UDP losses.
		};

		void add_counters(const counters& counters);

		static args_list_t default_data_format();
		static std::size_t data_format_dimension(names name);

	private:

		struct endpoint_impl; // forward declaration
		std::unique_ptr<endpoint_impl> _pimpl;

	};

private:

	std::shared_ptr<stats> _stats_ep;

	struct endpoint_impl; // forward declaration
	std::unique_ptr<endpoint_impl> _pimpl;

};

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(rawudp::names, {
	{ rawudp::names::UNKNOWN, 	nullptr			},
	{ rawudp::names::DATA, 		"DATA"s			},
	{ rawudp::names::SIZE, 		"SIZE"s			},
	{ rawudp::names::BUFFER_ID,	"BUFFER_ID"s	},
	{ rawudp::names::FLUSH,		"FLUSH"s		},
})

NLOHMANN_JSON_SERIALIZE_ENUM(rawudp::stats::names, {
	{ rawudp::stats::names::UNKNOWN,					nullptr							},
	{ rawudp::stats::names::RECEIVED_DATAGRAMS,			"RECEIVED_DATAGRAMS"s			},
	{ rawudp::stats::names::DISCARDED_DATAGRAMS,		"DISCARDED_DATAGRAMS"s			},
	{ rawudp::stats::names::RECEIVED_BYTES,				"RECEIVED_BYTES"s				},
	{ rawudp::stats::names::EMITTED_BYTES,				"EMITTED_BYTES"s				},
	{ rawudp::stats::names::COMPLETED_BUFFERS,			"COMPLETED_BUFFERS"s			},
	{ rawudp::stats::names::FLUSHED_BUFFERS,			"FLUSHED_BUFFERS"s				},
	{ rawudp::stats::names::INCOMPLETE_BUFFERS,			"INCOMPLETE_BUFFERS"s			},
	{ rawudp::stats::names::ESTIMATED_LOST_BUFFERS,		"ESTIMATED_LOST_BUFFERS"s		},
	{ rawudp::stats::names::RECOVERED_BYTES,			"RECOVERED_BYTES"s				},
	{ rawudp::stats::names::DISCARDED_BYTES,			"DISCARDED_BYTES"s				},
})

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_RA_UDP_HPP_ */
