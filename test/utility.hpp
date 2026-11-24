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
#include <unordered_map>
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

struct stateful_virtual_allocator_call_count_record
{
  std::size_t num_allocates = 0, num_reserves = 0, num_releases = 0;
};

template<
  typename T, 
  bool Pocca = true, bool Pocma = true,
  bool Pocs = true, bool AlwaysEqual = true
>
struct stateful_virtual_allocator: boost::container::virtual_allocator<T>
{
  using base = boost::container::virtual_allocator<T>;
  using pointer = typename base::pointer;
  using size_type = typename base::size_type;
  using reservation_handle = typename base::reservation_handle;

  using propagate_on_container_copy_assignment =
    std::integral_constant<bool, Pocca>;
  using propagate_on_container_move_assignment =
    std::integral_constant<bool, Pocma>;
  using propagate_on_container_swap =
    std::integral_constant<bool, Pocs>;
  using is_always_equal = std::integral_constant<bool, AlwaysEqual>;

  template<typename U>
  struct rebind 
  {
    using other = 
      stateful_virtual_allocator<U, Pocca, Pocma, Pocs, AlwaysEqual>;
  };

  stateful_virtual_allocator(int state_ = 0):
    state{state_},
    call_count{
      std::make_shared<stateful_virtual_allocator_call_count_record>()}
    {}

  template<typename U>
  stateful_virtual_allocator(
    const stateful_virtual_allocator<U, Pocca, Pocma, Pocs, AlwaysEqual>& x)
    noexcept:
    state{x.state}, call_count{x.call_count} {}

  pointer allocate(size_type n)
  {
    auto p = base::allocate(n);
    ++call_count->num_allocates;
    return p;
  }

  reservation_handle reserve(size_type n)
  {
    auto h = base::reserve(n);
    if(n != 0) {
      BOOST_TEST(
        active_reservations().emplace(
          base::data(h), reservation_record{n, state}).second);
      ++call_count->num_reserves;
    }
    return h;
  }

  reservation_handle resize(reservation_handle h, size_type n, size_type m)
  {
    if(h) {
      const auto it = active_reservations().find(base::data(h));
      BOOST_TEST(it != active_reservations().end());
      if(it != active_reservations().end()) {
        BOOST_TEST_EQ(it->second.maximum_size, n);
      }
    }
    return base::resize(std::move(h), n, m);
  }

  void release(const reservation_handle& h, size_type n) noexcept
  {
    if(h) {
      const auto it = active_reservations().find(base::data(h));
      BOOST_TEST(it != active_reservations().end());
      if(it != active_reservations().end()) {
        BOOST_TEST_EQ(it->second.maximum_size, n);
        BOOST_TEST(AlwaysEqual || it->second.state == state);
        active_reservations().erase(it);
      }
      ++call_count->num_releases;
    }
    base::release(h, n);
  }

  template<typename U>
  bool operator==(
    const stateful_virtual_allocator<U, Pocca, Pocma, Pocs, AlwaysEqual>& x)
  const noexcept
  {
    return AlwaysEqual || state == x.state;
  }

  template<typename U>
  bool operator!=(
    const stateful_virtual_allocator<U, Pocca, Pocma, Pocs, AlwaysEqual>& x)
  const noexcept
  {
    return !(*this == x);
  }

  static std::size_t num_active_reservations()
  { 
    return active_reservations().size();
  }

  std::size_t num_allocate_calls() { return call_count->num_allocates; }
  std::size_t num_reserve_calls() { return call_count->num_reserves; }

  int state;

private:
  template<typename, bool, bool, bool, bool>
  friend struct stateful_virtual_allocator;

  struct reservation_record
  {
    size_type maximum_size;
    int       state;
  };

  static std::unordered_map<void*, reservation_record>& active_reservations()
  {
    static std::unordered_map<void*, reservation_record> m;
    return m;
  }

  std::shared_ptr<stateful_virtual_allocator_call_count_record> call_count;
};

#endif
