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
*	\file		discovery.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "discovery.hpp"

#include <array>
#include <string>
#include <cassert>
#include <chrono>
#include <regex>
#include <stdexcept>
#include <numeric>
#include <list>
#include <memory>
#include <algorithm>

#include <boost/predef/os.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <spdlog/fmt/fmt.h>

#if BOOST_OS_WINDOWS
#else
#include <sys/types.h>
#include <ifaddrs.h>
#endif

#include <json/json_utilities.hpp>

#if BOOST_OS_WINDOWS
#else
#include "cpp-utility/bit.hpp"
#endif
#include "cpp-utility/string_view.hpp"
#include "lib_error.hpp"
#include "library_logger.hpp"

using namespace std::literals;
using namespace caen::literals;

namespace caen {

namespace dig2 {

namespace ssdp {

namespace detail {

struct line {
	friend std::istream& operator>>(std::istream &is, line& line) {
		auto& r = std::getline(is, line._s);
		if (line._s.back() == '\r')
			line._s.pop_back();
		return r;
	}
	operator const std::string&() const noexcept { return _s; }
	operator caen::string_view() const noexcept { return _s; }
private:
	std::string _s;
};

struct stupid_http_client {

	using line_iterator = std::istream_iterator<detail::line>;

	explicit stupid_http_client(boost::asio::io_context& io_context)
		: _io_context(io_context)
		, _response_buffer{}
		, _response_stream{&_response_buffer} {
	}

	std::string get_string(const std::string& url) {
		std::string res;
		try {
			get_to_buffer(url);
		}
		catch (const std::exception&) {
			return res;
		}
		auto it = line_iterator(_response_stream);
		// put all the content in a single string
		return std::accumulate(it, line_iterator(), res, std::plus<std::string>());
	}

	boost::property_tree::ptree get_xml(const std::string& url) {
		get_to_buffer(url);
		boost::property_tree::ptree res;
		// property_tree XML support is very limited but seems enough for SSDP
		boost::property_tree::read_xml(_response_stream, res);
		return res;
	}

private:

	static constexpr auto& http_request() noexcept {
		return "GET {} HTTP/1.1\r\nHost: {}\r\nAccept: */*\r\nConnection: close\r\n\r\n";
	}

	void get_to_buffer(const std::string& url) {

		static const std::regex ex("(http|https)://([^/ :]+):?([^/ ]*)(/?[^ #?]*)\\x3f?([^ #]*)#?([^ ]*)"s);
		std::smatch what;

		if (!regex_match(url, what, ex))
			throw ex::invalid_argument("invalid url");

		//auto& protocol = what[1];
		const auto& domain = what[2];
		const auto& port = what[3];
		const auto& path = what[4];
		//auto& query = what[5];

		boost::asio::ip::tcp::resolver::query q(domain, port);
		boost::asio::ip::tcp::resolver resolver(_io_context);
		auto endpoint_iterator = resolver.resolve(q);

		boost::asio::ip::tcp::socket socket(_io_context);
		boost::asio::connect(socket, endpoint_iterator);

		boost::asio::streambuf request;
		std::ostream request_stream(&request);
		fmt::print(request_stream, http_request(), path, domain);

		boost::system::error_code write_ec;
		boost::asio::write(socket, request, write_ec);
		if (write_ec)
			throw ex::runtime_error(write_ec.message());

		// Read the response
		boost::system::error_code read_ec;
		boost::asio::read(socket, _response_buffer, read_ec);
		if (read_ec != boost::asio::error::eof)
			throw ex::runtime_error(read_ec.message());

		auto it = line_iterator(_response_stream);

		// Check that response is OK.
		if (it == line_iterator())
			throw "invalid response"_ex;

		// for simplicity we only check if header response is HTTP (no check for HTTP code != 200)
		// as the code will fail as soon as we'll try to parse the content
		if (!boost::starts_with<caen::string_view>(*it, "HTTP/"_sv))
			throw "invalid response"_ex;

		// skip header, that ends with an empty line
		it = std::find_if(++it, line_iterator(), [](const std::string& line) {
			return line.empty();
		});

		// check if there is content
		if (it == line_iterator())
			throw "no content"_ex;

	}

	boost::asio::io_context& _io_context;
	boost::asio::streambuf _response_buffer;
	std::istream _response_stream;
};

} // namespace detail

enum struct device_type {
	UNKNOWN,
	USB,
	ETHERNET
};

NLOHMANN_JSON_SERIALIZE_ENUM(device_type, {
	{ device_type::UNKNOWN, 			nullptr 			},
	{ device_type::USB,					"USB"s				},
	{ device_type::ETHERNET,			"Ethernet"s			},
})

struct device {

	device(const std::string& model, const std::string& serial_number, const std::string& ip, device_type type)
	: _model(model)
	, _serial_number(serial_number)
	, _ip(ip)
	, _type{type} {}

	device()
	: _model{}
	, _serial_number{}
	, _ip{}
	, _type{device_type::UNKNOWN} {}

