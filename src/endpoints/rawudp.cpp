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
*	\file		rawudp.cpp
*	\brief
*	\author		Giovanni Cerretani, Alberto Potenza
*
******************************************************************************/

#include "endpoints/rawudp.hpp"

#include <array>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <boost/asio.hpp>
#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <spdlog/fmt/ostr.h>

#include <server_definitions.hpp>

#include "cpp-utility/bit.hpp"
#include "cpp-utility/circular_buffer.hpp"
#include "cpp-utility/counting_range.hpp"
#include "cpp-utility/cpu.hpp"
#include "cpp-utility/hash.hpp"
#include "cpp-utility/integer.hpp"
#include "cpp-utility/is_in.hpp"
#include "cpp-utility/scope_exit.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/span.hpp"
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

template <> struct fmt::formatter<std::filesystem::path> : ostream_formatter {};

using namespace std::literals;

namespace caen {

namespace dig2 {

namespace ep {

namespace {

std::optional<handle::internal_handle_t> find_stats_endpoint_handle(client& client, handle::internal_handle_t endpoint_handle) {
	for (const auto child_handle : client.get_child_handles(endpoint_handle, std::string{})) {
		const auto properties = client.get_node_properties(child_handle, std::string{});
		if (properties.first == "stats")
			return child_handle;
	}

	return std::nullopt;
}

struct rawudp_footer_data {
	struct s {
		// 1st word
		static inline constexpr std::size_t buffer_id{16};
		static inline constexpr std::size_t tbd_1{1};
		static inline constexpr std::size_t hash{32};
		static inline constexpr std::size_t datagram_id{24}; // not part of the datagram!
		static inline constexpr std::size_t aligned{1};
		static inline constexpr std::size_t n_words{13};
		static inline constexpr std::size_t last{1};
	};
	using word_t = std::uint64_t;
	using buffer_id_t = caen::uint_t<s::buffer_id>::fast;
	using datagram_id_t = caen::uint_t<s::datagram_id>::fast;
	buffer_id_t _buffer_id{};
	// - tbd_1 not saved into struct
	caen::uint_t<s::hash>::fast _hash{};
	datagram_id_t _datagram_id{};
	caen::uint_t<s::aligned>::fast _aligned{};
	caen::uint_t<s::n_words>::fast _n_words{};
	caen::uint_t<s::last>::fast _last{};
};

template <typename Iterator>
rawudp_footer_data parse_rawudp_footer(Iterator& it) {
	rawudp_footer_data footer;
	auto word = caen::serdes::deserialize<rawudp_footer_data::word_t>(it);
	caen::bit::mask_and_right_shift<rawudp_footer_data::s::last>(word, footer._last);
	caen::bit::mask_and_right_shift<rawudp_footer_data::s::n_words>(word, footer._n_words);
	caen::bit::mask_and_right_shift<rawudp_footer_data::s::aligned>(word, footer._aligned);
	caen::bit::mask_and_right_shift<rawudp_footer_data::s::hash>(word, footer._hash);
	caen::bit::right_shift<rawudp_footer_data::s::tbd_1>(word);
	caen::bit::mask_and_right_shift<rawudp_footer_data::s::buffer_id>(word, footer._buffer_id);
	BOOST_ASSERT_MSG(!word, "inconsistent word decoding");
	return footer;
}

enum class rawudp_flush_mode {
	none,
	keep_tail,
	drop_tail,
};

struct rawudp_reassembly_state {

	struct continuity {
		rawudp_footer_data::datagram_id_t _expected_datagram_id{};
		bool _discontinuity{};
		bool _previous_incomplete{};
		std::uint64_t _estimated_lost_buffers{};
	};

	void reset_assembly() noexcept {
		_last_aligned_size = 0;
		_current_assembling_buffer_id = {};
	}

	void reset() noexcept {
		_last_valid_footer = std::nullopt;
		reset_assembly();
	}

	continuity check_continuity(const rawudp_footer_data& footer) const noexcept {
		if (!_last_valid_footer.has_value())
			return {};

		const auto& lvf = *_last_valid_footer;
		const auto expected_buffer_id = static_cast<bool>(lvf._last) ? next_buffer_id(lvf._buffer_id) : lvf._buffer_id;

		continuity ret;
		if (footer._buffer_id == expected_buffer_id) {
			ret._expected_datagram_id = static_cast<bool>(lvf._last) ? 0 : next_datagram_id(lvf._datagram_id);
			return ret;
		}

		ret._discontinuity = true;
		ret._expected_datagram_id = 0;
		if (static_cast<bool>(lvf._last)) {
			ret._estimated_lost_buffers = buffer_distance(expected_buffer_id, footer._buffer_id);
		} else {
			ret._previous_incomplete = true;
			const auto distance = buffer_distance(lvf._buffer_id, footer._buffer_id);
			ret._estimated_lost_buffers = distance == 0 ? 0 : distance - 1;
		}
		return ret;
	}

