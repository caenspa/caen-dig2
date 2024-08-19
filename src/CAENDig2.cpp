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
*	\file		CAENDig2.cpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#include "CAENDig2.h"

#include <algorithm>

#include <boost/config.hpp>
#include <boost/predef/os.h>

#include "cpp-utility/is_in.hpp"
#include "cpp-utility/string.hpp"
#include "cpp-utility/string_view.hpp"
#include "api.hpp"
#include "last_error.hpp"
#include "lib_error.hpp"
#include "library_logger.hpp"

#if BOOST_OS_WINDOWS
#include <Windows.h> // DisableThreadLibraryCalls (not working including libloaderapi.h before Windows.h)
#include "cpp-utility/win32_process_terminate.hpp"
#endif

namespace lib = caen::dig2;

int CAEN_FELIB_API CAENDig2_GetLibInfo(char* jsonString, size_t size) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, jsonString)))
		throw lib::ex::invalid_argument("null");
	const auto res = lib::get_lib_info();
	caen::string::string_to_pointer_safe(jsonString, res, size);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetLibVersion(char version[16]) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, version)))
		throw lib::ex::invalid_argument("null");
	const auto res = lib::get_lib_version();
	caen::string::string_to_pointer_safe(version, res, lib::max_size::str::version);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetLastError(char description[1024]) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, description)))
		throw lib::ex::invalid_argument("null");
	auto& res = lib::last_error::instance();
	caen::string::string_to_pointer_safe(description, res, lib::max_size::str::last_error_description);
	res.clear();
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_DevicesDiscovery(char* jsonString, size_t size, int timeout) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, jsonString)))
		throw lib::ex::invalid_argument("null");
	if (BOOST_UNLIKELY(timeout < 0))
		throw lib::ex::invalid_argument("timeout must be positive");
	const auto res = lib::device_discovery(timeout);
	caen::string::string_to_pointer_safe(jsonString, res, size);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_Open(const char* url, uint32_t* handle) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, url, handle)))
		throw lib::ex::invalid_argument("null");
	lib::open(url, *handle);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_Close(uint32_t handle) try {
	lib::close(handle);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetDeviceTree(uint32_t handle, char* jsonString, size_t size) try {
	if (caen::is_in(nullptr, jsonString) && size != 0)
		throw lib::ex::invalid_argument("null and size != 0");
	const auto res = lib::get_device_tree(handle);
	const auto res_safe = caen::string_view(res).substr(0, size - 1);
	caen::string::string_to_pointer_safe(jsonString, res_safe, size);
	return static_cast<int>(res.size());
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetChildHandles(uint32_t handle, const char* path, uint32_t* handles, size_t size) try {
	// handles can be null: if size is 0 the function can be used to get the number of children (see std::snprintf)
	if (caen::is_in(nullptr, handles) && size != 0)
		throw lib::ex::invalid_argument("null and size != 0");
	const auto res = lib::get_child_handles(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path));
	std::copy_n(res.begin(), std::min(size, res.size()), handles);
	return static_cast<int>(res.size());
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetHandle(uint32_t handle, const char* path, uint32_t* pathHandle) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, pathHandle)))
		throw lib::ex::invalid_argument("null");
	*pathHandle = lib::get_handle(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path));
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetParentHandle(uint32_t handle, const char* path, uint32_t* parentHandle) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, parentHandle)))
		throw lib::ex::invalid_argument("null");
	*parentHandle = lib::get_parent_handle(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path));
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetPath(uint32_t handle, char path[256]) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, path)))
		throw lib::ex::invalid_argument("null");
	const auto res = lib::get_path(handle);
	caen::string::string_to_pointer_safe(path, res, lib::max_size::str::path);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetNodeProperties(uint32_t handle, const char* path, char name[32], CAEN_FELib_NodeType_t* type) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, name, type)))
		throw lib::ex::invalid_argument("null");
	const auto res = lib::get_node_properties(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path));
	caen::string::string_to_pointer_safe(name, res.first, lib::max_size::str::node_name);
	*type = res.second;
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetValue(uint32_t handle, const char* path, char value[256]) try {
	const auto res = lib::get_value(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path), caen::string::pointer_to_string_safe(value, lib::max_size::str::value));
	caen::string::string_to_pointer_safe(value, res, lib::max_size::str::value);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_SetValue(uint32_t handle, const char* path, const char* value) try {
	lib::set_value(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path), caen::string::pointer_to_string_safe(value, lib::max_size::str::value));
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_SendCommand(uint32_t handle, const char* path) try {
	lib::send_command(handle, caen::string::pointer_to_string_safe(path, lib::max_size::str::path));
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_GetUserRegister(uint32_t handle, uint32_t address, uint32_t* value) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, value)))
		throw lib::ex::invalid_argument("null");
	*value = lib::get_user_register(handle, address);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_SetUserRegister(uint32_t handle, uint32_t address, uint32_t value) try {
	lib::set_user_register(handle, address, value);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_SetReadDataFormat(uint32_t handle, const char* jsonString) try {
	if (BOOST_UNLIKELY(caen::is_in(nullptr, jsonString)))
		throw lib::ex::invalid_argument("null");
	lib::set_data_format(handle, jsonString);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_ReadDataV(uint32_t handle, int timeout, va_list args) try {
	// va_copy is needed here (see https://stackoverflow.com/q/60055862/3287591)
	std::va_list args_copy;
	va_copy(args_copy, args);
	// try/catch required to avoid skipping va_end if lib::read_data throws
	try {
		lib::read_data(handle, timeout, &args_copy);
	}
	catch (...) {
		va_end(args_copy);
		/*
		 * Here `throw;` would be more elegant to reuse the final catch block, but slower.
		 * Since lib::read_data is the only throwing function of CAENDig2_ReadDataV,
		 * and every exception is caught here, the final catch block is useless because
		 * cannot be reached, but is kept for consistency with the other functions.
		 */
		return handle_exception();
	}
	va_end(args_copy);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

int CAEN_FELIB_API CAENDig2_HasData(uint32_t handle, int timeout) try {
	lib::has_data(handle, timeout);
	return ::CAEN_FELib_Success;
}
catch (...) {
	return handle_exception();
}

namespace {
// perform here any library initialization.
void init_library() {
	// important: functions here should not create threads (i.e. async_log not supported)
	lib::library_logger::init();
}
// perform here any library deinitialization.
void deinit_library() {
}
} // unnamed namespace

#if BOOST_OS_WINDOWS
/**********************************************************************\
DllMain
\**********************************************************************/
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		/*
		 * DllMain documentation suggests to call DisableThreadLibraryCalls on DLL_PROCESS_ATTACH,
		 * if not linking against static CRT (and this is not the case).
		 *
		 * From DisableThreadLibraryCalls documentation:
		 * > This can be a useful optimization for multithreaded applications that have many DLLs,
		 * > frequently create and delete threads, and whose DLLs do not need these thread-level
		 * > notifications of attachment/detachment.
		 *
		 * Anyway, still from documentation:
		 * > This function does not perform any optimizations if static Thread Local Storage (TLS)
		 * > is enabled. Static TLS is enabled when using thread_local variables, __declspec(thread)
		 * > variables, or function-local static.
		 *
		 * TLS is enabled in this library because it is used at least by:
		 * - last error string (declared with `thread_local` storage duration)
		 * - spdlog (can be disabled defining `SPGLOG_NO_TLS`)
		 * - Boost.ASIO (can be disabled defining `BOOST_ASIO_DISABLE_THREADS`)
		 *
		 * However, even if it does not perform any optimization here, we keep this as memorandum.
		 *
		 * See:
		 * - https://docs.microsoft.com/en-us/windows/win32/dlls/dllmain
		 * - https://docs.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-disablethreadlibrarycalls
		 */
		::DisableThreadLibraryCalls(hinstDLL);
		init_library();
		break;
	case DLL_PROCESS_DETACH:
		if (lpReserved != nullptr) {
			/*
			 * If lpReserved is not NULL it means that the process is terminating: weird things
			 * may happen here if there were running threads. We set a global flag that
			 * can be useful on class destructors to perform some consistency checks.
			 *
			 * DllMain documentation suggests that:
			 * > In this case, it is not safe for the DLL to clean up the resources.
			 * > Instead, the DLL should allow the operating system to reclaim the memory.
			 *
			 * Alternatively, we may flush all files and loggers and terminate process here.
			 *
			 * See:
			 * - https://docs.microsoft.com/en-us/windows/win32/dlls/dllmain
			 */
			caen::win32_process_terminate::handler::get_instance().set_process_terminating();
		}
		deinit_library();
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
#else
namespace {
[[gnu::constructor]] void gnu_lib_constructor() {
	// important: functions here should not create threads (i.e. async_log not supported)
	init_library();
}
[[gnu::destructor]] void gnu_lib_destructor() {
	deinit_library();
}
} // unnamed namespace
#endif