	/**
	 * Convert JSON to device
	 * @param args any input of nlohmann::json::parse representing a JSON
	 * @return an instance of device parsing the input content
	 */
	template <typename... Args>
	static device marshal(Args&& ...args) {
		return nlohmann::json::parse(std::forward<Args>(args)...);
	}

	/**
	 * Convert device to JSON
	 * @return the JSON with no indentation
	 */
	nlohmann::json::string_t unmarshal() const {
		return nlohmann::json(*this).dump();
	}

	device(const device&) = default;
	device(device&&) = default;
	~device() = default;

	device& operator=(const device&) = default;
	device& operator=(device&&) = default;

	const std::string& get_model() const noexcept { return _model; }
	const std::string& get_serial_number() const noexcept{ return _model; }
	const std::string& get_ip() const noexcept { return _model; }
	const std::string& get_type() const noexcept { return _model; }

	static constexpr auto& key_model() noexcept { return "model"; }
	static constexpr auto& key_serial_number() noexcept { return "serial_number"; }
	static constexpr auto& key_ip() noexcept { return "ip"; }
	static constexpr auto& key_type() noexcept { return "type"; }

	friend void from_json(const nlohmann::json& j, device& e) {
		caen::json::get_if_not_null(j, key_model(), e._model);
		caen::json::get_if_not_null(j, key_serial_number(), e._serial_number);
		caen::json::get_if_not_null(j, key_ip(), e._ip);
		caen::json::get_if_not_null(j, key_type(), e._type);
	}

	friend void to_json(nlohmann::json& j, const device& e) {
		caen::json::set(j, key_model(), e._model);
		caen::json::set(j, key_serial_number(), e._serial_number);
		caen::json::set(j, key_ip(), e._ip);
		caen::json::set(j, key_type(), e._type);
	}
private:
	std::string _model;
	std::string _serial_number;
	std::string _ip;
	device_type _type;
};

struct discover {

	using line_iterator = std::istream_iterator<detail::line>;

	explicit discover()
	: _logger{library_logger::create_logger("ssdp discover"s)}
	, _io_context{}
	, _timeout_timer(_io_context)
	, _request_timer(_io_context)
	, _request_count{3}
	, _v4(_io_context, boost::asio::ip::address_v4({239, 255, 255, 250}))
	, _v6(_io_context, boost::asio::ip::address_v6({0xff, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc}))
	, _buffer{}
	, _local_interfaces{}
	, _list{} {

		assert(_v4._multicast_ep.address().to_v4().is_multicast());
		assert(_v6._multicast_ep.address().to_v6().is_multicast_link_local());

		fill_local_interfaces();

		_v4._socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
		_v4._socket.bind(_v4._local_ep);
		_v4._socket.set_option(boost::asio::ip::multicast::hops(4)); // UPnP default

		_v6._socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
		_v6._socket.bind(_v6._local_ep);
		_v6._socket.set_option(boost::asio::ip::multicast::hops(4)); // UPnP default

	}

	void fill_local_interfaces() {
#if BOOST_OS_WINDOWS
		boost::asio::ip::udp::resolver resolver(_io_context);
		const auto it = resolver.resolve(boost::asio::ip::host_name(), "");
		std::transform(it, decltype(it)(), std::back_inserter(_local_interfaces), [](const auto& resolver_entry) {
			return resolver_entry.endpoint().address();
		});
#else
		ifaddrs* ifa = nullptr;
		if (::getifaddrs(&ifa) != 0)
			throw std::runtime_error(fmt::format("getifaddrs failed: {}", std::strerror(errno)));
		// store ifa in a unique_ptr for RAII delete
		std::unique_ptr<ifaddrs, decltype(&::freeifaddrs)> deleter(ifa, &::freeifaddrs);
		for (; ifa != nullptr; ifa = ifa->ifa_next) {
			const auto ifa_member = ifa->ifa_addr;
			if (ifa_member == nullptr)
				continue;
			switch (ifa_member->sa_family) {
			case AF_INET:
			case AF_INET6: {
				std::array<char, std::max(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)> buff;
				auto sockaddr_in_member = caen::bit_cast<sockaddr_in>(*ifa_member);
				auto sin_addr_ptr = &sockaddr_in_member.sin_addr;
				if (::inet_ntop(ifa_member->sa_family, sin_addr_ptr, buff.data(), buff.size()) == nullptr)
					throw std::runtime_error(fmt::format("inet_ntop failed: {}", std::strerror(errno)));
				_local_interfaces.emplace_back(boost::asio::ip::address::from_string(buff.data()));
				break;
			}
			default:
				break;
			}
		}
#endif
	}

	void periodic_send() {
		_request_timer.async_wait([this](const boost::system::error_code& error) {
			if (error) {
				_logger->error("async_wait error: {}", error.message());
				return;
			}
			if (_request_count-- == 0)
				return;
			for (const auto& addr : _local_interfaces) {
				if (addr.is_v4()) {
					_v4._socket.set_option(boost::asio::ip::multicast::outbound_interface(addr.to_v4()));
					_v4._socket.send_to(_v4._ssdp_request_buffer, _v4._multicast_ep);
				} else if (addr.is_v6()) {
					_v6._socket.set_option(boost::asio::ip::multicast::outbound_interface(addr.to_v6().scope_id()));
					_v6._socket.send_to(_v6._ssdp_request_buffer, _v6._multicast_ep);
				}
			}
			_request_timer.expires_from_now(1s);
			periodic_send();
		});
	}

