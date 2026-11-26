/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/container/virtual_vector.hpp>
#include <boost/core/lightweight_test.hpp>
#include <climits>
#include <cstddef>
#include <forward_list>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "utility.hpp"

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
#include <ranges>
#endif

struct injected_failure: std::runtime_error
{
  injected_failure(): std::runtime_error{"injected failure"} {}
};

struct throwing_value
{
  char padding[120] = {}; /* fill memory pages without many elements */

  throwing_value(int n_ = 0): n{n_}
  {
    maybe_throw(construction_countdown);
    ++outstanding_objects;
  }

  throwing_value(const throwing_value& x): n{x.n}
  {
    maybe_throw(construction_countdown);
    ++outstanding_objects;
  }

  throwing_value(throwing_value&& x): n{x.n}
  {
    maybe_throw(construction_countdown);
    x.n = -1;
    ++outstanding_objects;
  }

  throwing_value& operator=(const throwing_value& x)
  {
    maybe_throw(assignment_countdown);
    n = x.n;
    return *this;
  }

  throwing_value& operator=(throwing_value&& x)
  {
    maybe_throw(assignment_countdown);
    n = x.n;
    x.n = -1;
    return *this;
  }

  ~throwing_value()
  {
    BOOST_TEST_NE(n, INT_MIN);
    n = INT_MIN;
    --outstanding_objects;
  }

  operator int() const { return n; }

  static void throw_on_construction(int n) { construction_countdown = n; }
  static void throw_on_assignment(int n) { assignment_countdown = n; }
  static void disable_throws()
  {
    construction_countdown = 0;
    assignment_countdown = 0;
  }

  static std::size_t num_outstanding_objects()
  {
    return outstanding_objects;
  }

  friend void check_valid(const throwing_value& x)
  {
    BOOST_TEST_NE(x.n, INT_MIN);
  }

private:
  static void maybe_throw(int& countdown)
  {
    if(countdown && !--countdown) throw injected_failure{};
  }

  static int         construction_countdown;
  static int         assignment_countdown;
  static std::size_t outstanding_objects;

  int n;
};

int         throwing_value::construction_countdown = 0;
int         throwing_value::assignment_countdown = 0;
std::size_t throwing_value::outstanding_objects = 0;

template<typename T>
struct throwing_virtual_allocator:
  stateful_virtual_allocator<T, false, false, false, false>
{
  using base = stateful_virtual_allocator<T, false, false, false, false>;
  using size_type = typename base::size_type;
  using reservation_handle = typename base::reservation_handle;

  template<typename U>
  struct rebind { using other = throwing_virtual_allocator<U>; };

  throwing_virtual_allocator(int state_ = 0): base{state_} {}

  template<typename U>
  throwing_virtual_allocator(const throwing_virtual_allocator<U>& x):
    base{x} {}

  reservation_handle reserve(size_type n)
  {
    if(n) maybe_throw(reserve_countdown);
    return base::reserve(n);
  }

  reservation_handle resize(reservation_handle h, size_type n, size_type m)
  {
    if(m > base::size(h)) maybe_throw(commit_countdown);
    return base::resize(std::move(h), n, m);
  }

  static void throw_on_reserve(int n) { reserve_countdown = n; }
  static void throw_on_commit(int n) { commit_countdown = n; }
  static void disable_throws()
  {
    reserve_countdown = 0;
    commit_countdown = 0;
  }

private:
  static void maybe_throw(int& countdown)
  {
    if(countdown && !--countdown) throw injected_failure{};
  }

  static int reserve_countdown;
  static int commit_countdown;
};

template<typename T> int throwing_virtual_allocator<T>::reserve_countdown = 0;
template<typename T> int throwing_virtual_allocator<T>::commit_countdown = 0;

struct throwing_input_iterator
{
  using iterator_category = std::input_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = const int*;
  using reference = const int&;

