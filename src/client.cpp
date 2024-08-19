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
*	\file		client.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "client.hpp"

#include <chrono>
#include <forward_list>
#include <list>
#include <mutex>
#include <regex>
#include <type_traits>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/predef/os.h>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/version.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/fmt/fmt.h>

#if BOOST_OS_LINUX && BOOST_VERSION < 107000
// network_v6 missing asio.hpp until Boost 1.70
#include <boost/asio/ip/network_v6.hpp>
#endif

#include <server_definitions.hpp>
#include <json/json_answer.hpp>
#include <json/json_cmd.hpp>
#include <json/json_node_type.hpp> // required by string_to_node_type to properly convert string to enum

#include "cpp-utility/hash.hpp"
#include "cpp-utility/lexical_cast.hpp"
#include "cpp-utility/serdes.hpp"
#include "cpp-utility/socket_option.hpp"
#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "endpoints/dpppha.hpp"
#include "endpoints/dpppsd.hpp"
#include "endpoints/dppzle.hpp"
#include "endpoints/events.hpp"
#include "endpoints/raw.hpp"
#include "endpoints/rawudp.hpp"
#include "endpoints/opendpp.hpp"
#include "endpoints/scope.hpp"

namespace caen {

namespace dig2 {

using namespace std::literals;
using namespace caen::literals;

namespace {

::CAEN_FELib_NodeType_t string_to_node_type(const std::string& type) {
	return nlohmann::json(type).get<::CAEN_FELib_NodeType_t>();
}

template <typename String>
std::string pid_to_ipv6(const String& pid_str) {

	std::uint32_t pid;
	if (!boost::conversion::try_lexical_convert(pid_str, pid))
		throw ex::invalid_argument(fmt::format("invalid PID: {}", pid_str));

	boost::asio::ip::address_v6::bytes_type ip_bytes{0xfd, 0xa7, 0xca, 0xe0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1};
	for (std::size_t i = 0; i < sizeof(pid); ++i)
		ip_bytes[7 - i] = static_cast<decltype(ip_bytes)::value_type>(pid >> i * CHAR_BIT);

	return boost::asio::ip::address_v6{ip_bytes}.to_string();

}

std::string url_to_address(const url_data& data) {

	// .internal TLD is reserved (https://www.rfc-editor.org/rfc/rfc6762#appendix-G)
	constexpr static auto authority_internal = "caen.internal"_sv;
	constexpr static auto authority_legacy_usb_prefix = "usb:"_sv;

	/*
	 * Routine to handle reserved URI
	 */
	if (caen::string::iequals(data._authority, authority_internal)) {

		constexpr static auto path_openarm = "/openarm"_sv;
		constexpr static auto path_usb_prefix = "/usb/"_sv;
		constexpr static auto path_usb_prefix_alt = "/usb"_sv;

		/*
		 * Use case: dig2://caen.internal/openarm
		 * This is meant to be used to simplify the connection to the server when
		 * running this library inside the Open ARM environment, where "localhost"
		 * cannot be used because it would point to the Docker container. The fake
		 * address is simply mapped to the Docker host address.
		 */
		if (caen::string::iequals(data._path, path_openarm)) {
			return boost::asio::ip::address_v4({172, 17, 0, 1}).to_string();
		}

		/*
		 * Use case: dig2://caen.internal/usb/PID
		 * Routine to convert PID to its IPv6 address.
		 */
		if (boost::istarts_with(data._path, path_usb_prefix)) {
			caen::string_view path_view{data._path};
			path_view.remove_prefix(path_usb_prefix.size());
			return pid_to_ipv6(path_view);
		}

		/*
		 * Use case: dig2://caen.internal/usb?pid=PID
		 * Routine to convert PID to its IPv6 address.
		 * Add for consistency with CAEN Dig1 library, as
		 * alternative to the previous case.
		 */
		if (caen::string::iequals(data._path, path_usb_prefix_alt)) {
			if (!data._pid)
				throw "usb path requires pid query"_ex;
			return pid_to_ipv6(*data._pid);
		}

	}

	/*
	 * Legacy:
	 *
	 * Routine to convert usb:PID to its IPv6 address.
	 */
	if (boost::istarts_with(data._authority, authority_legacy_usb_prefix)) {
		caen::string_view authority_view{data._authority};
		authority_view.remove_prefix(authority_legacy_usb_prefix.size());
		return pid_to_ipv6(authority_view);
	} 

	if (data._authority.size() > 2 && data._authority.front() == '[' && data._authority.back() == ']') {

		/*
		 * Support for RFC 2732 (IPv6 format on URI)
		 *
		 * Boost.ASIO functions accept IPv6 in the standard hexadecimal format,
		 * like "::1", on every operating systems. Even if undocumented, Windows
		 * accepts also addresses in RFC 2732, i.e. in brackets like "[::1]".
		 *
		 * This trick just removes brackets, if present, only if the string in
		 * brackets is a valid IPv6.
		 *
		 * For a more strict support, we should reject IPv6 addresses not in
		 * RFC 2732 format, but seems overkilling.
		 */

		const std::string ipv6_path(data._authority.begin() + 1, data._authority.end() - 1);

		boost::system::error_code ec;
		boost::asio::ip::make_address_v6(ipv6_path, ec);

		if (!ec)
			return ipv6_path;

	}

	return data._authority;

}

} // unnamed namespace

struct client::client_impl {