	void accept(rawudp_footer_data footer, rawudp_footer_data::datagram_id_t datagram_id) noexcept {
		footer._datagram_id = datagram_id;
		_last_valid_footer = footer;
	}

	bool has_last_valid_footer() const noexcept {
		return _last_valid_footer.has_value();
	}

	void update_aligned_checkpoint(const rawudp_footer_data& footer, std::size_t size) noexcept {
		if (static_cast<bool>(footer._aligned))
			_last_aligned_size = size;
	}

	bool has_aligned_checkpoint() const noexcept {
		return _last_aligned_size > 0;
	}

	static rawudp_footer_data::buffer_id_t next_buffer_id(rawudp_footer_data::buffer_id_t v) noexcept {
		return caen::bit::mask_at<rawudp_footer_data::s::buffer_id>(v + 1U);
	}

	static rawudp_footer_data::datagram_id_t next_datagram_id(rawudp_footer_data::datagram_id_t v) noexcept {
		return caen::bit::mask_at<rawudp_footer_data::s::datagram_id>(v + 1U);
	}

	static std::uint64_t buffer_distance(rawudp_footer_data::buffer_id_t from, rawudp_footer_data::buffer_id_t to) noexcept {
		return caen::bit::mask_at<rawudp_footer_data::s::buffer_id>(to - from);
	}

	std::optional<rawudp_footer_data> _last_valid_footer;
	std::size_t _last_aligned_size{};
	rawudp_footer_data::buffer_id_t _current_assembling_buffer_id{};

};

} // unnamed namespace

struct rawudp::stats::endpoint_impl {

	endpoint_impl()
		: _logger{library_logger::create_logger("rawudp_stats_ep"s)}
		, _args_list{rawudp::stats::default_data_format()} {
	}

	void set_data_format(const std::string& json_format) {
		data_format_utils<rawudp::stats>::parse_data_format(_args_list, json_format);
	}

	using counters = rawudp::stats::counters;

	counters get_snapshot() const {
		std::lock_guard l{_mtx};
		return _counters;
	}

	void add_counters(const counters& counters) {
		std::lock_guard l{_mtx};
		_counters._received_datagrams += counters._received_datagrams;
		_counters._discarded_datagrams += counters._discarded_datagrams;
		_counters._received_bytes += counters._received_bytes;
		_counters._emitted_bytes += counters._emitted_bytes;
		_counters._completed_buffers += counters._completed_buffers;
		_counters._flushed_buffers += counters._flushed_buffers;
		_counters._incomplete_buffers += counters._incomplete_buffers;
		_counters._estimated_lost_buffers += counters._estimated_lost_buffers;
		_counters._recovered_bytes += counters._recovered_bytes;
		_counters._discarded_bytes += counters._discarded_bytes;
	}

	void clear_data() {
		std::lock_guard l{_mtx};
		_counters = {};
	}

	std::shared_ptr<spdlog::logger> _logger;
	counters _counters;
	args_list_t _args_list;
	mutable std::mutex _mtx;

};

struct rawudp::endpoint_impl {

	enum class state {
		init,
		idle,
		clearing_receiver,
		decoder_started,
		quitting_decoder,
		ready,
	};

	endpoint_impl(client& client, handle::internal_handle_t endpoint_handle, std::shared_ptr<rawudp::stats> stats_ep)
		: _logger{library_logger::create_logger(fmt::format("rawudp {}", endpoint_handle))}
		, _max_size_getter{}
		, _is_decoded_getter{}
		, _stats_ep{std::move(stats_ep)}
		, _io_context{}
		, _endpoint(client.get_endpoint_address(), server_definitions::udp_port)
		, _socket(_io_context, _endpoint.protocol())
		, _receiver{}
		, _decoder{}
		, _receiver_thread_affinity{client.get_url_data()._receiver_thread_affinity}
		, _state{state::init}
		, _clear_buffer{false}
		, _send_stop{false}
		, _reassembly{}
		, _datagram_buffer(max_datagram_size)
		, _hash_buffer(max_hash_size)
		, _buffer()
		, _args_list{rawudp::default_data_format()} {

		SPDLOG_LOGGER_TRACE(_logger, "{}(endpoint_handle={})", __func__, endpoint_handle);

		if (_endpoint.address().is_v6())
			throw "rawudp endpoint does not support IPv6"_ex;

		// handle specific options
		if (auto&& rcvbuf = client.get_url_data()._rcvbuf; rcvbuf.has_value()) {
			decltype(_socket)::receive_buffer_size option;
			_socket.get_option(option);
			const auto default_value = option.value();
			const auto new_value = *rcvbuf;
			_logger->info("overwriting socket default receive_buffer_size (default_value={}, new_value={})", default_value, new_value);
			option = new_value;
			_socket.set_option(option);
		}

		if (auto&& dump_path = client.get_url_data()._dump_path; dump_path.has_value()) {
			// open file for writing
			const auto absolute_path = std::filesystem::absolute(*dump_path);
			_dump_file.open(absolute_path, std::ios::out | std::ios::binary);
			if (!_dump_file.is_open())
				throw ex::runtime_error(fmt::format("cannot open dump file: {}", absolute_path));
			_logger->info("dump file opened: {}", absolute_path);
		}

		// connect
		connect();

		// start receiver
		start_receiver();

		// wait for digitizer to handle connect initialization
		do {
			// send empty packet to expose local port
			const std::array<std::byte, 0> arr{};
			try {
				// errors ignored: see comment in async_receive handler for details
				_socket.send(boost::asio::buffer(arr));
			} catch (const boost::system::system_error& e) {
				if (e.code() == boost::asio::error::connection_refused)
					_logger->warn("ignoring connection_refused error: {}", e.code().message());
				else
					throw;
			}
			std::this_thread::sleep_for(100ms);
		} while (client.get_value(0, "/par/registermisc", "0x8014") == "0"s);

		// clear data to handle first fake event sent at connect
		clear_data();

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
			wt::construct_at(&_stats_ep);
			wt::construct_at(&_mtx_state);
			wt::construct_at(&_cv_state);
			wt::construct_at(&_sw_ep_list);
			wt::construct_at(&_reassembly);
			wt::construct_at(&_datagram_buffer);
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
		data_format_utils<rawudp>::parse_data_format(_args_list, json_format);
	}

