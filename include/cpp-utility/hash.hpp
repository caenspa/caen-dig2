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
#include <limits>
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
		return hash(Impl::offset, data);
	}
	// base operator, no dereferences if size == 0 (null terminators processed if found)
	template <typename CharT>
	constexpr UIntT operator()(const CharT* data, std::size_t size) const noexcept {
		return hash(Impl::offset, data, size);
	}
	// base operator for nullptr, no dereferences if size == 0
	constexpr UIntT operator()(std::nullptr_t data, std::size_t size) const noexcept {
		return operator()<char>(data, size);
	}
	// base operator, no dereferences if begin == end (null terminators processed if found)
	template <typename It>
	constexpr UIntT operator()(It begin, It end) const {
		return hash(Impl::offset, begin, end);
	}
	// operator wrapper for containers, strings and string views (null terminators processed if found)
	// SFINAE required to select `const CharT*` overload when argument is `CharT*` or `CharT[]`
	template <typename T, std::enable_if_t<!(std::is_pointer<T>::value || std::is_array<T>::value), int> = 0>
	constexpr UIntT operator()(const T& c) const {
		return operator()(std::begin(c), std::end(c));
	}
private:
	// convert character types to unsigned integer types without sign extension
	template <typename CharT>
	static constexpr UIntT safe_char_hash(UIntT value, CharT data) noexcept {
		return Impl::char_hash(value, static_cast<UIntT>(static_cast<std::make_unsigned_t<CharT>>(data)));
	}
	// hash function that stops just before the first null terminator
	template <typename CharT>
	static constexpr UIntT hash(UIntT value, const CharT* data) noexcept {
		while (*data != '\0')
			value = safe_char_hash(value, *data++);
		return value;
	}
	// hash function, no dereferences if size == 0 (null terminators processed if found)
	template <typename CharT>
	static constexpr UIntT hash(UIntT value, const CharT* data, std::size_t size) noexcept {
		for (; size != 0; --size)
			value = safe_char_hash(value, *data++);
		return value;
	}
	// hash function, no dereferences if begin == end (null terminators processed if found)
	template <typename It>
	static constexpr UIntT hash(UIntT value, It begin, It end) {
		while (begin != end)
			value = safe_char_hash(value, *begin++);
		return value;
	}
};

/**
 * @brief Hash using xor followed by product and addition.
 *
 * Implemented using `prime * (value ^ data) + bias`.
 */
template <typename UIntT, UIntT Prime, UIntT Offset = UIntT{}, UIntT Bias = UIntT{}>
struct xor_product_impl : base_hash_generator<xor_product_impl<UIntT, Prime, Offset, Bias>, UIntT> {
	static constexpr UIntT offset{Offset};
	static constexpr UIntT prime{Prime};
	static constexpr UIntT bias{Bias};
	static constexpr UIntT char_hash(UIntT value, UIntT data) noexcept {
		return prime * (value ^ data) + bias;
	}
};

/**
 * @brief Hash using product followed by xor.
 *
 * Implemented using `(prime * value) ^ data`.
 */
template <typename UIntT, UIntT Prime, UIntT Offset = UIntT{}>
struct product_xor_impl : base_hash_generator<product_xor_impl<UIntT, Prime, Offset>, UIntT> {
	static constexpr UIntT offset{Offset};
	static constexpr UIntT prime{Prime};
	static constexpr UIntT char_hash(UIntT value, UIntT data) noexcept {
		return (prime * value) ^ data;
	}
};

/**
 * @brief Hash using product followed by sum.
 *
 * Implemented using `(prime * value) + data`.
 */
template <typename UIntT, UIntT Prime, UIntT Offset = UIntT{}>
struct product_sum_impl : base_hash_generator<product_sum_impl<UIntT, Prime, Offset>, UIntT> {
	static constexpr UIntT offset{Offset};
	static constexpr UIntT prime{Prime};
	static constexpr UIntT char_hash(UIntT value, UIntT data) noexcept {
		return (prime * value) + data;
	}
};

/**
 * @brief Hash using PJW / ELF hash.
 *
 * Implemented using shift, xor and mask.
 */
