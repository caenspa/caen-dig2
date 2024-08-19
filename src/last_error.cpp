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
*	\file		last_error.cpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include "last_error.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#if __cplusplus < 201703L
#include <spdlog/fmt/bundled/ostream.h> // required to use fmt on boost::string_view in C++14
#endif

#include <CAEN_FELib.h>

#include "cpp-utility/string_view.hpp"
#include "lib_error.hpp"

using namespace caen::literals;

namespace caen {

namespace dig2 {

namespace last_error {

std::string& instance() noexcept(noexcept(std::string())) {
	// std::string default constructor is noexcept(std::allocator()), and std::allocator() noexcept
	thread_local std::string s;
	return s;
}

namespace {

template <typename StringT>
void store(StringT&& formatted_error) {
	instance() = std::string(std::forward<StringT>(formatted_error));
}

template <typename FuncT, typename StringT>
void store_and_log(FuncT&& func, StringT&& detail) noexcept {
	try {
		store(detail);
	} catch (...) {
		// do nothing
	}
	try {
		spdlog::error("[{}] {}", std::forward<FuncT>(func), std::forward<StringT>(detail));
	} catch (...) {
		// do nothing
	}
}

template <typename FuncT, typename TypeT>
void store_and_log(FuncT&& func, TypeT&& type, const std::exception& ex) noexcept try {
	auto detail = fmt::format("{}: {}", std::forward<TypeT>(type), ex.what());
	store_and_log(std::forward<FuncT>(func), std::move(detail));
} catch (...) {
	return;
}

} // unnamed namespace

int _handle_exception(caen::string_view func) noexcept try {
	// this throw usage is allowed when an exception is presently being handled, it calls std::terminate if used otherwise
	throw;
}
catch (const ex::timeout&) {
	// no log message to increase performance
	return ::CAEN_FELib_Timeout;
}
catch (const ex::stop&) {
	// no log message to increase performance
	return ::CAEN_FELib_Stop;
}
catch (const ex::invalid_argument& ex) {
	store_and_log(func, "invalid argument"_sv, ex);
	return ::CAEN_FELib_InvalidParam;
}
catch (const ex::invalid_handle& ex) {
	store_and_log(func, "invalid handle"_sv, ex);
	return ::CAEN_FELib_InvalidHandle;
}
catch (const ex::command_error& ex) {
	store_and_log(func, "command error"_sv, ex);
	return ::CAEN_FELib_CommandError;
}
catch (const ex::communication_error& ex) {
	store_and_log(func, "communication error"_sv, ex);
	return ::CAEN_FELib_CommunicationError;
}
catch (const ex::not_yet_implemented& ex) {
	store_and_log(func, "not yet implemented"_sv, ex);
	return ::CAEN_FELib_NotImplemented;
}
catch (const ex::device_not_found& ex) {
	store_and_log(func, "device not found"_sv, ex);
	return ::CAEN_FELib_DeviceNotFound;
}
catch (const ex::too_many_devices& ex) {
	store_and_log(func, "too many devices"_sv, ex);
	return ::CAEN_FELib_MaxDevicesError;
}
catch (const ex::bad_library_version& ex) {
	store_and_log(func, "bad library error"_sv, ex);
	return ::CAEN_FELib_BadLibraryVersion;
}
catch (const ex::not_enabled& ex) {
	store_and_log(func, "endpoint not enabled"_sv, ex);
	return ::CAEN_FELib_Disabled;
}
catch (const ex::runtime_error& ex) {
	store_and_log(func, "generic runtime error"_sv, ex);
	return ::CAEN_FELib_InternalError;
}
catch (const std::exception& ex) {
	store_and_log(func, "generic error"_sv, ex);
	return ::CAEN_FELib_GenericError;
}
catch (...) {
	store_and_log(func, "unknown exception type"_sv);
	return ::CAEN_FELib_GenericError;
}

} // namespace last_error

} // namespace dig2

} // namespace caen