	void read_data(timeout_t timeout, std::va_list* args) {

		if (BOOST_UNLIKELY(_decoder.joinable()))
			throw ex::not_enabled();

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
			case names::BUFFER_ID:
				utility::put_argument(args, type, br->_buffer_id);
				break;
			case names::FLUSH:
				utility::put_argument(args, type, br->_flush);
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

		// 6. reset statistics; UDP continuity is synchronized by the empty datagram handled by the receiver thread
		if (_stats_ep)
			_stats_ep->clear_data();

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

		// connect in UDP is only meant to use send/recv without specifying remote endpoint
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
			std::lock_guard lk{_mtx_state};
			_state = s;
		}
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

		// 1. resize current buffers
		_buffer.apply_all([max_size = _max_size_getter()](auto& b) {

			// reserve here to avoid allocations during run
			caen::reserve(b._data, max_size);

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
		});

		// 4. wait decoder thread to exit
		SPDLOG_LOGGER_DEBUG(_logger, "joining decoder thread");
		_decoder.join();

	}

	// receiver thread
	void receiver_main() try {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		// handle specific options
		if (_receiver_thread_affinity.has_value()) {
			const auto value = *_receiver_thread_affinity;
			_logger->info("setting receiver thread affinity to {}", value);
			caen::cpu::set_current_thread_affinity(value);
		}

		// work guard prevents run() to exit if there is no pending job
		const auto work_guard = boost::asio::make_work_guard(_io_context);

		enqueue_read();
		_io_context.run();

		SPDLOG_LOGGER_DEBUG(_logger, "quitting receiver thread");

	}
	catch (const std::exception& ex) {
		/*
		 * Do not crash: close the thread gracefully and surface the error to the user on the
		 * next read_data/has_data. Recovery from a receiver failure requires reconnecting to
		 * the device.
		 */
		_logger->error("receiver thread stopped due to an error: {}", ex.what());
		const auto msg = fmt::format("receiver thread stopped due to an error: {}. Reconnect to the device to recover.", ex.what());
		const auto eptr = std::make_exception_ptr(ex::runtime_error(msg));
		/*
		 * Order matters: claim the software endpoint buffers (decoded-mode readers) before
		 * _buffer, since set_error on _buffer is what wakes the decoder (set_error is first-wins).
		 */
		for (auto& ep : _sw_ep_list)
			ep->notify_error(eptr);
		_buffer.set_error(eptr);
	}

	void decode_hash_buffer(const caen::span<std::byte>& data) {
		BOOST_ASSERT_MSG(data.size() % sw_endpoint::word_size == 0, "invalid data size");
		const auto n_words = data.size() / sw_endpoint::word_size;
		caen::resize(_hash_buffer, 1 + n_words); // one slot for datagram id
		auto it = _hash_buffer.begin() + 1;
		for (auto p = data.cbegin(); p != data.cend();) {
			std::advance(p, sw_endpoint::half_word_size);
			caen::serdes::deserialize(p, *it++);
		}
		BOOST_ASSERT_MSG(it == _hash_buffer.end(), "inconsistent buffer decode for hash calculation");
	}