  throwing_input_iterator() = default;
  explicit throwing_input_iterator(const int* p_): p{p_} {}

  reference operator*() const { return *p; }

  throwing_input_iterator& operator++()
  {
    if(countdown && !--countdown) throw injected_failure{};
    ++p;
    return *this;
  }

  throwing_input_iterator operator++(int)
  {
    auto x = *this;
    ++*this;
    return x;
  }

  friend bool operator==(throwing_input_iterator x, throwing_input_iterator y)
  {
    return x.p == y.p;
  }

  friend bool operator!=(throwing_input_iterator x, throwing_input_iterator y)
  {
    return !(x == y);
  }

  static void throw_on_increment(int n) { countdown = n; }
  static void disable_throws() { countdown = 0; }

private:
  const int* p = nullptr;
  static int countdown;
};

int throwing_input_iterator::countdown = 0;

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
/* unsized sentinel forcing single-pass insertion ops on virtual_vector */
struct throwing_input_sentinel
{
  const int* p = nullptr;

  friend bool operator==(
    throwing_input_iterator it, throwing_input_sentinel last)
  {
    return it == throwing_input_iterator{last.p};
  }
};

using throwing_input_range =
  std::ranges::subrange<throwing_input_iterator, throwing_input_sentinel>;

static_assert(std::input_iterator<throwing_input_iterator>);
static_assert(!std::forward_iterator<throwing_input_iterator>);
static_assert(
  !std::sized_sentinel_for<throwing_input_sentinel, throwing_input_iterator>);

throwing_input_range make_throwing_input_range(const std::vector<int>& v)
{
  return {
    throwing_input_iterator{v.data()},
    throwing_input_sentinel{v.data() + v.size()}
  };
}
#endif

using allocator_type = throwing_virtual_allocator<throwing_value>;
using virtual_vector = 
  boost::container::virtual_vector<throwing_value, allocator_type>;

enum class failure_point
{
  construction, assignment, reservation, commitment, increment
};

enum class guarantee { basic, contents, strong };

void disable_throws()
{
  throwing_value::disable_throws();
  allocator_type::disable_throws();
  throwing_input_iterator::disable_throws();
}

void throw_at(failure_point point, int countdown)
{
  disable_throws();
  switch(point) {
  case failure_point::construction:
    throwing_value::throw_on_construction(countdown); break;
  case failure_point::assignment:
    throwing_value::throw_on_assignment(countdown); break;
  case failure_point::reservation:
    allocator_type::throw_on_reserve(countdown); break;
  case failure_point::commitment:
    allocator_type::throw_on_commit(countdown); break;
  case failure_point::increment:
    throwing_input_iterator::throw_on_increment(countdown); break;
  }
}

void check_valid(const virtual_vector& x)
{
  BOOST_TEST_LE(x.size(), x.capacity());
  BOOST_TEST_LE(x.capacity(), x.max_size());
  std::size_t n = 0;
  for(const auto& v: x) {
    check_valid(v);
    ++n;
  }
  BOOST_TEST_EQ(n, x.size());
}

std::vector<int> snapshot(const virtual_vector& x)
{
  return {x.begin(), x.end()};
}

void check_equal(const virtual_vector& x, const std::vector<int>& backup)
{
  BOOST_TEST_EQ(x.size(), backup.size());
  if(x.size() == backup.size()) {
    for(std::size_t i = 0; i < x.size(); ++i) {
      BOOST_TEST_EQ(static_cast<int>(x[i]), backup[i]);
    }
  }
}

void check_no_leaks(std::size_t objects, std::size_t reservations)
{
  BOOST_TEST_EQ(throwing_value::num_outstanding_objects(), objects);
  BOOST_TEST_EQ(allocator_type::num_active_reservations(), reservations);
}

virtual_vector make_vector(
  std::size_t n, int state = 1, std::size_t mc = 8192)
{
  virtual_vector x{mc, allocator_type{state}};
  for(std::size_t i = 0; i < n; ++i) x.emplace_back(static_cast<int>(i + 1));
  return x;
}

