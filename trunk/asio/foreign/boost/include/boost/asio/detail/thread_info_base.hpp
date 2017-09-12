//
// detail/thread_info_base.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_DETAIL_THREAD_INFO_BASE_HPP
#define BOOST_ASIO_DETAIL_THREAD_INFO_BASE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <climits>
#include <cstddef>
#include <boost/asio/detail/noncopyable.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace detail {

class thread_info_base {
public:
  thread_info_base() = default;
	thread_info_base(thread_info_base const &) = delete;

  ~thread_info_base() {
    if (Cached_Memory_Size)
      ::operator delete(Cached_Memory);
  }

  static void* 
	allocate(thread_info_base * This_Thread, std::size_t Size) {
    if (This_Thread && This_Thread->Cached_Memory_Size) {
      if (This_Thread->Cached_Memory_Size >= Size) {
				This_Thread->Cached_Memory_Size = 0;
        return This_Thread->Cached_Memory;
			} else
				This_Thread->Cached_Memory_Size = 0;
      ::operator delete(This_Thread->Cached_Memory);
    }
    return ::operator new(Size);
  }

  static void 
	deallocate(thread_info_base * This_Thread, void * Pointer, std::size_t Size) {
		if (This_Thread && This_Thread->Cached_Memory_Size == 0) {
			This_Thread->Cached_Memory_Size = Size;
			This_Thread->Cached_Memory = Pointer;
			return;
		}
    ::operator delete(Pointer);
  }

private:
	unsigned Cached_Memory_Size{0};
  void * Cached_Memory;
};

} // namespace detail
} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_ASIO_DETAIL_THREAD_INFO_BASE_HPP
