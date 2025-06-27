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
*	\file		cpu.hpp
*	\brief		Utilities to set CPU affinity
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_CPU_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_CPU_HPP_

#include <system_error>

#include <boost/predef/os.h>

#if BOOST_OS_WINDOWS
// Unfortunately, Boost.WinAPI does not declare SetThreadAffinityMask.
#include <Windows.h>
#include "bit.hpp"
#elif BOOST_OS_MACOS
#ifndef _DARWIN_C_SOURCE
#error _DARWIN_C_SOURCE must be defined
#endif
#include <mach/kern_return.h>
#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#else
#ifndef _GNU_SOURCE
#error _GNU_SOURCE must be defined
#endif
#include <pthread.h>
#include <sched.h>
#endif

namespace caen {

namespace cpu {

inline void set_current_thread_affinity(int cpu_id) {
#if BOOST_OS_WINDOWS
	const auto current_thread = ::GetCurrentThread();
	const auto mask = caen::bit::get_bit<::DWORD_PTR>(cpu_id);
	const auto r = ::SetThreadAffinityMask(current_thread, mask);
	if (r == 0)
		throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "SetThreadAffinityMask");
#elif BOOST_OS_MACOS
	::thread_affinity_policy_data_t policy = { cpu_id };
	const auto current_thread = ::mach_thread_self();
	const auto r = ::thread_policy_set(current_thread, THREAD_AFFINITY_POLICY, reinterpret_cast<::thread_policy_t>(&policy), THREAD_AFFINITY_POLICY_COUNT);
	if (r != KERN_SUCCESS)
		throw std::system_error(r, std::system_category(), "pthread_setaffinity_np");
#else
	::cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_id, &cpuset);
	const auto current_thread = ::pthread_self();
	const auto r = ::pthread_setaffinity_np(current_thread, sizeof(cpuset), &cpuset);
	if (r != 0)
		throw std::system_error(r, std::system_category(), "pthread_setaffinity_np");
#endif
}

} // namespace cpu

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_CPU_HPP_ */
