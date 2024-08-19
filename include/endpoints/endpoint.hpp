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
*	\file		endpoint.hpp
*	\brief
*	\author		Giovanni Cerretani, Matteo Fusco
*
******************************************************************************/

#ifndef CAEN_INCLUDE_ENDPOINTS_ENDPOINT_HPP_
#define CAEN_INCLUDE_ENDPOINTS_ENDPOINT_HPP_

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <memory>
#include <ratio>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/core/noncopyable.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>
#include <nlohmann/json.hpp>

#include "cpp-utility/args.hpp"
#include "cpp-utility/byte.hpp"
#include "cpp-utility/type_traits.hpp"
#include "cpp-utility/vector.hpp"
#include "lib_definitions.hpp"
#include "lib_error.hpp"

namespace caen {

namespace dig2 {

struct client; // forward declaration

namespace ep {

struct endpoint : private boost::noncopyable {
	
	enum class names; // to be defined in child class
	enum class types {
		UNKNOWN,
		U64,
		U32,
		U16,
		U8,
		I64,
		I32,
		I16,
		I8,
		CHAR,
		BOOL,
		SIZE_T,
		PTRDIFF_T,
		FLOAT,
		DOUBLE,
		LONG_DOUBLE,
	};

	using timeout_t = std::chrono::duration<int, std::milli>;

	endpoint(client& client, handle::internal_handle_t endpoint_server_handle);
	virtual ~endpoint();

	handle::internal_handle_t get_endpoint_server_handle() const noexcept;
	client& get_client() const noexcept; // requires #include "client.hpp" to be used

	virtual void set_data_format(const std::string &json_format) = 0;
	virtual void read_data(timeout_t timeout, std::va_list* args) = 0;
	virtual void has_data(timeout_t timeout) = 0;
	virtual void clear_data() = 0;

private:

	struct endpoint_impl; // forward declaration
	std::unique_ptr<endpoint_impl> _pimpl;

};

BOOST_STATIC_ASSERT(std::is_abstract<endpoint>::value);
BOOST_STATIC_ASSERT(std::has_virtual_destructor<endpoint>::value);

using namespace std::string_literals;

NLOHMANN_JSON_SERIALIZE_ENUM(endpoint::types, {
	{ endpoint::types::UNKNOWN,			nullptr				},
	{ endpoint::types::U64,				"U64"s				},
	{ endpoint::types::U32,				"U32"s				},
	{ endpoint::types::U16,				"U16"s				},
	{ endpoint::types::U8,				"U8"s				},
	{ endpoint::types::I64,				"I64"s				},
	{ endpoint::types::I32,				"I32"s				},
	{ endpoint::types::I16,				"I16"s				},
	{ endpoint::types::I8,				"I8"s				},
	{ endpoint::types::CHAR,			"CHAR"s				},
	{ endpoint::types::BOOL,			"BOOL"s				},
	{ endpoint::types::SIZE_T,			"SIZE_T"s			},
	{ endpoint::types::PTRDIFF_T,		"PTRDIFF_T"s		},
	{ endpoint::types::FLOAT,			"FLOAT"s			},
	{ endpoint::types::DOUBLE,			"DOUBLE"s			},
	{ endpoint::types::LONG_DOUBLE,		"LONG DOUBLE"s		}, // deliberately using space instead of underscore
})

namespace utility {

namespace detail {

using tp = endpoint::types;

template <tp> struct ref_type {}; // default case (compile time error)
template <> struct ref_type<tp::U8>				{ using type = std::uint8_t;	};
template <> struct ref_type<tp::U16>			{ using type = std::uint16_t;	};
template <> struct ref_type<tp::U32>			{ using type = std::uint32_t;	};
template <> struct ref_type<tp::U64>			{ using type = std::uint64_t;	};
template <> struct ref_type<tp::I8>				{ using type = std::int8_t;		};
template <> struct ref_type<tp::I16>			{ using type = std::int16_t;	};
template <> struct ref_type<tp::I32>			{ using type = std::int32_t;	};
template <> struct ref_type<tp::I64>			{ using type = std::int64_t;	};
template <> struct ref_type<tp::CHAR>			{ using type = char;			};
template <> struct ref_type<tp::BOOL>			{ using type = bool;			};
template <> struct ref_type<tp::SIZE_T>			{ using type = std::size_t;		};
template <> struct ref_type<tp::PTRDIFF_T>		{ using type = std::ptrdiff_t;	};
template <> struct ref_type<tp::FLOAT>			{ using type = float;			};
template <> struct ref_type<tp::DOUBLE>			{ using type = double;			};
template <> struct ref_type<tp::LONG_DOUBLE>	{ using type = long double;		};

template <tp Type>
using ref_type_t = typename ref_type<Type>::type;

// default case (compile time error)
template <std::size_t Dim>
struct insert_value_helper {};

template <>
struct insert_value_helper<0> {
	template <endpoint::types Type, typename... Args>
	static void insert(Args&&... args) noexcept {
		caen::args::insert_value<ref_type_t<Type>>(std::forward<Args>(args)...);
	}
};
template <>
struct insert_value_helper<1> {
	template <endpoint::types Type, typename... Args>
	static void insert(Args&&... args) noexcept {
		caen::args::insert_array<ref_type_t<Type>>(std::forward<Args>(args)...);
	}
};
template <>
struct insert_value_helper<2> {
	template <endpoint::types Type, typename... Args>
	static void insert(Args&&... args) noexcept {
		caen::args::insert_matrix<ref_type_t<Type>>(std::forward<Args>(args)...);
	}
};

template <std::size_t Dim, typename... Args>
void put_argument_generic(std::va_list* args, endpoint::types t, Args&&... value) {
	switch (t) {
		using tp = endpoint::types;
		using ivh = insert_value_helper<Dim>;
	case tp::U64:			return ivh::template insert<tp::U64>(args, std::forward<Args>(value)...);
	case tp::U32:			return ivh::template insert<tp::U32>(args, std::forward<Args>(value)...);
	case tp::U16:			return ivh::template insert<tp::U16>(args, std::forward<Args>(value)...);
	case tp::U8:			return ivh::template insert<tp::U8>(args, std::forward<Args>(value)...);
	case tp::I64:			return ivh::template insert<tp::I64>(args, std::forward<Args>(value)...);
	case tp::I32:			return ivh::template insert<tp::I32>(args, std::forward<Args>(value)...);
	case tp::I16:			return ivh::template insert<tp::I16>(args, std::forward<Args>(value)...);
	case tp::I8:			return ivh::template insert<tp::I8>(args, std::forward<Args>(value)...);
	case tp::CHAR:			return ivh::template insert<tp::CHAR>(args, std::forward<Args>(value)...);
	case tp::BOOL:			return ivh::template insert<tp::BOOL>(args, std::forward<Args>(value)...);
	case tp::SIZE_T:		return ivh::template insert<tp::SIZE_T>(args, std::forward<Args>(value)...);
	case tp::PTRDIFF_T:		return ivh::template insert<tp::PTRDIFF_T>(args, std::forward<Args>(value)...);
	case tp::FLOAT:			return ivh::template insert<tp::FLOAT>(args, std::forward<Args>(value)...);
	case tp::DOUBLE:		return ivh::template insert<tp::DOUBLE>(args, std::forward<Args>(value)...);
	case tp::LONG_DOUBLE:	return ivh::template insert<tp::LONG_DOUBLE>(args, std::forward<Args>(value)...);
	default:				throw ex::invalid_argument("invalid type");
	}
}

} // detail namespace

template <typename TIn>
void put_argument(std::va_list *args, endpoint::types t, TIn&& value) {
	detail::put_argument_generic<0>(args, t, std::forward<TIn>(value));
}

template <typename TIn>
void put_argument_raw_data(std::va_list *args, endpoint::types t, const TIn* p, std::size_t size) {
	switch (t) {
	using tp = endpoint::types;
	using namespace detail;
	case tp::U8:
		caen::args::insert_raw_data<caen::byte>(args, p, size);
		break;
	default:
		throw ex::invalid_argument("invalid type");
	}
}

// Container is a container
template <typename Container>
void put_argument_array(std::va_list *args, endpoint::types t, const Container& value) {
	detail::put_argument_generic<1>(args, t, value);
}

// Container is a container of containers
template <typename Container>
void put_argument_matrix(std::va_list *args, endpoint::types t, const Container& value) {
	detail::put_argument_generic<2>(args, t, value);
}

namespace detail {

template <typename T, typename = void>
struct is_names_defined : std::false_type {};
template <typename T>
struct is_names_defined<T, caen::void_t<typename T::names>> : std::is_enum<typename T::names> {};

template <typename T, typename = void>
struct is_types_defined : std::false_type {};
template <typename T>
struct is_types_defined<T, caen::void_t<typename T::types>> : std::is_enum<typename T::types> {};

template <typename T, typename = void>
struct is_args_list_t_defined : std::false_type {};
template <typename T>
struct is_args_list_t_defined<T, caen::void_t<typename T::args_list_t>> : std::true_type {};

template <typename T, typename = void>
struct is_unknown_defined : std::false_type {};
template <typename T>
struct is_unknown_defined<T, caen::void_t<decltype(T::UNKNOWN)>> : std::is_enum<T> {};

template <typename Type, typename... Types>
struct is_type_any_of : caen::disjunction<std::is_same<Type, Types>...> {};

namespace sanity_checks {

struct foo {
	enum class names;
	using types = int;
};

constexpr bool test_is_defined() noexcept {
	bool ret{true};
	ret &= (is_names_defined<foo>::value); // true even if opaque
	ret &= (!is_types_defined<foo>::value); // not an enum
	ret &= (!is_args_list_t_defined<foo>::value);
	ret &= (is_names_defined<endpoint>::value);
	ret &= (is_types_defined<endpoint>::value);
	ret &= (!is_args_list_t_defined<endpoint>::value);
	ret &= (!is_unknown_defined<endpoint::names>::value);
	ret &= (is_unknown_defined<endpoint::types>::value);
	return ret;
}

constexpr bool test_is_type_any_of() noexcept {
	bool ret{true};
	ret &= (is_type_any_of<int, int, bool, float>::value);
	ret &= (!is_type_any_of<int, bool, double>::value);
	return ret;
}

BOOST_STATIC_ASSERT(test_is_defined());
BOOST_STATIC_ASSERT(test_is_type_any_of());

} // namespace sanity_checks

} // namespace detail

/**
 * @brief Endpoint traits.
 *
 * Typically, traits are not useful for derived class. However C++ does not support inheritance and override of types,
 * So, traits in our case can be used to specify that each endpoint specialization must have an enumerator
 * named "names" containing at least "UNKNOWN". The other enumeration "types" must be inherited from base enum, that contains "UNKNOWN".
 * This is used in json_data_format class for serialization.
 * @tparam Endpoint	the endpoint type
 */
template <typename Endpoint>
struct endpoint_traits {

