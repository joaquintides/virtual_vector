/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <algorithm>
#include <boost/config.hpp>
#include <boost/config/workaround.hpp>
#include <boost/container/virtual_vector.hpp>
#include <boost/core/lightweight_test.hpp>
#include <cstddef>
#include <forward_list>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <type_traits>
#include <utility>
#include "utility.hpp"

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
#include <ranges>
#endif

enum tracked_provenance { ab_ovo = 0, from_copy, from_move };

template<typename T>
struct tracked
{
  tracked(const T& x_): x{x_}, origin{from_copy} {}
  tracked(T&& x_): x{std::move(x_)}, origin{from_move} {}
  tracked(const tracked& x_):
    x{x_.x}, origin{x_.origin}, last_op{from_copy} {}
  tracked(tracked&& x_):
    x{std::move(x_.x)}, origin{x_.origin}, last_op{from_move} {}

  tracked& operator=(const tracked& x_)
  {
    x = x_.x;
    origin = x_.origin;
    last_op = from_copy;
    return *this;
  }
  tracked& operator=(tracked&& x_)
  {
    x = x_.x;
    origin = x_.origin;
    last_op = from_move;
    return *this;
  }

  T x;
  tracked_provenance origin, last_op = ab_ovo;
};

template<typename T>
class fancy_ptr
{
public:
  using pointer = fancy_ptr;
  using element_type = T;
  using value_type = typename std::remove_cv<T>::type;
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using reference = typename std::add_lvalue_reference<T>::type;

  fancy_ptr() noexcept: p_{} {}
  fancy_ptr(std::nullptr_t) noexcept: p_{} {}
  explicit fancy_ptr(T* p) noexcept: p_{p} {}
  template<
    typename U, 
    typename std::enable_if<
      std::is_convertible<U*, T*>::value>::type* = nullptr
  >
  fancy_ptr(fancy_ptr<U> p) noexcept: p_{p.get()} {}
  fancy_ptr& operator=(std::nullptr_t) noexcept { p_ = nullptr; return *this; }

  explicit operator bool() const noexcept { return p_ != nullptr; }

  bool operator==(std::nullptr_t) const noexcept { return p_ == nullptr; }
  friend bool operator==(std::nullptr_t, fancy_ptr p) noexcept
  { return p == nullptr; }
  bool operator!=(std::nullptr_t) const noexcept 
  { return !(*this == nullptr); }
  friend bool operator!=(std::nullptr_t, fancy_ptr p) noexcept
  { return !(p == nullptr); }

  template<typename U>
  bool operator==(fancy_ptr<U> x) const noexcept { return get() == x.get(); }
  template<typename U>
  bool operator!=(fancy_ptr<U> x) const noexcept { return !(*this == x); }
  template<typename U>
  bool operator< (fancy_ptr<U> x) const noexcept { return get() < x.get(); }
  template<typename U>
  bool operator> (fancy_ptr<U> x) const noexcept { return x < *this; }
  template<typename U>
  bool operator<=(fancy_ptr<U> x) const noexcept { return !(x < *this); }
  template<typename U>
  bool operator>=(fancy_ptr<U> x) const noexcept { return !(*this < x); }

  friend std::ptrdiff_t operator-(fancy_ptr x, fancy_ptr y) noexcept 
  { return x.get() - y.get(); }
  friend fancy_ptr operator+(std::ptrdiff_t n, fancy_ptr p) noexcept
  { return p + n; }
  template<typename U = T>
  static fancy_ptr pointer_to(U& x) noexcept
  { return fancy_ptr{std::addressof(x)}; }

  T*        get() const noexcept { return p_; }
  reference operator*() const noexcept { return *p_; }
  T*        operator->() const noexcept { return p_; }
  reference operator[](difference_type n) const noexcept { return p_[n]; }

  fancy_ptr& operator++() noexcept { ++p_; return *this; }
  fancy_ptr  operator++(int) noexcept { auto x = *this; ++*this; return x; }
  fancy_ptr& operator--() noexcept { --p_; return *this; }
  fancy_ptr  operator--(int) noexcept { auto x = *this; --*this; return x; }
  fancy_ptr& operator+=(difference_type n) noexcept { p_ += n; return *this; }
  fancy_ptr& operator-=(difference_type n) noexcept { p_ -= n; return *this; }
  fancy_ptr  operator+(difference_type n) const noexcept
  { auto x = *this; return x += n; }
  fancy_ptr  operator-(difference_type n) const noexcept
  { auto x = *this; return x -= n; }

private:
  T* p_;
};

