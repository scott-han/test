#ifndef ARCHIVE_MAKER_PCH_H
#define ARCHIVE_MAKER_PCH_H

#include <iostream>
#include <fstream>
#include <atomic>
#include <thread>

#include <boost/algorithm/string.hpp>

#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/interprocess/sync/windows/winapi_mutex_wrapper.hpp>

#define WINDOWS_EVENT_LOGGING_SOURCE_ID "Archive_maker"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { 
namespace misc { namespace alloc {
::std::atomic_uint_fast64_t static Virtual_allocated_memory_size{0};
uint_fast64_t static Memory_alarm_threshold{static_cast<uint_fast64_t>(-1)};
}}
namespace synapse { 
namespace asio {
::std::atomic_bool static Exit_error{false};
}
namespace database {
int static nice_performance_degradation = 0;
::std::atomic_uint_fast64_t static Database_size{ 0 };
}
namespace amqp_0_9_1 {
unsigned constexpr static page_size = 4096;
}
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "../../../asio/io_service.h"
#include "../../../misc/sysinfo.h"
#include "Client.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
	namespace data_processors {
		namespace synapse {
			data_processors::misc::alloc::Large_page_preallocator<synapse::amqp_0_9_1::page_size> static Large_page_preallocator;
		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../../../database/message.h"
#include "../../../database/index.h"

#include <openssl/sha.h>
#include <openssl/md5.h>

#endif
