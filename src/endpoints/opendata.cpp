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
*	\file		opendata.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "endpoints/opendata.hpp"

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

auto get_port(client& client, handle::internal_handle_t endpoint_handle) {
	const auto res = client.get_value(endpoint_handle, "/port"s);
	return caen::lexical_cast<std::uint_least16_t>(res);
}

} // unnamed namespace

struct opendata::endpoint_impl {

	enum class state {
		init,
		ready,
	};

	endpoint_impl(client& client, handle::internal_handle_t endpoint_handle)
		: _logger{library_logger::create_logger(fmt::format("opendata {}", endpoint_handle))}
		, _max_size_getter{}
		, _io_context{}
		, _endpoint(client.get_endpoint_address(), get_port(client, endpoint_handle))
		, _socket(_io_context, _endpoint.protocol())
		, _receiver{}
		, _state{state::init}
		, _clear_buffer{false}
		, _header_buffer(server_definitions::header_size)
		, _buffer()
		, _args_list{opendata::default_data_format()} {

		SPDLOG_LOGGER_TRACE(_logger, "{}(endpoint_handle={})", __func__, endpoint_handle);

		// handle specific options
		if (auto&& rcvbuf = client.get_url_data()._rcvbuf; rcvbuf.has_value()) {
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

		/*
		 * Up to this point, the implementation is the same of raw and rawudp.
		 * The main difference is that here we can call read_data() even if
		 * the digitizer is not armed, since the data could be always available.
		 *
		 * The following 2 calls are from raw::arm_acquisition(), while the
		 * content of opendata::arm_acquisition() is empty.
		 */

		// resize all buffers
		resize();

		// set state to ready
		SPDLOG_LOGGER_DEBUG(_logger, "set state: ready");
		set_state(endpoint_impl::state::ready);

	}

	~endpoint_impl() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

#if BOOST_OS_WINDOWS

		namespace wt = caen::win32_process_terminate;

		if (wt::handler::get_instance().is_process_terminating()) {
			/*
			 * See comment on raw.cpp for details about this patch.
			 */

			_logger->warn("applying patch to make {} not block if invoked after ExitProcess", __func__);

			// assert that thread have been signaled, as per ExitProcess documentation
			BOOST_ASSERT_MSG(wt::is_thread_signaled_if_joinable(_receiver), "receiver thread not signaled");

			// use placement new to reset thread and mutex related members.
			// 1. boost::asio::io_context::~io_context remains always deadlocked
			wt::construct_at(&_io_context);
			wt::construct_at(&_endpoint);
			wt::construct_at(&_socket, _io_context);
			// 2. std::thread::~thread call std::terminate() if thread was joinable
			wt::construct_at(&_receiver);
			// 3. other fields with mutexes that could have been left in undefined status
			wt::construct_at(&_logger);
			wt::construct_at(&_max_size_getter);
			wt::construct_at(&_mtx_state);
			wt::construct_at(&_cv_state);
			wt::construct_at(&_buffer);

			return;
		}

		// 0. for consistency, assert that threads have not been signaled yet (`std::terminate` is called if threads fail)
		BOOST_ASSERT_MSG(wt::is_thread_not_signaled_if_joinable(_receiver), "receiver thread already signaled");

#endif

		// 1. set stop flag to io_context
		SPDLOG_LOGGER_DEBUG(_logger, "setting stop flag to io_context");
		_io_context.stop();

		// 2. shutdown and close socket
		disconnect();

		// 5. invalidate buffers to wake pending end_writing on receiver
		SPDLOG_LOGGER_DEBUG(_logger, "invalidating local buffers");
		_buffer.invalidate_buffers();

		_receiver.join();
	}

	void register_sw_endpoint([[maybe_unused]] std::shared_ptr<sw_endpoint> ep) {
		// nothing to do
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<opendata>::parse_data_format(_args_list, json_format);
	}

	void read_data(timeout_t timeout, std::va_list* args) {

		const auto br = _buffer.get_buffer_read(timeout);

		if (br == nullptr)
			throw ex::timeout();

		caen::scope_exit se([this] { _buffer.abort_reading(); });

		auto& data = br->_data;

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

		// not supported
	}

	void arm_acquisition() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// see comment on constructor
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