	client_impl(const url_data& data)
	: _url_data(data)
	, _monitor(data._monitor.value_or(default_monitor()))
	, _logger{library_logger::create_logger(_url_data._authority, _url_data._log_level)}
	, _io_context{}
	, _socket(_io_context)
	, _address{connect_to(_url_data)}
	, _endpoint_address{}
	, _digitizer_internal_handle{}
	, _server_version_aligned{false}
	, _endpoint_list{}
	, _user_register_path{"/par/registeruser"s}
	, _n_channels{} {

		// set keep alive interval to patch rare missing data from digitizer
		const auto keepalive = _url_data._keepalive.value_or(default_keepalive_interval());
		if (keepalive != 0) {
			_socket.set_option(boost::asio::socket_base::keep_alive{true});
			_socket.set_option(caen::socket_option::keep_interval{keepalive});
			_socket.set_option(caen::socket_option::keep_idle{keepalive});
			_socket.set_option(caen::socket_option::keep_cnt{20}); // hardcoded
		}

		// call to CONNECT
		constexpr cmd::handle_t tmp_handle{0x67696F}; // internal handle can be anything since it is not used in CONNECT
		const auto cmd = json_cmd::build(cmd::command::CONNECT, tmp_handle, "", _monitor ? "monitor" : "client");
		const auto ans = send(cmd);

		auto& val = ans.get_value();

		if (val.size() == 0)
			throw "invalid reply from the server"_ex;

		_digitizer_internal_handle = caen::lexical_cast<handle::internal_handle_t>(val[0]);

		if (val.size() == 2) {
			const auto server_version = caen::lexical_cast<std::decay_t<decltype(server_definitions::version)>>(val[1]);
			_logger->info("server version: {}", server_version);
			// ignore patch number
			constexpr decltype(server_definitions::version) patch_size{100};
			constexpr auto server_definitions_version_major_minor{server_definitions::version / patch_size};
			const auto server_version_major_minor = server_version / patch_size;
			_server_version_aligned = (server_version_major_minor <= server_definitions_version_major_minor);
		}

		// fill constants
		const auto n_channels_s = get_value(_digitizer_internal_handle, "/par/numch"s, std::string{});
		caen::lexical_cast_to(_n_channels, n_channels_s);
		const auto adc_samplrate_mhz_s = get_value(_digitizer_internal_handle, "/par/adc_samplrate"s, std::string{});
		_sampling_period_ns = 1e3 / caen::lexical_cast<double>(adc_samplrate_mhz_s);

		// compute endpoint address
		if (!_monitor)
			compute_endpoint_address();

	}

