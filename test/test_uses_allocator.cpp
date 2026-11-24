/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/container/virtual_vector.hpp>
#include <boost/core/lightweight_test.hpp>
#include <scoped_allocator>
#include <string>
#include "utility.hpp"

struct stateful_virtual_allocator_user
{
  using allocator_type = stateful_virtual_allocator<int>;

  stateful_virtual_allocator_user(const allocator_type& al_): al{al_} {}
  stateful_virtual_allocator_user(
    const stateful_virtual_allocator_user&, const allocator_type& al_):
    al{al_} {}

  allocator_type al;
};

int main()
{
  {
    using string = std::basic_string<
      char, std::char_traits<char>, stateful_virtual_allocator<char>>;
    using virtual_vector = boost::container::virtual_vector<
      string, 
      std::scoped_allocator_adaptor<stateful_virtual_allocator<string>>>;
    using allocator_type = virtual_vector::allocator_type;

    virtual_vector  x{2048, allocator_type(42)};
    auto            ac = x.get_allocator().num_allocate_calls();
    x.emplace_back(100, 'x');
    BOOST_TEST(x.front() == string(100, 'x'));
    BOOST_TEST_EQ(x.front().get_allocator().state, 42);
    BOOST_TEST_GT(x.get_allocator().num_allocate_calls(), ac);
  }
  {
    using virtual_vector = boost::container::virtual_vector<
      stateful_virtual_allocator_user,
      std::scoped_allocator_adaptor<
        stateful_virtual_allocator<stateful_virtual_allocator_user>>>;
    using allocator_type = virtual_vector::allocator_type;

    virtual_vector x{
      2048, 10, 
      stateful_virtual_allocator_user{allocator_type(0)}, allocator_type(42)};
    BOOST_TEST_EQ(x.size(), 10u);
    for(const auto& v: x) BOOST_TEST_EQ(v.al.state, 42);
  }
  {
    using string = std::basic_string<
      char, std::char_traits<char>, stateful_virtual_allocator<char>>;
    using inner_virtual_vector = boost::container::virtual_vector<
      string,
      std::scoped_allocator_adaptor<stateful_virtual_allocator<string>>>;
    using virtual_vector = boost::container::virtual_vector<
      inner_virtual_vector,
      std::scoped_allocator_adaptor<
        stateful_virtual_allocator<inner_virtual_vector>>>;
    using allocator_type = virtual_vector::allocator_type;

    virtual_vector        x{2048, allocator_type(42)};
    inner_virtual_vector& y = x.emplace_back(128, 10);
    BOOST_TEST_EQ(y.size(), 10u);
    BOOST_TEST_EQ(y.get_allocator().state, 42);
    BOOST_TEST_EQ(y.front().get_allocator().state, 42);
  }

  return boost::report_errors();
}
