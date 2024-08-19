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
*	\file		dll.hpp
*	\brief		Load library utils (Boost.DLL would be fine but, until 1.70, it required Boost.Filesystem that is not header-only)
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_DLL_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_DLL_HPP_

#include <stdexcept>
#include <string>

#include <spdlog/fmt/fmt.h>

#include <boost/predef/os.h>

#if BOOST_OS_WINDOWS
#include <system_error>
#include <boost/winapi/get_last_error.hpp>
#include <boost/winapi/dll.hpp>
#else
#include <dlfcn.h>
#endif

#include "string_view.hpp"

namespace caen {

namespace dll {

using namespace std::literals;

namespace detail {
#if BOOST_OS_WINDOWS
inline std::string windows_get_last_error() {
	const auto id = boost::winapi::GetLastError();
	const auto ec = std::system_category().default_error_condition(id);
	return fmt::format("{} error: {} ({})", ec.category().name(), ec.message(), ec.value());
}
#endif
} // namespace detail

struct dll {

#if BOOST_OS_WINDOWS
	using handle_t = boost::winapi::HMODULE_;
#else
	using handle_t = void*;
#endif

	dll(caen::string_view name)
	: _path(get_library_name(name))
	, _h{load_library(_path)} {}

	~dll() try {
		close_library(_h);
	} catch (...) {
		// suppress exception
		return;
	}

	template <typename FunctionType>
	FunctionType f(caen::string_view api_name) const {
		FunctionType p;
#if BOOST_OS_WINDOWS
		p = reinterpret_cast<FunctionType>(boost::winapi::get_proc_address(_h, api_name.data()));
		if (p == nullptr)
			throw std::runtime_error(fmt::format("GetProcAddress failed: {}", detail::windows_get_last_error()));
#else
		// error detection based on https://linux.die.net/man/3/dlsym
		::dlerror();
		p = reinterpret_cast<FunctionType>(::dlsym(_h, api_name.data()));
		const auto msg = ::dlerror();
		if (msg != nullptr)
			throw std::runtime_error(fmt::format("dlsym failed: {}", msg));
#endif
		return p;
	}

private:

	static void close_library(handle_t handle) {
#if BOOST_OS_WINDOWS
		if (boost::winapi::FreeLibrary(handle) == 0)
			throw std::runtime_error(fmt::format("FreeLibrary failed: {}", detail::windows_get_last_error()));
#else
		if (::dlclose(handle) != 0)
			throw std::runtime_error(fmt::format("dlclose failed: {}", ::dlerror()));
#endif
	}

	static handle_t load_library(caen::string_view library_name) {
		handle_t p;
#if BOOST_OS_WINDOWS
		p = boost::winapi::load_library(library_name.data());
		if (p == nullptr)
			throw std::runtime_error(fmt::format("LoadLibrary failed: {}", detail::windows_get_last_error()));
#else
		p = ::dlopen(library_name.data(), RTLD_NOW);
		if (p == nullptr)
			throw std::runtime_error(fmt::format("dlopen failed: {}", ::dlerror()));
#endif
		return p;
	}

	static std::string get_library_name(caen::string_view name) {
#if BOOST_OS_WINDOWS
		return fmt::format("{}.dll", name);
#else
		return fmt::format("lib{}.so", name);
#endif
	}

	const std::string _path;
	const handle_t _h;
};

} // namespace dll

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_DLL_HPP_ */
