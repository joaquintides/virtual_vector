/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_CONTAINER_VECTOR_TEST_UTILITY_HPP
#define BOOST_CONTAINER_VECTOR_TEST_UTILITY_HPP

#include <algorithm>
#include <boost/container/virtual_vector.hpp>
#include <boost/core/lightweight_test.hpp>
#include <cstddef>
#include <memory>
#include <vector>

template<typename Vector, typename U>
struct rebind_value_type;

template<typename T, typename VirtualAllocator, typename U>
struct rebind_value_type<
  boost::container::virtual_vector<T, VirtualAllocator>, U>
{
  using type = boost::container::virtual_vector<
    U,
    typename std::allocator_traits<VirtualAllocator>::template rebind_alloc<U>
  >;
};

template<typename Vector, typename U>
using rebind_value_type_t = typename rebind_value_type<Vector, U>::type;

template<typename T>
std::vector<T> make_range(std::size_t n)
{
  std::vector<T> result;
  for(std::size_t i = 0; i < n; ++i) {
    result.push_back(static_cast<T>(i));
  }
  return result;
}

template<typename Container1, typename Container2>
void test_equal(const Container1& x, const Container2& y)
{
  BOOST_TEST_EQ(x.size(), y.size());
  if(x.size() == y.size()) {
    BOOST_TEST(std::equal(x.begin(), x.end(), y.begin()));
  }
}

#endif