virtual_vector make_full_vector()
{
  virtual_vector x{8192, allocator_type{1}};
  x.emplace_back(1);
  const auto n = x.capacity();
  for(std::size_t i = 1; i < n; ++i) {
    x.emplace_back(static_cast<int>(i + 1));
  }
  return x;
}

template<typename Factory, typename Operation>
void test_operation(
  const char* label, Factory factory, Operation operation,
  failure_point point, guarantee expected)
{
  BOOST_LIGHTWEIGHT_TEST_OSTREAM << label << '\n';
  const auto objects = throwing_value::num_outstanding_objects();
  const auto reservations = allocator_type::num_active_reservations();
  bool saw_throw = false, saw_nothrow = false;

  /* Rather than finely calculating the exact throwing countdown needed to get
   * the failure at the point of interest, we simply run the operation with
   * increasing countdowns till it doesn't throw.
   */
  for(int n = 1; n <= 128 && !saw_nothrow; ++n) {
    {
      auto       x = factory();
      const auto backup = snapshot(x);
      const auto old_capacity = x.capacity();
      const auto old_data = x.data();
      const auto old_max_size = x.max_size();
      bool       threw = false;

      throw_at(point, n);
      try {
        operation(x);
      }
      catch(const injected_failure&) {
        threw = true;
        saw_throw = true;
      }
      catch(...) {
        BOOST_ERROR("Unexpected exception in test_operation");
      }
      disable_throws();

      check_valid(x); /* guarantee::basic */
      if(threw) {
        if(expected >= guarantee::contents) {
          check_equal(x, backup);
          BOOST_TEST_EQ(x.max_size(), old_max_size);
          BOOST_TEST(x.data() == old_data);
        }
        if(expected >= guarantee::strong) {
          BOOST_TEST_EQ(x.capacity(), old_capacity);
        }
      }
      else saw_nothrow = true;
    }
    check_no_leaks(objects, reservations);
  }
  BOOST_TEST(saw_throw);
  BOOST_TEST(saw_nothrow);
}

template<typename Factory1, typename Factory2, typename Operation>
void test_operation(
  const char* label, Factory1 factory1, Factory2 factory2, Operation operation,
  failure_point point, guarantee expected1, guarantee expected2)
{
  BOOST_LIGHTWEIGHT_TEST_OSTREAM << label << '\n';
  const auto objects = throwing_value::num_outstanding_objects();
  const auto reservations = allocator_type::num_active_reservations();
  bool saw_throw = false, saw_nothrow = false;

  for(int n = 1; n <= 128 && !saw_nothrow; ++n) {
    {
      auto       x1 = factory1();
      const auto backup1 = snapshot(x1);
      const auto old_capacity1 = x1.capacity();
      const auto old_data1 = x1.data();
      const auto old_max_size1 = x1.max_size();
      auto       x2 = factory2();
      const auto backup2 = snapshot(x2);
      const auto old_capacity2 = x2.capacity();
      const auto old_data2 = x2.data();
      const auto old_max_size2 = x2.max_size();
      bool       threw = false;

      throw_at(point, n);
      try {
        operation(x1, x2);
      }
      catch(const injected_failure&) {
        threw = true;
        saw_throw = true;
      }
      catch(...) {
        BOOST_ERROR("Unexpected exception in test_operation");
      }
      disable_throws();

      check_valid(x1); /* guarantee::basic */
      check_valid(x2);
      if(threw) {
        if(expected1 >= guarantee::contents) {
          check_equal(x1, backup1);
          BOOST_TEST_EQ(x1.max_size(), old_max_size1);
          BOOST_TEST(x1.data() == old_data1);
        }
        if(expected1 >= guarantee::strong) {
          BOOST_TEST_EQ(x1.capacity(), old_capacity1);
        }

        if(expected2 >= guarantee::contents) {
          check_equal(x2, backup2);
          BOOST_TEST_EQ(x2.max_size(), old_max_size2);
          BOOST_TEST(x2.data() == old_data2);
        }
        if(expected2 >= guarantee::strong) {
          BOOST_TEST_EQ(x2.capacity(), old_capacity2);
        }
      }
      else saw_nothrow = true;
    }
    check_no_leaks(objects, reservations);
  }
  BOOST_TEST(saw_throw);
  BOOST_TEST(saw_nothrow);
}