template<typename T>
struct fancy_virtual_allocator: boost::container::virtual_allocator<T>
{
  using base = boost::container::virtual_allocator<T>;
  using pointer = fancy_ptr<T>;
  using const_pointer = fancy_ptr<const T>;
  using size_type = typename base::size_type;
  using reservation_handle = typename base::reservation_handle;

  fancy_virtual_allocator() = default;
  template<typename U>
  fancy_virtual_allocator(const fancy_virtual_allocator<U>&) noexcept {}

  pointer allocate(size_type n) { return pointer{base::allocate(n)}; }

  void deallocate(pointer p, size_type n) noexcept
  { base::deallocate(p.get(), n); }

  pointer data(const reservation_handle& h) const noexcept
  { return pointer{base::data(h)}; }
};

template<typename Iterator, typename T>
void test_traversal(Iterator first, Iterator last, T* data)
{
  std::ptrdiff_t n = 0;
  for(auto it = first; it != last; ++it, ++n) {
    BOOST_TEST(first[n] == data[n]);
    BOOST_TEST(*it == data[n]);
    BOOST_TEST_EQ(it - first, n);
    BOOST_TEST_EQ(first - it, -n);
    BOOST_TEST(first + n == it);
    BOOST_TEST(n + first == it);
    BOOST_TEST(it - n == first);
    BOOST_TEST((first == it) == (0 == n));
    BOOST_TEST((first != it) == (0 != n));
    BOOST_TEST((first < it) == (0 < n));
    BOOST_TEST((first > it) == (0 > n));
    BOOST_TEST((first >= it) == (0 >= n));
    BOOST_TEST((first <= it) == (0 <= n));
    auto it1 = it, it2 = ++it1, it3 = --it2, it4 = it3++, it5 = it3--;
    BOOST_TEST(it1 == it + 1);
    BOOST_TEST(it2 == it);
    BOOST_TEST(it3 == it);
    BOOST_TEST(it4 == it);
    BOOST_TEST(it5 == it + 1);
  }
}

template<typename T> void avoid_unused_local_typedef() {}

template<typename Vector>
void test()
{
  using value_type = typename Vector::value_type;
  using allocator_type = typename Vector::allocator_type;
  using pointer = typename Vector::pointer;
  using const_pointer = typename Vector::const_pointer;
  using reference = typename Vector::reference;
  using const_reference = typename Vector::const_reference;
  using size_type = typename Vector::size_type;
  using difference_type = typename Vector::difference_type;
  using iterator = typename Vector::iterator;
  using const_iterator = typename Vector::const_iterator;
  using reverse_iterator = typename Vector::reverse_iterator;
  using const_reverse_iterator = typename Vector::const_reverse_iterator;

  static_assert(
    noexcept(Vector(std::declval<Vector&&>())),
    "move construction must be noexcept");
  static_assert(
    noexcept(std::declval<Vector&>() = std::declval<Vector&&>()),
    "move assignment must be noexcept");
  static_assert(noexcept(
    std::declval<Vector&>().swap(std::declval<Vector&>())),
    "swap must be noexcept");
  avoid_unused_local_typedef<pointer>();
  avoid_unused_local_typedef<const_pointer>();
  avoid_unused_local_typedef<reference>();
  avoid_unused_local_typedef<const_reference>();
  avoid_unused_local_typedef<size_type>();
  avoid_unused_local_typedef<reverse_iterator>();
  avoid_unused_local_typedef<const_reverse_iterator>();

  const std::size_t                 mc = 2048;
  const allocator_type              al{};
  auto                              rng = make_range<value_type>(20);
  std::initializer_list<value_type> il{rng[5], rng[1], rng[7]};
  std::vector<value_type>           zeros(7, value_type());
  std::vector<value_type>           repeated(10, rng[10]);

  /* construct/copy/destroy */
  {
    Vector x{mc};
    BOOST_TEST(x.empty());
    BOOST_TEST_EQ(x.max_size(), mc);
  }
  {
    Vector x{0};
    BOOST_TEST(x.empty());
    BOOST_TEST_EQ(x.max_size(), 0u);
    BOOST_TEST_EQ(x.capacity(), 0u);
    BOOST_TEST(x.begin() == x.end());
    Vector y{x};
    BOOST_TEST(y.empty());
    Vector z{mc};
    z = x;
    BOOST_TEST(z.empty());
    BOOST_TEST_EQ(z.max_size(), 0u);
    BOOST_TEST_THROWS(x.emplace_back(value_type(1)), std::bad_alloc);
    BOOST_TEST(x.empty());
  }
  {
    Vector x{mc, al};
    BOOST_TEST(x.empty());
  }
  {
    Vector x{mc, (size_type)zeros.size()},
           y{mc, (size_type)zeros.size(), al};
    test_equal(x, zeros);
    BOOST_TEST(x == y);
  }
  {
    Vector x{mc, (size_type)repeated.size(), repeated[0]},
           y{mc, (size_type)repeated.size(), repeated[0], al};
    test_equal(x, repeated);
    BOOST_TEST(x == y);
  }
  {
    /* value convertible to both size_type and value_type */
    Vector x(mc, 20, 20);
    BOOST_TEST_EQ(x.size(), 20);
    BOOST_TEST(std::count(x.begin(), x.end(), value_type(20)) == 20);
  }
  {
    Vector x{mc, rng.begin(), rng.end()},
           y{mc, rng.begin(), rng.end(), al};
    test_equal(x, rng);
    BOOST_TEST(x == y);
  }
#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  { 
    using boost::container::from_range;
    Vector x{mc, from_range, rng};
    Vector y{mc, from_range, rng, allocator_type{}};
    test_equal(x, rng);
    test_equal(y, rng);
  }
