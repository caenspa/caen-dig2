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
*	\file		ticket_mutex.hpp
*	\brief		Ticket mutex that meets C++ Mutex requirements
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_TICKET_MUTEX_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_TICKET_MUTEX_HPP_

#include <mutex>
#include <condition_variable>
#include <queue>
#include <utility>

namespace caen {

namespace detail {

#if __cplusplus < 201703L
inline
#endif
namespace pre_cxx17 {

// to emulate returning emplace also on C++14
template <typename T, typename... Args>
decltype(auto) emplace(std::queue<T>& q, Args&& ...args) {
	return q.emplace(std::forward<Args>(args)...), q.back();
}

} // namespace pre_cxx17

#if __cplusplus >= 201703L
inline namespace cxx17 {

template <typename T, typename... Args>
decltype(auto) emplace(std::queue<T>& q, Args&& ...args) {
	return q.emplace(std::forward<Args>(args)...);
}

} // namespace cxx17
#endif


struct extended_condition_variable {
	extended_condition_variable() : _var{false} {}
	bool _var;
	std::condition_variable _cv;
};

} // namespace detail

/**
 * @brief Fair mutex that meets C++ Mutex requirements.
 *
 * Can be used with `std::lock_guard` and `std::unique_lock` while, since
 * `std::condition_variable` supports only `std::mutex`, it must be used
 * with `std::condition_variable_any`.
 * @sa https://en.cppreference.com/w/cpp/named_req/Mutex
 */
struct ticket_mutex {

	ticket_mutex() : _locked{false} {};

	/**
	 * @brief Blocks the calling thread until exclusive ownership can be obtained.
	 *
	 * @warning The behavior is undefined if the calling thread already owns it.
	 */
	void lock() {
		std::unique_lock<std::mutex> lk{_mtx};
		if (std::exchange(_locked, true)) {
			auto& ecv = detail::emplace(_queue);
			ecv._cv.wait(lk, [&ecv] { return ecv._var; });
			_queue.pop();
		}
	}

	/**
	 * @brief Attempts to obtain exclusive ownership.
	 *
	 * @warning The function is allowed to spuriously fail and return even if the not currently owned by another thread.
	 * @return true if lock succeeded, false if already locked
	 */
	bool try_lock() {
		std::unique_lock<std::mutex> lk{_mtx, std::try_to_lock};
		// owns_lock may fail if another thread is locking internal _mtx
		if (!lk.owns_lock() || _locked)
			return false;
		_locked = true;
		return true;
	}

	/**
	 * @brief Releases the calling thread's ownership.
	 *
	 * @warning The behavior is undefined if the calling thread does not own it.
	 */
	void unlock() {
		std::unique_lock<std::mutex> lk{_mtx};
		if (_queue.empty()) {
			_locked = false;
		} else {
			auto& ecv = _queue.front();
			ecv._var = true;
			ecv._cv.notify_one();
		}
	}

private:
	std::queue<detail::extended_condition_variable> _queue;
	std::mutex _mtx;
	bool _locked;
};

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_TICKET_MUTEX_HPP_ */
