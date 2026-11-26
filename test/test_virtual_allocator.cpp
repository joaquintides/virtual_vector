/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/container/virtual_allocator.hpp>
#include <boost/core/lightweight_test.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

static const auto page = 
  boost::container::virtual_allocator_detail::page_size();

void test_empty()
{
  using allocator_type = boost::container::virtual_allocator<int>;

  allocator_type                     al;
  allocator_type::reservation_handle h;

  BOOST_TEST(!h);
  BOOST_TEST(al.data(h) == nullptr);
  BOOST_TEST_EQ(al.size(h), 0u);

  h = al.reserve(0);
  BOOST_TEST(!h);
  h = al.resize(h, 0, 0);
  BOOST_TEST(!h);
  BOOST_TEST_EQ(al.size(h), 0u);
  al.release(h, 0);

  h = al.reserve(1);
  BOOST_TEST(h);
  BOOST_TEST_EQ(al.size(h), 0u);
  al.release(h, 1);
}

struct three_bytes
{
  char bytes[3];
};

void test_rounding()
{
  using allocator_type = boost::container::virtual_allocator<three_bytes>;

  const auto     n = page / sizeof(three_bytes) + 2;
  allocator_type al;
  auto           h = al.reserve(n);

  BOOST_TEST(h);
  h = al.resize(h, n, 1);
  BOOST_TEST_EQ(al.size(h), page / sizeof(three_bytes));
  h = al.resize(h, n, n);
  BOOST_TEST_EQ(al.size(h), n);
  al.release(h, n);
}

void test_resize()
{
  using allocator_type = boost::container::virtual_allocator<char>;

  const auto     n = 3 * page + 17;
  allocator_type al;
  auto           h = al.reserve(n);
  auto           p = al.data(h);

  BOOST_TEST(h);
  BOOST_TEST(p != nullptr);
  BOOST_TEST_EQ(al.size(h), 0u);

  h = al.resize(h, n, 1);
  BOOST_TEST_EQ(al.size(h), page);
  BOOST_TEST(al.data(h) == p);
  p[0] = 'a';
  p[page - 1] = 'b';

  h = al.resize(h, n, page + 1);
  BOOST_TEST_EQ(al.size(h), 2 * page);
  BOOST_TEST(al.data(h) == p);
  BOOST_TEST_EQ(p[0], 'a');
  BOOST_TEST_EQ(p[page - 1], 'b');
  p[page] = 'c';
  p[2 * page - 1] = 'd';

  h = al.resize(h, n, 3 * page + 1);
  BOOST_TEST_EQ(al.size(h), n);
  BOOST_TEST(al.data(h) == p);
  p[3 * page] = 'e';
  p[n - 1] = 'f';

  h = al.resize(h, n, page + 1);
  BOOST_TEST_EQ(al.size(h), 2 * page);
  BOOST_TEST_EQ(p[page], 'c');
  BOOST_TEST_EQ(p[2 * page - 1], 'd');

  h = al.resize(h, n, 0);
  BOOST_TEST_EQ(al.size(h), 0u);
  BOOST_TEST(al.data(h) == p);

  h = al.resize(h, n, n);
  BOOST_TEST_EQ(al.size(h), n);
  BOOST_TEST(al.data(h) == p);
  p[0] = 'x';
  p[page] = 'y';
  p[n - 1] = 'z';
  BOOST_TEST_EQ(p[0], 'x');
  BOOST_TEST_EQ(p[page], 'y');
  BOOST_TEST_EQ(p[n - 1], 'z');

  al.release(h, n);
}

struct alignas(8192) over_aligned
{
  char data[8192];
};

void test_alignment()
{
  using allocator_type = boost::container::virtual_allocator<over_aligned>;

  allocator_type    al;
  const std::size_t n = 3;
  auto              h = al.reserve(n);
  auto              p = al.data(h);

  BOOST_TEST(h);
  BOOST_TEST(p != nullptr);
  BOOST_TEST_EQ(
    reinterpret_cast<std::uintptr_t>(p) % alignof(over_aligned), 0u);

  h = al.resize(h, n, 2);
  BOOST_TEST_GE(al.size(h), 2u);
  BOOST_TEST(al.data(h) == p);
  ::new (static_cast<void*>(p)) over_aligned{};
  ::new (static_cast<void*>(p + 1)) over_aligned{};
  p[0].data[0] = 'a';
  p[1].data[0] = 'b';
  BOOST_TEST_EQ(p[0].data[0], 'a');
  BOOST_TEST_EQ(p[1].data[0], 'b');

  p[1].~over_aligned();
  h = al.resize(h, n, 1);
  BOOST_TEST_GE(al.size(h), 1u);
  BOOST_TEST_EQ(p[0].data[0], 'a');

  p[0].~over_aligned();
  h = al.resize(h, n, 0);
  BOOST_TEST_EQ(al.size(h), 0u);
  h = al.resize(h, n, n);
  BOOST_TEST_EQ(al.size(h), n);
  BOOST_TEST(al.data(h) == p);
  ::new (static_cast<void*>(p + 2)) over_aligned{};
  p[2].data[0] = 'c';
  BOOST_TEST_EQ(p[2].data[0], 'c');
  p[2].~over_aligned();

  al.release(h, n);
}

void test_large_alignment()
{
  /* Such a large alignment as 128 * 1024 is not even supported by some
   * compilers, so we white-box test it. Exercises internal code branches
   * not visited for smaller alignments.
   */

  namespace detail = boost::container::virtual_allocator_detail;

  const std::size_t alignment = 128 * 1024;
  const auto        bytes = 3 * alignment;
  const auto        p = detail::reserve(bytes, alignment);

  BOOST_TEST(p != nullptr);
  BOOST_TEST_EQ(reinterpret_cast<std::uintptr_t>(p) % alignment, 0u);
  const auto committed = detail::commit(p, alignment);
  BOOST_TEST(committed);
  auto data = static_cast<char*>(p);
  data[0] = 'a';
  data[alignment - 1] = 'b';
  BOOST_TEST_EQ(data[0], 'a');
  BOOST_TEST_EQ(data[alignment - 1], 'b');
  detail::decommit(p, alignment);
  const auto recommitted = detail::commit(p, alignment);
  BOOST_TEST(recommitted);
  data[0] = 'c';
  BOOST_TEST_EQ(data[0], 'c');
  detail::release(p, bytes, alignment);
}

void test_limits()
{
  using allocator_type = boost::container::virtual_allocator<char>;

  allocator_type al;
  BOOST_TEST_THROWS(
    (void)al.reserve((std::numeric_limits<std::size_t>::max)()),
    std::bad_alloc);
}

int main()
{
  test_empty();
  test_rounding();
  test_resize();
  test_alignment();
  test_large_alignment();
  test_limits();

  return boost::report_errors();
}
