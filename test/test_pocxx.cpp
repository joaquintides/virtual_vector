/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/container/virtual_vector.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/mp11/list.hpp>
#include <cstddef>
#include <type_traits>
#include <utility>
#include "utility.hpp"

#if defined(BOOST_MSVC)
#pragma warning(push)
#pragma warning(disable:4127) /* conditional expression is constant */
#endif

template<bool Pocca, bool Pocma, bool Pocs, bool AlwaysEqual>
void test(int src_state, std::size_t dst_mc, std::size_t src_mc)
{
  using allocator_type = 
    stateful_virtual_allocator<int, Pocca, Pocma, Pocs, AlwaysEqual>;
  using vector = boost::container::virtual_vector<int, allocator_type>;

  static_assert(
    noexcept(std::declval<vector&>() = std::declval<vector&&>()) ==
    (Pocma || AlwaysEqual), 
    "move assignment must be noexcept if POCMA || AlwaysEqual");
  static_assert(
    noexcept(std::declval<vector&>().swap(std::declval<vector&>())) ==
    (Pocs || AlwaysEqual), 
    "swap must be noexcept if POCS || AlwaysEqual");

  const auto     short_rng = make_range<int>(200),
                 long_rng = make_range<int>(400);
  allocator_type al0{0},
                 al1{src_state};
  const bool     allocators_equal = (al0 == al1);
  const bool     different_mc = (dst_mc != src_mc);

  {
    const auto& xrng = short_rng;
    const auto& yrng = long_rng;
    vector x{dst_mc, xrng.begin(), xrng.end(), al0};
    vector y{src_mc, yrng.begin(), yrng.end(), al1};
    const auto old_x_data = x.data();
    const auto old_y_data = y.data();
    const auto rc0 = al0.num_reserve_calls();
    const auto rc1 = al1.num_reserve_calls();

    x = y;

    BOOST_TEST_EQ(x.get_allocator().state, Pocca? al1.state: al0.state);
    BOOST_TEST_EQ(y.get_allocator().state, al1.state);
    BOOST_TEST_EQ(x.max_size(), src_mc);
    BOOST_TEST_EQ(y.max_size(), src_mc);
    test_equal(x, yrng);
    test_equal(y, yrng);
    BOOST_TEST_EQ(y.data(), old_y_data);
    if(!different_mc && (!Pocca || allocators_equal)) {
      BOOST_TEST_EQ(x.data(), old_x_data);
    }

    const auto delta0 = !Pocca && different_mc? 1u: 0u;
    const auto delta1 = Pocca && (!allocators_equal || different_mc)? 1u: 0u;
    BOOST_TEST_EQ(al0.num_reserve_calls(), rc0 + delta0);
    BOOST_TEST_EQ(al1.num_reserve_calls(), rc1 + delta1);
  }
  {
    vector x{dst_mc, short_rng.begin(), short_rng.end(), al0};
    vector y{src_mc, long_rng.begin(), long_rng.end(), al1};
    const auto old_x_data = x.data();
    const auto old_y_data = y.data();
    const auto rc0 = al0.num_reserve_calls();
    const auto rc1 = al1.num_reserve_calls();
    const bool transfer = Pocma || allocators_equal;

    x = std::move(y);

    BOOST_TEST_EQ(x.get_allocator().state, Pocma ? al1.state : al0.state);
    BOOST_TEST_EQ(x.max_size(), src_mc);
    test_equal(x, long_rng);
    BOOST_TEST(y.empty());
    if(transfer) {
      BOOST_TEST_EQ(x.data(), old_y_data);
      BOOST_TEST_EQ(y.max_size(), 0u);
      BOOST_TEST_EQ(y.data(), nullptr);
    }
    else {
      BOOST_TEST_EQ(y.max_size(), src_mc);
      BOOST_TEST_EQ(y.data(), old_y_data);
      BOOST_TEST(x.data() != old_y_data);
      if(!different_mc) BOOST_TEST_EQ(x.data(), old_x_data);
    }

    const auto delta0 = !transfer && different_mc ? 1u : 0u;
    BOOST_TEST_EQ(al0.num_reserve_calls(), rc0 + delta0);
    BOOST_TEST_EQ(al1.num_reserve_calls(), rc1);
  }
  if(Pocs || allocators_equal) {
    vector     x{dst_mc, short_rng.begin(), short_rng.end(), al0},
               y{src_mc, long_rng.begin(), long_rng.end(), al1};
    const auto old_x_data = x.data(),
               old_y_data = y.data();
    const auto rc0 = al0.num_reserve_calls(),
               rc1 = al1.num_reserve_calls();

    x.swap(y);

    BOOST_TEST_EQ(x.get_allocator().state, Pocs? al1.state: al0.state);
    BOOST_TEST_EQ(y.get_allocator().state, Pocs? al0.state: al1.state);
    BOOST_TEST_EQ(x.max_size(), src_mc);
    BOOST_TEST_EQ(y.max_size(), dst_mc);
    BOOST_TEST_EQ(x.data(), old_y_data);
    BOOST_TEST_EQ(y.data(), old_x_data);
    test_equal(x, long_rng);
    test_equal(y, short_rng);
    BOOST_TEST_EQ(al0.num_reserve_calls(), rc0);
    BOOST_TEST_EQ(al1.num_reserve_calls(), rc1);
  }

  BOOST_TEST_EQ(allocator_type::num_active_reservations(), 0);
}

#if defined(BOOST_MSVC)
#pragma warning(pop) /* C4127 */
#endif

struct invoke_tests
{
  template<typename... Bs>
  void operator()(boost::mp11::mp_list<Bs...>) const
  {
    for(int src_state: {0, 1}) {
      test<Bs::value...>(src_state, 512, 512);
      test<Bs::value...>(src_state, 256, 1024);
      test<Bs::value...>(src_state, 1024, 512);
    }
  }
};

void test()
{
  using namespace boost::mp11;
  using boolean = mp_list<mp_false, mp_true>;

  mp_for_each<
    mp_product<mp_list, boolean, boolean, boolean, boolean>
  >(invoke_tests{});
}

int main()
{
  test();

  return boost::report_errors();
}