		// nothing to wait, nothing to do
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
		std::lock_guard lk{_mtx_state};
		_state = s;
		_cv_state.notify_all();
	}

	void wait_state(state s) {
		std::unique_lock lk{_mtx_state};
		_cv_state.wait(lk, [this, s] { return caen::is_in(_state, s); });
	}

	bool check_state(state s) {
		std::lock_guard lk{_mtx_state};
		return (_state == s);
	}

	void resize() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		const auto max_size = 1 << 26; // TODO read from opendatasize max value

		// 1. resize current buffers
		_buffer.apply_all([max_size](auto& b) {

			// reserve here to avoid allocations during run
			caen::reserve(b._data, max_size);

		});

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
		[[maybe_unused]] const auto data_n_events = caen::serdes::deserialize<std::uint32_t>(header_buffer_it);
		const auto aligned = static_cast<bool>(caen::serdes::deserialize<std::uint8_t>(header_buffer_it));
		BOOST_ASSERT_MSG(header_buffer_it <= _header_buffer.cend(), "inconsistent buffer decoding");

		SPDLOG_LOGGER_DEBUG(_logger, "header received (data_size={}, data_n_events={}, aligned={})", data_size, data_n_events, aligned);

		{
			std::unique_lock lk{_mtx_state};

			// data_size == 0 is a special software packed injected by the server after a clear
			if (data_size == 0) {
				// just ignore it on this endpoint, clear not supported
				return;
			}

			SPDLOG_LOGGER_DEBUG(_logger, "waiting for state: ready or clearing_receiver");
			_cv_state.wait(lk, [this] { return caen::is_in(_state, endpoint_impl::state::ready); });
		}

		const auto bw = _buffer.get_buffer_write();
		caen::scope_exit se_abort([this] { _buffer.abort_writing(); });

		auto& data = bw->_data;

		if (std::exchange(_clear_buffer, false)) {
			caen::clear(data);
		}

		const auto offset = data.size();

		// resize (no allocation, unless user changed max data size related parameters after disarm with data still to be read)
		caen::safe_increase_size(data, data_size);

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

		if (aligned) {

			BOOST_ASSERT_MSG(!data.empty(), "unexpected empty real buffer, reserved for fake writes");

			SPDLOG_LOGGER_DEBUG(_logger, "buffer completed (size={})", data.size());

			se_abort.release();
			_buffer.end_writing();

			_clear_buffer = true;

			SPDLOG_LOGGER_DEBUG(_logger, "do_read completed");

		} else {

			SPDLOG_LOGGER_DEBUG(_logger, "buffer not completed (size={})", data.size());

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

	// members

	struct raw_data {
		caen::vector<std::byte> _data;
	};

	std::shared_ptr<spdlog::logger> _logger;

	std::function<std::size_t()> _max_size_getter;

	boost::asio::io_context _io_context;
	const boost::asio::ip::tcp::endpoint _endpoint;
	boost::asio::ip::tcp::socket _socket;

	std::thread _receiver;

	state _state;
	mutable std::mutex _mtx_state;
	mutable std::condition_variable _cv_state;

	bool _clear_buffer;

	caen::vector<std::byte> _header_buffer;

	static inline constexpr std::size_t circular_buffer_size{2};

	caen::circular_buffer<raw_data, circular_buffer_size> _buffer;
	args_list_t _args_list;
};

opendata::opendata(client& client, handle::internal_handle_t endpoint_handle) try
	: hw_endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>(get_client(), get_endpoint_server_handle())} {
}
catch (const std::exception& ex) {
	spdlog::error("{} failed: {}", __func__, ex.what());
}

opendata::~opendata() = default;

void opendata::register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) {
	_pimpl->register_sw_endpoint(std::move(ep));
}

void opendata::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void opendata::read_data(timeout_t timeout, std::va_list* args) {
	_pimpl->read_data(std::move(timeout), args);
}

void opendata::has_data(timeout_t timeout) {
	_pimpl->has_data(std::move(timeout));
}

void opendata::clear_data() {
	_pimpl->clear_data();
}

void opendata::arm_acquisition() {
	_pimpl->arm_acquisition();
}

void opendata::disarm_acquisition() {
	_pimpl->disarm_acquisition();
}

void opendata::event_start() {
	_pimpl->event_start();
}

void opendata::event_stop() {
	_pimpl->event_stop();
}

opendata::args_list_t opendata::default_data_format() {
	using vt = data_format_utils<opendata>::args_type;
	return {{
			vt{names::DATA,	types::U8,		1	},
			vt{names::SIZE,	types::SIZE_T,	0	},
	}};
}

std::size_t opendata::data_format_dimension(names name) {
	switch (name) {
	case names::SIZE:
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