template <typename UIntT>
struct pjw_impl : base_hash_generator<pjw_impl<UIntT>, UIntT> {
	static_assert(std::numeric_limits<UIntT>::digits % 8 == 0, "UIntT must have a bit width divisible by 8");
	static constexpr UIntT offset{0};
	static constexpr UIntT shift{std::numeric_limits<UIntT>::digits / 8};
	static constexpr UIntT high_shift{std::numeric_limits<UIntT>::digits - shift};
	static constexpr UIntT fold_shift{std::numeric_limits<UIntT>::digits * 3 / 4};
	static constexpr UIntT high_mask{std::numeric_limits<UIntT>::max() << high_shift};
	static constexpr UIntT char_hash(UIntT value, UIntT data) noexcept {
		value <<= shift;
		value += data;
		const UIntT high = value & high_mask;
		if (high != 0) {
			value ^= (high >> fold_shift);
			value &= ~high;
		}
		return value;
	}
};

/**
* @defgroup HashAlgorithms Hash algorithm
 * @brief Hash algorithms implemented in this header.
* 
 * Definition of the known non-cryptographic hash algorithms that fit the current
 *   header-only utility layer.
* 
* @{ */
using fnv0_32 = product_xor_impl<std::uint32_t, 0x1000193>;								//!< 32-bit FNV-0. @warning Not good, used only to compute FNV-1 offset basis.
using fnv0_64 = product_xor_impl<std::uint64_t, 0x100000001b3>;							//!< 64-bit FNV-0. @warning Not good, used only to compute FNV-1 offset basis.
using fnv1_32 = product_xor_impl<std::uint32_t, fnv0_32::prime, 0x811c9dc5>;			//!< 32-bit FNV-1.
using fnv1_64 = product_xor_impl<std::uint64_t, fnv0_64::prime, 0xcbf29ce484222325>;	//!< 64-bit FNV-1.
using fnv1a_32 = xor_product_impl<std::uint32_t, fnv0_32::prime, fnv1_32::offset>;		//!< 32-bit FNV-1a.
using fnv1a_64 = xor_product_impl<std::uint64_t, fnv0_64::prime, fnv1_64::offset>;		//!< 64-bit FNV-1a.
using pearson = xor_product_impl<std::uint8_t, 0xa7, 0x0, 0xd>;							//!< 8-bit Pearson.
using djb2 = product_sum_impl<std::uint32_t, 0x21, 0x1505>;								//!< 32-bit DJB2.
using djb2a = product_xor_impl<std::uint32_t, djb2::prime, djb2::offset>;				//!< 32-bit DJB2a.
using sdbm = product_sum_impl<std::uint32_t, 0x1003f>;									//!< 32-bit SDBM.
using bkdr = product_sum_impl<std::uint32_t, 0x83>;										//!< 32-bit BKDR.
using lose_lose = product_sum_impl<std::uint32_t, 0x1>;									//!< 32-bit lose-lose from K&R (1st ed). @warning Extremely simple, terrible hashing.
using java = product_sum_impl<std::uint32_t, 0x1f>;										//!< 32-bit Java String hashCode.
using larson = product_sum_impl<std::uint32_t, 0x65>;									//!< 32-bit Paul Larson.
using pjw_32 = pjw_impl<std::uint32_t>;													//!< 32-bit PJW / ELF.
using pjw_64 = pjw_impl<std::uint64_t>;													//!< 64-bit PJW / ELF.
/** @} */

