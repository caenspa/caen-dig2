/******************************************************************************
*
*	CAEN SpA - Software Division
*	Via Vetraia, 11 - 55049 - Viareggio ITALY
*	+39 0594 388 398 - www.caen.it
*
*******************************************************************************
*
*	Copyright (C) 2019 Glen Joseph Fernandes (glenjofe@gmail.com)
*
*	This file is distributed under the Boost Software License, Version 1.0.
*	(http://www.boost.org/LICENSE_1_0.txt)
*
*	SPDX-License-Identifier: BSL-1.0
*
***************************************************************************//*!
*
*	\file		span.hpp
*	\brief		`std::span` replacement, copied from single header implementation in Boost 1.78.0.
*	\author		Glen Joseph Fernandes, Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_CPP_UTILITY_SPAN_HPP_
#define CAEN_INCLUDE_CPP_UTILITY_SPAN_HPP_

#include <array>
#include <iterator>
#include <type_traits>
#include <cstddef>

#include "byte.hpp"

/*
 * We could use `std::span` when compiling as C++20 but, unfortunately, it
 * is not supported by Boost.Range: we prefer to always use `boost::span`
 * because Boost.Range is largely used across CAEN libraries.
 *
 * Alternatives to replace Boost.Range could be:
 * - standard *Ranges library*, available since C++20 too
 * - *range-v3*, essentially the same of the standard Ranges library,
 *     compatible with C++14 but that would restrict supported compilers
 *     to GCC >= 6.5, clang >= 5.0 (no problems with VS2019)
 *
 * The second option would be preferable, also because preparatory to a
 * smooth migration to C++20. Anyway, we prefer to keep this
 * implementation, at least until we'll be able to drop GCC 5 support.
 * See https://github.com/boostorg/range/issues/142.
 *
 * We also do not provide fallback to `boost::span` with inline namespace
 * based on BOOST_VERSION >= 107800 because `boost::span::as_bytes` and
 * `boost::span::as_writable_bytes` are available only on C++17, while
 * here they are always available because based on `caen::byte`.
 */

namespace caen {

constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

template<class T, std::size_t E = dynamic_extent>
class span;

namespace detail {

template<class U, class T>
struct span_convertible {
	static constexpr bool value = std::is_convertible<U(*)[], T(*)[]>::value;
};

template<std::size_t E, std::size_t N>
struct span_capacity {
	static constexpr bool value = E == dynamic_extent || E == N;
};

template<class T, std::size_t E, class U, std::size_t N>
struct span_compatible {
	static constexpr bool value = span_capacity<E, N>::value &&
		span_convertible<U, T>::value;
};

template<class T>
struct span_uncvref {
	typedef typename std::remove_cv<typename
		std::remove_reference<T>::type>::type type;
};

template<class>
struct span_is_span {
	static constexpr bool value = false;
};

template<class T, std::size_t E>
struct span_is_span<span<T, E>> {
	static constexpr bool value = true;
};

template<class T>
struct span_is_array {
	static constexpr bool value = false;
};

template<class T, std::size_t N>
struct span_is_array<std::array<T, N> > {
	static constexpr bool value = true;
};

template<class, class = void>
struct span_data { };

template<class T>
struct span_data<T,
	typename std::enable_if<std::is_pointer<decltype(std::declval<T
		&>().data())>::value>::type> {
	typedef typename std::remove_pointer<decltype(std::declval<T
		&>().data())>::type type;
};

template<class, class, class = void>
struct span_has_data {
	static constexpr bool value = false;
};

template<class R, class T>
struct span_has_data<R, T, typename std::enable_if<span_convertible<typename
	span_data<R>::type, T>::value>::type> {
	static constexpr bool value = true;
};

template<class, class = void>
struct span_has_size {
	static constexpr bool value = false;
};

template<class R>
struct span_has_size<R, typename
	std::enable_if<std::is_convertible<decltype(std::declval<R&>().size()),
		std::size_t>::value>::type> {
	static constexpr bool value = true;
};

template<class R, class T>
struct span_is_range {
	static constexpr bool value = (std::is_const<T>::value ||
		std::is_lvalue_reference<R>::value) &&
		!span_is_span<typename span_uncvref<R>::type>::value &&
		!span_is_array<typename span_uncvref<R>::type>::value &&
		!std::is_array<typename span_uncvref<R>::type>::value &&
		span_has_data<R, T>::value &&
		span_has_size<R>::value;
};

template<std::size_t E, std::size_t N>
struct span_implicit {
	static constexpr bool value = E == dynamic_extent ||
		N != dynamic_extent;
};

template<class T, std::size_t E, class U, std::size_t N>
struct span_copyable {
	static constexpr bool value = (N == dynamic_extent ||
		span_capacity<E, N>::value) && span_convertible<U, T>::value;
};

template<std::size_t E, std::size_t O>
struct span_sub {
	static constexpr std::size_t value = E == dynamic_extent ?
		dynamic_extent : E - O;
};

template<class T, std::size_t E>
struct span_store {
	constexpr span_store(T* p_, std::size_t) noexcept
		: p(p_) { }
	static constexpr std::size_t n = E;
	T* p;
};

template<class T>
struct span_store<T, dynamic_extent> {
	constexpr span_store(T* p_, std::size_t n_) noexcept
		: p(p_)
		, n(n_) { }
	T* p;
	std::size_t n;
};

template<class T, std::size_t E>
struct span_bytes {
	static constexpr std::size_t value = sizeof(T) * E;
};

template<class T>
struct span_bytes<T, dynamic_extent> {
	static constexpr std::size_t value = dynamic_extent;
};

} /* detail */

template<class T, std::size_t E>
class span {
public:
	typedef T element_type;
	typedef typename std::remove_cv<T>::type value_type;
	typedef std::size_t size_type;
	typedef std::ptrdiff_t difference_type;
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef T& reference;
	typedef const T& const_reference;
	typedef T* iterator;
	typedef const T* const_iterator;
	typedef std::reverse_iterator<T*> reverse_iterator;
	typedef std::reverse_iterator<const T*> const_reverse_iterator;

