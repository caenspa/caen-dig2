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
*	\file		circular_buffer.hpp
*	\brief		Circular buffer
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_CIRCULAR_BUFFER_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_CIRCULAR_BUFFER_HPP_

#include <array>
#include <functional>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdexcept>

#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>
#include <boost/config.hpp>

#include "scoped_set.hpp"
#include "to_address.hpp"

namespace caen {

template <typename T, std::size_t N, typename = void> // last parameter to be removed, legacy support for timeout type
class circular_buffer {
public:

	/*
	 * Notes on the implementation.
	 *
	 * 1. Why the internal buffer is implemented using a std::array instead of a
	 * std::vector?
	 *
	 * Even if not strictly necessary, if the constant N is known at compile time,
	 * there can be some interesting optimizations. For example, size() relies on
	 * modulo operation. If the divisor is not known at compile time, an integer
	 * division is required, but, if N is a compile time constant, the modulo
	 * operation is optimized using either the Montgomery modular multiplication
	 * or, if N is a power of 2, a single AND operation with a mask.
	 *
	 * 2. Why capacity is N - 1?
	 *
	 * Since the circular buffer is implemented using two iterators that always
	 * point to valid iterators of the internal container (i.e. never pointing to
	 * the container end), there is no way to distinguish the empty case and the
	 * full case when the two iterators are equal. This allow the get_buffer_write
	 * to be non blocking, because there is always at least an element that can
	 * be written.
	 */

	static_assert(N > 0, "N cannot be zero");

	using container_type = std::array<T, N>;
	using value_type = typename container_type::value_type;
	using size_type = typename container_type::size_type;
	using iterator = typename container_type::iterator;
	using const_iterator = typename container_type::const_iterator;
	using reference = typename container_type::reference;
	using const_reference = typename container_type::const_reference;
	using pointer = typename container_type::pointer;
	using const_pointer = typename container_type::const_pointer;

	template <typename Timeout>
	static constexpr Timeout infinite_timeout() {
		return Timeout{-1};
	}

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_GNUC, <, 12, 0, 0)
	/*
	 * Due to GCC bug 71165, fixed on GCC 12, the aggregate initialization
	 * of buffer, for large value of N, would generate large code and
	 * take a lot of time to compile. As a workaround we use fill().
	 */
	circular_buffer()
		: _read_iterator{_buffer.begin()}
		, _write_iterator{_buffer.begin()}
		, _valid{true}
		, _halt{false}
		, _read_halt{true}
		, _write_halt{true}
		, _read_pending{false} {
		_buffer.fill(T{});
	}
#else
	circular_buffer()
		: _buffer{}
		, _read_iterator{_buffer.begin()}
		, _write_iterator{_buffer.begin()}
		, _valid{true}
		, _halt{false}
		, _read_halt{true}
		, _write_halt{true}
		, _read_pending{false} {
	}
#endif

	~circular_buffer() = default;

	constexpr std::size_t capacity() const noexcept {
		return _buffer.size() - 1;
	}

	void apply_all(std::function<void(T&)> f) {
		supervisor_call([this, &f] {
			_valid = false;
			_read_iterator = _buffer.begin();
			_write_iterator = _buffer.begin();
			std::for_each(_buffer.begin(), _buffer.end(), f);
		});
	}

	void invalidate_buffers() {
		supervisor_call([this] {
			_valid = false;
			_read_iterator = _buffer.begin();
			_write_iterator = _buffer.begin();
		});
	}

	// atomic function to clear and insert a fake event manipulated with f
	void fake_write(std::function<void(T&)> f) {
		supervisor_call([this, &f] {
			_valid = true;
			_read_iterator = _buffer.begin();
			_write_iterator = _buffer.begin() + 1;
			f(_buffer.front());
		});
	}

	bool has_data() {
		std::unique_lock<std::mutex> lk(_mtx);
		return valid_and_not_empty();
	}

	void wait_empty() {
		std::unique_lock<std::mutex> lk(_mtx);
		_cv.wait(lk, [this] { return valid_and_empty(); });
	}

	void notify() noexcept {
		_cv.notify_all();
	}

	const_pointer get_buffer_read() {
		return get_buffer_read(infinite_timeout<std::chrono::milliseconds>());
	}

