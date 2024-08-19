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
*	\file		raw.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "endpoints/raw.hpp"

#include <condition_variable>
#include <exception>
#include <list>
#include <mutex>
#include <thread>
#include <utility>

#include <boost/asio.hpp>
#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <server_definitions.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/circular_buffer.hpp"
#include "cpp-utility/is_in.hpp"
#include "cpp-utility/lexical_cast.hpp"
#include "cpp-utility/scope_exit.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/to_address.hpp"
#include "cpp-utility/to_underlying.hpp"
#include "cpp-utility/vector.hpp"
#include "endpoints/sw_endpoint.hpp"
#include "client.hpp"
#include "data_format_utils.hpp"
#include "lib_error.hpp"
#include "lib_definitions.hpp"
#include "library_logger.hpp"

#if BOOST_OS_WINDOWS
#include "cpp-utility/win32_process_terminate.hpp"
#endif

using namespace std::literals;

namespace caen {

namespace dig2 {

namespace ep {

namespace {

/*
 * Check if cast from size to container size type is narrowing and, if safe, resize
 *
 * The problem arises since expected size is always provided as std::uint64_t in the buffer
 * header, while container sizes are expressed in std::size_t that depends on the system
 * architecture, and could be narrower than std::uint64_t.
 * If a 64-bit size is larger than std::numeric_limits<std::size_t>::max(), than
 * a cast to std::size_t would be performed with a narrowing conversion, i.e. with a bit
 * truncation of some most significant non-zero bits.
 *
 * This is extremely unlikely, since buffers are allocated at arm_acquisition() at the
 * maximum data size that depends on the current configuration of the board. In case of too
 * large values, there should be errors in resize(), invoked by arm_acquisition(). This
 * failure would happen only if max data size related parameters are changed are after
 * disarm, but with data still to be read from the FPGA, and the new size become too large
 * to be stored in a std::size_t.
 *
 * Moreover, actually the max size allowed by a container is provided by max_size(),
 * that is usually equal to std::numeric_limits<std::ptrdiff_t>::max(), but this is checked
 * just later by caen::resize().
 *
 * Practically, this could be a problem only on 32-bit systems, where usually
 * caen::vector<caen::byte>::max_size() == 0x7fffffff. For a scope firmware, it would
 * need an event with 1'073'741'822 samples, for example 64 channels with 16'777'215
 * samples, but this configuration currently is not supported by any digitizer.
 */
template <typename Container, typename StdIntT>
void safe_increase_size(Container& buffer, StdIntT size) {
	// 1. compute required size using standard integer common type
	const auto required_size = buffer.size() + size;
	// 2. try to cast to container size type, or throw if it overflows
	const auto safe_required_size = boost::numeric_cast<typename Container::size_type>(required_size);
	// 3.resize
	caen::resize(buffer, safe_required_size);
}

auto get_port(client& client, handle::internal_handle_t endpoint_handle) {
	const auto res = client.get_value(endpoint_handle, "/port"s);
	return caen::lexical_cast<std::uint_least16_t>(res);
}

} // unnamed namespace

struct raw::endpoint_impl {

	enum class state {
		init,
		idle,
		clearing_receiver,
		decoder_started,
		quitting_decoder,
		ready,
	};

	endpoint_impl(client& client, handle::internal_handle_t endpoint_handle)
		: _logger{library_logger::create_logger(fmt::format("raw {}", endpoint_handle))}
		, _max_size_getter{}
		, _is_decoded_getter{}
		, _io_context{}
		, _endpoint(client.get_endpoint_address(), get_port(client, endpoint_handle))
		, _socket(_io_context, _endpoint.protocol())
		, _receiver{}
		, _decoder{}
		, _state{state::init}
		, _clear_buffer{false}
		, _send_stop{false}
		, _header_buffer(server_definitions::header_size)
		, _buffer()
		, _args_list{raw::default_data_format()} {

		SPDLOG_LOGGER_TRACE(_logger, "{}(endpoint_handle={})", __func__, endpoint_handle);

		// handle specific options
		const auto& rcvbuf = client.get_url_data()._rcvbuf;
		if (rcvbuf) {
			decltype(_socket)::receive_buffer_size option;
			_socket.get_option(option);
			const auto default_value = option.value();
			const auto new_value = *rcvbuf;
			SPDLOG_LOGGER_DEBUG(_logger, "overwriting socket default receive_buffer_size (default_value={}, new_value={})", default_value, new_value);
			boost::ignore_unused(default_value);
			option = new_value;
			_socket.set_option(option);
		}

		// connect
		connect();

		// start receiver
		start_receiver();

		// clear data to handle first fake event sent at connect
		clear_data();

	}

