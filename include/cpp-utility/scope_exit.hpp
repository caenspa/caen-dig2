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
*	This file is part of the CAEN C++ Utility.
*
*	The CAEN C++ Utility is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	The CAEN C++ Utility is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN C++ Utility; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		scope_exit.hpp
*	\brief		Scope exit, inspired to TS v3 `std::experimental::scope_exit`
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_SCOPE_EXIT_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_SCOPE_EXIT_HPP_

#include <functional>

namespace caen {

struct scope_exit {

	template <typename Function>
	explicit scope_exit(Function&& f)
		: _f(std::forward<Function>(f)) {}

	scope_exit(scope_exit&& other) = default;
	scope_exit(const scope_exit&) = delete;

	~scope_exit() {
		if (_f)
			_f();
	}

	void release() noexcept {
		_f = nullptr;
	}

private:
	std::function<void()> _f;
};

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_SCOPE_EXIT_HPP_ */
