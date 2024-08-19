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
*	\file		global.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_GLOBAL_HPP_
#define CAEN_INCLUDE_GLOBAL_HPP_

#include <array>
#include <memory>
#include <utility>

#include <boost/core/noncopyable.hpp>

#include "lib_definitions.hpp"
#include "client.hpp"

namespace caen {

namespace dig2 {

struct global : private boost::noncopyable {

	using client_array_type = std::array<std::unique_ptr<client>, max_size::devices>;

	// singleton
	static global& get_instance() {
		static global instance;
		return instance;
	}

	template <typename... Args>
	void create_client(std::size_t board, Args&& ...args) {
		_clients[board] = std::make_unique<client>(std::forward<Args>(args)...);
	}

	void destroy_client(std::size_t board) noexcept {
		_clients[board] = nullptr;
	}

	bool is_used(std::size_t board) const noexcept {
		return _clients[board] != nullptr;
	}

	auto& get_client(std::size_t board) const noexcept {
		return _clients[board];
	}

	const auto& get_clients() const noexcept {
		return _clients;
	}

private:

	global() = default;
	~global() = default;

	client_array_type _clients;

};

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_GLOBAL_HPP_ */