#endif
  {
    const Vector x{mc, rng.begin(), rng.end()};
    Vector       y{x}, z{y, al};
    BOOST_TEST(x == y);
    BOOST_TEST(x == z);
    BOOST_TEST_EQ(y.max_size(), x.max_size());
    BOOST_TEST_EQ(z.max_size(), x.max_size());
  }
  {
    Vector x{mc, rng.begin(), rng.end()};
    Vector y{std::move(x)};
    BOOST_TEST(x.empty());
    test_equal(y, rng);
    Vector z{std::move(y), al};
    BOOST_TEST(y.empty());
    test_equal(z, rng);
  }
  {
    Vector x{mc, il}, y{mc, il, al};
    test_equal(x, il);
    BOOST_TEST(x == y);
  }
  {
    Vector  x{mc, rng.begin(), rng.end()};
    Vector  y{mc};
    Vector& ry = (y = x);
    BOOST_TEST(&ry == &y);
    BOOST_TEST(x == y);
  }
  {
    Vector x{mc, rng.begin(), rng.end()};
    Vector y{mc / 2, rng.begin(), rng.begin() + 3};
    y = x;
    test_equal(y, rng);
    BOOST_TEST_EQ(y.max_size(), mc);
    Vector& self = y;
    y = self;
    test_equal(y, rng);
    x.assign(rng.begin(), rng.begin() + 3);
    test_equal(x, std::vector<value_type>(rng.begin(), rng.begin() + 3));
    x.assign(rng.begin(), rng.end());
    test_equal(x, rng);
  }
  {
    Vector  x{mc, rng.begin(), rng.end()};
    Vector  y{mc};
    Vector& ry = (y = std::move(x));
    BOOST_TEST(&ry == &y);
    BOOST_TEST(x.empty());
    test_equal(y, rng);
  }
  {
    Vector x{mc, rng.begin(), rng.end()};
    Vector y{mc / 2, rng.begin(), rng.begin() + 3};
    auto base = x.data();
    y = std::move(x);
    test_equal(y, rng);
    BOOST_TEST_EQ(y.max_size(), mc);
    BOOST_TEST_EQ(y.data(), base);
    BOOST_TEST(x.empty());
    BOOST_TEST_EQ(x.max_size(), 0u);
    Vector& self = y;
    y = std::move(self);
    test_equal(y, rng);
  }
  {
    Vector  x{mc};
    Vector& rx = (x = il);
    BOOST_TEST(&rx == &x);
    test_equal(x, il);
  }
  {
    Vector x{mc};
    x.assign(rng.begin(), rng.end());
    test_equal(x, rng);
  }
#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  { 
    Vector x{mc};
    x.assign_range(rng);
    test_equal(x, rng);
    std::vector<typename Vector::value_type> short_rng(
      rng.begin(), rng.begin() + rng.size() / 2);
    x.assign_range(short_rng);
    test_equal(x, short_rng);
    x.assign_range(rng);
    test_equal(x, rng);
  }
