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
*	\file		scoped_set.hpp
*	\brief		Scoped set
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_SCOPED_SET_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_SCOPED_SET_HPP_

#include <utility>
#include <type_traits>

#include "optional.hpp"

namespace caen {

template <typename T>
class scoped_set {

	using value_type = T;
	using reference = T&;
	using const_reference = const T&;

	reference _variable;
	optional<value_type> _original_value;

public:

	template <typename Arg>
	scoped_set(reference variable, Arg&& value) noexcept
	: _variable{variable}
	, _original_value{std::exchange(_variable, std::forward<Arg>(value))}{
		static_assert(std::is_nothrow_assignable<reference, Arg>::value, "is is better to restrict to noexcept assignment");
	}

	~scoped_set() {
		if (_original_value)
			_variable = std::move_if_noexcept(*_original_value);
	}

	const_reference get() const noexcept {
		return _variable;
	}

	const_reference get_original_value() const {
		return _original_value.value(); // may throw bad_optional_access
	}

	operator const_reference() const noexcept {
		return get();
	}

	void release() noexcept {
		_original_value = nullopt;
	}

	bool released() const noexcept {
		return static_cast<bool>(_original_value);
	}

};

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_SCOPED_SET_HPP_ */