	~endpoint_impl() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

#if BOOST_OS_WINDOWS

		namespace wt = caen::win32_process_terminate;

		if (wt::handler::get_instance().is_process_terminating()) {
			/*
			 * Usually this destructor is executed within a call to CAENDig2_Close. However, if CAENDig2_Close has
			 * not been called before application main returned, this code is executed after DllMain called with
			 * DLL_PROCESS_DETACH.
			 *
			 * From DllMain documentation:
			 *
			 * > When handling DLL_PROCESS_DETACH, a DLL should free resources such as heap memory only if the DLL is
			 * > being unloaded dynamically (the lpReserved parameter is NULL). If the process is terminating (the
			 * > lpvReserved parameter is non-NULL), all threads in the process except the current thread either have
			 * > exited already or have been explicitly terminated by a call to the ExitProcess function, which might
			 * > leave some process resources such as heaps in an inconsistent state. In this case, it is not safe
			 * > for the DLL to clean up the resources.Instead, the DLL should allow the operating system to reclaim
			 * > the memory.
			 *
			 * From ExitProcess documentation:
			 *
			 * > Exiting a process causes the following:
			 * >
			 * > - All of the threads in the process, except the calling thread, terminate their execution without
			 * >     receiving a DLL_THREAD_DETACH notification.
			 * > - The states of all of the threads terminated in step 1 become signaled.
			 * > - The entry-point functions of all loaded dynamic-link libraries (DLLs) are called with
			 * >     DLL_PROCESS_DETACH.
			 * > - After all attached DLLs have executed any process termination code, the ExitProcess function
			 * >     terminates the current process, including the calling thread.
			 * > [...]
			 * > Note that returning from the main function of an application results in a call to ExitProcess.
			 *
			 * In our case, we have see that this leaves _io_context and the two threads in a inconsistent state. For
			 * example, the ~io_context() remains deadlocked in a call to GetQueuedCompletionStatus() because the
			 * internal field outstanding_work_ has been left not zero.
			 *
			 * See:
			 * - https://docs.microsoft.com/en-us/windows/win32/dlls/dllmain
			 * - https://docs.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices
			 * - https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess
			 *
			 * See also:
			 * - this bug report on Boost.ASIO: https://github.com/chriskohlhoff/asio/issues/869
			 * - this StackOverflow question: https://stackoverflow.com/q/68318199/3287591
			 *
			 * First of all we print a critical message on the logger. Then, there are two possible workarounds:
			 * - Force a return without proper cleanup, even if is not clear if it is safe to std::_Exit() from the
			 *     library cleanup (i.e. just after DllMain), and if it changes the application main() return code
			 *     (even if it seems it doesn't);
			 * - Manually reset content of the boost::asio::io_context with a placement new that resets the status of
			 *     the _io_context, as well as the content of the other instances related with threads and mutexes.
			 *
			 * The second solution seems preferable as it lets the library deinitialization to proceed correctly: in
			 * std::_Exit case the DllMain of CAEN_FELib, typically unloaded after this DLL (even if there is no
			 * guarantee about the order for libraries loaded with LoadLibrary, see this link for details
			 * https://devblogs.microsoft.com/oldnewthing/20050523-05/?p=35573) is not reached, while it is reached
			 * in the placement new case.
			 *
			 * Note that both solutions have memory leaks, but we don't care as here we are closing the library.
			 */

			_logger->warn("applying patch to make {} not block if invoked after ExitProcess", __func__);

			// assert that thread have been signaled, as per ExitProcess documentation
			BOOST_ASSERT_MSG(wt::is_thread_signaled_if_joinable(_receiver), "receiver thread not signaled");
			BOOST_ASSERT_MSG(wt::is_thread_signaled_if_joinable(_decoder), "decoder thread not signaled");

			// use placement new to reset thread and mutex related members.
			// 1. boost::asio::io_context::~io_context remains always deadlocked
			wt::construct_at(&_io_context);
			wt::construct_at(&_endpoint);
			wt::construct_at(&_socket, _io_context);
			// 2. std::thread::~thread call std::terminate() if thread was joinable
			wt::construct_at(&_receiver);
			wt::construct_at(&_decoder);
			// 3. other fields with mutexes that could have been left in undefined status
			wt::construct_at(&_logger);
			wt::construct_at(&_max_size_getter);
			wt::construct_at(&_is_decoded_getter);
			wt::construct_at(&_mtx_state);
			wt::construct_at(&_cv_state);
			wt::construct_at(&_sw_ep_list);
			wt::construct_at(&_buffer);

			return;
		}