#endif
  {
    Vector x{mc};
    x.assign((size_type)repeated.size(), repeated[0]);
    test_equal(x, repeated);
  }
  {
    Vector x{mc, rng.begin(), rng.end()};
    x.assign((size_type)5, rng[12]);
    test_equal(x, std::vector<value_type>(5, rng[12]));
    x.assign((size_type)25, rng[4]);
    test_equal(x, std::vector<value_type>(25, rng[4]));
    x.assign(rng.begin(), rng.begin() + 2);
    test_equal(x, std::vector<value_type>(rng.begin(), rng.begin() + 2));
  }
  {
    Vector x{mc};
    x.assign(il);
    test_equal(x, il);
  }
  {
    const Vector x{mc, al};
    BOOST_TEST(x.get_allocator() == al);
  }

  /* iterators */
  {
    Vector        x{mc, rng.begin(), rng.end()};
    const Vector& cx = x;
    BOOST_TEST_EQ(std::addressof(*x.begin()), x.data());
    BOOST_TEST_EQ(std::addressof(*cx.begin()), x.data());
    BOOST_TEST_EQ(std::addressof(*(x.end() - 1)), x.data() + x.size() - 1);
    BOOST_TEST_EQ(std::addressof(*(cx.end() - 1)), x.data() + x.size() - 1);
    BOOST_TEST(x.rbegin().base() == x.end());
    BOOST_TEST(cx.rbegin().base() == cx.end());
    BOOST_TEST(x.rend().base() == x.begin());
    BOOST_TEST(cx.rend().base() == cx.begin());
    BOOST_TEST(cx.cbegin() == cx.begin());
    BOOST_TEST(cx.cend() == cx.end());
    BOOST_TEST(cx.crbegin() == cx.rbegin());
    BOOST_TEST(cx.crend() == cx.rend());
    iterator       it = x.begin(), it2 = x.end();
    const_iterator cit = it;
    BOOST_TEST(cit == it);
    cit = it2;
    BOOST_TEST(cit == it2);
    it = it2;
    BOOST_TEST(it == it2);
    test_traversal(x.begin(), x.end(), x.data());
    test_traversal(x.cbegin(), x.cend(), x.data());
    BOOST_TEST(std::equal(x.rbegin(), x.rend(), rng.rbegin()));
    BOOST_TEST(std::equal(cx.rbegin(), cx.rend(), rng.rbegin()));
    BOOST_TEST(std::equal(x.crbegin(), x.crend(), rng.rbegin()));
    BOOST_TEST(std::equal(cx.crbegin(), cx.crend(), rng.rbegin()));
  }
  {
    /* operator-> */
    rebind_value_type_t<Vector, std::pair<int, int>> x{mc};
    x.emplace_back(18, 42);
    BOOST_TEST_EQ(x.begin()->first, 18);
    BOOST_TEST_EQ(x.cbegin()->second, 42);
  }
  {
    Vector        x{mc};
    const Vector& cx = x;
    BOOST_TEST_EQ(cx.max_size(), mc);
    x.reserve(1000);
    BOOST_TEST_GE(cx.capacity(), 1000u);
    auto base = x.data();
    x.insert(x.end(), rng.begin(), rng.end());
    BOOST_TEST(!cx.empty());
    BOOST_TEST_EQ(cx.size(), rng.size());
    BOOST_TEST_EQ(cx.data(), base);
    x.resize(rng.size() / 2);
    BOOST_TEST_EQ(cx.size(), rng.size() / 2);
    x.resize(rng.size());
    BOOST_TEST_EQ(cx.size(), rng.size());
    BOOST_TEST((
      std::count(
        x.begin() + (difference_type)(rng.size() / 2),
        x.end(), value_type()) ==
      (difference_type)(rng.size() - rng.size() / 2)));
    x.resize(2 * rng.size(), rng[5]);
    BOOST_TEST((
      std::count(x.begin() + (difference_type)rng.size(), x.end(), rng[5]) ==
      (difference_type)rng.size()));
    Vector x2 = x;
    x.shrink_to_fit();
    BOOST_TEST(x == x2);
    BOOST_TEST_EQ(cx.size(), 2 * rng.size());
    BOOST_TEST_GE(cx.capacity(), 2 * rng.size());
    BOOST_TEST_EQ(cx.data(), base);
    BOOST_TEST_EQ(cx.max_size(), mc);
    x.reserve(10); // cannot decommit below size()
    BOOST_TEST_GE(cx.capacity(), cx.size());
    BOOST_TEST_EQ(cx.data(), base);
  }
  {
    const size_type big_max_capacity = (size_type)(mc * 64);
    Vector x{big_max_capacity};
    x.reserve(big_max_capacity);
    auto full_capacity = x.capacity();
    auto base = x.data();
    x.resize(10);
    x.shrink_to_fit();
    BOOST_TEST_LT(x.capacity(), full_capacity);
    BOOST_TEST_GE(x.capacity(), x.size());
    BOOST_TEST_EQ(x.data(), base);
    x.reserve(big_max_capacity);
    BOOST_TEST_EQ(x.data(), base);
  }
  {
    Vector x{2};
    x.emplace_back(value_type(1));
    x.emplace_back(value_type(2));
    BOOST_TEST_THROWS(x.emplace_back(value_type(3)), std::bad_alloc);
    BOOST_TEST_EQ(x.size(), 2u);
    BOOST_TEST_EQ(x.front(), value_type(1));
    BOOST_TEST_EQ(x.back(), value_type(2));
  }

  /* element access */
  {
    Vector        x{mc, rng.begin(), rng.end()};
    const Vector& cx = x;
    auto          n = x.size();
    auto          data = x.data();
    BOOST_TEST_EQ(std::addressof(x[n / 2]), std::addressof(data[n / 2]));
    BOOST_TEST_EQ(std::addressof(cx[n / 2]), std::addressof(data[n / 2]));
    BOOST_TEST_EQ(std::addressof(x.at(n / 2)), std::addressof(data[n / 2]));
    BOOST_TEST_EQ(std::addressof(cx.at(n / 2)), std::addressof(data[n / 2]));
    BOOST_TEST_THROWS((void)x.at(n), std::out_of_range);
    BOOST_TEST_THROWS((void)cx.at(n), std::out_of_range);
    BOOST_TEST_EQ(std::addressof(x.front()), std::addressof(data[0]));
    BOOST_TEST_EQ(std::addressof(cx.front()), std::addressof(data[0]));
    BOOST_TEST_EQ(std::addressof(x.back()), std::addressof(data[n - 1]));
    BOOST_TEST_EQ(std::addressof(cx.back()), std::addressof(data[n - 1]));
  }

  /* data access */
  {
    Vector        x{mc, rng.begin(), rng.end()};
    const Vector& cx = x;
    auto          n = x.size();
    auto          data = x.data();
    auto          cdata = cx.data();
    BOOST_TEST_EQ(data, cdata);
    data[n / 2] += value_type(1);
    BOOST_TEST(x[n / 2] == data[n / 2]);
  }

  /* modifiers */
  using tracked_vector = rebind_value_type_t<Vector, tracked<value_type>>;
  using tracked_value_type = tracked<value_type>;
  {
    tracked_vector     x{mc};
    tracked_value_type v{value_type{}};
    x.emplace_back(v.x);
    BOOST_TEST(x.back().x == v.x);
    BOOST_TEST(x.back().origin == from_copy);
    v.x += value_type(1);
    tracked_value_type probe{value_type{v.x}};
    BOOST_TEST_EQ(probe.origin, from_move);
    x.emplace_back(value_type{v.x});
    BOOST_TEST(x.back().x == v.x);
    BOOST_TEST_EQ(x.back().origin, from_move);
    BOOST_TEST_EQ(x.back().last_op, ab_ovo);
    v.x += value_type(1);
    x.push_back(v);
    BOOST_TEST(x.back().x == v.x);
    BOOST_TEST(x.back().last_op == from_copy);
    v.x += value_type(1);
    x.push_back(tracked_value_type{v});
    BOOST_TEST(x.back().x == v.x);
    BOOST_TEST(x.back().last_op == from_move);
    auto  s = x.size();
    auto& r = *(x.end() - 2);
    x.pop_back();
    BOOST_TEST_EQ(x.size(), s - 1);
    BOOST_TEST_EQ(std::addressof(x.back()), std::addressof(r));
  }
