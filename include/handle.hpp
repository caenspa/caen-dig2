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
*	\file		handle.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_HANDLE_HPP_
#define CAEN_INCLUDE_HANDLE_HPP_

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include <boost/config.hpp>
#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>
#include <boost/static_assert.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/integer.hpp"
#include "global.hpp"
#include "lib_definitions.hpp"
#include "lib_error.hpp"

namespace caen {

namespace dig2 {

namespace handle {

struct lib {

	using board_t = caen::uint_t<board_bits>::fast;

private:

	static constexpr auto client_array_size = std::tuple_size<global::client_array_type>::value;

	// compile time error if max BoardT is smaller than array size (no check_board_index is defined in that case)

	// no range check if handle::board_t stores exactly only valid array indexes
	template <typename BoardT, std::enable_if_t<(std::numeric_limits<BoardT>::max() == client_array_size - 1), int> = 0>
	static constexpr void check_board_index(BoardT) noexcept {
	}

	// range check if handle::board_t can store values bigger than array size
	template <typename BoardT, std::enable_if_t<(std::numeric_limits<BoardT>::max() > client_array_size - 1), int> = 0>
	static constexpr void check_board_index(BoardT board) {
#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_GNUC, <, 9, 1, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits" // invalid warning with GCC 8, fixed in GCC 9
#endif
		if (BOOST_UNLIKELY(board >= client_array_size))
			throw ex::invalid_argument("invalid board");
#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_GNUC, <, 9, 1, 0)
#pragma GCC diagnostic pop
#endif
	}

	const board_t _board;
	const internal_handle_t _internal_handle;

public:

	static lib get(client_handle_t h) try {
		return lib(h);
	}
	catch (const ex::invalid_argument&) {
		throw ex::invalid_handle(h);
	}

	static lib get_if_used(client_handle_t h) {
		auto ret = get(h);
		if (!ret.is_used())
			throw ex::invalid_handle(h);
		return ret;
	}

	constexpr lib(board_t board, internal_handle_t internal_handle) noexcept(noexcept(check_board_index(_board)))
	: _board{board}
	, _internal_handle{internal_handle} {
		check_board_index(_board);
	}

	constexpr explicit lib(client_handle_t h) noexcept(noexcept(check_board_index(_board)))
	: lib(get_board_handle(h), get_server_handle(h)) {}

	client& get_client() const {
		if (!is_used())
			throw "unused board"_ex;
		return *global::get_instance().get_client(_board);
	}

	constexpr client_handle_t client_handle() const noexcept {
		return static_cast<client_handle_t>(_internal_handle) | (static_cast<client_handle_t>(_board) << server_handle_bits);
	}

	constexpr board_t board() const noexcept { return _board; }
	constexpr internal_handle_t internal_handle() const noexcept { return _internal_handle; }

private:

	bool is_used() const noexcept {
		return global::get_instance().is_used(_board);
	}

	static constexpr board_t get_board_handle(client_handle_t h) noexcept {
		return caen::bit::mask_at<board_bits, server_handle_bits, board_t>(h);
	}

	static constexpr internal_handle_t get_server_handle(client_handle_t h) noexcept {
		return caen::bit::mask_at<server_handle_bits, 0, internal_handle_t>(h);
	}

};

namespace sanity_checks {

constexpr bool test_handle() noexcept {
	bool ret{true};
	ret &= (lib(0, 0).client_handle() == client_handle_t{0x00000000});
	ret &= (lib(0, 1).client_handle() == client_handle_t{0x00000001});
	ret &= (lib(1, 0).client_handle() == client_handle_t{0x01000000});
	ret &= (lib(1, 1).client_handle() == client_handle_t{0x01000001});
	ret &= (lib(0xFF, 0xF0F0F0).client_handle() == client_handle_t{0xFFF0F0F0});
	ret &= (lib(0xFFF0F0F0).client_handle() == client_handle_t{0xFFF0F0F0});
	ret &= (lib(0xFFF0F0F0).board() == lib::board_t{0xFF});
	ret &= (lib(0xFFF0F0F0).internal_handle() == internal_handle_t{0xF0F0F0});
	return ret;
}

BOOST_STATIC_ASSERT(test_handle());

} // namespace sanity_checks

} // namespace handle

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_HANDLE_HPP_ */
