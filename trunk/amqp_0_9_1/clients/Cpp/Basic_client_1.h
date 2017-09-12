#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_BASIC_CLIENT_1_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_BASIC_CLIENT_1_H

#include <atomic>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio {
	::std::atomic_bool static Exit_error{false};
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "../../../asio/io_service.h"
#include "../../../misc/sysinfo.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { 
	namespace misc { namespace alloc {
		#ifndef SYNAPSE_SINGLE_THREADED
		alignas(data_processors::synapse::misc::Cache_line_size) 
		#endif
		::std::atomic_uint_fast64_t static Virtual_allocated_memory_size{0};
		uint_fast64_t static Memory_alarm_threshold{data_processors::synapse::misc::Get_system_memory_size()};
	}}
	namespace synapse { namespace database {
			int static nice_performance_degradation = 0;
		}
		namespace amqp_0_9_1 {
			unsigned constexpr static page_size = 4096;
	}}
}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "Client.h"

#endif