	~client_impl() {

		_logger->info("closing client to {}", _address);

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

	void initialize_endpoints(client& client) {

		std::shared_ptr<ep::hw_endpoint> hw_ep;
		std::list<std::shared_ptr<ep::sw_endpoint>> sw_ep_list;

		// add other endpoints
		for (auto&& handle : get_child_handles(_digitizer_internal_handle, "/endpoint"s)) {
			const auto prop = get_node_properties(handle, std::string{});
			if (prop.second == ::CAEN_FELib_NodeType_t::CAEN_FELib_ENDPOINT) {
				BOOST_ASSERT_MSG(prop.first == boost::to_lower_copy(prop.first), "node name must be returned lowercase by backend-server");
				switch (caen::hash::generator{}(prop.first)) {
					using namespace caen::hash::literals;
				case "raw"_h: {
					auto ep = create_endpoint<ep::raw>(client, handle);
					ep->set_max_size_getter([this]() {
						const auto max_raw_data_size_s = get_value(_digitizer_internal_handle, "/par/maxrawdatasize"s, std::string{});
						return caen::lexical_cast<std::size_t>(max_raw_data_size_s);
					});
					ep->set_is_decoded_getter([this]() {
						// pretty simple version, just check if is not raw
						const auto active_endpoint_s = get_value(_digitizer_internal_handle, "/endpoint/par/activeendpoint"s, std::string{});
						return !caen::string::iequals(active_endpoint_s, "raw"_sv);
					});
					// set as main hardware endpoint
					BOOST_ASSERT_MSG(hw_ep == nullptr, "defining more than one hardware endpoint");
					hw_ep = std::move(ep);
					break;
				}
				case "rawudp"_h: {
					auto ep = create_endpoint<ep::rawudp>(client, handle);
					ep->set_max_size_getter([this]() {
						const auto max_raw_data_size_s = get_value(_digitizer_internal_handle, "/par/maxrawdatasize"s, std::string{});
						return caen::lexical_cast<std::size_t>(max_raw_data_size_s);
					});
					ep->set_is_decoded_getter([this]() {
						// pretty simple version, just check if is not raw
						const auto active_endpoint_s = get_value(_digitizer_internal_handle, "/endpoint/par/activeendpoint"s, std::string{});
						return !caen::string::iequals(active_endpoint_s, "rawudp"_sv);
					});
					// set as main hardware endpoint
					BOOST_ASSERT_MSG(hw_ep == nullptr, "defining more than one hardware endpoint");
					hw_ep = std::move(ep);
					break;
				}
				case "opendata"_h: {
					auto ep = create_endpoint<ep::raw>(client, handle);
					ep->set_max_size_getter([] {
						return std::size_t{1 << 26}; // fixed, opendatasize can be changed in run
					});
					ep->set_is_decoded_getter([]() {
						// cannot be decoded
						return false;
					});
					break;
				}
				case "scope"_h: {
					sw_ep_list.emplace_back(create_endpoint<ep::scope>(client, handle));
					break;
				}
				case "opendpp"_h: {
					sw_ep_list.emplace_back(create_endpoint<ep::opendpp>(client, handle));
					break;
				}
				case "dpppha"_h: {
					sw_ep_list.emplace_back(create_endpoint<ep::dpppha>(client, handle));
					break;
				}
				case "dpppsd"_h: {
					sw_ep_list.emplace_back(create_endpoint<ep::dpppsd>(client, handle));
					break;
				}
				case "dppzle"_h: {
					sw_ep_list.emplace_back(create_endpoint<ep::dppzle>(client, handle));
					break;
				}
				default:
					throw ex::runtime_error(fmt::format("unsupported software endpoint {}", prop.first));
				}
			}
		}

		if (hw_ep == nullptr)
			throw "hardware endpoint not found"_ex;

		// add event endpoint manually
		auto evt_ep = create_endpoint<ep::events>(client, *hw_ep);
		sw_ep_list.emplace_back(std::move(evt_ep));

		for (auto& sw_ep : sw_ep_list)
			hw_ep->register_sw_endpoint(std::move(sw_ep));

	}

	std::string get_device_tree(handle::internal_handle_t handle) {
		const auto cmd = json_cmd::build(cmd::command::GET_DEVICE_TREE, handle, std::string{});
		const auto ans = send(cmd);
		return ans.get_value().at(0);
	}

	std::vector<handle::internal_handle_t> get_child_handles(handle::internal_handle_t handle, const std::string& path) {
		const auto cmd = json_cmd::build(cmd::command::GET_CHILD_HANDLES, handle, path);
		const auto ans = send(cmd);
		std::vector<handle::internal_handle_t> res;
		res.reserve(ans.get_value().size());
		boost::transform(ans.get_value(), std::back_inserter(res), [](const auto& str) {
			return caen::lexical_cast<handle::internal_handle_t>(str);
		});
		return res;
	}

	handle::internal_handle_t get_handle(handle::internal_handle_t handle, const std::string& path) {
		const auto cmd = json_cmd::build(cmd::command::GET_HANDLE, handle, path);
		const auto ans = send(cmd);
		return caen::lexical_cast<handle::internal_handle_t>(ans.get_value().at(0));
	}

	handle::internal_handle_t get_parent_handle(handle::internal_handle_t handle, const std::string& path) {
		const auto cmd = json_cmd::build(cmd::command::GET_PARENT_HANDLE, handle, path);
		const auto ans = send(cmd);
		return caen::lexical_cast<handle::internal_handle_t>(ans.get_value().at(0));
	}

	std::string get_path(handle::internal_handle_t handle) {
		const auto cmd = json_cmd::build(cmd::command::GET_PATH, handle, std::string{});
		const auto ans = send(cmd);
		return ans.get_value().at(0);
	}

	std::pair<std::string, ::CAEN_FELib_NodeType_t> get_node_properties(handle::internal_handle_t handle, const std::string& path) {
		const auto cmd = json_cmd::build(cmd::command::GET_NODE_PROPERTIES, handle, path);
		const auto ans = send(cmd);
		auto& value_name = ans.get_value().at(0);
		auto& value_type = ans.get_value().at(1);
		return { value_name, string_to_node_type(value_type) };
	}

	std::string get_value(handle::internal_handle_t handle, const std::string& path, const std::string& arg) {
		const auto cmd = json_cmd::build(cmd::command::GET_VALUE, handle, path, arg);
		const auto ans = send(cmd);
		return ans.get_value().at(0);
	}

	void set_value(handle::internal_handle_t handle, const std::string& path, const std::string& value) {
		const auto cmd = json_cmd::build(cmd::command::SET_VALUE, handle, path, value);
		send(cmd);
	}

	void send_command(handle::internal_handle_t handle, const std::string& path) {
		const auto cmd = json_cmd::build(cmd::command::SEND_COMMAND, handle, path);
		const auto ans = send(cmd);
		auto hw_ep_list = [&ep_list = _endpoint_list]() {
			auto cast_to_hw_ep = [](auto&& ptr) { return std::dynamic_pointer_cast<ep::hw_endpoint>(std::forward<decltype(ptr)>(ptr)); };
			auto not_null = [](const auto& ptr) { return ptr != nullptr; };
			return ep_list | boost::adaptors::transformed(cast_to_hw_ep) | boost::adaptors::filtered(not_null);
		};
		switch (ans.get_flag()) {
			using f = answer::flag;
		case f::ARM:
			for (const auto& hw_ptr : hw_ep_list())
				hw_ptr->arm_acquisition();
			break;
		case f::DISARM:
			for (const auto& hw_ptr : hw_ep_list())
				hw_ptr->disarm_acquisition();
			break;
		case f::RESET:
		case f::CLEAR:
			for (const auto& hw_ptr : hw_ep_list())
				hw_ptr->clear_data();
			break;
		default:
			break;
		}
	}

	std::uint32_t get_user_register(handle::internal_handle_t handle, std::uint32_t address) {
		if (handle != _digitizer_internal_handle)
			throw "get_user_register must me invoked on digitizer handle"_ex;
		const auto res = get_value(handle, _user_register_path, fmt::format("{}", address));
		return caen::lexical_cast<std::uint32_t>(res);
	}

	void set_user_register(handle::internal_handle_t handle, std::uint32_t address, std::uint32_t value) {
		if (handle != _digitizer_internal_handle)
			throw "set_user_register must me invoked on digitizer handle"_ex;
		set_value(handle, _user_register_path, fmt::format("{}={}", address, value));
	}

	void set_data_format(handle::internal_handle_t handle, const std::string& format) {
		get_endpoint(handle, __func__).set_data_format(format);
	}

	void read_data(handle::internal_handle_t handle, int timeout, va_list* args) {
		get_endpoint(handle, __func__).read_data(get_timeout(timeout), args);
	}

	void has_data(handle::internal_handle_t handle, int timeout) {
		get_endpoint(handle, __func__).has_data(get_timeout(timeout));
	}

	// trivial getters
	auto is_monitor() const noexcept { return _monitor; }
	const auto& get_url_data() const noexcept { return _url_data; }
	const auto& get_address() const noexcept { return _address; }
	const auto& get_endpoint_address() const noexcept { return _endpoint_address; }
	auto get_digitizer_internal_handle() const noexcept { return _digitizer_internal_handle; }
	auto is_server_version_aligned() const noexcept { return _server_version_aligned; }
	auto& get_endpoint_list() noexcept { return _endpoint_list; }
	const auto& get_endpoint_list() const noexcept { return _endpoint_list; }
	auto get_n_channels() const noexcept { return _n_channels; }
	auto get_sampling_period_ns() const noexcept { return _sampling_period_ns; }

private:

	/*
	 * These two constants cannot be made static constexpr variables since clang <= 5
	 * (supporting only C++14) does not inline their values on boost::optional::value_or.
	 */
	static constexpr int default_keepalive_interval() noexcept { return 4; }
	static constexpr bool default_monitor() noexcept { return false; }

	static constexpr ep::endpoint::timeout_t get_timeout(int timeout) noexcept {
		return ep::endpoint::timeout_t{timeout};
	}

	template <typename Duration, typename Callable>
	void run_context_for(Duration&& timeout, Callable stopped_callback) {

		_io_context.reset();

		/*
		 * See example at
		 * https://www.boost.org/doc/libs/1_67_0/doc/html/boost_asio/example/cpp03/timeouts/blocking_tcp_client.cpp
		 *
		 * Block until the asynchronous operation has completed, or timed out. If
		 * the pending asynchronous operation is a composed operation, the deadline
		 * applies to the entire operation, rather than individual operations on
		 * the socket.
		 */
		_io_context.run_for(std::forward<Duration>(timeout));

		/*
		 * If the asynchronous operation completed successfully then the io_context
		 * would have been stopped due to running out of work. If it was not
		 * stopped, then the io_context::run_for call must have timed out.
		 */
		if (!_io_context.stopped())
			stopped_callback();

	}

	boost::asio::ip::address connect_to(const url_data& data) {

		// at this point URL can be either plain hostname or special hostname with "caen.internal" authority or legacy "usb:PID"
		const auto address = url_to_address(data);

		boost::system::error_code ec;

		boost::asio::ip::tcp::resolver resolver{_io_context};
		const auto endpoint_iter = resolver.resolve(address, caen::lexical_cast<std::string>(server_definitions::command_port), ec);

		if (ec) {
			const auto msg = ec.message();
			_logger->warn("resolve failed: {}", msg);
			throw ex::device_not_found(msg);
		}

		boost::asio::async_connect(_socket, endpoint_iter, [&ec](boost::system::error_code new_ec, const boost::asio::ip::tcp::endpoint&) {
			ec = new_ec;
		});

		run_context_for(4s, [this] {
			// close the socket to cancel the outstanding asynchronous operation
			_socket.close();

			// run the io_context again until the operation completes: this will set ec to an error
			_io_context.run();
		});

		if (ec) {
			const auto msg = ec.message();
			_logger->warn("device not found: {}", msg);
			throw ex::device_not_found(msg);
		}

		BOOST_ASSERT_MSG(_socket.is_open(), "socket not opened");

		_logger->info("connected to {}", _socket.remote_endpoint());

		return _socket.remote_endpoint().address();
	}

	template <typename N, typename... Args>
	std::shared_ptr<N> create_endpoint(Args&&... args) {
		static_assert(std::is_base_of<ep::endpoint, N>::value, "invalid endpoint type");
		const auto ep = std::make_shared<N>(std::forward<Args>(args)...);
		_endpoint_list.push_back(ep);
		_logger->info("endpoint created at handle {:#x}", ep->get_endpoint_server_handle());
		return ep;
	}

	json_answer send(const json_cmd& cmd) try {

		SPDLOG_LOGGER_DEBUG(_logger, R"(sending {}({}, "{}", "{}"))", caen::json::to_json_string(cmd.get_cmd()), cmd.get_handle(), cmd.get_query(), cmd.get_value());

		std::array<caen::byte, server_definitions::header_size> header_buffer{};

		// generate request
		auto b_it = header_buffer.begin();
		const auto request_string = cmd.unmarshal();
		caen::serdes::serialize<std::uint64_t>(b_it, request_string.size());
		BOOST_ASSERT_MSG(b_it <= header_buffer.end(), "inconsistent header decoding");

		// send request
		const std::array<boost::asio::const_buffer, 2> buffers{
			boost::asio::buffer(header_buffer),
			boost::asio::buffer(request_string)
		};

		std::unique_lock<std::mutex> lk{ _mtx };

		boost::asio::write(_socket, buffers);

		// read header
		boost::asio::read(_socket, boost::asio::buffer(header_buffer));

		auto b_const_it = header_buffer.cbegin();
		auto size = caen::serdes::deserialize<std::uint64_t>(b_const_it);
		BOOST_ASSERT_MSG(b_const_it <= header_buffer.cend(), "inconsistent header decoding");

		SPDLOG_LOGGER_DEBUG(_logger, "reply received (size={})", size);

		// read data
		boost::asio::streambuf reply_buffer(size);
		boost::asio::read(_socket, reply_buffer);

		lk.unlock();

		// process request
		std::istream reply_stream(&reply_buffer);
		auto res = json_answer::marshal(reply_stream);
		BOOST_ASSERT_MSG(res.get_cmd() == cmd.get_cmd(), "unexpected command type on reply");
		if (!res.get_result()) {
			const auto error_message = fmt::format("digitizer error: {}", fmt::join(res.get_value(), " "));
			_logger->error(error_message);
			throw ex::command_error(error_message);
		}

		return res;
	}
	catch (const nlohmann::json::exception& ex) {
		throw ex::command_error(fmt::format("JSON error: {}", ex.what()));
	}
	catch (const boost::system::system_error& ex) {
		throw ex::communication_error(fmt::format("Boost ASIO error: {}", ex.what()));
	}

	template <typename String>
	ep::endpoint& get_endpoint(handle::internal_handle_t handle, const String& function) const {
		const auto it = boost::find_if(_endpoint_list, [handle](auto& ptr) { return handle == ptr->get_endpoint_server_handle(); });
		if (BOOST_UNLIKELY(it == _endpoint_list.end()))
			throw ex::invalid_argument(fmt::format("{} allowed only on endpoint handles", function));
		return **it;
	}

	void compute_endpoint_address() {
#if BOOST_OS_LINUX
		/*
		 * Hack to fallback on CDC interface in case of RNDIS.
		 */
		if (_address.is_v6()) {
			const auto address = _address.to_v6();
			const auto rndis_address = boost::asio::ip::address_v6({0xfd, 0xa7, 0xca, 0xe0});
			const auto rndis_network = boost::asio::ip::network_v6(rndis_address, 32);
			const auto hosts = rndis_network.hosts();
			const auto is_rndis = hosts.find(address) != hosts.end();
			if (is_rndis) {
				const auto has_cdc = [this] {
					try {
						const auto has_cdc_s = get_value(_digitizer_internal_handle, "/par/hascdc"s, std::string{});
						return caen::string::iequals(has_cdc_s, "True"_sv);
					}
					catch (const std::exception&) {
						// hascdc parameter not found, assuming false
						return false;
					}
				}();
				if (has_cdc) {
					auto bytes = address.to_bytes();
					BOOST_ASSERT_MSG(bytes[1] == 0xa7, "invalid implementation");
					bytes[1] = 0xa6; // fda7 -> fda6
					_endpoint_address = boost::asio::ip::make_address_v6(bytes);
					return;
				}
			}
		}
#endif
		_endpoint_address = _address;
	}

	mutable std::mutex _mtx;
	const url_data _url_data;
	const bool _monitor;
	std::shared_ptr<spdlog::logger> _logger;
	boost::asio::io_context _io_context;
	boost::asio::ip::tcp::socket _socket;
	const boost::asio::ip::address _address;
	boost::asio::ip::address _endpoint_address;
	handle::internal_handle_t _digitizer_internal_handle;
	bool _server_version_aligned;
	std::list<std::shared_ptr<ep::endpoint>> _endpoint_list;
	const std::string _user_register_path;
	std::size_t _n_channels;
	double _sampling_period_ns;
};

url_data parse_url(const std::string& url) {

	url_data data;

	const auto url_complete = fmt::format("dig2://{}", url); // add prefix removed by CAEN FELib to get compliant URI
	const auto url_lowercase = boost::to_lower_copy(url_complete);

	/*
	 * Parsing a URI Reference with a Regular Expression
	 * Copied from RFC 3986 at https://www.rfc-editor.org/rfc/rfc3986#page-50
	 */
	std::regex url_regex(R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)"s, std::regex::extended);
	std::smatch url_match_result;

	if (!std::regex_match(url_lowercase, url_match_result, url_regex))
		throw ex::invalid_argument(fmt::format("invalid URI: {}", url));

	data._scheme = url_match_result[2];
	data._authority = url_match_result[4];
	data._path = url_match_result[5];
	data._query = url_match_result[7];
	data._fragment = url_match_result[9];

	// parse optional query
	std::forward_list<std::string> split_query;
	boost::split(split_query, data._query, boost::is_any_of("&"));
	for (const auto& str : split_query) {
		std::vector<std::string> split_single_query;
		boost::split(split_single_query, str, boost::is_any_of("="));
		switch (caen::hash::generator{}(split_single_query.at(0))) {
			using namespace caen::hash::literals;
		case "monitor"_h:
			data._monitor = true;
			break;
		case "log_level"_h:
			data._log_level = spdlog::level::from_str(split_single_query.at(1));
			break;
		case "pid"_h:
			data._pid = split_single_query.at(1);
			break;
		case "keepalive"_h:
			data._keepalive = caen::lexical_cast<int>(split_single_query.at(1));
			break;
		case "rcvbuf"_h:
			data._rcvbuf = caen::lexical_cast<int>(split_single_query.at(1));
			break;
		case "receiver_thread_affinity"_h:
			data._receiver_thread_affinity = caen::lexical_cast<int>(split_single_query.at(1));
			break;
		default:
			break;
		}
	}

	return data;
}

client::client(const url_data& data)
: _pimpl{std::make_unique<client_impl>(data)} {
	if (!_pimpl->is_monitor())
		_pimpl->initialize_endpoints(*this);
}

client::~client() = default;

const url_data& client::get_url_data() const noexcept {
	return _pimpl->get_url_data();
}

const boost::asio::ip::address& client::get_address() const noexcept {
	return _pimpl->get_address();
}

const boost::asio::ip::address& client::get_endpoint_address() const noexcept {
	return _pimpl->get_endpoint_address();
}

void client::register_endpoint(std::shared_ptr<ep::endpoint> ep) {
	_pimpl->get_endpoint_list().push_back(std::move(ep));
}

const std::list<std::shared_ptr<ep::endpoint>>& client::get_endpoint_list() const noexcept {
	return _pimpl->get_endpoint_list();
}

handle::internal_handle_t client::get_digitizer_internal_handle() const noexcept {
	return _pimpl->get_digitizer_internal_handle();
}

bool client::is_server_version_aligned() const noexcept {
	return _pimpl->is_server_version_aligned();
}

std::size_t client::get_n_channels() const noexcept {
	return _pimpl->get_n_channels();
}

double client::get_sampling_period_ns() const noexcept {
	return _pimpl->get_sampling_period_ns();
}

std::string client::get_device_tree(handle::internal_handle_t handle) {
	return _pimpl->get_device_tree(handle);
}

std::vector<handle::internal_handle_t> client::get_child_handles(handle::internal_handle_t handle, const std::string& path) {
	return _pimpl->get_child_handles(handle, path);
}

handle::internal_handle_t client::get_handle(handle::internal_handle_t handle, const std::string& path) {
	return _pimpl->get_handle(handle, path);
}

handle::internal_handle_t client::get_parent_handle(handle::internal_handle_t handle, const std::string& path) {
	return _pimpl->get_parent_handle(handle, path);
}

std::string client::get_path(handle::internal_handle_t handle) {
	return _pimpl->get_path(handle);
}

std::pair<std::string, ::CAEN_FELib_NodeType_t> client::get_node_properties(handle::internal_handle_t handle, const std::string& path) {
	return _pimpl->get_node_properties(handle, path);
}

std::string client::get_value(handle::internal_handle_t handle, const std::string& path, const std::string& arg) {
	return _pimpl->get_value(handle, path, arg);
}

void client::set_value(handle::internal_handle_t handle, const std::string& path, const std::string& value) {
	_pimpl->set_value(handle, path, value);
}

void client::send_command(handle::internal_handle_t handle, const std::string& path) {
	_pimpl->send_command(handle, path);
}

std::uint32_t client::get_user_register(handle::internal_handle_t handle, std::uint32_t address) {
	return _pimpl->get_user_register(handle, address);
}

void client::set_user_register(handle::internal_handle_t handle, std::uint32_t address, std::uint32_t value) {
	_pimpl->set_user_register(handle, address, value);
}

void client::set_data_format(handle::internal_handle_t handle, const std::string& format) {
	_pimpl->set_data_format(handle, format);
}

void client::read_data(handle::internal_handle_t handle, int timeout, std::va_list* args) {
	_pimpl->read_data(handle, timeout, args);
}

void client::has_data(handle::internal_handle_t handle, int timeout) {
	_pimpl->has_data(handle, timeout);
}

bool client::is_monitor() const noexcept {
	return _pimpl->is_monitor();
}

} // namespace dig2

} // namespace caen