	template <typename Timeout>
	const_pointer get_buffer_read(Timeout timeout) {
		std::unique_lock<std::mutex> lk(_mtx);
		// prevent this function to be called by two threads until the buffer is released
		if (BOOST_UNLIKELY(_read_pending))
			throw std::runtime_error("another call to get_buffer_read is pending");
		scoped_set<bool> ss(_read_pending, true);
		auto condition = [this] { return valid_and_not_empty(); };
		switch (timeout.count()) {
		case infinite_timeout<decltype(timeout)>().count():
			_cv.wait(lk, condition);
			break;
		case decltype(timeout)::zero().count():
			// special case to avoid wait with 0 timeout
			if (!condition())
				return nullptr;
			break;
		default:
			if (!_cv.wait_for(lk, timeout, condition))
				return nullptr;
			break;
		}
		ss.release();
		_read_halt = false;
		// no need to notify for _read_halt set to false
		return caen::to_address(_read_iterator);
	}

	void abort_reading() {
		finalize_reading<false>();
	}

	void end_reading() {
		finalize_reading<true>();
		notify();
	}

	void end_reading_relaxed() {
		const auto current_size = finalize_reading<true>();
		if (current_size == 0)
			notify();
	}

	pointer get_buffer_write() {
		std::unique_lock<std::mutex> lk(_mtx);
		_write_halt = false;
		_valid = true; // can be set to false by supervisor calls
		// no need to notify for _write_halt set to false
		return caen::to_address(_write_iterator);
	}

	void abort_writing() {
		finalize_writing<false>();
	}

	void end_writing() {
		finalize_writing<true>();
		notify();
	}

	void end_writing_relaxed() {
		const auto current_size = finalize_writing<true>();
		if (current_size == capacity())
			notify();
	}

	bool is_read_pending() {
		std::unique_lock<std::mutex> lk(_mtx);
		return _read_pending;
	}

	[[deprecated("renamed is_read_pending")]] bool is_get_buffer_read_pending() {
		return is_read_pending();
	}

private:

	bool empty() const noexcept {
		return _write_iterator == _read_iterator;
	}

	std::size_t size() const noexcept {
		return (_write_iterator - _read_iterator) % _buffer.size();
	}

	void handle_supervisor_halt(std::unique_lock<std::mutex>& lk) {
		if (_halt) {
			lk.unlock();
			_cv_supervisor.notify_all();
			lk.lock();
		}
	}

	template <bool Success>
	std::size_t finalize_reading() {
		std::unique_lock<std::mutex> lk(_mtx);
		if (Success)
			increment_iterator(_read_iterator);
		_read_halt = true;
		handle_supervisor_halt(lk);
		_read_pending = false;
		return size();
	}

	template <bool Success>
	std::size_t finalize_writing() {
		std::unique_lock<std::mutex> lk(_mtx);
		_write_halt = true;
		handle_supervisor_halt(lk);
		if (Success) {
			_cv.wait(lk, [this] { return !_halt && !full(); });
			if (_valid) // could be set to false by supervisor calls
				increment_iterator(_write_iterator);
		} else {
			_cv.wait(lk, [this] { return !_halt; });
		}
		return size();
	}

	bool valid() const noexcept {
		return !_halt && _valid;
	}

	bool valid_and_not_empty() const noexcept {
		return valid() && !empty();
	}

	bool valid_and_empty() const noexcept {
		return valid() && empty();
	}

	bool full() const noexcept {
		return size() == capacity();
	}

	template <typename It>
	void increment_iterator(It& it) noexcept {
		if (++it == _buffer.end())
			it = _buffer.begin();
	}

	template <typename Callable>
	void supervisor_call(const Callable &call) {
		{
			std::unique_lock<std::mutex> lk(_mtx);
			scoped_set<bool> ss(_halt, true);
			_cv_supervisor.wait(lk, [this] { return _read_halt && _write_halt; });
			call();
		}
		notify();
	}

	container_type _buffer;
	const_iterator _read_iterator;
	iterator _write_iterator;
	bool _valid;
	bool _halt;
	bool _read_halt;
	bool _write_halt;
	bool _read_pending;
	mutable std::mutex _mtx;
	mutable std::condition_variable _cv;
	mutable std::condition_variable _cv_supervisor;
};

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_CIRCULAR_BUFFER_HPP_ */