		// 0. for consistency, assert that threads have not been signaled yet (`std::terminate` is called if threads fail)
		BOOST_ASSERT_MSG(wt::is_thread_not_signaled_if_joinable(_receiver), "receiver thread already signaled");
		BOOST_ASSERT_MSG(wt::is_thread_not_signaled_if_joinable(_decoder), "decoder thread already signaled");

#endif

		// 1. set stop flag to io_context
		SPDLOG_LOGGER_DEBUG(_logger, "setting stop flag to io_context");
		_io_context.stop();

		// 2. shutdown and close socket
		disconnect();

		// 3. close decoder thread, if present
		close_decoder();

		// 4. set state to clearing_receiver to unlock receiver thread
		SPDLOG_LOGGER_DEBUG(_logger, "set state: clearing_receiver");
		set_state(endpoint_impl::state::clearing_receiver);

		// 5. invalidate buffers to wake pending end_writing on receiver
		SPDLOG_LOGGER_DEBUG(_logger, "invalidating local buffers");
		_buffer.invalidate_buffers();

		_receiver.join();
	}

	void register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) {
		_sw_ep_list.emplace_back(std::move(ep));
	}

	void set_max_size_getter(std::function<std::size_t()> f) {
		_max_size_getter = std::move(f);
	}

	void set_is_decoded_getter(std::function<bool()> f) {
		_is_decoded_getter = std::move(f);
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<raw>::parse_data_format(_args_list, json_format);
	}

	void read_data(timeout_t timeout, std::va_list* args) {

		if (BOOST_UNLIKELY(_decoder.joinable()))
			throw ex::not_enabled();

		const auto br = _buffer.get_buffer_read(timeout);

		if (br == nullptr)
			throw ex::timeout();

		caen::scope_exit se([this] { _buffer.abort_reading(); });

		auto& data = br->_data;
		auto& n_events = br->_n_events;

		for (const auto& arg : _args_list) {
			const auto name = std::get<0>(arg);
			const auto type = std::get<1>(arg);
			switch (name) {
			case names::DATA:
				utility::put_argument_raw_data(args, type, data.data(), data.size());
				break;
			case names::SIZE:
				utility::put_argument(args, type, data.size());
				break;
			case names::N_EVENTS:
				utility::put_argument(args, type, n_events);
				break;
			default:
				throw "unsupported data type"_ex;
			}
		}

		se.release();
		_buffer.end_reading();
	}

	void has_data(timeout_t timeout) {

		const auto br = _buffer.get_buffer_read(timeout);

		if (br == nullptr)
			throw ex::timeout();

		caen::scope_exit se([this] { _buffer.abort_reading(); });
	}

	void clear_data() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// 1. close decoder thread, if present
		close_decoder();

		// 2. reset _send_stop flag
		_send_stop = false;

		// 3. set state to clearing_receiver to discard received data until first empty event
		SPDLOG_LOGGER_DEBUG(_logger, "set state: clearing_receiver");
		set_state(endpoint_impl::state::clearing_receiver);

		// 4. invalidate buffers to wake pending end_writing on receiver
		SPDLOG_LOGGER_DEBUG(_logger, "invalidating local buffers");
		_buffer.invalidate_buffers();

		// 5. wait for idle, generated by the first empty event
		SPDLOG_LOGGER_DEBUG(_logger, "waiting for state: idle");
		wait_state(endpoint_impl::state::idle);

		SPDLOG_LOGGER_DEBUG(_logger, "clear completed");
	}