#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  { 
    Vector x{mc};
    x.append_range(rng);
    test_equal(x, rng);
    x.append_range(rng);
    BOOST_TEST_EQ(x.size(), 2 * rng.size());
    BOOST_TEST(std::equal(x.begin(), x.begin() + rng.size(), rng.begin()));
    BOOST_TEST(std::equal(x.begin() + rng.size(), x.end(), rng.begin()));
  }
#endif
  {
    tracked_vector x{mc};
    auto it = x.emplace(x.end(), value_type(2));
    BOOST_TEST_EQ(it->x, 2);
    BOOST_TEST(it->origin == from_move);
    it = x.emplace(it, value_type(3));
    BOOST_TEST_EQ(it->x, 3);
    BOOST_TEST(it->origin == from_move);
    BOOST_TEST_EQ(x.cend() - it, 2);
    tracked_value_type v{value_type(4)};
    it = x.insert(it, v);
    BOOST_TEST_EQ(it->x, 4);
    BOOST_TEST(it->origin == from_move);
    BOOST_TEST(it->last_op == from_move);
    BOOST_TEST_EQ(x.cend() - it, 3);
    v = *it;
    v.x += value_type(1);
    it = x.insert(it + 1, std::move(v));
    BOOST_TEST_EQ(it->x, 5);
    BOOST_TEST(it->last_op == from_move);
    BOOST_TEST_EQ(x.cend() - it, 3);
  }
  {
    /* fill insertion: empty, short tail, long tail and end */
    Vector x{mc, rng.begin(), rng.begin() + 5};
    std::vector<value_type> expected(rng.begin(), rng.begin() + 5);
    auto it = x.insert(x.cbegin() + 2, (size_type)0, rng[7]);
    BOOST_TEST(it == x.begin() + 2);
    test_equal(x, expected);
    it = x.insert(x.cbegin() + 2, (size_type)2, rng[7]);
    expected.insert(expected.begin() + 2, 2, rng[7]);
    BOOST_TEST(it == x.begin() + 2);
    test_equal(x, expected);
    it = x.insert(x.cbegin() + 1, (size_type)7, rng[8]);
    expected.insert(expected.begin() + 1, 7, rng[8]);
    BOOST_TEST(it == x.begin() + 1);
    test_equal(x, expected);
    it = x.insert(x.cend(), (size_type)3, rng[4]);
    expected.insert(expected.end(), 3, rng[4]);
    BOOST_TEST(it == x.end() - 3);
    test_equal(x, expected);
  }
  {
    /* single-element and fill insertion must handle self-aliasing */
    Vector x{mc, rng.begin(), rng.end()};
    std::vector<value_type> expected(rng);
    auto value = expected[10];
    auto it = x.insert(x.cbegin() + 2, x[10]);
    expected.insert(expected.begin() + 2, value);
    BOOST_TEST(it == x.begin() + 2);
    test_equal(x, expected);
    value = expected[12];
    it = x.emplace(x.cbegin() + 4, x[12]);
    expected.insert(expected.begin() + 4, value);
    BOOST_TEST(it == x.begin() + 4);
    test_equal(x, expected);
    value = expected[15];
    it = x.insert(x.cbegin() + 3, (size_type)4, x[15]);
    expected.insert(expected.begin() + 3, 4, value);
    BOOST_TEST(it == x.begin() + 3);
    test_equal(x, expected);
  }
  {
    Vector x{mc};
    auto it = x.insert(x.cend(), rng.begin(), rng.begin());
    BOOST_TEST(x.empty());
    BOOST_TEST(it == x.cend());
    it = x.insert(x.end(), rng.begin() + rng.size() * 2 / 3, rng.end());
    it = x.insert(it, rng.begin(), rng.begin() + rng.size() * 1 / 3);
    it = x.insert(
      it + rng.size() * 1 / 3,
      rng.begin() + rng.size() * 1 / 3, rng.begin() + rng.size() * 2 / 3);
    BOOST_TEST_EQ(it - x.cbegin(), rng.size() * 1 / 3);
    test_equal(x, rng);
  }