template<typename Operation>
void test_operation(
  const char* label, Operation operation, failure_point point)
{
  /* The operation does not act on an existing virtual_vector (most
   * constructors): delegate to normal test_operation with a dummy factory.
   */
  test_operation(
    label,
    [] { return virtual_vector{0}; }, 
    [&] (virtual_vector&) { operation(); },
    point, guarantee::basic);
}

void test_construction()
{
  test_operation("empty constructor, reservation throws", [] {
    virtual_vector x{8192};
    (void)x;
  }, failure_point::reservation);

  test_operation("size constructor, commitment throws", [] {
    virtual_vector x{8192, 12};
    (void)x;
  }, failure_point::commitment);

  test_operation("size constructor, value_type throws", [] {
    virtual_vector x{8192, 12};
    (void)x;
  }, failure_point::construction);

  test_operation("fill constructor, value_type throws", [] {
    const throwing_value v{42};
    virtual_vector x{8192, 12, v};
    (void)x;
  }, failure_point::construction);

  test_operation("range constructor, value_type throws", [] {
    const std::vector<int> src{1, 2, 3, 4, 5, 6, 7};
    virtual_vector x{8192, src.begin(), src.end()};
    (void)x;
  }, failure_point::construction);

  test_operation("input range constructor, increment throws", [] {
    const std::vector<int> src{1, 2, 3, 4, 5, 6, 7};
    virtual_vector x{
      8192,
      throwing_input_iterator{src.data()},
      throwing_input_iterator{src.data() + src.size()}};
    (void)x;
  }, failure_point::increment);

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  test_operation("from_range, sized range, value_type throws", [] {
    const std::vector<int> src{1, 2, 3, 4, 5, 6, 7};
    virtual_vector x{8192, boost::container::from_range, src};
    (void)x;
  }, failure_point::construction);

  test_operation(
    "from_range, unsized forward range, value_type throws", [] {
    const std::forward_list<int> src{1, 2, 3, 4, 5, 6, 7};
    virtual_vector x{8192, boost::container::from_range, src};
    (void)x;
  }, failure_point::construction);

  test_operation("from_range, input range, increment throws", [] {
    const std::vector<int> src{1, 2, 3, 4, 5, 6, 7};
    auto rg = make_throwing_input_range(src);
    virtual_vector x{8192, boost::container::from_range, rg};
    (void)x;
  }, failure_point::increment);

  test_operation("from_range, input range, value_type throws", [] {
    const std::vector<int> src{1, 2, 3, 4, 5, 6, 7};
    auto rg = make_throwing_input_range(src);
    virtual_vector x{8192, boost::container::from_range, rg};
    (void)x;
  }, failure_point::construction);

  test_operation("from_range, reservation throws", [] {
    const std::vector<int> src{1, 2, 3};
    virtual_vector x{8192, boost::container::from_range, src};
    (void)x;
  }, failure_point::reservation);

  test_operation("from_range, commitment throws", [] {
    const std::vector<int> src{1, 2, 3};
    virtual_vector x{8192, boost::container::from_range, src};
    (void)x;
  }, failure_point::commitment);
#endif

  const auto src = make_vector(12);

  test_operation("copy constructor, value_type throws", [&] {
    virtual_vector x{src};
    (void)x;
  }, failure_point::construction);

  test_operation("copy constructor, reservation throws", [&] {
    virtual_vector x{src};
    (void)x;
  }, failure_point::reservation);

  test_operation("copy constructor, commitment throws", [&] {
    virtual_vector x{src};
    (void)x;
  }, failure_point::commitment);

  test_operation("move constructor, value_type throws",
    [] { return make_vector(8, 1); },
    [] (virtual_vector& src_) {
      virtual_vector x{std::move(src_), allocator_type{2}};
      (void)x;
    }, failure_point::construction, guarantee::basic);

  test_operation("move constructor, reservation throws",
    [] { return make_vector(8, 1); },
    [] (virtual_vector& src_) {
      virtual_vector x{std::move(src_), allocator_type{2}};
      (void)x;
    }, failure_point::reservation, guarantee::strong);

  test_operation("move constructor, commitment throws",
    [] { return make_vector(8, 1); },
    [] (virtual_vector& src_) {
      virtual_vector x{std::move(src_), allocator_type{2}};
      (void)x;
    }, failure_point::commitment, guarantee::strong);
}