	void arm_acquisition() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// 1. handle clear, part of the arm command
		clear_data();

		// 2. resize all buffers
		resize();

		// 3. start decoder thread, if required
		start_decoder();

		// 4. set state to ready
		SPDLOG_LOGGER_DEBUG(_logger, "set state: ready");
		set_state(endpoint_impl::state::ready);
	}

	void disarm_acquisition() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// nothing to wait, nothing to do
	}

	void event_start() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// nothing to wait, nothing to do
	}

	void event_stop() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		_send_stop = true;
	}

private:

	void start_receiver() {
		_receiver = std::thread([this] { receiver_main(); });
	}

	void connect() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		_socket.connect(_endpoint);
	}

	void disconnect() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		if (_socket.is_open()) {

			boost::system::error_code ec;

			_socket.shutdown(decltype(_socket)::shutdown_both, ec);
			if (ec)
				_logger->warn("socket shutdown failed: {}", ec.message());

			_socket.close(ec);
			if (ec)
				_logger->warn("socket close failed: {}", ec.message());

		}
	}

	void set_state(state s) {
		{
			std::lock_guard<std::mutex> lk{_mtx_state};
			_state = s;
		}
		_cv_state.notify_all();
	}

	void wait_state(state s) {
		std::unique_lock<std::mutex> lk{_mtx_state};
		_cv_state.wait(lk, [this, s] { return caen::is_in(_state, s); });
	}

	bool check_state(state s) {
		std::lock_guard<std::mutex> lk{_mtx_state};
		return (_state == s);
	}

	void resize() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// 1. resize current buffers
		_buffer.apply_all([max_size = _max_size_getter()](auto& b) {

			// reserve here to avoid allocations during run
			caen::reserve(b._data, max_size);

			// reset also n_events
			b._n_events = 0;

		});

		// 2. resize child decoded endpoints
		for (auto& ep : _sw_ep_list)
			ep->resize();

	}

	void start_decoder() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		BOOST_ASSERT_MSG(!_decoder.joinable(), "decoder thread must not be joiniable");

		const auto is_decoded = _is_decoded_getter();

		if (!is_decoded)
			return;

		// 1. start decored thread
		SPDLOG_LOGGER_DEBUG(_logger, "starting decoder thread");
		_decoder = std::thread([this] { decoder_main(); });

		// 2. wait for decoder_started
		SPDLOG_LOGGER_DEBUG(_logger, "waiting for state: decoder_started");
		wait_state(endpoint_impl::state::decoder_started);

		BOOST_ASSERT_MSG(_decoder.joinable(), "decoder thread must be joiniable");

		// 3. unlock any pending call to read_data on raw endpoint
		if (_buffer.is_read_pending()) {

			/*
			 * The get_buffer_read can be called by only one thread per time: this workaround generates a fake
			 * empty buffer to unlock that thread; since now on, the check for decoder thread to be joinable at
			 * the beginning of read_data will block the function before get_read_buffer is called.
			 * It is safe to assume that the thread is still locked when calling fake_write because the producer
			 * thread (receiver) is still blocked waiting for the ready state.
			 */

			// 3.a. send a fake event to wake the pending read_data on raw endpoint
			SPDLOG_LOGGER_DEBUG(_logger, "pending read_data on raw endpoint found: sending a fake empty buffer to unlock the call");
			_buffer.fake_write([](auto& b) {
				// empty buffer cannot be generated by receiver thread
				caen::clear(b._data);
				b._n_events = 0;
			});

			// 3.b. wait for the fake event to be consumed by the user
			SPDLOG_LOGGER_DEBUG(_logger, "waiting for the fake event to be consumed by the user");
			_buffer.wait_empty();

		}

	}

	void close_decoder() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		if (!_decoder.joinable())
			return;

		// 1. set state to quitting_decoder to avoid decoder to process other events after clearing software endpoints
		SPDLOG_LOGGER_DEBUG(_logger, "set state: quitting_decoder");
		set_state(endpoint_impl::state::quitting_decoder);

		// 2. wake end_writing pending in software endpoint; pending user read_data are not unlocked
		SPDLOG_LOGGER_DEBUG(_logger, "clearing data from software endpoints");
		for (auto& ep : _sw_ep_list)
			ep->clear_data();

		// 3. send a fake event to wake decoder thread
		SPDLOG_LOGGER_DEBUG(_logger, "sending a fake event to wake decoder thread");
		_buffer.fake_write([](auto& b) {
			// empty buffer cannot be generated by receiver thread
			caen::clear(b._data);
			b._n_events = 0;
		});

		// 4. wait decoder thread to exit
		SPDLOG_LOGGER_DEBUG(_logger, "joining decoder thread");
		_decoder.join();

	}

	// receiver thread
	void receiver_main() try {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// work guard prevents run() to exit if there is no pending job
		const auto work_guard = boost::asio::make_work_guard(_io_context);

		enqueue_read();
		_io_context.run();

		SPDLOG_LOGGER_DEBUG(_logger, "quitting receiver thread");

	}
	catch (const std::exception& ex) {
		_logger->critical("receiver critical error: {}", ex.what());
		_logger->flush();
		std::terminate();
	}

	void do_read(std::size_t bytes_transferred) {

		SPDLOG_LOGGER_TRACE(_logger, "{}(bytes_transferred={})", __func__, bytes_transferred);
		boost::ignore_unused(bytes_transferred);

		// impossible when using default boost::asio::transfer_all completion condition
		BOOST_ASSERT_MSG(bytes_transferred == _header_buffer.size(), "invalid bytes_transferred");

		auto header_buffer_it = _header_buffer.cbegin();
		const auto data_size = caen::serdes::deserialize<std::uint64_t>(header_buffer_it);
		const auto data_n_events = caen::serdes::deserialize<std::uint32_t>(header_buffer_it);
		const auto aligned = static_cast<bool>(caen::serdes::deserialize<std::uint8_t>(header_buffer_it));
		BOOST_ASSERT_MSG(header_buffer_it <= _header_buffer.cend(), "inconsistent buffer decoding");

		SPDLOG_LOGGER_DEBUG(_logger, "header received (data_size={}, data_n_events={}, aligned={})", data_size, data_n_events, aligned);

		{
			std::unique_lock<std::mutex> lk{_mtx_state};

			// data_size == 0 is a special software packed injected by the server after a clear
			if (data_size == 0) {
				SPDLOG_LOGGER_DEBUG(_logger, "waiting for state: clearing_receiver");
				_cv_state.wait(lk, [this] { return caen::is_in(_state, endpoint_impl::state::clearing_receiver); });
				_clear_buffer = true;
				SPDLOG_LOGGER_DEBUG(_logger, "set idle state");
				_state = endpoint_impl::state::idle;
				lk.unlock();
				_cv_state.notify_all();
				return;
			}

			SPDLOG_LOGGER_DEBUG(_logger, "waiting for state: ready or clearing_receiver");
			_cv_state.wait(lk, [this] { return caen::is_in(_state, endpoint_impl::state::ready, endpoint_impl::state::clearing_receiver); });
		}

		const auto bw = _buffer.get_buffer_write();
		caen::scope_exit se_abort([this] { _buffer.abort_writing(); });

		auto& data = bw->_data;
		auto& n_events = bw->_n_events;

		if (std::exchange(_clear_buffer, false)) {
			caen::clear(data);
			n_events = 0;
		}

		const auto offset = data.size();

		// resize (no allocation, unless user changed max data size related parameters after disarm with data still to be read)
		safe_increase_size(data, data_size);

		const auto read_buffer = boost::asio::buffer(data) + offset;

		BOOST_ASSERT_MSG(read_buffer.size() == data_size, "invalid read_buffer size");

		// read data from network
		boost::system::error_code ec;
		const auto reply_length = boost::asio::read(_socket, read_buffer, ec);
		if (ec) {
			_logger->error("data read failed: {}", ec.message());
			return;
		}

		// impossible when using default boost::asio::transfer_all completion condition
		BOOST_ASSERT_MSG(reply_length == read_buffer.size(), "invalid reply_length");

		SPDLOG_LOGGER_DEBUG(_logger, "data received (size={})", reply_length);

		n_events += data_n_events;

		if (aligned) {

			if (BOOST_UNLIKELY(check_state(endpoint_impl::state::clearing_receiver))) {
				SPDLOG_LOGGER_DEBUG(_logger, "discarding data received in clearing_receiver state");
				_clear_buffer = true;
				return;
			}

			BOOST_ASSERT_MSG(!data.empty(), "unexpected empty real buffer, reserved for fake writes");

			SPDLOG_LOGGER_DEBUG(_logger, "buffer completed (size={}, n_events={})", data.size(), n_events);

			se_abort.release();
			_buffer.end_writing();

			_clear_buffer = true;

			SPDLOG_LOGGER_DEBUG(_logger, "do_read completed");

		} else {

			SPDLOG_LOGGER_DEBUG(_logger, "buffer not completed (size={}, n_events={})", data.size(), n_events);

		}

	}

	void enqueue_read() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		boost::asio::async_read(_socket, boost::asio::buffer(_header_buffer), [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
			if (ec) {
				_logger->error("async_read failed: {} (bytes_transferred={})", ec.message(), bytes_transferred);
				disconnect();
				return;
			}
			do_read(bytes_transferred);
			enqueue_read();
		});

	}

	// decoder thread
	void decoder_main() try {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		decoder_loop();

		SPDLOG_LOGGER_DEBUG(_logger, "quitting decoder thread");

	}
	catch (const std::exception& ex) {
		_logger->critical("decoder critical error: {}", ex.what());
		_logger->flush();
		std::terminate();
	}

	void decoder_loop() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		SPDLOG_LOGGER_DEBUG(_logger, "decoder: set state: decoder_started");
		set_state(endpoint_impl::state::decoder_started);

		SPDLOG_LOGGER_DEBUG(_logger, "decoder: waiting for state: ready");
		wait_state(endpoint_impl::state::ready);

		std::size_t decoded_size{};
		std::size_t decoded_n_events{};

		for (;;) {

			SPDLOG_LOGGER_DEBUG(_logger, "decoder: waiting for data");

			if (check_state(endpoint_impl::state::quitting_decoder)) {
				SPDLOG_LOGGER_DEBUG(_logger, "decoder: event received in quitting_decoder state");
				break;
			}

			const auto br = _buffer.get_buffer_read();

			caen::scope_exit se([this] { _buffer.abort_reading(); });

			auto& data = br->_data;
			auto& n_events = br->_n_events;

			if (data.empty()) {
				SPDLOG_LOGGER_DEBUG(_logger, "decoder: discarding empty buffer");
				se.release();
				_buffer.end_reading();
				continue;
			}

			SPDLOG_LOGGER_DEBUG(_logger, "decoder: buffer received (size={}, n_events={})", data.size(), n_events);

			BOOST_ASSERT_MSG(decoded_size < data.size(), "inconsistent buffer size");

			const auto size_left = data.size() - decoded_size;

			if (BOOST_UNLIKELY(size_left < sw_endpoint::word_size))
				throw ex::runtime_error(fmt::format("not enough space for a word (size_left={})", size_left));

			const auto it = data.cbegin() + decoded_size;

			sw_endpoint::evt_header evt;

			sw_endpoint::word_t word;

			// 1st header
			auto it_tmp = it;
			caen::serdes::deserialize(it_tmp, word);
			caen::bit::mask_and_right_shift<sw_endpoint::evt_header::s::n_words>(word, evt._n_words);
			caen::bit::right_shift<sw_endpoint::evt_header::s::implementation_defined>(word);
			caen::bit::mask_and_right_shift<sw_endpoint::evt_header::s::format>(word, evt._format);
			BOOST_ASSERT_MSG(!word, "inconsistent word decoding");

			if (BOOST_UNLIKELY(evt._n_words == 0))
				throw ex::runtime_error(fmt::format("unexpected event size (n_words={})", evt._n_words));

			const std::size_t evt_size{evt._n_words * sw_endpoint::word_size};

			if (BOOST_UNLIKELY(evt_size > size_left))
				throw ex::runtime_error(fmt::format("inconsistent event size (evt_size={}, size_left={})", evt_size, size_left));

			SPDLOG_LOGGER_DEBUG(_logger, "decoder: start decoding (type={:#x}, n_words={})", caen::to_underlying(evt._format), evt._n_words);

			for (auto& ep : _sw_ep_list)
				ep->decode(caen::to_address(it), evt_size);

			// to be done after decode: events endpoint may have set _send_stop flag
			if (std::exchange(_send_stop, false)) {
				SPDLOG_LOGGER_DEBUG(_logger, "decoder: passing stop events to all endpoints");
				for (auto& ep : _sw_ep_list)
					ep->stop();
			}

			SPDLOG_LOGGER_DEBUG(_logger, "decoder: decode completed");

			decoded_size += evt_size;
			++decoded_n_events;

			BOOST_ASSERT_MSG(decoded_size <= data.size(), "inconsistent decoding");

			if (decoded_size == data.size()) {

				if (BOOST_UNLIKELY(decoded_n_events != n_events))
					throw ex::runtime_error(fmt::format("inconsistent n_events (decoded_n_events={}, n_events={})", decoded_n_events, n_events));

				SPDLOG_LOGGER_DEBUG(_logger, "decoder: buffer completed (decoded_size={}, n_events={})", decoded_size, n_events);

				se.release();
				_buffer.end_reading();

				decoded_size = 0;
				decoded_n_events = 0;

			} else {

				const auto remaining_decoded_data = data.size() - decoded_size;
				const auto remaining_n_events = n_events - decoded_n_events;

				BOOST_ASSERT_MSG(remaining_n_events > 0, "inconsistent number of events");

				SPDLOG_LOGGER_DEBUG(_logger, "decoder: buffer not completed (remaining_decoded_data={}, remaining_n_events={})", remaining_decoded_data, remaining_n_events);

				boost::ignore_unused(remaining_decoded_data, remaining_n_events);

			}
		}
	}

	// members

	struct raw_data {
		caen::vector<caen::byte> _data;
		std::uint32_t _n_events;
	};

	std::shared_ptr<spdlog::logger> _logger;

	std::function<std::size_t()> _max_size_getter;
	std::function<bool()> _is_decoded_getter;

	boost::asio::io_context _io_context;
	const boost::asio::ip::tcp::endpoint _endpoint;
	boost::asio::ip::tcp::socket _socket;

	std::thread _receiver;
	std::thread _decoder;

	state _state;
	mutable std::mutex _mtx_state;
	mutable std::condition_variable _cv_state;

	bool _clear_buffer;
	bool _send_stop;

	std::list<std::shared_ptr<sw_endpoint>> _sw_ep_list;

	caen::vector<caen::byte> _header_buffer;

	static constexpr std::size_t circular_buffer_size{2};

	caen::circular_buffer<raw_data, circular_buffer_size> _buffer;
	args_list_t _args_list;
};

