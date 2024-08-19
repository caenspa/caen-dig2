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
*	\file		hash.hpp
*	\brief		Compile time string hash
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_HASH_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_HASH_HPP_

#include <cstdint>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include <boost/predef/compiler.h>
#include <boost/predef/other/workaround.h>

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_MSVC, <, 19, 24, 0)
/*
 * Visual Studio emits a false warning; fixed on MSVC2019 16.4 (_MSC_VER == 1924).
 * See https://developercommunity.visualstudio.com/t/unsigned-integer-overflows-in-constexpr-functionsa/211134
 */
#pragma warning(push)
#pragma warning(disable: 4307)
#endif

namespace caen {

namespace hash {

namespace detail {

/**
 * @brief Algorithm agnostic class to be uses using CRTP idiom.
 */
template <typename Impl, typename UIntT>
struct base_hash_generator {
	// requirements
	static_assert(std::is_unsigned<UIntT>::value, "UIntT must be an unsigned integral type");
	// base operator that stops just before the first null terminator
	template <typename CharT>
	constexpr UIntT operator()(const CharT* data) const noexcept {
		return hash(Impl::offset_basis, data);
	}
	// base operator, no dereferences if size == 0 (null terminators processed if found)
	template <typename CharT>
	constexpr UIntT operator()(const CharT* data, std::size_t size) const noexcept {
		return hash(Impl::offset_basis, data, size);
	}
	// base operator for nullptr, no dereferences if size == 0
	constexpr UIntT operator()(std::nullptr_t data, std::size_t size) const noexcept {
		return operator()<char>(data, size);
	}
	// base operator, no dereferences if begin == end (null terminators processed if found)
	template <typename It>
	constexpr UIntT operator()(It begin, It end) const {
		return hash(Impl::offset_basis, begin, end);
	}
	// operator wrapper for containers, strings and string views (null terminators processed if found)
	// SFINAE required to select `const CharT*` overload when argument is `CharT*` or `CharT[]`
	template <typename T, std::enable_if_t<!(std::is_pointer<T>::value || std::is_array<T>::value), int> = 0>
	constexpr UIntT operator()(const T& c) const {
		return operator()(std::begin(c), std::end(c));
	}
private:
	// hash function that stops just before the first null terminator
	template <typename CharT>
	constexpr UIntT hash(UIntT value, const CharT* data) const noexcept {
		while (*data != '\0')
			value = Impl::char_hash(value, *data++);
		return value;
	}
	// hash function, no dereferences if size == 0 (null terminators processed if found)
	template <typename CharT>
	constexpr UIntT hash(UIntT value, const CharT* data, std::size_t size) const noexcept {
		for (; size != 0; --size)
			value = Impl::char_hash(value, *data++);
		return value;
	}
	// hash function, no dereferences if begin == end (null terminators processed if found)
	template <typename It>
	constexpr UIntT hash(UIntT value, It begin, It end) const {
		while (begin != end)
			value = Impl::char_hash(value, *begin++);
		return value;
	}
};

/**
 * @brief Hash using xor followed by product.
 * 
 * Implemented using `prime * (value ^ data)`.
 */
template <typename UIntT, UIntT OffsetBasis, UIntT Prime>
struct xor_product_impl : base_hash_generator<xor_product_impl<UIntT, OffsetBasis, Prime>, UIntT> {
	static constexpr UIntT offset_basis{OffsetBasis};
	static constexpr UIntT prime{Prime};
	template <typename CharT> static constexpr UIntT char_hash(UIntT value, CharT data) noexcept { return prime * (value ^ data); }
};

/**
 * @brief Hash using product followed by xor.
 *
 * Implemented using `(prime * value) ^ data`.
 */
template <typename UIntT, UIntT OffsetBasis, UIntT Prime>
struct product_xor_impl : base_hash_generator<product_xor_impl<UIntT, OffsetBasis, Prime>, UIntT> {
	static constexpr UIntT offset_basis{OffsetBasis};
	static constexpr UIntT prime{Prime};
	template <typename CharT> static constexpr UIntT char_hash(UIntT value, CharT data) noexcept { return (prime * value) ^ data; }
};

/**
 * @brief Hash using product followed by sum.
 *
 * Implemented using `(prime * value) + data`.
 */
template <typename UIntT, UIntT OffsetBasis, UIntT Prime>
struct product_sum_impl : base_hash_generator<product_sum_impl<UIntT, OffsetBasis, Prime>, UIntT> {
	static constexpr UIntT offset_basis{OffsetBasis};
	static constexpr UIntT prime{Prime};
	template <typename CharT> static constexpr UIntT char_hash(UIntT value, CharT data) noexcept { return (prime * value) + data; }
};

/**
* @defgroup HashAlgorithms Hash algorithm
* @brief Hash algorithms that can be easily implemented with xor+product, product+xor and product+sum.
* 
* Definition of every known and documented hash algorithms that can be implemented
*   using one of @ref xor_product_impl, @ref product_xor_impl and @ref product_sum_impl.
* 
* @{ */
using fnv0_32 = product_xor_impl<std::uint32_t, std::uint32_t{0x0}, std::uint32_t{0x1000193}>;		//!< 32-bit FNV-0. @warning Not good, used only to compute FNV-1 offset basis.
using fnv0_64 = product_xor_impl<std::uint64_t, std::uint64_t{0x0}, std::uint64_t{0x100000001b3}>;	//!< 64-bit FNV-0. @warning Not good, used only to compute FNV-1 offset basis.
using fnv1_32 = product_xor_impl<std::uint32_t, std::uint32_t{0x811c9dc5}, fnv0_32::prime>;			//!< 32-bit FNV-1.
using fnv1_64 = product_xor_impl<std::uint64_t, std::uint64_t{0xcbf29ce484222325}, fnv0_64::prime>;	//!< 64-bit FNV-1.
using fnv1a_32 = xor_product_impl<std::uint32_t, fnv1_32::offset_basis, fnv0_32::prime>;			//!< 32-bit FNV-1a.
using fnv1a_64 = xor_product_impl<std::uint64_t, fnv1_64::offset_basis, fnv0_64::prime>;			//!< 64-bit FNV-1a.
using djb2 = product_sum_impl<std::uint32_t, std::uint32_t{0x1505}, std::uint32_t{0x21}>;			//!< 32-bit DJB2.
using djb2a = product_xor_impl<std::uint32_t, djb2::offset_basis, djb2::prime>;						//!< 32-bit DJB2a.
using sdbm = product_sum_impl<std::uint32_t, std::uint32_t{0x0}, std::uint32_t{0x1003f}>;			//!< 32-bit SDBM hash algorithm.
using lose_lose = product_sum_impl<std::uint32_t, std::uint32_t{0x0}, std::uint32_t{0x1}>;			//!< 32-bit lose-lose from K&R (1st ed). @warning Extremely simple, terrible hashing.
/** @} */

namespace sanity_checks {

namespace detail {

template <typename... Args>
constexpr bool hello_word_consistency(Args&&... args) noexcept {
	// sanity checks with hello world of CharT type
	bool ret{true};
	ret &= (fnv1_32{}(std::forward<Args>(args)...) == std::uint32_t{0x548da96f});
	ret &= (fnv1_64{}(std::forward<Args>(args)...) == std::uint64_t{0x7dcf62cdb1910e6f});
	ret &= (fnv1a_32{}(std::forward<Args>(args)...) == std::uint32_t{0xd58b3fa7});
	ret &= (fnv1a_64{}(std::forward<Args>(args)...) == std::uint64_t{0x779a65e7023cd2e7});
	ret &= (djb2{}(std::forward<Args>(args)...) == std::uint32_t{0x3551c8c1});
	ret &= (djb2a{}(std::forward<Args>(args)...) == std::uint32_t{0xf8c65345});
	ret &= (sdbm{}(std::forward<Args>(args)...) == std::uint32_t{0x19ae84c4});
	ret &= (lose_lose{}(std::forward<Args>(args)...) == std::uint32_t{0x45c});
	return ret;
}

} // namespace detail

constexpr bool test_hash_utils_1() noexcept {
	// sanity checks for FNV-1 offset basis
	// see http://www.isthe.com/chongo/tech/comp/fnv/index.html
	bool ret{true};
	const char chongo[] = R"(chongo <Landon Curt Noll> /\../\)";
	ret &= (fnv0_32{}(chongo) == fnv1_32::offset_basis);
	ret &= (fnv0_64{}(chongo) == fnv1_64::offset_basis);
	return ret;
}
constexpr bool test_hash_utils_2() noexcept {
	// sanity checks with strings known to provide null hash
	// see http://www.isthe.com/chongo/tech/comp/fnv/index.html
	bool ret{true};
	ret &= (!fnv1_32{}("ba,1q"));
	ret &= (!fnv1_32{}("T u{["));
	ret &= (!fnv1_32{}("03SB["));
	ret &= (!fnv1_64{}("!v)EYwYVk&"));
	ret &= (!fnv1_64{}("Mt5Kexny31n"));
	ret &= (!fnv1_64{}("OjSHjikPNYV"));
	ret &= (!fnv1_64{}("YIA9YWMOARX"));
	ret &= (!fnv1a_32{}("eSN.1"));
	ret &= (!fnv1a_32{}("68m* "));
	ret &= (!fnv1a_32{}("+!=yG"));
	ret &= (!fnv1a_64{}("!0IC=VloaY"));
	ret &= (!fnv1a_64{}("QvXtM>@Fp%"));
	ret &= (!fnv1a_64{}("77kepQFQ8Kl"));
	return ret;
}
constexpr bool test_hash_utils_3() noexcept {
	// sanity checks for known collisions
	// see https://softwareengineering.stackexchange.com/q/49550
	bool ret{true};
	ret &= (fnv1_32{}("creamwove") == fnv1_32{}("quists"));
	ret &= (fnv1a_32{}("costarring") == fnv1a_32{}("liquid"));
	ret &= (fnv1a_32{}("declinate") == fnv1a_32{}("macallums"));
	ret &= (fnv1a_32{}("altarage") == fnv1a_32{}("zinke"));
	ret &= (djb2{}("ar") == djb2{}("c0"));
	ret &= (djb2{}("hetairas") == djb2{}("mentioner"));
	ret &= (djb2{}("heliotropes") == djb2{}("neurospora"));
	ret &= (djb2{}("depravement") == djb2{}("serafins"));
	ret &= (djb2{}("stylist") == djb2{}("subgenera"));
	ret &= (djb2{}("joyful") == djb2{}("synaphea"));
	ret &= (djb2{}("redescribed") == djb2{}("urites"));
	ret &= (djb2{}("dram") == djb2{}("vivency"));
	ret &= (djb2{}("appling") == djb2{}("bedaggle"));
	ret &= (djb2{}("broadened") == djb2{}("kilohm"));
	ret &= (djb2a{}("haggadot") == djb2a{}("loathsomenesses"));
	ret &= (djb2a{}("playwright") == djb2a{}("snush"));
	ret &= (djb2a{}("adorablenesses") == djb2a{}("rentability"));
	ret &= (djb2a{}("treponematoses") == djb2a{}("waterbeds"));
	return ret;
}
constexpr bool test_hash_utils_4() noexcept {
	// sanity checks with hello world of various character types
	char non_const_array[] = "hello world";
	bool ret{true};
	ret &= (detail::hello_word_consistency("hello world"));
	ret &= (detail::hello_word_consistency(u8"hello world"));
	ret &= (detail::hello_word_consistency(L"hello world"));
	ret &= (detail::hello_word_consistency(u"hello world"));
	ret &= (detail::hello_word_consistency(U"hello world"));
	ret &= (detail::hello_word_consistency(non_const_array));
	ret &= (detail::hello_word_consistency(static_cast<char*>(non_const_array)));
	ret &= (detail::hello_word_consistency(non_const_array, 11));
	ret &= (!detail::hello_word_consistency(non_const_array, 12));
	ret &= (!detail::hello_word_consistency("hello world long"));
	ret &= (!detail::hello_word_consistency("hello world long", 10));
	ret &= (detail::hello_word_consistency("hello world long", 11));
	ret &= (!detail::hello_word_consistency("hello world long", 12));
	return ret;
}
constexpr bool test_hash_utils_5() noexcept {
	// sanity checks for empty strings
	bool ret{true};
	const char empty[] = "";
	ret &= (fnv0_32{}(empty) == fnv0_32::offset_basis);
	ret &= (fnv0_64{}(empty) == fnv0_64::offset_basis);
	ret &= (fnv1_32{}(empty) == fnv1_32::offset_basis);
	ret &= (fnv1_64{}(empty) == fnv1_64::offset_basis);
	ret &= (fnv1a_32{}(empty) == fnv1a_32::offset_basis);
	ret &= (fnv1a_64{}(empty) == fnv1a_64::offset_basis);
	ret &= (djb2{}(empty) == djb2::offset_basis);
	ret &= (djb2a{}(empty) == djb2a::offset_basis);
	ret &= (sdbm{}(empty) == sdbm::offset_basis);
	ret &= (lose_lose{}(empty) == lose_lose::offset_basis);
	return ret;
}
constexpr bool test_hash_utils_6() noexcept {
	// sanity checks for null pointers with zero size (no dereferences using hash)
	bool ret{true};
	ret &= (fnv0_32{}(nullptr, 0) == fnv0_32::offset_basis);
	ret &= (fnv0_64{}(nullptr, 0) == fnv0_64::offset_basis);
	ret &= (fnv1_32{}(nullptr, 0) == fnv1_32::offset_basis);
	ret &= (fnv1_64{}(nullptr, 0) == fnv1_64::offset_basis);
	ret &= (fnv1a_32{}(nullptr, 0) == fnv1a_32::offset_basis);
	ret &= (fnv1a_64{}(nullptr, 0) == fnv1a_64::offset_basis);
	ret &= (djb2{}(nullptr, 0) == djb2::offset_basis);
	ret &= (djb2a{}(nullptr, 0) == djb2a::offset_basis);
	ret &= (sdbm{}(nullptr, 0) == sdbm::offset_basis);
	ret &= (lose_lose{}(nullptr, 0) == lose_lose::offset_basis);
	return ret;
}

static_assert(test_hash_utils_1(), "inconsistent hash implementation");
static_assert(test_hash_utils_2(), "inconsistent hash implementation");
static_assert(test_hash_utils_3(), "inconsistent hash implementation");
static_assert(test_hash_utils_4(), "inconsistent hash implementation");
static_assert(test_hash_utils_5(), "inconsistent hash implementation");
static_assert(test_hash_utils_6(), "inconsistent hash implementation");

} // namespace sanity_checks

} // namespace detail

// imported into hash namespace
using detail::fnv1_32;
using detail::fnv1_64;
using detail::fnv1a_32;
using detail::fnv1a_64;
using detail::djb2;
using detail::djb2a;
using detail::sdbm;
using detail::lose_lose;

using generator = fnv1a_64;						//!< Set default hash generator for string to @ref caen::hash::detail::fnv1a_64
using default_hash_generator = generator;		//!< Legacy alias

namespace literals {

/**
 * @brief UDL to convert string literal to hash using @ref caen::hash::generator.
 * 
 * @return hash of input string
 */
constexpr auto operator""_h(const char* data, std::size_t size) noexcept {
	return generator{}(data, size);
}

/**
 * @brief UDL to convert wide string literals to hash using @ref caen::hash::generator.
 *
 * @return hash of input string
 */
constexpr auto operator""_h(const wchar_t* data, std::size_t size) noexcept {
	return generator{}(data, size);
}

namespace sanity_checks {

namespace detail {

enum class switch_output { a, b, other };

constexpr switch_output hash_utils_switch(const char *str) noexcept {
	switch (generator{}(str)) {
	case "a"_h:	return switch_output::a;
	case "b"_h:	return switch_output::b;
	default:	return switch_output::other;
	}
}

} // namespace detail

constexpr bool test_hash_utils_literals() noexcept {
	bool ret{true};
	ret &= (detail::hash_utils_switch("a") == detail::switch_output::a);
	ret &= (detail::hash_utils_switch("b") == detail::switch_output::b);
	ret &= (detail::hash_utils_switch("c") == detail::switch_output::other);
	ret &= ("hello world"_h == generator{}("hello world"));
	ret &= (L"hello world"_h == generator{}(L"hello world"));
	ret &= (""_h == generator{}(""));
	ret &= (L""_h == generator{}(L""));
	return ret;
}

static_assert(test_hash_utils_literals(), "inconsistent literal implementation");

} // namespace sanity_checks

} // namespace literals

} // namespace hash

} // namespace caen

#if BOOST_PREDEF_WORKAROUND(BOOST_COMP_MSVC, <, 19, 24, 0)
#pragma warning(pop)
#endif

#endif /* CAEN_INCLUDE_CPP_UTILITY_HASH_HPP_ */
