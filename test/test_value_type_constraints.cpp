/* Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/container/virtual_vector.hpp>
#include <utility>

using boost::container::virtual_vector;

struct non_moveable
{
  explicit non_moveable(int) {}
  non_moveable(const non_moveable&) = delete;
  non_moveable(non_moveable&&) = delete;
  non_moveable& operator=(const non_moveable&) = delete;
  non_moveable& operator=(non_moveable&&) = delete;
};

struct non_move_assignable
{
  explicit non_move_assignable(int) {}
  non_move_assignable(const non_move_assignable&) = default;
  non_move_assignable(non_move_assignable&&) = default;
  non_move_assignable& operator=(const non_move_assignable&) = default;
  non_move_assignable& operator=(non_move_assignable&&) = delete;
};

struct non_copyable
{
  explicit non_copyable(int) {}
  non_copyable(const non_copyable&) = delete;
  non_copyable(non_copyable&&) = default;
  non_copyable& operator=(const non_copyable&) = delete;
  non_copyable& operator=(non_copyable&&) = default;
};

struct non_copy_assignable
{
  explicit non_copy_assignable(int) {}
  non_copy_assignable(const non_copy_assignable&) = default;
  non_copy_assignable(non_copy_assignable&&) = default;
  non_copy_assignable& operator=(const non_copy_assignable&) = delete;
  non_copy_assignable& operator=(non_copy_assignable&&) = default;
};

int main()
{
  {
    virtual_vector<non_moveable> x{8}, y{8};
    (void)x.emplace_back(1);
    x.swap(y);
    x = std::move(y);
    virtual_vector<non_moveable> z{std::move(x)};
    (void)z;
  }
  {
    virtual_vector<non_move_assignable> x{8};
    (void)x.emplace_back(1);
    x.assign(2, x.front());
  }
  {
    virtual_vector<non_copyable> x{8};
    (void)x.emplace_back(1);
    (void)x.emplace(x.begin(), 2);
    (void)x.erase(x.begin());
    virtual_vector<non_copyable> y{std::move(x)};
    (void)y;
  }
  {
    virtual_vector<non_copy_assignable> x{8};
    (void)x.emplace_back(1);
    (void)x.emplace(x.begin(), 2);
    (void)x.erase(x.begin());
    virtual_vector<non_copy_assignable> y{x};
    (void)y;
  }
}