#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  { 
    Vector x{mc};
    x.insert_range(x.cend(), rng);
    test_equal(x, rng);
    x.insert_range(x.cbegin() + 1, rng);
    x.erase(x.cbegin());
    x.erase(x.cbegin() + rng.size(), x.cend());
    test_equal(x, rng);
  }
#endif
  {
    Vector x{mc};
    x.insert(x.cend(), il);
    test_equal(x, il);
    x.insert(x.cbegin() + 1, il);
    x.erase(x.cbegin());
    x.erase(x.cbegin() + il.size(), x.cend());
    test_equal(x, il);
  }
  {
    Vector x{mc, rng.begin(), rng.end()};
    auto it = x.erase(x.cbegin());
    BOOST_TEST_EQ(x.size(), rng.size() - 1);
    BOOST_TEST(*it == rng[1]);
    it = x.erase(x.cend(), x.cend());
    BOOST_TEST_EQ(x.size(), rng.size() - 1);
    BOOST_TEST(it == x.cend());
    it = x.erase(x.cend() - 1, x.cend() - 1);
    BOOST_TEST_EQ(x.size(), rng.size() - 1);
    BOOST_TEST(it == x.cend() - 1);
    it = x.erase(x.cbegin() + x.size() / 2, x.cend());
    BOOST_TEST_EQ(x.size(), (rng.size() - 1) / 2);
    BOOST_TEST(it == x.cend());
  }
  {
    /* max capacity is swapped */
    Vector x{mc, rng.begin(), rng.end()};
    Vector y{mc / 2, rng.begin(), rng.begin() + 3};
    auto px = x.data(), py = y.data();
    x.swap(y);
    BOOST_TEST_EQ(x.max_size(), mc / 2);
    BOOST_TEST_EQ(y.max_size(), mc);
    BOOST_TEST_EQ(x.data(), py);
    BOOST_TEST_EQ(y.data(), px);
    test_equal(x, std::vector<value_type>(rng.begin(), rng.begin() + 3));
    test_equal(y, rng);
  }
  {
    Vector x0{mc, rng.begin(), rng.end()},
           y0{mc, rng.begin(), rng.begin() + rng.size() / 2},
           x = x0, y = y0;
    x.swap(x);
    BOOST_TEST(x == x0);
    swap(x, x);
    BOOST_TEST(x == x0);
    x.swap(y);
    BOOST_TEST(x == y0);
    BOOST_TEST(y == x0);
    swap(x, y);
    BOOST_TEST(x == x0);
    BOOST_TEST(y == y0);
  }
  {
    Vector x{mc, rng.begin(), rng.end()};
    x.clear();
    BOOST_TEST(x.empty());
    x.clear();
    BOOST_TEST(x.empty());
  }

  /* input from non-random-access iterators supported */
  {
    std::istringstream in{"1 2 3 4"};
    Vector x{
      mc,
      std::istream_iterator<int>(in), std::istream_iterator<int>()};
    std::vector<value_type> expected{1, 2, 3, 4};
    test_equal(x, expected);

    std::istringstream more{"7 8"};
    auto it = x.insert(
      x.cbegin() + 2,
      std::istream_iterator<int>(more), std::istream_iterator<int>());
    expected.insert(expected.begin() + 2, {7, 8});
    BOOST_TEST(it == x.begin() + 2);
    test_equal(x, expected);

    std::istringstream replacement{"5 6"};
    x.assign(
      std::istream_iterator<int>(replacement), std::istream_iterator<int>());
    test_equal(x, std::vector<value_type>{5, 6});
  }
  {
    std::forward_list<value_type> fl{9, 10, 11};
    Vector x{mc, fl.begin(), fl.end()};
    std::vector<value_type> expected{9, 10, 11};
    test_equal(x, expected);
    auto it = x.insert(x.cbegin() + 1, fl.begin(), fl.end());
    expected.insert(expected.begin() + 1, fl.begin(), fl.end());
    BOOST_TEST(it == x.begin() + 1);
    test_equal(x, expected);
  }

  /* comparison */
  {
    const Vector x1{mc},
                 x2{mc, rng.begin(), rng.begin() + rng.size()/2},
                 x3{mc, rng.begin(), rng.end()};
    BOOST_TEST(  x1 == x1 ); BOOST_TEST(!(x1 == x2)); BOOST_TEST(!(x1 == x3));
    BOOST_TEST(!(x2 == x1)); BOOST_TEST(  x2 == x2 ); BOOST_TEST(!(x2 == x3));
    BOOST_TEST(!(x3 == x1)); BOOST_TEST(!(x3 == x2)); BOOST_TEST(  x3 == x3 );

    BOOST_TEST(!(x1 != x1)); BOOST_TEST(  x1 != x2 ); BOOST_TEST(  x1 != x3 );
    BOOST_TEST(  x2 != x1 ); BOOST_TEST(!(x2 != x2)); BOOST_TEST(  x2 != x3 );
    BOOST_TEST(  x3 != x1 ); BOOST_TEST(  x3 != x2 ); BOOST_TEST(!(x3 != x3));
    BOOST_TEST(!(x1 <  x1)); BOOST_TEST(  x1 <  x2 ); BOOST_TEST(  x1 <  x3 );
    BOOST_TEST(!(x2 <  x1)); BOOST_TEST(!(x2 <  x2)); BOOST_TEST(  x2 <  x3 );
    BOOST_TEST(!(x3 <  x1)); BOOST_TEST(!(x3 <  x2)); BOOST_TEST(!(x3 <  x3));

    BOOST_TEST(!(x1 >  x1)); BOOST_TEST(!(x1 >  x2)); BOOST_TEST(!(x1 >  x3));
    BOOST_TEST(  x2 >  x1 ); BOOST_TEST(!(x2 >  x2)); BOOST_TEST(!(x2 >  x3));
    BOOST_TEST(  x3 >  x1 ); BOOST_TEST(  x3 >  x2 ); BOOST_TEST(!(x3 >  x3));
    BOOST_TEST(  x1 <= x1 ); BOOST_TEST(  x1 <= x2 ); BOOST_TEST(  x1 <= x3 );
    BOOST_TEST(!(x2 <= x1)); BOOST_TEST(  x2 <= x2 ); BOOST_TEST(  x2 <= x3 );
    BOOST_TEST(!(x3 <= x1)); BOOST_TEST(!(x3 <= x2)); BOOST_TEST(  x3 <= x3 );

    BOOST_TEST(  x1 >= x1 ); BOOST_TEST(!(x1 >= x2)); BOOST_TEST(!(x1 >= x3));
    BOOST_TEST(  x2 >= x1 ); BOOST_TEST(  x2 >= x2 ); BOOST_TEST(!(x2 >= x3));
    BOOST_TEST(  x3 >= x1 ); BOOST_TEST(  x3 >= x2 ); BOOST_TEST(  x3 >= x3 );
  }