	void send(std::chrono::milliseconds timeout_ms) {
		_request_timer.expires_from_now(0ms);
		periodic_send();
		_timeout_timer.expires_from_now(timeout_ms);
		_timeout_timer.async_wait([this](const boost::system::error_code& error) {
			if (error) {
				_logger->error("async_wait error: {}", error.message());
				return;
			}
			_request_timer.cancel();
			_v4._socket.close();
			_v6._socket.close();
		});
	}

	void receive() {
		auto mutable_buffer = _buffer.prepare(4096);
		auto reply_handler = [this](const boost::asio::ip::udp::endpoint& local_ep) {
			return [this, &local_ep](const boost::system::error_code& error, std::size_t s) {
				if (error) {
					_logger->error("async_receive_from error: {}", error.message());
					return;
				}

				_buffer.commit(s);
				std::istream response_stream(&_buffer);

				const std::string remote_ip = local_ep.address().to_string();

				// find line starting with "location:"
				static constexpr auto prefix_view = "location:"_sv;
				auto it = std::find_if(line_iterator(response_stream), line_iterator(), [&](caen::string_view line_view) {
					return boost::istarts_with(line_view, prefix_view);
				});

				if (it != line_iterator()) {
					caen::string_view line_view(*it);

					// remove "location:" and eventual additional whitespaces
					line_view.remove_prefix(prefix_view.size());
					line_view.remove_prefix(std::min(line_view.find_first_not_of(" \t\r\f\v\n"_sv), line_view.size()));

					// get xml content from url
					detail::stupid_http_client hc(_io_context);

					boost::property_tree::ptree tree;

					try {
						tree = hc.get_xml(std::string(line_view.data(), line_view.size()));
					}
					catch (const std::exception& ex) {
						_logger->warn("invalid XML from {}: {}", line_view, ex.what());
						return;
					}

					//auto friendly_name = tree.get<std::string>("root.device.friendlyName");
					const auto model_name = tree.get<std::string>("root.device.modelName");
					const auto serial_number = tree.get<std::string>("root.device.serialNumber");
					//auto input_channels = tree.get<unsigned int>("root.device.inputChannels", 0);

					_list.emplace_back(model_name, serial_number, remote_ip, device_type::UNKNOWN);

					_logger->info("device found: {}", nlohmann::json(_list.back()).dump());

				} else {
					_logger->warn("location not found in SSDP reply from: {}", remote_ip);
				}

				// wait for another message
				receive();
			};
		};
		_v4._socket.async_receive_from(mutable_buffer, _v4._local_ep, reply_handler(_v4._local_ep));
		_v6._socket.async_receive_from(mutable_buffer, _v6._local_ep, reply_handler(_v6._local_ep));
	}

	void run(std::chrono::milliseconds timeout_ms) {
		send(timeout_ms);
		receive();
		_io_context.run();
	}
	auto& get_list() const noexcept { return _list; }

private:

	static constexpr auto& ssdp_request() noexcept {
		return "M-SEARCH * HTTP/1.1\r\nHost: {}\r\nMan: \"ssdp:discover\"\r\nST: upnp:rootdevice\r\nMX: {}\r\nUser-Agent: CAEN/1.0\r\n\r\n";
	}

	template <typename AddressVersion>
	struct socket_impl {
		socket_impl(boost::asio::io_context& ctx, AddressVersion multicast_address)
			: _multicast_ep(multicast_address, ssdp_port)
			, _ssdp_request(fmt::format(ssdp_request(), _multicast_ep, 1))
			, _ssdp_request_buffer(_ssdp_request.data(), _ssdp_request.size())
			, _local_ep(AddressVersion::any(), 0)
			, _socket(ctx, _local_ep.protocol()) {}
		const boost::asio::ip::udp::endpoint _multicast_ep;
		const std::string _ssdp_request;
		const boost::asio::const_buffer _ssdp_request_buffer;
		boost::asio::ip::udp::endpoint _local_ep;
		boost::asio::ip::udp::socket _socket;
	};
	static constexpr unsigned short ssdp_port{1900};
	std::shared_ptr<spdlog::logger> _logger;
	boost::asio::io_context _io_context;
	boost::asio::steady_timer _timeout_timer;
	boost::asio::steady_timer _request_timer;
	unsigned int _request_count;
	socket_impl<boost::asio::ip::address_v4> _v4;
	socket_impl<boost::asio::ip::address_v6> _v6;
	boost::asio::streambuf _buffer;
	std::list<boost::asio::ip::address> _local_interfaces;
	std::list<device> _list;
};

nlohmann::json get_ssdp_devices(std::chrono::milliseconds timeout_ms) {
	ssdp::discover discover;
	discover.run(timeout_ms);
	return discover.get_list();
}

} // namespace ssdp

} // namespace dig2

} // namespace caen
