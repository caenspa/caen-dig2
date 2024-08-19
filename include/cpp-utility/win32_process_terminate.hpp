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
*	\file		win32_process_terminate.hpp
*	\brief		Utility class to handle proper shutdown on Windows
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_WIN32_PROCESS_TERMINATE_HANDLER_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_WIN32_PROCESS_TERMINATE_HANDLER_HPP_

#include <boost/predef/os.h>

#if BOOST_OS_WINDOWS

#include <type_traits>
#include <thread>
#include <utility>

#if __cplusplus >= 202002L
#include <memory>
#endif

#include <boost/winapi/wait.hpp>
#include <boost/core/noncopyable.hpp>

namespace caen {

namespace win32_process_terminate {

struct handler : private boost::noncopyable {

	// singleton
	static handler& get_instance() {
		static handler instance;
		return instance;
	}

	void set_process_terminating() noexcept {
		_is_process_terminating = true;
	}

	bool is_process_terminating() const noexcept {
		return _is_process_terminating;
	}

private:

	handler() : _is_process_terminating{false} {};
	~handler() = default;

	bool _is_process_terminating;
};

inline bool is_thread_signaled_if_joinable(std::thread& t) {
	return !t.joinable() || boost::winapi::WaitForSingleObject(t.native_handle(), 0) == boost::winapi::wait_object_0;
}

inline bool is_thread_not_signaled_if_joinable(std::thread& t) {
	return !t.joinable() || boost::winapi::WaitForSingleObject(t.native_handle(), 0) == boost::winapi::wait_timeout;
}

#if __cplusplus < 202002L
inline
#endif
namespace pre_cxx20 {

/**
 * @brief Replacement for `std::construct_at` on pre C++20.
 * 
 * @note Cannot be done `constexpr` because it requires compiler support.
 * @sa https://en.cppreference.com/w/cpp/memory/construct_at
 */
template <typename T, typename... Args>
T* construct_at(T* p, Args&& ...args) {
	return ::new (const_cast<void*>(static_cast<const volatile void*>(p))) T(std::forward<Args>(args)...);
}

} // namespace pre_cxx20

#if __cplusplus >= 202002L
inline namespace cxx20 {

using std::construct_at;

} // namespace cxx20
#endif

} // namespace win32_process_terminate

} // namespace caen

#endif

#endif /* CAEN_INCLUDE_CPP_UTILITY_WIN32_PROCESS_TERMINATE_HANDLER_HPP_ */