#if !defined(BOOST_NO_CXX20_HDR_COMPARE)
  {
    const Vector x{mc, rng.begin(), rng.begin() + 3};
    const Vector y{mc, rng.begin(), rng.begin() + 4};
    BOOST_TEST((x <=> y) <  0);
    BOOST_TEST((y <=> x) >  0);
    BOOST_TEST((x <=> x) == 0);
  }
#endif

  /* (global) erasure */
  {
    Vector      x{mc};
    auto        even = [](const value_type& v) { return (int)(v) % 2 == 0; };
    const auto& odd_value = *std::find_if_not(rng.begin(), rng.end(), even);

    BOOST_TEST_EQ(erase(x, odd_value), 0);
    BOOST_TEST_EQ(x.size(), 0);
    BOOST_TEST_EQ(erase_if(x, even), 0);
    BOOST_TEST_EQ(x.size(), 0);
    x.insert(x.cbegin(), rng.begin(), rng.end());
    auto s = x.size();
    auto n = erase(x, odd_value);
    BOOST_TEST_EQ(
      n, (size_type)std::count(rng.begin(), rng.end(), odd_value));
    BOOST_TEST_EQ(std::count(x.begin(), x.end(), odd_value), 0);
    BOOST_TEST_EQ(x.size(), s - n);
    s = x.size();
    n = erase_if(x, even);
    BOOST_TEST_EQ(
      n, (size_type)std::count_if(rng.begin(), rng.end(), even));
    BOOST_TEST_EQ(std::count_if(x.begin(), x.end(), even), 0);
    BOOST_TEST_EQ(x.size(), s - n);
  }
}