raw::raw(client& client, handle::internal_handle_t endpoint_handle) try
	: hw_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>(get_client(), get_endpoint_server_handle())} {
}
catch (const std::exception& ex) {
	spdlog::error("{} failed: {}", __func__, ex.what());
}

raw::~raw() = default;

void raw::register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) {
	_pimpl->register_sw_endpoint(std::move(ep));
}

void raw::set_max_size_getter(std::function<std::size_t()> f) {
	_pimpl->set_max_size_getter(std::move(f));
}

void raw::set_is_decoded_getter(std::function<bool()> f) {
	_pimpl->set_is_decoded_getter(std::move(f));
}

void raw::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void raw::read_data(timeout_t timeout, std::va_list* args) {
	_pimpl->read_data(std::move(timeout), args);
}

void raw::has_data(timeout_t timeout) {
	_pimpl->has_data(std::move(timeout));
}

void raw::clear_data() {
	_pimpl->clear_data();
}

void raw::arm_acquisition() {
	_pimpl->arm_acquisition();
}

void raw::disarm_acquisition() {
	_pimpl->disarm_acquisition();
}

void raw::event_start() {
	_pimpl->event_start();
}

void raw::event_stop() {
	_pimpl->event_stop();
}

raw::args_list_t raw::default_data_format() {
	using vt = data_format_utils<raw>::args_type;
	return {{
			vt{names::DATA,	types::U8,		1	},
			vt{names::SIZE,	types::SIZE_T,	0	},
	}};
}

std::size_t raw::data_format_dimension(names name) {
	switch (name) {
	case names::SIZE:
	case names::N_EVENTS:
		return 0;
	case names::DATA:
		return 1;
	default:
		throw "unsupported name"_ex;
	}
}

} // namespace ep

} // namespace dig2

} // namespace caen