void test_assignment()
{
  {
    const auto src = make_vector(12);
    const auto backup = snapshot(src);

    test_operation("copy assignment, construction throws",
      [] { return make_vector(3); },
      [&] (virtual_vector& x) { x = src; },
      failure_point::construction, guarantee::basic);

    test_operation("copy assignment, assignment throws",
      [] { return make_vector(8); },
      [&] (virtual_vector& x) { x = src; },
      failure_point::assignment, guarantee::basic);

    const auto big_src = make_vector(make_vector(1).capacity() + 5);
    const auto big_backup = snapshot(big_src);

    test_operation("copy assignment, commitment throws",
      [] { return make_vector(3); },
      [&] (virtual_vector& x) { x = big_src; },
      failure_point::commitment, guarantee::basic);

    test_operation("copy assignment, reservation throws",
      [] { return make_vector(3, 1, 16384); },
      [&] (virtual_vector& x) { x = src; },
      failure_point::reservation, guarantee::strong);

    check_equal(src, backup);
    check_equal(big_src, big_backup);
  }

  {
    test_operation("unequal-allocator move assignment, construction throws",
      [] { return make_vector(3, 2); },
      [] { return make_vector(12, 1); },
      [] (virtual_vector& dst, virtual_vector& src) { dst = std::move(src); },
      failure_point::construction, guarantee::basic, guarantee::basic);

    test_operation("unequal-allocator move assignment, assignment throws",
      [] { return make_vector(3, 2); },
      [] { return make_vector(12, 1); },
      [] (virtual_vector& dst, virtual_vector& src) { dst = std::move(src); },
      failure_point::assignment, guarantee::basic, guarantee::basic);

    const auto big_mc = make_vector(3, 2).capacity() + 5;
    test_operation("unequal-allocator move assignment, commitment throws",
      [] { return make_vector(3, 2); },
      [=] { return make_vector(big_mc, 1); },
      [] (virtual_vector& dst, virtual_vector& src) { dst = std::move(src); },
      failure_point::commitment, guarantee::basic, guarantee::basic);

    test_operation("unequal-allocator move assignment, reservation throws",
      [] { return make_vector(3, 2, 16384); },
      [] { return make_vector(8, 1); },
      [] (virtual_vector& dst, virtual_vector& src) { dst = std::move(src); },
      failure_point::reservation, guarantee::strong, guarantee::strong);
  }

  test_operation("range assign, construction throws",
    [] { return make_vector(3); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24, 25};
      x.assign(src.begin(), src.end());
    },
    failure_point::construction, guarantee::basic);

  test_operation("range assign, commitment throws",
    [] { return make_vector(3); },
    [] (virtual_vector& x) {
      std::vector<int> src(x.capacity() + 5, 42);
      x.assign(src.begin(), src.end());
    },
    failure_point::commitment, guarantee::basic);

  test_operation("range assign, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24, 25};
      x.assign(src.begin(), src.end());
    },
    failure_point::assignment, guarantee::basic);

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  test_operation("assign_range, value_type throws",
    [] { return make_vector(3); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24, 25};
      x.assign_range(src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("assign_range, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24, 25};
      x.assign_range(src);
    },
    failure_point::assignment, guarantee::basic);

  test_operation("assign_range, unsized forward range, value_type throws",
    [] { return make_vector(3); },
    [] (virtual_vector& x) {
      const std::forward_list<int> src{20, 21, 22, 23, 24, 25};
      x.assign_range(src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("assign_range, commitment throws",
    [] { return make_vector(3); },
    [] (virtual_vector& x) {
      const std::vector<int> src(x.capacity() + 5, 42);
      x.assign_range(src);
    },
    failure_point::commitment, guarantee::basic);

  test_operation("assign_range, input range, increment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24, 25};
      auto rg = make_throwing_input_range(src);
      x.assign_range(rg);
    },
    failure_point::increment, guarantee::basic);

  test_operation("assign_range, empty vector, input increment throws",
    [] { return make_vector(0); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      auto rg = make_throwing_input_range(src);
      x.assign_range(rg);
    },
    failure_point::increment, guarantee::strong);
#endif
}

void test_capacity_ops()
{
  test_operation("reserve, commitment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) { x.reserve(x.capacity() + 5); },
    failure_point::commitment, guarantee::strong);

  test_operation("resize, value_type throws across commitment",
    [] {
      auto x = make_full_vector();
      x.pop_back();
      x.pop_back();
      return x;
    },
    [] (virtual_vector& x) { x.resize(x.capacity() + 2); },
    failure_point::construction, guarantee::strong);

  test_operation("resize with aliased value, value_type throws",
    [] {
      auto x = make_full_vector();
      x.pop_back();
      x.pop_back();
      return x;
    },
    [] (virtual_vector& x) { x.resize(x.capacity() + 2, x[0]); },
    failure_point::construction, guarantee::strong);

  test_operation("resize, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) { x.resize(x.capacity() + 2); },
    failure_point::commitment, guarantee::strong);
}

void test_insertion()
{
  test_operation("emplace_back, value_type throws without growth",
    [] { return make_vector(8); },
    [] (virtual_vector& x) { x.emplace_back(42); },
    failure_point::construction, guarantee::strong);

  test_operation("emplace_back, value_type throws after growth",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) { x.emplace_back(42); },
    failure_point::construction, guarantee::contents);

  test_operation("emplace_back, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) { x.emplace_back(42); },
    failure_point::commitment, guarantee::strong);

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  test_operation("append_range, sized range, value_type throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24};
      x.append_range(src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("append_range, unsized forward range, value_type throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::forward_list<int> src{20, 21, 22, 23, 24};
      x.append_range(src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("append_range, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24};
      x.append_range(src);
    },
    failure_point::commitment, guarantee::strong);

  test_operation("append_range, input range, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      auto rg = make_throwing_input_range(src);
      x.append_range(rg);
    },
    failure_point::commitment, guarantee::strong);

  test_operation("append_range, input range, increment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      auto rg = make_throwing_input_range(src);
      x.append_range(rg);
    },
    failure_point::increment, guarantee::strong);

  test_operation("append_range, input range, value_type throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      auto rg = make_throwing_input_range(src);
      x.append_range(rg);
    },
    failure_point::construction, guarantee::strong);
#endif

  test_operation("middle emplace, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) { x.emplace(x.begin() + 4, 42); },
    failure_point::construction, guarantee::basic);

  test_operation("middle emplace, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) { x.emplace(x.begin() + 4, 42); },
    failure_point::assignment, guarantee::basic);

  test_operation("middle emplace, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) { x.emplace(x.end() - 3, 42); },
    failure_point::commitment, guarantee::strong);

  test_operation("count insert, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const throwing_value v{42};
      x.insert(x.begin() + 4, 3, v);
    },
    failure_point::construction, guarantee::basic);

  test_operation("count insert, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const throwing_value v{42};
      x.insert(x.begin() + 4, 3, v);
    },
    failure_point::assignment, guarantee::basic);

  test_operation("sized range insert, count <= tail, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21};
      x.insert(x.begin() + 4, src.begin(), src.end());
    },
    failure_point::construction, guarantee::basic);

  test_operation("sized range insert, count <= tail, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21};
      x.insert(x.begin() + 4, src.begin(), src.end());
    },
    failure_point::assignment, guarantee::basic);

  test_operation("sized range insert, count > tail, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24};
      x.insert(x.begin() + 6, src.begin(), src.end());
    },
    failure_point::construction, guarantee::basic);

  test_operation("sized range insert, count > tail, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24};
      x.insert(x.begin() + 6, src.begin(), src.end());
    },
    failure_point::assignment, guarantee::basic);

  test_operation("sized range insert, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22};
      x.insert(x.end() - 2, src.begin(), src.end());
    },
    failure_point::commitment, guarantee::strong);

  test_operation("single-pass range insert, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      std::istringstream is{"20 21 22 23 24"};
      x.insert(x.begin() + 4, std::istream_iterator<int>{is},
        std::istream_iterator<int>{});
    },
    failure_point::construction, guarantee::basic);

  test_operation("single-pass range insert, increment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      x.insert(x.begin() + 2,
        throwing_input_iterator{src.data()},
        throwing_input_iterator{src.data() + src.size()});
    },
    failure_point::increment, guarantee::strong);

  test_operation("single-pass range insert, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      std::istringstream is{"20 21 22 23 24"};
      x.insert(x.begin() + 4, std::istream_iterator<int>{is},
        std::istream_iterator<int>{});
    },
    failure_point::assignment, guarantee::basic);


