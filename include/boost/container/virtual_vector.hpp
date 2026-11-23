/* Virtual vector container.
 *
 * Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_CONTAINER_VIRTUAL_VECTOR_HPP
#define BOOST_CONTAINER_VIRTUAL_VECTOR_HPP

#include <algorithm>
#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/container/detail/destroyers.hpp>
#include <boost/container/detail/mpl.hpp>
#include <boost/container/detail/range_utils.hpp>
#include <boost/container/virtual_allocator.hpp>
#include <boost/core/allocator_access.hpp>
#include <boost/core/empty_value.hpp>
#include <boost/core/no_exceptions_support.hpp>
#include <boost/throw_exception.hpp>
#include <boost/type_traits/make_void.hpp>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#if defined(BOOST_NO_CXX20_HDR_CONCEPTS) || defined(BOOST_NO_CXX20_HDR_RANGES)
#define BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES
#elif BOOST_WORKAROUND(BOOST_CLANG_VERSION, < 170100) && \
      defined(BOOST_LIBSTDCXX_VERSION)
/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109647
 * https://github.com/llvm/llvm-project/issues/49620
 */
#define BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES
#endif

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
#include <concepts>
#endif

#if !defined(BOOST_NO_CXX20_HDR_COMPARE)
#include <compare>
#endif

namespace boost {
namespace container {
namespace virtual_vector_detail {

template<typename T, typename = void>
struct is_input_iterator: std::false_type {};

template<typename T>
struct is_input_iterator<
  T,
  boost::void_t<typename std::iterator_traits<T>::iterator_category>
>:
std::is_convertible<
  typename std::iterator_traits<T>::iterator_category,
  std::input_iterator_tag
> {};

template<typename T, typename = void>
struct is_forward_iterator: std::false_type {};

template<typename T>
struct is_forward_iterator<
  T,
  boost::void_t<typename std::iterator_traits<T>::iterator_category>
>:
std::is_convertible<
  typename std::iterator_traits<T>::iterator_category,
  std::forward_iterator_tag
> {};

template<typename T>
using enable_if_is_input_iterator_t = typename std::enable_if<
  is_input_iterator<T>::value
>::type;

template<typename T>
using enable_if_is_not_input_iterator_t = typename std::enable_if<
  !is_input_iterator<T>::value
>::type;

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
template<class R>
using range_iterator_t = 
  decltype(dtl::adl_range::adl_begin(std::declval<R&>()));

template<class R>
using range_sentinel_t =
  decltype(dtl::adl_range::adl_end(std::declval<R&>()));

template<class R>
using range_reference_t = std::iter_reference_t<range_iterator_t<R> >;

template<class R>
using range_value_t = std::iter_value_t<range_iterator_t<R> >;

template<class R>
concept input_range_like =
  requires(R& r) {
    dtl::adl_range::adl_begin(r);
    dtl::adl_range::adl_end(r);
  } &&
  std::input_iterator<range_iterator_t<R>> &&
  std::sentinel_for<range_sentinel_t<R>, range_iterator_t<R>>;

template<class R, class T>
concept container_compatible_range =
  input_range_like<R> &&
  std::convertible_to<range_reference_t<R>, T>;
#endif

template<typename Incrementable, typename Sentinel, typename = void>
struct non_destructive_distance: std::true_type
{
  auto operator()(Incrementable first, Sentinel last) const
    -> decltype(last - first)
  {
    return last - first;
  }
};

template<typename Incrementable, typename Sentinel>
struct non_destructive_distance<
  Incrementable, Sentinel,
  enable_if_is_input_iterator_t<Incrementable>
>: is_forward_iterator<Incrementable>
{
  auto operator()(const Incrementable& first, const Sentinel& last) const
    -> decltype(std::distance(first, last))
  {
    return std::distance(first, last);
  }
};

template<
  typename Incrementable, typename Distance,
  enable_if_is_not_input_iterator_t<Incrementable>* = nullptr
>
Incrementable generalized_next(Incrementable i, Distance n)
{
#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  /* a C++20 iterator may lack ::iterator_category */
  if constexpr(std::forward_iterator<Incrementable>) {
    return std::ranges::next(std::move(i), n);
  }
  else
#endif
  {
    return i + n;
  }
}

template<
  typename Incrementable, typename Distance,
  enable_if_is_input_iterator_t<Incrementable>* = nullptr
>
Incrementable generalized_next(Incrementable i, Distance n)
{
  return std::next(i, n);
}

template<typename T>
void copy_assign_if(std::true_type, T& x, const T& y) { x = y; }

template<typename T>
void copy_assign_if(std::false_type, T&, const T&) {}

template<typename T>
void move_assign_if(std::true_type, T& x, T& y) { x = std::move(y); }

template<typename T>
void move_assign_if(std::false_type, T&, T&) {}

template<typename VirtualVector>
void move_assign_elements_if(
  std::true_type, VirtualVector& x, VirtualVector& y)
{
  x.assign(
    std::make_move_iterator(y.begin()), std::make_move_iterator(y.end()));
}

template<typename VirtualVector>
void move_assign_elements_if(
  std::false_type, VirtualVector&, VirtualVector&) {}

template<typename T>
void swap_if(std::true_type, T& x, T& y) { using std::swap; swap(x, y); }

template<typename T>
void swap_if(std::false_type, T&, T&) {}

} /* namespace virtual_vector_detail */

template<typename T, typename VirtualAllocator = virtual_allocator<T>>
class virtual_vector: empty_value<VirtualAllocator, 0>
{
  static_assert(
    !std::is_const<T>::value && !std::is_volatile<T>::value && 
    !std::is_function<T>::value && !std::is_reference<T>::value && 
    !std::is_void<T>::value,
    "T must be a cv-unqualified object type");
  static_assert(
    std::is_same<T, typename VirtualAllocator::value_type>::value,
    "VirtualAllocator::value_type must be T");

public:
  using value_type = T;
  using allocator_type = VirtualAllocator;
  using pointer = allocator_pointer_t<VirtualAllocator>;
  using const_pointer = allocator_const_pointer_t<VirtualAllocator>;
  using reference = T&;
  using const_reference = const T&;
  using size_type = allocator_size_type_t<VirtualAllocator>;
  using difference_type = allocator_difference_type_t<VirtualAllocator>;