	// static assertions
	static_assert(boost::is_complete<Endpoint>::value, "endpoint_traits requires a complete type (see args_list)");
	static_assert(detail::is_names_defined<Endpoint>::value, "endpoint must define names");
	static_assert(detail::is_types_defined<Endpoint>::value, "endpoint must define types");
	static_assert(detail::is_args_list_t_defined<Endpoint>::value, "endpoint must define args_list_t");

	// public definitions
	using names_type = typename Endpoint::names;
	using types_type = typename Endpoint::types;
	using args_list_type = typename Endpoint::args_list_t;

	// more static assertions
	static_assert(std::is_base_of<endpoint, Endpoint>::value, "invalid endpoint type");
	static_assert(std::is_same<endpoint::types, types_type>::value, "types cannot be overridden");
	static_assert(detail::is_unknown_defined<names_type>::value, "names must define UNKNOWN");
	static_assert(detail::is_unknown_defined<types_type>::value, "types must define UNKNOWN");

	// utility function to check if value is unknown for both names and types
	template <typename Enum>
	static constexpr bool is_unknown(Enum v) noexcept {
		static_assert(detail::is_type_any_of<Enum, names_type, types_type>::value, "invalid enum type");
		return v == Enum::UNKNOWN;
	}

};

/**
 * @brief Args list generator.
 * 
 * Used to define a member of endpoint classes, we cannot use Endpoint as template argument
 * since type it is still incomplete if used in the class definition.
 * @sa https://stackoverflow.com/q/71717523/3287591
 * @tparam Names	names enumerator, that is endpoint specific
 * @tparam Types	types enumerator (could also be replaced directly with endpont::types since endpoints cannot override it)
 */
template <typename Names, typename Types>
struct args_list {

	// public definitions
	using names_type = Names;
	using types_type = Types;

	// static assertions
	static_assert(std::is_same<endpoint::types, types_type>::value, "types cannot be overridden");
	static_assert(detail::is_unknown_defined<names_type>::value, "names must define UNKNOWN");
	static_assert(detail::is_unknown_defined<types_type>::value, "types must define UNKNOWN");

	// public definitions
	using type = caen::vector<std::tuple<names_type, types_type, std::size_t>>;

};

template <typename Names, typename Types>
using args_list_t = typename args_list<Names, Types>::type;

} // namespace utility

} // namespace ep

} // namespace dig2

} // namespace caen

#endif /* CAEN_INCLUDE_ENDPOINTS_ENDPOINT_HPP_ */