	/*
	 * This custom hash serves two purposes:
	 * - Mitigating issues described in RFC 4963.
	 * - Verifying the datagram ID (included in the hash).
	 *
	 * If the check fails, it is likely due to a lost datagram (more probable) or an incorrectly assembled datagram (rare).
	 * Since the datagram_id is not sent as a plain value but only as part of the hash, it is difficult to determine the
	 * datagram_id of the received datagram (i.e., to know how many datagrams have been lost).
	 * In any case, we must discard the packet as there is no way to recover the missing information. We reduce protocol
	 * overhead by combining the datagram_id and the hash into a single 32-bit value.
	 *
	 * We could guess the datagram_id by performing a brute-force check: this is done to check for a reset by calling this
	 * function with 0 as the first argument (see do_read).
	 */
	bool check_datagram_id(std::uint32_t expected_datagram_id, std::uint32_t expected_hash) {
		_hash_buffer.front() = expected_datagram_id;
		const auto hash = caen::hash::djb2a{}(_hash_buffer);
		return hash == expected_hash;
	}

	void do_read(std::size_t bytes_transferred) {

		SPDLOG_LOGGER_TRACE(_logger, "{}(bytes_transferred={})", __func__, bytes_transferred);

		SPDLOG_LOGGER_DEBUG(_logger, "data received (size={})", bytes_transferred);

		if (BOOST_UNLIKELY(bytes_transferred == 0)) {
			SPDLOG_LOGGER_DEBUG(_logger, "ignoring empty UDP datagram");
			return;
		}

		auto stats_delta = rawudp::stats::counters{};
		caen::scope_exit publish_stats([this, &stats_delta] {
			if (_stats_ep)
				_stats_ep->add_counters(stats_delta);
		});

		// datagram cannot be larger than 65507 bytes and must contain at least the footer
		BOOST_ASSERT_MSG(bytes_transferred <= _datagram_buffer.size(), "invalid bytes_transferred");
		if (BOOST_UNLIKELY(bytes_transferred < datagram_footer_size)) {
			++stats_delta._received_datagrams;
			++stats_delta._discarded_datagrams;
			stats_delta._received_bytes += bytes_transferred;
			_logger->warn("discarding UDP datagram shorter than footer (size={})", bytes_transferred);
			return;
		}

		++stats_delta._received_datagrams;
		stats_delta._received_bytes += bytes_transferred;

		const auto datagram_buffer = caen::span<std::byte>(_datagram_buffer.data(), bytes_transferred);
		auto footer_buffer_it = datagram_buffer.cend() - datagram_footer_size;
		const auto footer = parse_rawudp_footer(footer_buffer_it);

		BOOST_ASSERT_MSG(footer_buffer_it == datagram_buffer.cend(), "inconsistent footer decoding");

		SPDLOG_LOGGER_DEBUG(_logger, "datagram received (buffer_id={}, hash={:08x}, n_words={}, aligned={}, last={})", footer._buffer_id, footer._hash, footer._n_words, footer._aligned, footer._last);

		const auto data_size = footer._n_words * sw_endpoint::word_size;
		if (BOOST_UNLIKELY(data_size > datagram_buffer.size() - datagram_footer_size))
			throw ex::runtime_error(fmt::format("inconsistent data size (data_size={}, bytes_transferred={})", data_size, bytes_transferred));

		const auto datagram_data_buffer = datagram_buffer.subspan(0, data_size);
		decode_hash_buffer(datagram_data_buffer);

		auto continuity = _reassembly.check_continuity(footer);
		auto expected_datagram_id = continuity._expected_datagram_id;
		auto reset_reassembly = false;
		auto discard_pending_on_clear = false;
		auto incomplete_buffer_counted = false;

		if (!_reassembly.has_last_valid_footer())
			_clear_buffer = true;

		// check for datagram_id continuity
		SPDLOG_LOGGER_DEBUG(_logger, "expected_datagram_id={}", expected_datagram_id);
		// handle possible datagram loss or clear
		if (!check_datagram_id(expected_datagram_id, footer._hash)) {
			if (footer._buffer_id == 0 && check_datagram_id(0, footer._hash)) {
				// there have been a clear
				expected_datagram_id = 0;
				reset_reassembly = true;
				SPDLOG_LOGGER_DEBUG(_logger, "counters reset, probably due to a clear");
			} else {
				// datagram of the current buffer lost
				SPDLOG_LOGGER_DEBUG(_logger, "some datagrams of current buffer have been lost, or bad hash (buffer_id={}, expected_datagram_id={})", footer._buffer_id, expected_datagram_id);
				if (BOOST_UNLIKELY(data_size == 0)) {
					// this strange case seems to happen expecially on clearing_receiver state
					SPDLOG_LOGGER_DEBUG(_logger, "keeping current empty datagram that could be used to handle clearing_receiver state");
					expected_datagram_id = 0; // force to 0, unclear if it is what we need, but since we are clearing it should be the same
				} else {
					SPDLOG_LOGGER_DEBUG(_logger, "discarding current datagram");
					++stats_delta._discarded_datagrams;
					stats_delta._discarded_bytes += data_size;
					return;
				}
			}
		}

		if (reset_reassembly) {
			_reassembly.reset();
			_clear_buffer = true;
			continuity = {};
		}

		auto flush_mode = rawudp_flush_mode::none;
		if (continuity._discontinuity) {
			SPDLOG_LOGGER_DEBUG(_logger, "buffer_id discontinuity detected (buffer_id={}, estimated_lost_buffers={})", footer._buffer_id, continuity._estimated_lost_buffers);
			_clear_buffer = true;
			discard_pending_on_clear = true;
			if (continuity._previous_incomplete) {
				++stats_delta._incomplete_buffers;
				incomplete_buffer_counted = true;
			}
			if (continuity._estimated_lost_buffers != 0)
				stats_delta._estimated_lost_buffers += continuity._estimated_lost_buffers;
			/*
			 * A buffer_id jump means we cannot safely connect the current tail to the next
			 * buffer. If we already have an aligned checkpoint, emit only that prefix and
			 * discard the remaining tail.
			 */
			if (_reassembly.has_aligned_checkpoint())
				flush_mode = rawudp_flush_mode::drop_tail;
		}

		if (data_size == 0 && !_clear_buffer && _reassembly.has_aligned_checkpoint()) {
			/*
			 * Empty datagrams are valid keep-alives. With datagram_id continuity there is
			 * no data loss, so flush the aligned prefix without forcing a buffer reset.
			 * Any tail after the last aligned checkpoint is kept locally and prepended to
			 * the next non-empty datagram of the same buffer.
			 */
			flush_mode = rawudp_flush_mode::keep_tail;
			SPDLOG_LOGGER_DEBUG(_logger, "flushing aligned prefix on empty keep-alive datagram (aligned_size={})", _reassembly._last_aligned_size);
		}

		const auto flush = flush_mode != rawudp_flush_mode::none;

		// this datagram is going to be used
		_reassembly.accept(footer, expected_datagram_id);

		{
			std::unique_lock lk{_mtx_state};

			// data_size == 0 is a special firmware packed injected by the UDP block a second after the last data sent
			if (data_size == 0) {
				if (caen::is_in(_state, endpoint_impl::state::clearing_receiver)) {
					SPDLOG_LOGGER_DEBUG(_logger, "empty data while in clearing_receiver");
					_cv_state.wait(lk, [this] { return caen::is_in(_state, endpoint_impl::state::clearing_receiver); });
					_clear_buffer = true;
					// Keep the verified empty datagram as the UDP continuity anchor. A clear
					// does not imply that the remote counters have reset: that is detected
					// exclusively by the dedicated datagram_id=0 hash check above.
					_reassembly.reset_assembly();
					SPDLOG_LOGGER_DEBUG(_logger, "set idle state");
					_state = endpoint_impl::state::idle;
					lk.unlock();
					_cv_state.notify_all();
					return;
				}
				// proceed if we need to finalize current buffer
				if (!flush)
					return;
			}

			SPDLOG_LOGGER_DEBUG(_logger, "waiting for state: ready or clearing_receiver");
			_cv_state.wait(lk, [this] { return caen::is_in(_state, endpoint_impl::state::ready, endpoint_impl::state::clearing_receiver); });
		}

		if (flush) {

			auto bw = _buffer.get_buffer_write();
			caen::scope_exit se_abort([this] { _buffer.abort_writing(); });

			auto& data = bw->_data;

			BOOST_ASSERT_MSG(_reassembly._last_aligned_size > 0, "flush requires aligned checkpoint");
			BOOST_ASSERT_MSG(_reassembly._last_aligned_size <= data.size(), "aligned checkpoint beyond current buffer size");

			const auto aligned_size = _reassembly._last_aligned_size;
			const auto discarded_tail_size = data.size() - aligned_size;

			caen::vector<std::byte> tail_data;
			// For keep-alive flushes, preserve the bytes after the last aligned checkpoint.
			if (flush_mode == rawudp_flush_mode::keep_tail && discarded_tail_size != 0) {
				caen::resize(tail_data, discarded_tail_size);
				boost::copy(caen::span<std::byte>(data.data() + aligned_size, discarded_tail_size), tail_data.begin());
			}

			caen::resize(data, aligned_size);

			if (BOOST_UNLIKELY(check_state(endpoint_impl::state::clearing_receiver))) {
				SPDLOG_LOGGER_DEBUG(_logger, "discarding data received in clearing_receiver state");
				_clear_buffer = true;
				_reassembly.reset();
				return;
			}

			BOOST_ASSERT_MSG(!data.empty(), "unexpected empty real buffer, reserved for fake writes");

			SPDLOG_LOGGER_DEBUG(_logger, "buffer flushed (size={})", data.size());

			stats_delta._emitted_bytes += data.size();
			++stats_delta._flushed_buffers;
			if (flush_mode == rawudp_flush_mode::drop_tail) {
				// drop_tail is a conservative recovery path: the flushed prefix is recovered data.
				stats_delta._recovered_bytes += data.size();
				if (!incomplete_buffer_counted)
					++stats_delta._incomplete_buffers;
				if (discarded_tail_size != 0)
					stats_delta._discarded_bytes += discarded_tail_size;
			}

			bw->_buffer_id = static_cast<std::uint16_t>(_reassembly._current_assembling_buffer_id);
			bw->_flush = true;

			se_abort.release();
			_buffer.end_writing();

			SPDLOG_LOGGER_DEBUG(_logger, "do_read completed");

			_reassembly._last_aligned_size = 0;
			discard_pending_on_clear = false;

			/*
			 * If we kept a tail, write it back immediately so the next non-empty datagram
			 * can continue assembling the same buffer without an extra copy later on.
			 */
			if (!tail_data.empty()) {
				auto carry_bw = _buffer.get_buffer_write();
				caen::scope_exit se_carry([this] { _buffer.abort_writing(); });
				auto& carry_data = carry_bw->_data;

				/*
				 * Start a new in-memory assembly span from the retained tail. _clear_buffer
				 * must stay false here, otherwise the next write would wipe what we just kept.
				 */
				caen::clear(carry_data);
				_reassembly._last_aligned_size = 0;
				_reassembly._current_assembling_buffer_id = footer._buffer_id;
				_clear_buffer = false;

				caen::safe_increase_size(carry_data, tail_data.size());
				boost::copy(tail_data, carry_data.begin());

				SPDLOG_LOGGER_DEBUG(_logger, "retained tail after keep-alive flush (size={})", carry_data.size());
			} else {
				// No tail to carry: the next write can start from a clean buffer.
				_clear_buffer = true;
			}

		}

		auto bw = _buffer.get_buffer_write();
		caen::scope_exit se_abort([this] { _buffer.abort_writing(); });

		auto& write_data = bw->_data;

		if (std::exchange(_clear_buffer, false)) {
			if (discard_pending_on_clear && !write_data.empty())
				stats_delta._discarded_bytes += write_data.size();
			caen::clear(write_data);
			_reassembly._last_aligned_size = 0;
			_reassembly._current_assembling_buffer_id = footer._buffer_id;
		}

		if (data_size != 0) {

			const auto offset = write_data.size();

			// resize (no allocation, unless user changed max data size related parameters after disarm with data still to be read)
			caen::safe_increase_size(write_data, datagram_data_buffer.size());

			// read data from datagram
			boost::copy(datagram_data_buffer, write_data.begin() + offset);

			SPDLOG_LOGGER_DEBUG(_logger, "data copied (size={})", datagram_data_buffer.size());

			// track last aligned boundary for partial flush recovery
			_reassembly.update_aligned_checkpoint(footer, write_data.size());
		}

		if (static_cast<bool>(footer._last)) {
			if (BOOST_UNLIKELY(check_state(endpoint_impl::state::clearing_receiver))) {
				SPDLOG_LOGGER_DEBUG(_logger, "discarding data received in clearing_receiver state");
				_clear_buffer = true;
				_reassembly.reset();
				return;
			}

			BOOST_ASSERT_MSG(!write_data.empty(), "unexpected empty real buffer, reserved for fake writes");

			SPDLOG_LOGGER_DEBUG(_logger, "buffer completed (size={}, flush={})", write_data.size(), false);

			stats_delta._emitted_bytes += write_data.size();
			++stats_delta._completed_buffers;

			bw->_buffer_id = static_cast<std::uint16_t>(_reassembly._current_assembling_buffer_id);
			bw->_flush = false;

			se_abort.release();
			_buffer.end_writing();

			SPDLOG_LOGGER_DEBUG(_logger, "do_read completed");

			_clear_buffer = true;
			_reassembly._last_aligned_size = 0;

		} else {

			SPDLOG_LOGGER_DEBUG(_logger, "buffer not completed (size={})", bw->_data.size());

		}

	}