	static constexpr std::size_t extent = E;

	template<std::size_t N = E,
		typename std::enable_if<N == dynamic_extent || N == 0, int>::type = 0>
	constexpr span() noexcept
		: s_(0, 0) { }

	template<class I,
		typename std::enable_if<E == dynamic_extent &&
			detail::span_convertible<I, T>::value, int>::type = 0>
	constexpr span(I* f, size_type c)
		: s_(f, c) { }

	template<class I,
		typename std::enable_if<E != dynamic_extent &&
			detail::span_convertible<I, T>::value, int>::type = 0>
	explicit constexpr span(I* f, size_type c)
		: s_(f, c) { }

	template<class I, class L,
		typename std::enable_if<E == dynamic_extent &&
			detail::span_convertible<I, T>::value, int>::type = 0>
	constexpr span(I* f, L* l)
		: s_(f, l - f) { }

	template<class I, class L,
		typename std::enable_if<E != dynamic_extent &&
			detail::span_convertible<I, T>::value, int>::type = 0>
	explicit constexpr span(I* f, L* l)
		: s_(f, l - f) { }

	template<std::size_t N,
		typename std::enable_if<detail::span_capacity<E, N>::value,
			int>::type = 0>
	constexpr span(typename std::enable_if<true, T>::type (&a)[N]) noexcept
		: s_(a, N) { }

	template<class U, std::size_t N,
		typename std::enable_if<detail::span_compatible<T, E, U, N>::value,
			int>::type = 0>
	constexpr span(std::array<U, N>& a) noexcept
		: s_(a.data(), N) { }

	template<class U, std::size_t N,
		typename std::enable_if<detail::span_compatible<T, E, const U,
			N>::value, int>::type = 0>
	constexpr span(const std::array<U, N>& a) noexcept
		: s_(a.data(), N) { }

	template<class R,
		typename std::enable_if<E == dynamic_extent &&
			detail::span_is_range<R, T>::value, int>::type = 0>
	constexpr span(R&& r) noexcept(noexcept(r.data()) && noexcept(r.size()))
		: s_(r.data(), r.size()) { }

	template<class R,
		typename std::enable_if<E != dynamic_extent &&
			detail::span_is_range<R, T>::value, int>::type = 0>
	explicit constexpr span(R&& r) noexcept(noexcept(r.data()) &&
		noexcept(r.size()))
		: s_(r.data(), r.size()) { }

	template<class U, std::size_t N,
		typename std::enable_if<detail::span_implicit<E, N>::value &&
			detail::span_copyable<T, E, U, N>::value, int>::type = 0>
	constexpr span(const span<U, N>& s) noexcept
		: s_(s.data(), s.size()) { }

