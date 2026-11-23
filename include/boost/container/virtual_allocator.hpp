/* Virtual memory allocator.
 *
 * Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_CONTAINER_VIRTUAL_ALLOCATOR_HPP
#define BOOST_CONTAINER_VIRTUAL_ALLOCATOR_HPP

#include <algorithm>
#include <boost/assert.hpp>
#include <boost/container/detail/workaround.hpp>
#include <boost/container/new_allocator.hpp>
#include <boost/throw_exception.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <type_traits>

#if defined(__linux__) || (defined(__APPLE__) && defined(__MACH__)) || \
    defined(__CYGWIN__)
#define BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX
#elif defined(_WIN32) /* must be after previous because of Cygwin */
#define BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS
#else
#error "boost::container::virtual_allocator: unsupported platform"
#endif

#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
#include <boost/container/detail/winapi.hpp>
#elif defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace boost {
namespace container {

namespace virtual_allocator_detail {

#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
/* not provided by <boost/container/detail/winapi.hpp> */

static const winapi::dword_type page_noaccess_c = 0x01;
static const winapi::dword_type mem_decommit_c = 0x4000;
#endif

inline bool is_power_of_two(std::size_t n) noexcept
{
  return n != 0 && (n & (n - 1)) == 0;
}

inline std::size_t page_size() noexcept
{
#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
  static const std::size_t size = [] {
    winapi::system_info info;
    ::GetSystemInfo(reinterpret_cast<::_SYSTEM_INFO*>(&info));
    const auto n = static_cast<std::size_t>(info.dwPageSize);
    if(!is_power_of_two(n)) std::terminate();
    return n;
  }();
  return size;
#elif defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX)
  static const std::size_t size = [] {
    const auto r = ::sysconf(_SC_PAGESIZE);
    if(r < 0) std::terminate();
    const auto n = static_cast<std::size_t>(r);
    if(!is_power_of_two(n)) std::terminate();
    return n;
  }();
  return size;
#endif
}

#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
inline std::size_t allocation_granularity() noexcept
{
  static const std::size_t granularity = [] {
    winapi::system_info info;
    ::GetSystemInfo(reinterpret_cast<::_SYSTEM_INFO*>(&info));
    return static_cast<std::size_t>(info.dwAllocationGranularity);
  }();
  return granularity;
}
#endif

inline std::size_t round_up(std::size_t n, std::size_t a) noexcept
{
  const auto rem = n % a;
  return rem ? n + (a - rem) : n;
}

inline void* reserve(std::size_t bytes, std::size_t alignment) noexcept
{
  /* In the unlikely case that alignment is greater than page alignment,
   * we reserve extra storage to align the returned address; additionally,
   * in POSIX we can munmap the excess memory at both ends of the mapping
   * after address adjustment.
   */

#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
  const auto granularity = allocation_granularity();
  const auto extra = alignment > granularity ? alignment - 1 : 0;
  void* base = ::VirtualAlloc(
    nullptr, bytes + extra, winapi::mem_reserve_c, page_noaccess_c);
  if(!base) return nullptr;
  const auto address = reinterpret_cast<std::uintptr_t>(base);
  const auto aligned = (address + alignment - 1) & ~(alignment - 1);
  return reinterpret_cast<void*>(aligned);
#elif defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX)
  const auto page = page_size();
  const auto extent = round_up(bytes, page);
  const auto extra = alignment > page? alignment - page: 0;
  void*      base = ::mmap(
    nullptr, extent + extra, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if(base == MAP_FAILED) return nullptr;
  const auto address = reinterpret_cast<std::uintptr_t>(base);
  const auto aligned = (address + alignment - 1) & ~(alignment - 1);
  const auto prefix = static_cast<std::size_t>(aligned - address);
  const auto suffix = extra - prefix;
  if(prefix && ::munmap(base, prefix) != 0) {
    (void)::munmap(base, extent + extra);
    return nullptr;
  }
  if(suffix && ::munmap(
       reinterpret_cast<void*>(aligned + extent), suffix) != 0) {
    (void)::munmap(reinterpret_cast<void*>(aligned), extent + suffix);
    return nullptr;
  }
  return reinterpret_cast<void*>(aligned);
#endif
}

inline bool commit(void* p, std::size_t bytes) noexcept
{
#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
  return ::VirtualAlloc(
    p, bytes, winapi::mem_commit_c, winapi::page_readwrite_c) != nullptr;
#elif defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX)
  return ::mprotect(p, bytes, PROT_READ | PROT_WRITE) == 0;
#endif
}

inline void decommit(void* p, std::size_t bytes) noexcept
{
#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
  (void)::VirtualFree(p, bytes, mem_decommit_c);
#elif defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX)
  (void)::mprotect(p, bytes, PROT_NONE);
  /* Advising the kernel is best effort. In particular, POSIX does not
   * promise the same commit-accounting semantics as Windows.
   */
#if defined(__APPLE__) && defined(MADV_FREE)
  (void)::madvise(p, bytes, MADV_FREE);
#elif defined(MADV_DONTNEED)
  (void)::madvise(p, bytes, MADV_DONTNEED);
#endif
#endif
}

inline void release(void* p, std::size_t bytes, std::size_t alignment) noexcept
{
#if defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_WINDOWS)
  (void)bytes;
  if(alignment > allocation_granularity()) {
    /* get base address */
    winapi::memory_basic_information info;
    if(::VirtualQuery(
         p, reinterpret_cast<::_MEMORY_BASIC_INFORMATION*>(&info), 
         sizeof(info)) == 0) {
      std::terminate();
    }
    p = info.AllocationBase;
  }
  (void)::VirtualFree(p, 0, winapi::mem_release_c);
#elif defined(BOOST_CONTAINER_VIRTUAL_ALLOCATOR_POSIX)
  (void)alignment;
  (void)::munmap(p, round_up(bytes, page_size()));
#endif
}

} /* namespace virtual_allocator_detail */

template<typename T>
class virtual_allocator
{
public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using is_always_equal = std::true_type;