  // TODO: wrap with something like boost::container::vec_iterator for
  // std::contiguous_iterator_tag and operator->
  using iterator = pointer;  
  using const_iterator = const_pointer;

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // TODO: do we provide a default ctor?

  explicit virtual_vector(
    size_type mc,const VirtualAllocator& al_ = VirtualAllocator()):
    allocator_base{empty_init, al_}, h_{al().reserve(mc)},
    max_capacity_{mc} {}

  virtual_vector(
    size_type mc, size_type n, 
    const VirtualAllocator& al_ = VirtualAllocator()):
    virtual_vector{mc, al_}
  {
    append_impl(size_type(0), n, [&, this] (T* p, size_type) {
      allocator_construct(al(), p);
    });
  }

  virtual_vector(
    size_type mc, size_type n, const T& x,
    const VirtualAllocator& al_ = VirtualAllocator()):
    virtual_vector{mc, al_}
  {
    append_impl(size_type(0), n, [&, this] (T* p, size_type) {
      allocator_construct(al(), p, x);
    });
  }

  template<
    typename InputIterator, 
    typename = 
      virtual_vector_detail::enable_if_is_input_iterator_t<InputIterator>
  >
  virtual_vector(
    size_type mc, InputIterator first, InputIterator last,
    const VirtualAllocator& al_ = VirtualAllocator()):
    virtual_vector{mc, al_}
  {
    append_impl(first, last, [this] (T* p, InputIterator it) {
      allocator_construct(al(), p, *it);
    });
  }

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  template<virtual_vector_detail::container_compatible_range<T> R>
  virtual_vector(
    size_type mc, from_range_t, R&& rg,
    const VirtualAllocator& al_ = VirtualAllocator()):
    virtual_vector{mc, al_}
  {
    append_range(std::forward<R>(rg));
  }
#endif

