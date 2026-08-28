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
#include <exception>

#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>
#include <boost/config.hpp>

#include "scoped_set.hpp"
#include "to_address.hpp"

namespace caen {

/*!
 * @brief Thread-safe fixed-capacity circular buffer for producer-consumer data exchange.
 *
 * Designed for single-producer single-reader use in DAQ pipelines. Provides blocking
 * reads (with or without timeout) and non-blocking writes. Supervisor calls
 * (apply_all(), invalidate_buffers(), fake_write()) wait for no in-progress reads
 * or writes before modifying buffer state.
 *
 * Supports error propagation from producer threads to blocked readers via set_error():
 * a stored exception is rethrown by get_buffer_read() so errors surface naturally
 * through the read path without any changes to call sites.
 *
 * @tparam T Element type.
 * @tparam N Internal array size; usable capacity is N-1.
 */
template <typename T, std::size_t N> // last parameter to be removed, legacy support for timeout type
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

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_GNUC, <, 12, 0, 0)
	/*!
	 * @brief Default constructor.
	 *
	 * Initializes the buffer in a valid, empty state with no pending reads or writes.
	 * Uses fill() to work around GCC bug 71165 (fixed in GCC 12) where aggregate
	 * initialization of large std::array generates excessive code.
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
	/*!
	 * @brief Default constructor.
	 *
	 * Initializes the buffer in a valid, empty state with no pending reads or writes.
	 */
	circular_buffer()
		: _buffer{}
		, _read_iterator{_buffer.begin()}
		, _write_iterator{_buffer.begin()}
		, _valid{true}
		, _halt{false}
		, _read_halt{true}
		, _write_halt{true}
		, _read_pending{false}
		, _error{} {
	}