	void enqueue_read() {

		SPDLOG_LOGGER_TRACE(_logger, "{}()", __func__);

		_socket.async_receive(boost::asio::buffer(_datagram_buffer), [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
			/*
			 * Ignore connection_refused error, that might happen if the last send of empty packet
			 * to expose local port is done after the remote endpoint has been closed. In this case,
			 * the remote endpoint sends an ICMP port unreachable message that causes the next operation
			 * to fail with connection_refused error, that we can safely ignore.
			 */
			if (ec) {
				_logger->error("async_read failed: {} (bytes_transferred={})", ec.message(), bytes_transferred);
				if (ec == boost::asio::error::connection_refused) {
					_logger->warn("ignoring connection_refused errors");
				} else {
					disconnect();
					return;
				}
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
		/*
		 * Do not crash: close the thread gracefully and surface the error to the user on the
		 * next read_data/has_data. The decoder is recreated on arm_acquisition, so re-arming
		 * recovers.
		 */
		_logger->error("decoder thread stopped due to an error: {}", ex.what());
		const auto msg = fmt::format("decoder thread stopped due to an error: {}. Re-arm the acquisition to recover.", ex.what());
		const auto eptr = std::make_exception_ptr(ex::runtime_error(msg));
		for (auto& ep : _sw_ep_list)
			ep->notify_error(eptr);
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

			if (data.empty()) {
				SPDLOG_LOGGER_DEBUG(_logger, "decoder: discarding empty buffer");
				se.release();
				_buffer.end_reading();
				continue;
			}

			SPDLOG_LOGGER_DEBUG(_logger, "decoder: buffer received (size={})", data.size());

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

			if (BOOST_UNLIKELY(evt_size > size_left)) {
				dump_data(data.data(), data.size());
				throw ex::runtime_error(fmt::format("inconsistent event size (evt_size={}, size_left={})", evt_size, size_left));
			}

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

				SPDLOG_LOGGER_DEBUG(_logger, "decoder: buffer completed (decoded_size={}, decoded_n_events={})", decoded_size, decoded_n_events);

				se.release();
				_buffer.end_reading();

				decoded_size = 0;
				decoded_n_events = 0;

			} else {

				const auto remaining_decoded_data = data.size() - decoded_size;

				SPDLOG_LOGGER_DEBUG(_logger, "decoder: buffer not completed (remaining_decoded_data={})", remaining_decoded_data);

				boost::ignore_unused(remaining_decoded_data);

			}
		}
	}

	void dump_data(const std::byte* data, std::size_t size) {
		if (!_dump_file.is_open())
			return;
		const auto data_written = _dump_file.sputn(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
		_dump_file.pubsync();
		_logger->info("dumped {} bytes to file", data_written);
	}

	// members

	struct raw_data {
		caen::vector<std::byte> _data;
		std::uint16_t _buffer_id;
		bool _flush;
	};

	std::shared_ptr<spdlog::logger> _logger;

	std::function<std::size_t()> _max_size_getter;
	std::function<bool()> _is_decoded_getter;
	std::shared_ptr<rawudp::stats> _stats_ep;

	boost::asio::io_context _io_context;
	const boost::asio::ip::udp::endpoint _endpoint;
	boost::asio::ip::udp::socket _socket;

	std::thread _receiver;
	std::thread _decoder;

	std::optional<int> _receiver_thread_affinity;

	state _state;
	mutable std::mutex _mtx_state;
	mutable std::condition_variable _cv_state;

	bool _clear_buffer;
	bool _send_stop;

	rawudp_reassembly_state _reassembly;

	std::filebuf _dump_file;

	std::list<std::shared_ptr<sw_endpoint>> _sw_ep_list;

	static inline constexpr std::size_t datagram_footer_size{8};
	static inline constexpr std::size_t max_datagram_size{65507}; // even if we should limit to 65504, aligned to a 64-bit word
	caen::vector<std::byte> _datagram_buffer;

	static inline constexpr std::size_t max_hash_size{max_datagram_size / sw_endpoint::word_size};
	caen::vector<sw_endpoint::half_word_t> _hash_buffer;

	static inline constexpr std::size_t circular_buffer_size{4};

	caen::circular_buffer<raw_data, circular_buffer_size> _buffer;
	args_list_t _args_list;
};

rawudp::stats::stats(client& client, handle::internal_handle_t endpoint_handle)
	: endpoint(client, endpoint_handle)
	, _pimpl{std::make_unique<endpoint_impl>()} {
}

rawudp::stats::~stats() = default;

rawudp::stats::args_list_t rawudp::stats::default_data_format() {
	using vt = data_format_utils<rawudp::stats>::args_type;
	return {{
			vt{names::RECEIVED_DATAGRAMS,			types::U64,	0	},
			vt{names::DISCARDED_DATAGRAMS,			types::U64,	0	},
			vt{names::RECEIVED_BYTES,				types::U64,	0	},
			vt{names::EMITTED_BYTES,				types::U64,	0	},
			vt{names::COMPLETED_BUFFERS,			types::U64,	0	},
			vt{names::FLUSHED_BUFFERS,				types::U64,	0	},
			vt{names::INCOMPLETE_BUFFERS,			types::U64,	0	},
			vt{names::ESTIMATED_LOST_BUFFERS,		types::U64,	0	},
			vt{names::RECOVERED_BYTES,				types::U64,	0	},
			vt{names::DISCARDED_BYTES,				types::U64,	0	},
	}};
}

std::size_t rawudp::stats::data_format_dimension(names name) {
	switch (name) {
	case names::RECEIVED_DATAGRAMS:
	case names::DISCARDED_DATAGRAMS:
	case names::RECEIVED_BYTES:
	case names::EMITTED_BYTES:
	case names::COMPLETED_BUFFERS:
	case names::FLUSHED_BUFFERS:
	case names::INCOMPLETE_BUFFERS:
	case names::ESTIMATED_LOST_BUFFERS:
	case names::RECOVERED_BYTES:
	case names::DISCARDED_BYTES:
		return 0;
	default:
		throw "unsupported name"_ex;
	}
}

void rawudp::stats::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void rawudp::stats::read_data(timeout_t timeout, std::va_list* args) {
	boost::ignore_unused(timeout);
	const auto data = _pimpl->get_snapshot();
	for (const auto& arg : _pimpl->_args_list) {
		const auto name = std::get<0>(arg);
		const auto type = std::get<1>(arg);
		switch (name) {
		case names::RECEIVED_DATAGRAMS:
			utility::put_argument(args, type, data._received_datagrams);
			break;
		case names::DISCARDED_DATAGRAMS:
			utility::put_argument(args, type, data._discarded_datagrams);
			break;
		case names::RECEIVED_BYTES:
			utility::put_argument(args, type, data._received_bytes);
			break;
		case names::EMITTED_BYTES:
			utility::put_argument(args, type, data._emitted_bytes);
			break;
		case names::COMPLETED_BUFFERS:
			utility::put_argument(args, type, data._completed_buffers);
			break;
		case names::FLUSHED_BUFFERS:
			utility::put_argument(args, type, data._flushed_buffers);
			break;
		case names::INCOMPLETE_BUFFERS:
			utility::put_argument(args, type, data._incomplete_buffers);
			break;
		case names::ESTIMATED_LOST_BUFFERS:
			utility::put_argument(args, type, data._estimated_lost_buffers);
			break;
		case names::RECOVERED_BYTES:
			utility::put_argument(args, type, data._recovered_bytes);
			break;
		case names::DISCARDED_BYTES:
			utility::put_argument(args, type, data._discarded_bytes);
			break;
		default:
			throw "unsupported data type"_ex;
		}
	}
}

void rawudp::stats::has_data(timeout_t timeout) {
	boost::ignore_unused(timeout);
}

void rawudp::stats::clear_data() {
	_pimpl->clear_data();
}

void rawudp::stats::add_counters(const counters& counters) {
	_pimpl->add_counters(counters);
}

rawudp::rawudp(client& client, handle::internal_handle_t endpoint_handle) try
	: hw_endpoint(client, endpoint_handle)
	, _stats_ep{}
	, _pimpl{} {
	if (const auto stats_handle = find_stats_endpoint_handle(client, endpoint_handle); stats_handle.has_value()) {
		_stats_ep = std::make_shared<stats>(client, *stats_handle);
		get_client().register_endpoint(_stats_ep);
	} else {
		spdlog::warn("rawudp stats endpoint is unavailable; UDP statistics are disabled");
	}

	_pimpl = std::make_unique<endpoint_impl>(get_client(), get_endpoint_server_handle(), _stats_ep);
}
catch (const std::exception& ex) {
	spdlog::error("{} failed: {}", __func__, ex.what());
}

rawudp::~rawudp() = default;

void rawudp::register_sw_endpoint(std::shared_ptr<sw_endpoint> ep) {
	_pimpl->register_sw_endpoint(std::move(ep));
}

void rawudp::set_max_size_getter(std::function<std::size_t()> f) {
	_pimpl->set_max_size_getter(std::move(f));
}

void rawudp::set_is_decoded_getter(std::function<bool()> f) {
	_pimpl->set_is_decoded_getter(std::move(f));
}

void rawudp::set_data_format(const std::string& json_format) {
	_pimpl->set_data_format(json_format);
}

void rawudp::read_data(timeout_t timeout, std::va_list* args) {
	_pimpl->read_data(std::move(timeout), args);
}

void rawudp::has_data(timeout_t timeout) {
	_pimpl->has_data(std::move(timeout));
}

void rawudp::clear_data() {
	_pimpl->clear_data();
}

void rawudp::arm_acquisition() {
	_pimpl->arm_acquisition();
}

void rawudp::disarm_acquisition() {
	_pimpl->disarm_acquisition();
}

void rawudp::event_start() {
	_pimpl->event_start();
}

void rawudp::event_stop() {
	_pimpl->event_stop();
}

rawudp::args_list_t rawudp::default_data_format() {
	using vt = data_format_utils<rawudp>::args_type;
	return {{
			vt{names::DATA,	types::U8,		1	},
			vt{names::SIZE,	types::SIZE_T,	0	},
	}};
}

std::size_t rawudp::data_format_dimension(names name) {
	switch (name) {
	case names::SIZE:
	case names::BUFFER_ID:
	case names::FLUSH:
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