void test_three_way_fallback()
{
#if !defined(BOOST_NO_CXX20_HDR_COMPARE)
  struct less_only
  {
    less_only(int n_): n{n_} {}
    bool operator<(const less_only& x) const { return n < x.n; }
    int n;
  };

  boost::container::virtual_vector<less_only> x{16}, y{16};
  x.emplace_back(3);
  y.emplace_back(4);
  static_assert(std::is_same<decltype(x <=> y), std::weak_ordering>::value);
  BOOST_TEST((x <=> y) < 0);
  BOOST_TEST((y <=> x) > 0);
  BOOST_TEST((x <=> x) == 0);
#endif
}

void test_ctad()
{
#if !defined(BOOST_NO_CXX17_DEDUCTION_GUIDES) && \
    !BOOST_WORKAROUND(BOOST_CLANG_VERSION, < 90001)
  using boost::container::virtual_vector;
  using boost::container::virtual_allocator;

  const std::size_t mc = 2048;

  std::vector<int> rng({0, 1, 2, 3});
  virtual_vector x1(mc, {0, 1, 2, 3});
  virtual_vector x2(mc, {0, 1, 2, 3}, virtual_allocator<int>{});
  virtual_vector x3(mc, rng.begin(), rng.end());
  virtual_vector x4(
    mc, rng.begin(), rng.end(), virtual_allocator<int>{});
  test_equal(x1, rng);
  test_equal(x2, rng);
  test_equal(x3, rng);
  test_equal(x4, rng);
#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  virtual_vector x5(mc, boost::container::from_range, rng);
  virtual_vector x6(mc, boost::container::from_range, rng,
                    virtual_allocator<int>{});
  test_equal(x5, rng);
  test_equal(x6, rng);
#endif
#endif
}

int main()
{
  test<boost::container::virtual_vector<int>>();
  test<boost::container::virtual_vector<std::size_t>>();
  test<boost::container::virtual_vector<int, fancy_virtual_allocator<int>>>();
  test_three_way_fallback();
  test_ctad();

  return boost::report_errors();
}