#endif

	//! Destructor.
	~circular_buffer() = default;

	/*!
	 * @brief Returns the maximum number of elements the buffer can hold.
	 *
	 * The capacity is N-1 because one slot is reserved to distinguish the empty
	 * and full states when both iterators are equal (see implementation notes).
	 *
	 * @return Maximum number of elements.
	 */
	constexpr std::size_t capacity() const noexcept {
		return _buffer.size() - 1;
	}

	/*!
	 * @brief Applies a function to every element, then re-validates the buffer.
	 *
	 * Supervisor call: waits until no read or write is in progress, then resets
	 * iterators and the error state, applies @p f to each element, and notifies
	 * all waiters. Typical use: resize elements or reset per-element state on re-arm.
	 *
	 * @param f Function to apply to each element.
	 */
	void apply_all(std::function<void(T&)> f) {
		supervisor_call([this, &f] {
			_valid = false;
			_error = nullptr;
			_read_iterator = _buffer.begin();
			_write_iterator = _buffer.begin();
			std::for_each(_buffer.begin(), _buffer.end(), f);
		});
	}

	/*!
	 * @brief Marks the buffer as invalid and resets iterators and the error state.
	 *
	 * Supervisor call: waits until no read or write is in progress, then clears
	 * the buffer state without touching element data. Writers that complete after
	 * this call will not advance the write iterator (because _valid is false).
	 * Used to drain the buffer on clear or re-arm.
	 */
	void invalidate_buffers() {
		supervisor_call([this] {
			_valid = false;
			_error = nullptr;
			_read_iterator = _buffer.begin();
			_write_iterator = _buffer.begin();
		});
	}

	/*!
	 * @brief Stores an exception to be rethrown by blocked or future readers.
	 *
	 * First-wins: if an error is already stored, subsequent calls are silently
	 * ignored. Wakes all threads blocked in get_buffer_read(). The stored error
	 * is cleared by invalidate_buffers() and apply_all() (called on re-arm), so
	 * the buffer returns to a clean state without manual intervention.
	 *
	 * Does not affect the write path: get_buffer_write() and end_writing() are
	 * unaffected and continue to operate normally.
	 *
	 * @param e Exception pointer to store and rethrow to readers.
	 */
	void set_error(std::exception_ptr e) {
		{
			std::lock_guard<std::mutex> lk(_mtx);
			if (!_error)
				_error = std::move(e);
		}
		notify();
	}

	/*!
	 * @brief Atomically clears the buffer and inserts a single fake element.
	 *
	 * Supervisor call: waits until no read or write is in progress, resets the
	 * buffer to contain exactly one element initialized by @p f, then notifies
	 * all waiters. Useful for injecting sentinel or control events.
	 *
	 * @param f Function to initialize the fake element.
	 */
	void fake_write(std::function<void(T&)> f) {
		supervisor_call([this, &f] {
			_valid = true;
			_read_iterator = _buffer.begin();
			_write_iterator = _buffer.begin() + 1;
			f(_buffer.front());
		});
	}

	/*!
	 * @brief Returns whether the buffer currently holds at least one element.
	 *
	 * Non-blocking. Returns true only if the buffer is valid, not halted, and not empty.
	 *
	 * @return @c true if data is immediately available.
	 */
	bool has_data() {
		std::unique_lock<std::mutex> lk(_mtx);
		return valid_and_not_empty();
	}

	/*!
	 * @brief Blocks until the buffer is valid and empty.
	 *
	 * Used by the writer side to wait for the reader to drain all elements before
	 * a supervisor operation or shutdown.
	 */
	void wait_empty() {
		std::unique_lock<std::mutex> lk(_mtx);
		_cv.wait(lk, [this] { return valid_and_empty(); });
	}

	/*!
	 * @brief Wakes all threads blocked on the buffer's condition variable.
	 *
	 * Calls notify_all() on the internal condition variable. Used after writing
	 * or after set_error() to unblock waiting readers.
	 */
	void notify() noexcept {
		_cv.notify_all();
	}

	/*!
	 * @brief Blocks until an element is available, then returns a pointer to it.
	 *
	 * Acquires the mutex and waits until data is available or an error is set.
	 * If set_error() was called, rethrows the stored exception (takes precedence
	 * over available data). The element remains owned by the buffer until the caller
	 * invokes end_reading() (to commit) or abort_reading() (to discard). Only one
	 * read can be pending at a time; a second concurrent call throws.
	 *
	 * @return Pointer to the next readable element.
	 * @throws std::runtime_error If another read is already pending.
	 * @throws Any exception stored by set_error().
	 */
	const_pointer get_buffer_read() {
		std::unique_lock<std::mutex> lk(_mtx);
		// prevent this function to be called by two threads until the buffer is released
		if (BOOST_UNLIKELY(_read_pending))
			throw std::runtime_error("another call to get_buffer_read is pending");
		scoped_set<bool> ss(_read_pending, true);
		auto condition = [this] { return _error || valid_and_not_empty(); };
		// wait until the condition is satisfied
		_cv.wait(lk, condition);
		// an error set by a producer thread takes precedence: rethrow it (~ss restores
		// _read_pending under lock, then ~lk unlocks, during stack unwinding)
		if (BOOST_UNLIKELY(static_cast<bool>(_error)))
			std::rethrow_exception(_error);
		ss.release();
		_read_halt = false;
		// no need to notify for _read_halt set to false
		return caen::to_address(_read_iterator);
	}

	/*!
	 * @brief Returns the sentinel value that represents an infinite timeout.
	 *
	 * Passing this value to get_buffer_read(timeout) makes it behave exactly like
	 * the blocking no-timeout overload.
	 *
	 * @tparam Timeout Duration type.
	 * @return Duration value of -1, used as the infinite-timeout sentinel.
	 */
	template <typename Timeout>
	static constexpr Timeout infinite_timeout() {
		return Timeout{ -1 };
	}

	/*!
	 * @brief Waits up to @p timeout for an element, then returns a pointer to it.
	 *
	 * If @p timeout equals infinite_timeout(), delegates to the blocking overload.
	 * If the timeout expires before data is available, returns @c nullptr. If
	 * set_error() was called, rethrows the stored exception (takes precedence over
	 * timeout). Only one read can be pending at a time; a second concurrent call throws.
	 *
	 * @param timeout Maximum time to wait; infinite_timeout() means no limit.
	 * @return Pointer to the next readable element, or @c nullptr on timeout.
	 * @throws std::runtime_error If another read is already pending.
	 * @throws Any exception stored by set_error().
	 */
	template <typename Rep, typename Period>
	const_pointer get_buffer_read(std::chrono::duration<Rep, Period> timeout) {
		if (timeout == infinite_timeout<decltype(timeout)>())
			return get_buffer_read();
		std::unique_lock<std::mutex> lk(_mtx);
		// prevent this function to be called by two threads until the buffer is released
		if (BOOST_UNLIKELY(_read_pending))
			throw std::runtime_error("another call to get_buffer_read is pending");
		scoped_set<bool> ss(_read_pending, true);
		auto condition = [this] { return _error || valid_and_not_empty(); };
		// call wait_for only if the condition is not satisfied and the timeout is not zero,
		// to avoid overheads of transforming wait_for into wait_until
		if (!condition() && (timeout == decltype(timeout)::zero() || !_cv.wait_for(lk, timeout, condition)))
			return nullptr;
		// an error set by a producer thread takes precedence: rethrow it (~ss restores
		// _read_pending under lock, then ~lk unlocks, during stack unwinding)
		if (BOOST_UNLIKELY(static_cast<bool>(_error)))
			std::rethrow_exception(_error);
		ss.release();
		_read_halt = false;
		// no need to notify for _read_halt set to false
		return caen::to_address(_read_iterator);
	}

	/*!
	 * @brief Discards the current read without advancing the read iterator.
	 *
	 * The element pointed to by the last get_buffer_read() remains in the buffer
	 * and will be returned again on the next get_buffer_read() call. Must be called
	 * after get_buffer_read() if the element should not be consumed.
	 */
	void abort_reading() {
		finalize_reading<false>();
	}

	/*!
	 * @brief Commits the current read, advancing the read iterator, and notifies writers.
	 *
	 * Marks the element pointed to by the last get_buffer_read() as consumed,
	 * advances the read iterator, and notifies all waiting writers. Must be called
	 * after get_buffer_read() to release the element.
	 */
	void end_reading() {
		finalize_reading<true>();
		notify();
	}

	/*!
	 * @brief Commits the current read, notifying writers only if the buffer is now empty.
	 *
	 * Like end_reading(), but skips the notify() call when the buffer still contains
	 * elements, avoiding unnecessary wakeups in high-throughput scenarios.
	 */
	void end_reading_relaxed() {
		const auto current_size = finalize_reading<true>();
		if (current_size == 0)
			notify();
	}

	/*!
	 * @brief Returns a pointer to the current write slot (non-blocking).
	 *
	 * Marks a write as in progress and returns a pointer to the element to be filled.
	 * The caller must follow up with end_writing() (to commit) or abort_writing()
	 * (to discard). There is always at least one writable slot because capacity is N-1.
	 *
	 * @return Pointer to the element to write into.
	 */
	pointer get_buffer_write() {
		std::unique_lock<std::mutex> lk(_mtx);
		_write_halt = false;
		_valid = true; // can be set to false by supervisor calls
		// no need to notify for _write_halt set to false
		return caen::to_address(_write_iterator);
	}

	/*!
	 * @brief Discards the current write without advancing the write iterator.
	 *
	 * The write slot is released but its contents are not made visible to readers.
	 * Must be called after get_buffer_write() if the element should not be published.
	 */
	void abort_writing() {
		finalize_writing<false>();
	}

	/*!
	 * @brief Commits the current write, advancing the write iterator, and notifies readers.
	 *
	 * Makes the element filled since get_buffer_write() visible to readers, advances
	 * the write iterator, and notifies all waiting readers. Blocks if the buffer is
	 * full until a reader consumes an element.
	 */
	void end_writing() {
		finalize_writing<true>();
		notify();
	}

	/*!
	 * @brief Commits the current write, notifying readers only if the buffer is now full.
	 *
	 * Like end_writing(), but skips the notify() call when the buffer still has free
	 * slots, avoiding unnecessary wakeups in high-throughput scenarios.
	 */
	void end_writing_relaxed() {
		const auto current_size = finalize_writing<true>();
		if (current_size == capacity())
			notify();
	}

	/*!
	 * @brief Returns whether a read is currently in progress.
	 *
	 * Thread-safe. Returns @c true between a call to get_buffer_read() and the
	 * corresponding end_reading() or abort_reading().
	 *
	 * @return @c true if a read is pending.
	 */
	bool is_read_pending() {
		std::unique_lock<std::mutex> lk(_mtx);
		return _read_pending;
	}

	/*!
	 * @deprecated Use is_read_pending() instead.
	 */
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
	std::exception_ptr _error;
	mutable std::mutex _mtx;
	mutable std::condition_variable _cv;
	mutable std::condition_variable _cv_supervisor;
};

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_CIRCULAR_BUFFER_HPP_ */