  virtual_vector(const virtual_vector& x):
    virtual_vector{
      x, allocator_select_on_container_copy_construction(x.al())} {}

  virtual_vector(virtual_vector&& x) noexcept:
    virtual_vector{std::move(x), VirtualAllocator(std::move(x.al()))}{}

  virtual_vector(
    const virtual_vector& x, 
    const dtl::type_identity_t<VirtualAllocator>& al_):
    virtual_vector{x.max_capacity_, x.begin(), x.end(), al_}{}

  virtual_vector(
    virtual_vector&& x, const dtl::type_identity_t<VirtualAllocator>& al_):
    virtual_vector{size_type(0), al_}
  {
    if(al() == x.al()) {
      transfer_from(x);
    }
    else {
      h_ = al().reserve(x.max_capacity_);
      max_capacity_ = x.max_capacity_;
      append_impl(x.begin(), x.end(), [this] (T* p, iterator it) {
        allocator_construct(al(), p, std::move(*it));
      });
      x.clear();
    }
  }

  virtual_vector(
    size_type mc, std::initializer_list<T> il,
    const VirtualAllocator& al_ = VirtualAllocator()):
    virtual_vector{mc, il.begin(), il.end(), al_}{}

  ~virtual_vector() { reset(); }

  virtual_vector& operator=(const virtual_vector& x)
  {
    using pocca =
      allocator_propagate_on_container_copy_assignment_t<VirtualAllocator>;

    if(this != &x) {
      if(pocca::value && al() != x.al()) {
        clear();
        shrink_to_fit();
        virtual_vector tmp{x, x.al()};
        reset();
        virtual_vector_detail::copy_assign_if(pocca{}, al(), tmp.al());
        transfer_from(tmp);
      }
      else{
        virtual_vector_detail::copy_assign_if(pocca{}, al(), x.al());
        if(max_capacity_ != x.max_capacity_) {
          virtual_vector tmp{x.max_capacity_, al()};
          reset();
          transfer_from(tmp);
        }
        assign(x.begin(), x.end());
      }
    }
    return *this;
  }

  virtual_vector& operator=(virtual_vector&& x)
    noexcept(
      allocator_propagate_on_container_move_assignment_t<VirtualAllocator>::
        value ||
      allocator_is_always_equal_t<VirtualAllocator>::value)
  {
    using pocma = 
      allocator_propagate_on_container_move_assignment_t<VirtualAllocator>;
    using element_moveability_required = std::integral_constant<
      bool,
      !pocma::value &&
      /* extension to [container.alloc.reqmts]/26 */
      !allocator_is_always_equal_t<VirtualAllocator>::value
    >;

    if(this != &x) {
      if(pocma::value || al() == x.al()) {
        reset();
        virtual_vector_detail::move_assign_if(pocma{}, al(), x.al());
        transfer_from(x);
      }
      else{
        if(max_capacity_ != x.max_capacity_) {
          virtual_vector tmp{x.max_capacity_, al()};
          reset();
          transfer_from(tmp);
        }
        virtual_vector_detail::move_assign_elements_if(
          element_moveability_required{}, *this, x);
        x.clear();
      }
    }
    return *this;
  }

  virtual_vector& operator=(std::initializer_list<T> il)
  {
    assign(il);
    return *this;
  }

  template<
    typename InputIterator,
    typename = 
      virtual_vector_detail::enable_if_is_input_iterator_t<InputIterator>
  >
  void assign(InputIterator first, InputIterator last)
  {
    assign_impl(
      first, last, 
      [this] (T* p, InputIterator it) { allocator_construct(al(), p, *it); },
      [] (T* p, InputIterator it) { *p = *it; });
  }

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  template<virtual_vector_detail::container_compatible_range<T> R>
  void assign_range(R&& rg)
  {
    auto       first = dtl::adl_range::adl_begin(rg);
    auto       last = dtl::adl_range::adl_end(rg);
    const auto pb = boost::to_address(data_ptr()),
               pe = boost::to_address(data_ptr() + size_);
    auto       p = pb;
    for( ; p != pe && first != last; ++p, ++first) *p = *first;
    truncate(static_cast<size_type>(p - pb));
    append_range_impl(
      std::move(first), std::move(last), 
      [this] (T* p, const auto& it) { allocator_construct(al(), p, *it); });
  }
#endif

