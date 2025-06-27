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
*	\file		library_logger.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "library_logger.hpp"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/predef/os.h>
#include <boost/version.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#if BOOST_OS_WINDOWS
#include <spdlog/sinks/msvc_sink.h>
#endif
#include <nlohmann/json.hpp>

#include <CAEN_FELib.h>

#include "CAENDig2.h"

using namespace std::literals;

namespace caen {

namespace dig2 {

namespace library_logger {

namespace {

void log_library_versions() {

	auto int_to_triplet = [](int v) -> std::array<int, 3> { return { (v / 10000), (v / 100) % 100, v % 100 }; };
	auto boost_int_to_triplet = [](int v) -> std::array<int, 3> { return { (v / 100000), (v / 100) % 1000, v % 100 }; };

	static constexpr auto caen_dig2_version = CAEN_DIG2_VERSION_STRING ""sv;
	static constexpr auto caen_fe_version = CAEN_FELIB_VERSION_STRING ""sv;
	static constexpr auto compiler_version = BOOST_COMPILER ""sv;
	static constexpr auto platform_name = BOOST_PLATFORM ""sv;
	static constexpr auto stdlib_version = BOOST_STDLIB ""sv;
	static constexpr auto json_version = { NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH };
	static constexpr auto spdlog_version = { SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH };
	static constexpr auto fmt_version = FMT_VERSION;
	static constexpr auto boost_version = BOOST_VERSION;

	spdlog::info("built on {} {}", __DATE__, __TIME__);
	spdlog::info("compiled with {} on {}", compiler_version, platform_name);
	spdlog::info("stdlib version: {}", stdlib_version);
	spdlog::info("caen-dig2 version: {}", caen_dig2_version);
	spdlog::info("caen-fe version: {}", caen_fe_version);
	spdlog::info("JSON for Modern C++ version: {}", fmt::join(json_version, "."));
	spdlog::info("spdlog version: {}", fmt::join(spdlog_version, "."));
	spdlog::info("{{fmt}} version (provided by spdlog): {}", fmt::join(int_to_triplet(fmt_version), "."));
	spdlog::info("Boost version: {}", fmt::join(boost_int_to_triplet(boost_version), "."));

}

// sink singleton
template<typename T, typename... Args>
std::shared_ptr<spdlog::sinks::sink> sink(Args&& ...args) {
	static_assert(std::is_base_of<spdlog::sinks::sink, T>::value);
	static auto sink_instance = std::make_shared<T>(std::forward<Args>(args)...);
	return sink_instance;
}

#if BOOST_OS_WINDOWS
std::shared_ptr<spdlog::sinks::sink> msvc_sink() {
	using sink_type = spdlog::sinks::msvc_sink_mt;
	return sink<sink_type>();
}
#endif

std::shared_ptr<spdlog::sinks::sink> file_sink() {
	using sink_type = spdlog::sinks::basic_file_sink_mt;
#if BOOST_OS_WINDOWS
	const auto localappdata_env = std::getenv("LOCALAPPDATA");
	BOOST_ASSERT_MSG(localappdata_env != nullptr, "unexpected unset LOCALAPPDATA");
	const auto filename = fmt::format(SPDLOG_FILENAME_T("{}/CAEN/caendig2.log"), localappdata_env);
#else
	const auto home_env = std::getenv("HOME");
	BOOST_ASSERT_MSG(home_env != nullptr, "unexpected unset HOME");
	const auto filename = fmt::format(SPDLOG_FILENAME_T("{}/.CAEN/caendig2.log"), home_env);
#endif
	static constexpr bool truncate{true};
	return sink<sink_type>(filename, truncate);
}

} // unnamed namespace

void init() {

	/*
	 * Important notes about logger:
	 * - async loggers are not supported in a dynamic library
	 * - SPDLOG_LOGGER_TRACE and SPDLOG_LOGGER_DEBUG are not even compiled unless macro SPDLOG_ACTIVE_LEVEL is redefined at compile time
	 */

	// registration is required for name-based global access and timer-based flush
	spdlog::set_automatic_registration(false);

	// set a default level to off and then invoke load_env_levels to override the default value using SPDLOG_LEVEL
	spdlog::set_level(spdlog::level::off);
	spdlog::cfg::load_env_levels();

	// flush is always set on active level, since log is for debug only
	spdlog::flush_on(spdlog::get_level());

	// create the default logger with these settings
	spdlog::set_default_logger(create_logger("default"s));

	log_library_versions();
}

std::shared_ptr<spdlog::logger> create_logger(const std::string& name) {
	spdlog::sinks_init_list sink_list{
		file_sink(),
#if BOOST_OS_WINDOWS
		msvc_sink(),
#endif
	};
	return spdlog::create<spdlog::sinks::dist_sink_mt>(name, std::move(sink_list));
}

std::shared_ptr<spdlog::logger> create_logger(const std::string& name, const std::optional<spdlog::level::level_enum>& level) {
	const auto logger = create_logger(name);
	if (level) {
		logger->set_level(*level);
		logger->flush_on(*level);
	}
	return logger;
}

} // namespace library_logger

} // namespace dig2

} // namespace caen

// custom handler for assertions in debug
#if defined(BOOST_ENABLE_ASSERT_DEBUG_HANDLER) && !defined(NDEBUG)
void boost::assertion_failed(char const* expr, char const* function, char const* file, long line) {
	assertion_failed_msg(expr, "", function, file, line);
}
void boost::assertion_failed_msg(char const* expr, char const* msg, char const* function, char const* file, long line) {
	spdlog::critical("{}: {}\t{}\t{}\t{}", file, line, function, expr, msg);
	std::abort();
}
#endif

#undef CAEN_SPDLOG_LEVEL_PATCH