namespace sanity_checks {

namespace detail {

template <typename... Args>
constexpr bool hello_word_consistency(Args&&... args) noexcept {
	// sanity checks with hello world of CharT type
	bool ret{true};
	ret &= (fnv1_32{}(std::forward<Args>(args)...) == 0x548da96f);
	ret &= (fnv1_64{}(std::forward<Args>(args)...) == 0x7dcf62cdb1910e6f);
	ret &= (fnv1a_32{}(std::forward<Args>(args)...) == 0xd58b3fa7);
	ret &= (fnv1a_64{}(std::forward<Args>(args)...) == 0x779a65e7023cd2e7);
	ret &= (pearson{}(std::forward<Args>(args)...) == 0x19);
	ret &= (djb2{}(std::forward<Args>(args)...) == 0x3551c8c1);
	ret &= (djb2a{}(std::forward<Args>(args)...) == 0xf8c65345);
	ret &= (sdbm{}(std::forward<Args>(args)...) == 0x19ae84c4);
	ret &= (bkdr{}(std::forward<Args>(args)...) == 0x4e195644);
	ret &= (lose_lose{}(std::forward<Args>(args)...) == 0x45c);
	ret &= (java{}(std::forward<Args>(args)...) == 0x6aefe2c4);
	ret &= (larson{}(std::forward<Args>(args)...) == 0x496f954c);
	ret &= (pjw_32{}(std::forward<Args>(args)...) == 0x0114ac14);
	ret &= (pjw_64{}(std::forward<Args>(args)...) == 0x006f201f0a1e0064);
	return ret;
}

} // namespace detail

constexpr bool test_hash_utils_1() noexcept {
	// sanity checks for FNV-1 offset basis
	// see http://www.isthe.com/chongo/tech/comp/fnv/index.html
	bool ret{true};
	const char chongo[] = R"(chongo <Landon Curt Noll> /\../\)";
	ret &= (fnv0_32{}(chongo) == fnv1_32::offset);
	ret &= (fnv0_64{}(chongo) == fnv1_64::offset);
	return ret;
}
constexpr bool test_hash_utils_2() noexcept {
	// sanity checks with strings known to provide null hash
	// see http://www.isthe.com/chongo/tech/comp/fnv/index.html
	bool ret{true};
	ret &= (!fnv1_32{}("\aevJ;"));
	ret &= (!fnv1_32{}("ba,1q"));
	ret &= (!fnv1_32{}("T u{["));
	ret &= (!fnv1_32{}("03SB["));
	ret &= (!fnv1_32{}("3d7A5K"));
	ret &= (!fnv1_32{}("9q6Mq3"));
	ret &= (!fnv1_32{}("HHYSLE"));
	ret &= (!fnv1_32{}("TrLdZ1"));
	ret &= (!fnv1_32{}("VL3BqC"));
	ret &= (!fnv1_32{}("WAd2`W"));
	ret &= (!fnv1_32{}("YUo19l"));
	ret &= (!fnv1_32{}("Yf6hP6"));
	ret &= (!fnv1_32{}("hIrjFj"));
	ret &= (!fnv1_32{}("n`bv3R"));
	ret &= (!fnv1_32{}("plIzl`"));
	ret &= (!fnv1_32{}("uMk`9Q"));
	ret &= (!fnv1_32{}("ysopHl"));
	ret &= (!fnv1_32{}("zcIkCe"));
	ret &= (!fnv1_64{}("!v)EYwYVk&"));
	ret &= (!fnv1_64{}("Mt5Kexny31n"));
	ret &= (!fnv1_64{}("OjSHjikPNYV"));
	ret &= (!fnv1_64{}("YIA9YWMOARX"));
	ret &= (!fnv1a_32{}("+!=yG"));
	ret &= (!fnv1a_32{}("68m* "));
	ret &= (!fnv1a_32{}("eSN.1"));
	ret &= (!fnv1a_32{}("Y\b&`b"));
	ret &= (!fnv1a_32{}("3pjNqM"));
	ret &= (!fnv1a_32{}("5R0Lg7"));
	ret &= (!fnv1a_32{}("7oE486"));
	ret &= (!fnv1a_32{}("BR42qf"));
	ret &= (!fnv1a_32{}("FouBSr"));
	ret &= (!fnv1a_32{}("GkkzFD"));
	ret &= (!fnv1a_32{}("HVGZq9"));
	ret &= (!fnv1a_32{}("IwPSdT"));
	ret &= (!fnv1a_32{}("`nrY3G"));
	ret &= (!fnv1a_32{}("qzs0UD"));
	ret &= (!fnv1a_32{}("sXbssr"));
	ret &= (!fnv1a_32{}("uh4tSI"));
	ret &= (!fnv1a_64{}("!0IC=VloaY"));
	ret &= (!fnv1a_64{}("=. hx\"iX<;"));
	ret &= (!fnv1a_64{}("QvXtM>@Fp%"));
	ret &= (!fnv1a_64{}("_\"kWk=-v$c"));
	ret &= (!fnv1a_64{}("77kepQFQ8Kl"));
	ret &= (!java{}("pollinating sandboxes"));
	ret &= (!java{}("Airlia unhallow"));
	ret &= (!java{}("amusement & hemophilias"));
	ret &= (!java{}("schoolworks = perversive"));
	ret &= (!java{}("electrolysissweeteners.net"));
	ret &= (!java{}("constitutionalunstableness.net"));
	ret &= (!java{}("grinnerslaphappier.org"));
	ret &= (!java{}("BLEACHINGFEMININELY.NET"));
	ret &= (!java{}("WWW.BUMRACEGOERS.ORG"));
	ret &= (!java{}("WWW.RACCOONPRUDENTIALS.NET"));
	ret &= (!java{}("Microcomputers: the unredeemed lollipop..."));
	ret &= (!java{}("Incentively, my dear, I don't tessellate a derangement."));
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
	ret &= (pearson{}("of") == pearson{}("up"));
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
	ret &= (java{}("Siblings") == java{}("Teheran"));
	ret &= (java{}("misused") == java{}("horsemints"));
	ret &= (java{}("isohel") == java{}("epistolaries"));
	ret &= (java{}("righto") == java{}("buzzards"));
	ret &= (java{}("hierarch") == java{}("crinolines"));
	ret &= (java{}("inwork") == java{}("hypercatalexes"));
	ret &= (java{}("wainages") == java{}("presentencing"));
	ret &= (java{}("trichothecenes") == java{}("locular"));
	ret &= (java{}("pomatoes") == java{}("eructation"));
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
	ret &= (fnv0_32{}(empty) == fnv0_32::offset);
	ret &= (fnv0_64{}(empty) == fnv0_64::offset);
	ret &= (fnv1_32{}(empty) == fnv1_32::offset);
	ret &= (fnv1_64{}(empty) == fnv1_64::offset);
	ret &= (fnv1a_32{}(empty) == fnv1a_32::offset);
	ret &= (fnv1a_64{}(empty) == fnv1a_64::offset);
	ret &= (pearson{}(empty) == pearson::offset);
	ret &= (djb2{}(empty) == djb2::offset);
	ret &= (djb2a{}(empty) == djb2a::offset);
	ret &= (sdbm{}(empty) == sdbm::offset);
	ret &= (lose_lose{}(empty) == lose_lose::offset);
	ret &= (java{}(empty) == java::offset);
	ret &= (larson{}(empty) == larson::offset);
	ret &= (pjw_32{}(empty) == pjw_32::offset);
	ret &= (pjw_64{}(empty) == pjw_64::offset);
	return ret;
}
constexpr bool test_hash_utils_6() noexcept {
	// sanity checks for null pointers with zero size (no dereferences using hash)
	bool ret{true};
	ret &= (fnv0_32{}(nullptr, 0) == fnv0_32::offset);
	ret &= (fnv0_64{}(nullptr, 0) == fnv0_64::offset);
	ret &= (fnv1_32{}(nullptr, 0) == fnv1_32::offset);
	ret &= (fnv1_64{}(nullptr, 0) == fnv1_64::offset);
	ret &= (fnv1a_32{}(nullptr, 0) == fnv1a_32::offset);
	ret &= (fnv1a_64{}(nullptr, 0) == fnv1a_64::offset);
	ret &= (pearson{}(nullptr, 0) == pearson::offset);
	ret &= (djb2{}(nullptr, 0) == djb2::offset);
	ret &= (djb2a{}(nullptr, 0) == djb2a::offset);
	ret &= (sdbm{}(nullptr, 0) == sdbm::offset);
	ret &= (lose_lose{}(nullptr, 0) == lose_lose::offset);
	ret &= (java{}(nullptr, 0) == java::offset);
	ret &= (larson{}(nullptr, 0) == larson::offset);
	ret &= (pjw_32{}(nullptr, 0) == pjw_32::offset);
	ret &= (pjw_64{}(nullptr, 0) == pjw_64::offset);
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
using detail::pearson;
using detail::djb2;
using detail::djb2a;
using detail::sdbm;
using detail::lose_lose;
using detail::java;
using detail::larson;
using detail::pjw_32;
using detail::pjw_64;

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