  void assign(size_type n, const T& x)
  {
    assign_impl(
      size_type(0), n,
      [&, this] (T* p, size_type) { allocator_construct(al(), p, x); },
      [&] (T* p, size_type) { *p = x; });
  }

  void assign(std::initializer_list<T> il)
  {
    assign(il.begin(), il.end());
  }

  allocator_type get_allocator() const noexcept { return al(); }

  iterator               begin() noexcept { return data_ptr(); }
  const_iterator         begin() const noexcept { return data_ptr(); }
  iterator               end() noexcept { return data_ptr() + size_; }
  const_iterator         end() const noexcept { return data_ptr() + size_; }
  reverse_iterator       rbegin() noexcept { return reverse_iterator{end()}; }
  const_reverse_iterator rbegin() const noexcept 
                         { return const_reverse_iterator{end()}; }
  reverse_iterator       rend() noexcept { return reverse_iterator{begin()}; }
  const_reverse_iterator rend() const noexcept 
                         { return const_reverse_iterator{begin()}; }

  const_iterator         cbegin() const noexcept { return begin(); }
  const_iterator         cend() const noexcept { return end(); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  bool      empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type max_size() const noexcept { return max_capacity_; }
  size_type capacity() const noexcept { return al().size(h_); }

  void resize(size_type n)
  {
    resize_impl(n, [this] (T* p) { allocator_construct(al(), p); });
  }

  void resize(size_type n, const T& v)
  {
    resize_impl(n, [&, this] (T* p) { allocator_construct(al(), p, v); });
  }

  void reserve(size_type n) /* unlike std::vector, this can reduce capacity */
  {
    h_ = al().resize(h_, max_capacity_, (std::max)(size_, n));
  }

  void shrink_to_fit() { reserve(size_); }

  reference       operator[](size_type n) { return data_ptr()[n]; }
  const_reference operator[](size_type n) const { return data_ptr()[n]; }

  reference at(size_type n)
  {
    if(n >= size_) boost::throw_exception(
      std::out_of_range("boost::container::virtual_vector::at"));
    return (*this)[n];
  }

  const_reference at(size_type n) const
  {
    return const_cast<virtual_vector*>(this)->at(n);
  }

  reference front()
  {
    BOOST_ASSERT(!empty());
    return *data_ptr();
  }

  const_reference front() const
  {
    return const_cast<virtual_vector*>(this)->front();
  }

  reference back()
  {
    BOOST_ASSERT(!empty());
    return data_ptr()[size_ - 1];
  }

  const_reference back() const
  {
    return const_cast<virtual_vector*>(this)->back();
  }

  T*       data() noexcept { return boost::to_address(data_ptr()); }
  const T* data() const noexcept { return boost::to_address(data_ptr()); }

  template<typename... Args>
  reference emplace_back(Args&&... args)
  {
    if(size_ == capacity()) reserve_for_insertion(1);
    const auto p = boost::to_address(data_ptr() + size_);
    allocator_construct(al(), p, std::forward<Args>(args)...);
    ++size_;
    return *p;
  }

  void push_back(const T& v) { emplace_back(v); }
  void push_back(T&& v) { emplace_back(std::move(v)); }

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  template<virtual_vector_detail::container_compatible_range<T> R>
  void append_range(R&& rg)
  {
    append_range_impl(
      std::forward<R>(rg), 
      [this] (T* p, const auto& it) { allocator_construct(al(), p, *it); });
  }
#endif

  void pop_back()
  {
    BOOST_ASSERT(!empty());
    --size_;
    allocator_destroy(al(), boost::to_address(data_ptr() + size_));
  }

  template<typename... Args> 
  iterator emplace(const_iterator pos, Args&&... args)
  {
    if(pos == end()) {
      emplace_back(std::forward<Args>(args)...);
      return end() - 1;
    }

    /* avoid aliasing */
    alignas(T) unsigned char storage[sizeof(T)];
    const auto tmp = reinterpret_cast<T*>(storage);
    allocator_construct(al(), tmp, std::forward<Args>(args)...);
    dtl::scoped_destructor<VirtualAllocator> guard(al(), tmp);

    const auto  p = data_ptr() + (pos - begin()),
                last = data_ptr() + size_;
    reserve_for_insertion(1);
    allocator_construct(al(), boost::to_address(last), std::move(*(last - 1)));
    ++size_;
    std::move_backward(p, last - 1, last);
    *p = std::move(*tmp);
    return {p};
  }

  iterator insert(const_iterator pos, const T& x) { return emplace(pos, x); }
  iterator insert(const_iterator pos, T&& x) 
  { return emplace(pos, std::move(x)); }

  iterator insert(const_iterator pos, size_type n, const T& x)
  {
    if(n == 0) return {data_ptr() + (pos - begin())};

    /* avoid aliasing */
    alignas(T) unsigned char storage[sizeof(T)];
    const auto tmp = reinterpret_cast<T*>(storage);
    allocator_construct(al(), tmp, x);
    dtl::scoped_destructor<VirtualAllocator> guard(al(), tmp);

    return insert_impl(
      pos, size_type(0), n,
      [&, this] (T* p, size_type) { allocator_construct(al(), p, *tmp); },
      [&] (T* p, size_type) { *p = *tmp; });
  }

  template<
    typename InputIterator,
    typename = 
      virtual_vector_detail::enable_if_is_input_iterator_t<InputIterator>
  >
  iterator insert(
    const_iterator pos, InputIterator first, InputIterator last)
  {
    return insert_impl(
      pos, first, last,
      [this] (T* p, InputIterator it) { allocator_construct(al(), p, *it); },
      [] (T* p, InputIterator it) { *p = *it; });
  }

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  template<virtual_vector_detail::container_compatible_range<T> R>
  iterator insert_range(const_iterator pos, R&& rg)
  {
    const auto construct_ = [this] (T* p, const auto& it)
                            { allocator_construct(al(), p, *it); };
    const auto assign_ = [] (T* p, const auto& it) { *p = *it; };

    auto first = dtl::adl_range::adl_begin(rg);
    auto last = dtl::adl_range::adl_end(rg);

    if constexpr(std::forward_iterator<decltype(first)>) {
      const auto n = std::ranges::distance(first, last);
      return sized_insert_impl(
        pos, std::move(first), std::move(last), n, construct_, assign_);
    }
    else {
      return insert_impl(
        pos, std::move(first), std::move(last),
        construct_, assign_, std::false_type{});
    }
  }
#endif

  iterator insert(const_iterator pos, std::initializer_list<T> il)
  {
    return insert(pos, il.begin(), il.end());
  }

  iterator erase(const_iterator pos)
  {
    BOOST_ASSERT(pos != end());
    const auto p = data_ptr() + (pos - begin()),
               pe = data_ptr() + size_;
    std::move(p + 1, pe, p);
    pop_back();
    return {p};
  }

  iterator erase(const_iterator first, const_iterator last)
  {
    const auto pf = data_ptr() + (first - begin()),
               pl = data_ptr() + (last - begin()),
               pe = data_ptr() + size_;
    auto       n = pl - pf;
    if(n != 0) {
      std::move(pl, pe, pf);
      do pop_back(); while(--n);
    }
    return {pf};
  }

  void swap(virtual_vector& x)
    noexcept(
      allocator_propagate_on_container_swap_t<VirtualAllocator>::value ||
      allocator_is_always_equal_t<VirtualAllocator>::value)
  {
    using pocs = allocator_propagate_on_container_swap_t<VirtualAllocator>;

    BOOST_ASSERT(pocs::value || al() == x.al());
    virtual_vector_detail::swap_if(pocs{}, al(), x.al());
    std::swap(h_, x.h_);
    std::swap(size_, x.size_);
    std::swap(max_capacity_, x.max_capacity_);
  }

  void clear() noexcept
  {
    while(size_ != 0) pop_back();
  }

private:
  using allocator_base = empty_value<VirtualAllocator, 0>;
  using reservation_handle = typename VirtualAllocator::reservation_handle;

  allocator_type&       al() noexcept { return allocator_base::get(); }
  const allocator_type& al() const noexcept { return allocator_base::get(); }
  pointer               data_ptr() noexcept { return al().data(h_); }
  const_pointer         data_ptr() const noexcept { return al().data(h_); }

  void make_empty() noexcept
  {
    h_ = {};
    size_ = 0;
    max_capacity_ = 0;
  }

  void transfer_from(virtual_vector& x)
  {
    BOOST_ASSERT(al() == x.al());
    h_ = x.h_;
    size_ = x.size_;
    max_capacity_ = x.max_capacity_;
    x.make_empty();
  }

  void reset() noexcept
  {
    clear();
    al().release(h_, max_capacity_);
    make_empty();
  }

  void truncate(size_type n) noexcept
  {
    while(size_ > n) pop_back();
  }

  void reserve_for_insertion(size_type n)
  {
    const auto current = capacity();
    if(n <= current - size_) return;
    if(n > max_capacity_ - size_) boost::throw_exception(std::bad_alloc());

    /* geometric growth, capped by max_capacity_ */
    const auto needed = size_ + n;
    const auto doubled =
      current <= max_capacity_ / 2 ? 2 * current : max_capacity_;
    h_ = al().resize(h_, max_capacity_, needed > doubled? needed: doubled);
  }

  template<typename Incrementable, typename Sentinel, typename Construct>
  void append_impl(
    Incrementable first, Sentinel last, Construct construct_)
  {
    append_impl(
      first, last, construct_, 
      virtual_vector_detail::
        non_destructive_distance<Incrementable, Sentinel>{});
  }

  template<typename Incrementable, typename Sentinel, typename Construct>
  void append_impl(
    Incrementable first, Sentinel last, Construct construct_,
    std::false_type /* no nondestructive distance */)
  {
    const auto old_size = size_;
    const auto old_capacity = capacity();
    BOOST_TRY {
      while(first != last) {
        reserve_for_insertion(1);
        construct_(boost::to_address(data_ptr() + size_), first);
        ++size_;
        ++first;
      }
    }
    BOOST_CATCH(...) {
      /* rollback mandated by standard on non-T-related throw */
      truncate(old_size);
      if(capacity() != old_capacity) reserve(old_capacity);
      BOOST_RETHROW;
    }
    BOOST_CATCH_END
  }

  template<typename Incrementable, typename Sentinel, typename Construct>
  void append_impl(
    Incrementable first, Sentinel last, Construct construct_,
    std::true_type /* nondestructive distance */)
  {
    sized_append_impl(
      first, last,
      virtual_vector_detail::
        non_destructive_distance<Incrementable, Sentinel>{}(first, last),
      construct_);
  }

  template<
    typename Incrementable, typename Sentinel, typename Integer,
    typename Construct
  >
  void sized_append_impl(
    Incrementable first, Sentinel last, Integer n, Construct construct_)
  {
    reserve_for_insertion(static_cast<size_type>(n));
    while(first != last) {
      construct_(boost::to_address(data_ptr() + size_), first);
      ++size_;
      ++first;
    }
  }

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
  template<typename R, typename Construct>
  void append_range_impl(R&& rg, Construct construct_)
  {
    append_range_impl(
      dtl::adl_range::adl_begin(rg), dtl::adl_range::adl_end(rg), construct_);
  }

  template<typename InputIterator, typename Sentinel, typename Construct>
  void append_range_impl(
    InputIterator first, Sentinel last, Construct construct_)
  {
    if constexpr(std::sized_sentinel_for<Sentinel, InputIterator>) {
      const auto n = last - first;
      sized_append_impl(std::move(first), std::move(last), n, construct_);
    }
    else if constexpr(std::forward_iterator<InputIterator>) {
      const auto n = std::ranges::distance(first, last);
      sized_append_impl(std::move(first), std::move(last), n, construct_);
    }
    else {
      append_impl(
        std::move(first), std::move(last), construct_, std::false_type{});
    }
  }
#endif

  template<
    typename Incrementable, typename Sentinel,
    typename Construct, typename Assign
  >
  void assign_impl(
    Incrementable first, Sentinel last,
    Construct construct_, Assign assign_)
  {
    const auto pb = boost::to_address(data_ptr()),
               pe = boost::to_address(data_ptr() + size_);
    auto       p = pb;
    for( ; p != pe && first != last; ++p, ++first) assign_(p, first);
    truncate(static_cast<size_type>(p - pb));
    append_impl(first, last, construct_);
  }

  template<
    typename Incrementable, typename Sentinel,
    typename Construct, typename Assign
  >
  iterator insert_impl(
    const_iterator pos, Incrementable first, Sentinel last,
    Construct construct_, Assign assign_)
  {
    return insert_impl(
      pos, first, last, construct_, assign_,
      virtual_vector_detail::
        non_destructive_distance<Incrementable, Sentinel>{});
  }

  template<
    typename Incrementable, typename Sentinel,
    typename Construct, typename Assign
  >
  iterator insert_impl(
    const_iterator pos, Incrementable first, Sentinel last,
    Construct construct_, Assign /* assign_ */,
    std::false_type /* no nondestructive distance */)
  {
    const auto p = data_ptr() + (pos - begin()),
               mid = data_ptr() + size_;
    append_impl(
      std::move(first), std::move(last), construct_, std::false_type{});
    std::rotate(p, mid, data_ptr() + size_);
    return {p};
  }

  template<
    typename Incrementable, typename Sentinel,
    typename Construct, typename Assign
  >
  iterator insert_impl(
    const_iterator pos, Incrementable first, Sentinel last,
    Construct construct_, Assign assign_,
    std::true_type /* nondestructive distance */)
  {
    return sized_insert_impl(
      pos, first, last,
      virtual_vector_detail::
        non_destructive_distance<Incrementable, Sentinel>{}(first, last),
      construct_, assign_);
  }

  template<
    typename Incrementable, typename Sentinel, typename Integer,
    typename Construct, typename Assign
  >
  iterator sized_insert_impl(
    const_iterator pos, Incrementable first, Sentinel last,
    Integer n, Construct construct_, Assign assign_)
  {
    const auto s = static_cast<size_type>(n),
               tail = static_cast<size_type>(end() - pos);
    const auto p = data_ptr() + (pos - begin());
    reserve_for_insertion(s);
    if(s == 0) { 
      /* Check needed, s <= tail branch would call 
       * std::move_backward(p, pe, pe) resulting in self-assignment.
       */
    }
    else if(s <= tail) {
      const auto pe = data_ptr() + size_;
      for(auto q = pe - s; q != pe; ++q) {
        allocator_construct(
          al(), boost::to_address(data_ptr() + size_), std::move(*q));
        ++size_;
      }
      std::move_backward(p, pe - s, pe);
      for(auto q = p; first != last; ++q, ++first) {
        assign_(boost::to_address(q), first);
      }
    }
    else {
      auto mid = virtual_vector_detail::generalized_next(first, tail);
      for(auto it = mid; it != last; ++it) {
        construct_(boost::to_address(data_ptr() + size_), it);
        ++size_;
      }
      for(auto q = p; first != mid; ++q, ++first) {
        allocator_construct(
          al(), boost::to_address(data_ptr() + size_), std::move(*q));
        ++size_;
        assign_(boost::to_address(q), first);
      }
    }
    return {p};
  }

  template<typename Construct>
  void resize_impl(size_type n, Construct construct_)
  {
    if(n <= size_) {
      truncate(n);
    }
    else {
      const auto old_size = size_;
      const auto old_capacity = capacity();
      if(n > capacity()) reserve(n);
      BOOST_TRY {
        while(size_ < n) {
          construct_(boost::to_address(data_ptr() + size_));
          ++size_;
        }
      }
      BOOST_CATCH(...) {
        truncate(old_size);
        if(capacity() != old_capacity) reserve(old_capacity);
        BOOST_RETHROW;
      }
      BOOST_CATCH_END
    }
  }

  reservation_handle h_ = {};
  size_type          size_ = 0;
  size_type          max_capacity_ = 0;
};

#if !defined(BOOST_NO_CXX17_DEDUCTION_GUIDES)
template<
  typename Size, typename InputIterator,
  virtual_vector_detail::
    enable_if_is_input_iterator_t<InputIterator>* = nullptr
>
virtual_vector(Size, InputIterator, InputIterator) 
  -> virtual_vector<
  typename std::iterator_traits<InputIterator>::value_type>;

template<
  typename Size, typename InputIterator, typename VirtualAllocator,
  virtual_vector_detail::
    enable_if_is_input_iterator_t<InputIterator>* = nullptr
>
virtual_vector(Size, InputIterator, InputIterator, VirtualAllocator)
  -> virtual_vector<
    typename std::iterator_traits<InputIterator>::value_type,
    VirtualAllocator>;

template<typename Size, typename T, typename VirtualAllocator>
virtual_vector(Size, std::initializer_list<T>, VirtualAllocator)
  -> virtual_vector<T, VirtualAllocator>;

#if !defined(BOOST_CONTAINER_VIRTUAL_VECTOR_NO_RANGES)
template<typename Size, virtual_vector_detail::input_range_like R>
virtual_vector(Size, from_range_t, R&&)
  -> virtual_vector<virtual_vector_detail::range_value_t<R> >;

template<
  typename Size, virtual_vector_detail::input_range_like R,
  typename VirtualAllocator
>
virtual_vector(Size, from_range_t, R&&, VirtualAllocator)
  -> virtual_vector<virtual_vector_detail::range_value_t<R>, VirtualAllocator>;
#endif
#endif

template<typename T, typename VirtualAllocator>
bool operator==(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin());
}

#if defined(BOOST_NO_CXX20_HDR_COMPARE)
template<typename T, typename VirtualAllocator>
bool operator!=(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return !(x == y);
}

template<typename T, typename VirtualAllocator>
bool operator<(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return std::lexicographical_compare(
    x.begin(), x.end(), y.begin(), y.end());
}

template<typename T, typename VirtualAllocator>
bool operator>(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return y < x;
}

template<typename T, typename VirtualAllocator>
bool operator<=(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return !(y < x);
}

template<typename T, typename VirtualAllocator>
bool operator>=(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return !(x < y);
}
#else /* BOOST_NO_CXX20_HDR_COMPARE */
template<typename T, typename VirtualAllocator>
auto operator<=>(
  const virtual_vector<T, VirtualAllocator>& x,
  const virtual_vector<T, VirtualAllocator>& y)
{
  return std::lexicographical_compare_three_way(
    x.begin(), x.end(), y.begin(), y.end(),
    [](const T& a, const T& b) {
      if constexpr(std::three_way_comparable<T>) {
        return a <=> b;
      }
      else {
        if(a < b) return std::weak_ordering::less;
        if(b < a) return std::weak_ordering::greater;
        return std::weak_ordering::equivalent;
      }
    });
}
#endif

template<typename T, typename VirtualAllocator>
inline void swap(
  virtual_vector<T, VirtualAllocator>& x,
  virtual_vector<T, VirtualAllocator>& y)
  noexcept(noexcept(x.swap(y)))
{
  x.swap(y);
}

template<typename T, typename VirtualAllocator, typename U = T>
typename virtual_vector<T, VirtualAllocator>::size_type
erase(virtual_vector<T, VirtualAllocator>& x, const U& value)
{
  const auto old_size = x.size();
  x.erase(std::remove(x.begin(), x.end(), value), x.end());
  return old_size - x.size();
}

template<typename T, typename VirtualAllocator, typename Pred>
typename virtual_vector<T, VirtualAllocator>::size_type
erase_if(virtual_vector<T, VirtualAllocator>& x, Pred pred)
{
  const auto old_size = x.size();
  x.erase(std::remove_if(x.begin(), x.end(), pred), x.end());
  return old_size - x.size();
}

} /* namespace container */
} /* namespace boost */

#endif