  class reservation_handle
  {
  public:
    reservation_handle() = default;

    explicit operator bool() const noexcept { return data != nullptr; }

  private:
    friend class virtual_allocator;

    explicit reservation_handle(pointer data_) noexcept: data{data_} {}

    pointer   data = nullptr;
    size_type size = 0;
  };

  virtual_allocator() = default;

  template<typename U>
  virtual_allocator(const virtual_allocator<U>&) noexcept {}

  BOOST_CONTAINER_NODISCARD pointer allocate(size_type n)
  {
    return new_allocator<T>{}.allocate(n);
  }

  void deallocate(pointer p, size_type n) noexcept
  {
    new_allocator<T>{}.deallocate(p, n);
  }

  BOOST_CONTAINER_NODISCARD reservation_handle reserve(size_type n)
  {
    if(n == 0) return {};
    if(n > max_reservation_size()) {
      boost::throw_exception(std::bad_alloc()); // TODO: std::length_error?
    }
    const auto bytes = n * sizeof(T);
    void*      data_ = virtual_allocator_detail::reserve(
                 bytes, alignof(T));
    if(!data_) boost::throw_exception(std::bad_alloc());
    return reservation_handle{static_cast<pointer>(data_)};
  }

  BOOST_CONTAINER_NODISCARD 
  pointer data(const reservation_handle& h) const noexcept { return h.data; }

  BOOST_CONTAINER_NODISCARD
  size_type size(const reservation_handle& h) const noexcept { return h.size; }

  BOOST_CONTAINER_NODISCARD
  reservation_handle resize(reservation_handle h, size_type n, size_type m)
  {
    BOOST_ASSERT((h && n != 0) || (!h && n == 0));
    BOOST_ASSERT(m <= n);
    if(m != h.size) {
      const auto  page = virtual_allocator_detail::page_size();
      char* const base = reinterpret_cast<char*>(h.data);
      const auto  uprounded_size = 
        virtual_allocator_detail::round_up(h.size * sizeof(T), page);
      const auto  new_uprounded_size =
        virtual_allocator_detail::round_up(m * sizeof(T), page);

      if(new_uprounded_size > uprounded_size) {
        if(!virtual_allocator_detail::commit(
             base + uprounded_size, new_uprounded_size - uprounded_size)) {
          boost::throw_exception(std::bad_alloc());
        }
      }
      else if(new_uprounded_size < uprounded_size) {
        virtual_allocator_detail::decommit(
          base + new_uprounded_size, uprounded_size - new_uprounded_size);
      }
      h.size = m? (std::min)(n, new_uprounded_size / sizeof(T)): 0;
    }
    return h;
  }

  void release(const reservation_handle& h, size_type n)
  {
    BOOST_ASSERT((h && n != 0) || (!h && n == 0));
    if(h) {
      virtual_allocator_detail::release(h.data, n * sizeof(T), alignof(T));
    }
  }

  BOOST_CONTAINER_NODISCARD
  bool operator==(const virtual_allocator&) const noexcept { return true; }

  BOOST_CONTAINER_NODISCARD
  bool operator!=(const virtual_allocator&) const noexcept { return false; }

private:
  static size_type max_reservation_size() noexcept
  {
    static const size_type size = [] {
      /* subtract overhead for alignment and page granularity */
      const auto limit = static_cast<size_type>(
        (std::numeric_limits<difference_type>::max)());
      const auto overhead = 
        (alignof(T) - 1) + (virtual_allocator_detail::page_size() - 1);
      return overhead > limit ? 0 : (limit - overhead) / sizeof(T);
    }();
    return size;
  }
};

} /* namespace container */
} /* namespace boost */

#endif
