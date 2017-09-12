#ifndef UDT_CONNECTED_PROTOCOL_LOGGER_NO_LOG_H_
#define UDT_CONNECTED_PROTOCOL_LOGGER_NO_LOG_H_

#include <cstdint>

#include <string>

#include "udt/connected_protocol/logger/log_entry.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace logger {

class NoLog {
 public:
  enum : bool { ACTIVE = false };

  enum : int { FREQUENCY = -1 };

 public:
  void Log(const LogEntry& log) {}
};

}  // logger
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_LOGGER_NO_LOG_H_