#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  test_operation("insert_range, count <= tail, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21};
      x.insert_range(x.begin() + 4, src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("insert_range, count <= tail, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21};
      x.insert_range(x.begin() + 4, src);
    },
    failure_point::assignment, guarantee::basic);

  test_operation("insert_range, count > tail, construction throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24};
      x.insert_range(x.begin() + 6, src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("insert_range, count > tail, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22, 23, 24};
      x.insert_range(x.begin() + 6, src);
    },
    failure_point::assignment, guarantee::basic);

  test_operation("insert_range, unsized forward range, value_type throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) {
      const std::forward_list<int> src{20, 21, 22, 23, 24};
      x.insert_range(x.begin() + 6, src);
    },
    failure_point::construction, guarantee::basic);

  test_operation("insert_range, commitment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src{20, 21, 22};
      x.insert_range(x.end() - 2, src);
    },
    failure_point::commitment, guarantee::strong);

  test_operation("insert_range, input range, increment throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      auto rg = make_throwing_input_range(src);
      x.insert_range(x.begin() + 2, rg);
    },
    failure_point::increment, guarantee::strong);

  test_operation("insert_range, input range, value_type throws",
    [] { return make_full_vector(); },
    [] (virtual_vector& x) {
      const std::vector<int> src(40, 42);
      auto rg = make_throwing_input_range(src);
      x.insert_range(x.begin() + 2, rg);
    },
    failure_point::construction, guarantee::basic);
#endif
}

void test_erasure()
{
  test_operation("erase, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) { x.erase(x.begin() + 2); },
    failure_point::assignment, guarantee::basic);

  test_operation("range erase, assignment throws",
    [] { return make_vector(8); },
    [] (virtual_vector& x) { x.erase(x.begin() + 2, x.begin() + 4); },
    failure_point::assignment, guarantee::basic);
}

int main()
{
  test_construction();
  test_assignment();
  test_capacity_ops();
  test_insertion();
  test_erasure();
  check_no_leaks(0, 0);

  return boost::report_errors();
}
