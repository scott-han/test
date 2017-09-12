#ifndef SERVER_PCH_H
#define SERVER_PCH_H

#define BOOST_ASIO_STRAND_IMPLEMENTATIONS 1777

// We dont need to use this one at the server end (we are caching it anyways)
#define DATA_PROCESSORS_NO_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME

#ifdef __WIN32__
#include <Windows.h>
#include <WinIoCtl.h>
#ifdef __MINGW64__
#include <ntdef.h>
#include <ntstatus.h>
#endif
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include <iostream>
#include <string>
#include <atomic>
#include <chrono>
#include <type_traits>
#include <thread>
#include <cstdlib>
#include <cstddef>
#include <mutex>
#include <forward_list>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/pool/pool.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/integer/static_log2.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/signal_set.hpp>

// Deprecated for the time being (unused/unsupported/experimental for a long time).
// #include "../asio/foreign/udt/src/udt/ip/udt.h"

#include "foreign/copernica/endian.h"
#include "foreign/copernica/endian.h"
#include "foreign/copernica/protocolexception.h"
#include "foreign/copernica/bytebuffer.h"
#include "foreign/copernica/outbuffer.h"
#include "foreign/copernica/table.h"
#include "foreign/copernica/receivedframe.h"
#include "foreign/copernica/connectionstartframe.h"
#include "foreign/copernica/connectionstartokframe.h"
#include "foreign/copernica/connectiontuneframe.h"
#include "foreign/copernica/connectiontuneokframe.h"
#include "foreign/copernica/connectionopenframe.h"
#include "foreign/copernica/connectionopenokframe.h"
#include "foreign/copernica/connectioncloseokframe.h"
#include "foreign/copernica/channelopenframe.h"
#include "foreign/copernica/channelopenokframe.h"
#include "foreign/copernica/channelcloseokframe.h"
#include "foreign/copernica/basicpublishframe.h"
#include "foreign/copernica/basicconsumeframe.h"
#include "foreign/copernica/basicconsumeokframe.h"
#include "foreign/copernica/basiccancelframe.h"
#include "foreign/copernica/basiccancelokframe.h"
#include "foreign/copernica/basicdeliverframe.h"
#include "foreign/copernica/basicheaderframe.h"
#include "foreign/copernica/queuedeclareframe.h"
#include "foreign/copernica/queuedeclareokframe.h"
#include "foreign/copernica/queuebindframe.h"
#include "foreign/copernica/queuebindokframe.h"
#include "foreign/copernica/queueunbindframe.h"
#include "foreign/copernica/queueunbindokframe.h"
#include "foreign/copernica/exchangedeclareframe.h"
#include "foreign/copernica/exchangedeclareokframe.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio {
::std::atomic_bool static Exit_error{false};
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../asio/io_service.h"

#if defined(__WIN32__) && ( !defined(BOOST_ASIO_HAS_IOCP) || !defined(BOOST_ASIO_HEADER_ONLY) )
	#error "Asio or Boost are insufficiently configured (IOCP)."
#endif

#if !defined(BOOST_ASIO_HAS_MOVE) || defined(ASIO_DISABLE_SMALL_BLOCK_RECYCLING)
	#error "Asio or Boost are insufficiently configured."
#endif

#ifdef SYNAPSE_SINGLE_THREADED
	#if defined(BOOST_ASIO_HAS_STD_THREAD) || defined(BOOST_ASIO_HAS_STD_MUTEX_AND_CONDVAR) || defined(BOOST_HAS_THREADS) || !defined(BOOST_DISABLE_THREADS) || !defined(BOOST_ASIO_DISABLE_THREADS) || !defined(BOOST_SP_DISABLE_THREADS) || !defined(BOOST_NO_RTTI) || !defined(BOOST_NO_TYPEID)
		#error "Asio or Boost are insufficiently configured for single threaded hardcore build. Need to link/use single-threaded, hardcore, boost libraries (compiler them separetely and put in dedicated dir/location)."
	#endif
#else
	#if !defined(BOOST_ASIO_HAS_STD_THREAD) || !defined(BOOST_ASIO_HAS_STD_MUTEX_AND_CONDVAR) || !defined(BOOST_HAS_THREADS)
		#error "Asio or Boost are insufficiently configured for multi threaded build."
	#endif
#endif

// This is just a ballpark, generic level of covering checks...
#if defined(NDEBUG) && (defined(BOOST_ENABLE_ASSERT_HANDLER) || defined(BOOST_ASIO_ENABLE_BUFFER_DEBUGGING))
	#error "Asio or Boost are insufficiently configured (disable BOOST_ENABLE_ASSERT_HANDLER during release builds)."
#endif

#endif