	template<class U, std::size_t N,
		typename std::enable_if<!detail::span_implicit<E, N>::value &&
			detail::span_copyable<T, E, U, N>::value, int>::type = 0>
	explicit constexpr span(const span<U, N>& s) noexcept
		: s_(s.data(), s.size()) { }

	template<std::size_t C>
	constexpr span<T, C> first() const {
		static_assert(C <= E, "Count <= Extent");
		return span<T, C>(s_.p, C);
	}

	template<std::size_t C>
	constexpr span<T, C> last() const {
		static_assert(C <= E, "Count <= Extent");
		return span<T, C>(s_.p + (s_.n - C), C);
	}

	template<std::size_t O, std::size_t C = dynamic_extent>
	constexpr typename std::enable_if<C == dynamic_extent,
		span<T, detail::span_sub<E, O>::value> >::type subspan() const {
		static_assert(O <= E, "Offset <= Extent");
		return span<T, detail::span_sub<E, O>::value>(s_.p + O, s_.n - O);
	}

	template<std::size_t O, std::size_t C = dynamic_extent>
	constexpr typename std::enable_if<C != dynamic_extent,
		span<T, C> >::type subspan() const {
		static_assert(O <= E && C <= E - O,
			"Offset <= Extent && Count <= Extent - Offset");
		return span<T, C>(s_.p + O, C);
	}

	constexpr span<T, dynamic_extent> first(size_type c) const {
		return span<T, dynamic_extent>(s_.p, c);
	}

	constexpr span<T, dynamic_extent> last(size_type c) const {
		return span<T, dynamic_extent>(s_.p + (s_.n - c), c);
	}

	constexpr span<T, dynamic_extent> subspan(size_type o,
		size_type c = dynamic_extent) const {
		return span<T, dynamic_extent>(s_.p + o,
			c == dynamic_extent ? s_.n - o : c);
	}

	constexpr size_type size() const noexcept {
		return s_.n;
	}

	constexpr size_type size_bytes() const noexcept {
		return s_.n * sizeof(T);
	}

	constexpr bool empty() const noexcept {
		return s_.n == 0;
	}

	constexpr reference operator[](size_type i) const {
		return s_.p[i];
	}

	constexpr reference front() const {
		return *s_.p;
	}

	constexpr reference back() const {
		return s_.p[s_.n - 1];
	}

	constexpr pointer data() const noexcept {
		return s_.p;
	}

	constexpr iterator begin() const noexcept {
		return s_.p;
	}

	constexpr iterator end() const noexcept {
		return s_.p + s_.n;
	}

	constexpr reverse_iterator rbegin() const noexcept {
		return reverse_iterator(s_.p + s_.n);
	}

	constexpr reverse_iterator rend() const noexcept {
		return reverse_iterator(s_.p);
	}

	constexpr const_iterator cbegin() const noexcept {
		return s_.p;
	}

	constexpr const_iterator cend() const noexcept {
		return s_.p + s_.n;
	}

	constexpr const_reverse_iterator crbegin() const noexcept {
		return const_reverse_iterator(s_.p + s_.n);
	}

	constexpr const_reverse_iterator crend() const noexcept {
		return const_reverse_iterator(s_.p);
	}

	friend constexpr iterator begin(span s) noexcept {
		return s.begin();
	}

	friend constexpr iterator end(span s) noexcept {
		return s.end();
	}

private:
	detail::span_store<T, E> s_;
};

template<class T, std::size_t E>
constexpr std::size_t span<T, E>::extent;

template<class T, std::size_t E>
inline span<const caen::byte, detail::span_bytes<T, E>::value>
as_bytes(span<T, E> s) noexcept
{
	return span<const caen::byte, detail::span_bytes<T,
		E>::value>(reinterpret_cast<const caen::byte*>(s.data()),
			s.size_bytes());
}

template<class T, std::size_t E>
inline typename std::enable_if<!std::is_const<T>::value,
	span<caen::byte, detail::span_bytes<T, E>::value> >::type
as_writable_bytes(span<T, E> s) noexcept
{
	return span<caen::byte, detail::span_bytes<T,
		E>::value>(reinterpret_cast<caen::byte*>(s.data()), s.size_bytes());
}

} // namespace caen

#endif /* CAEN_INCLUDE_CPP_UTILITY_SPAN_HPP_ */