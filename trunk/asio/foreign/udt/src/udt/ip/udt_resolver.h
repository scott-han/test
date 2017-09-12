#ifndef UDT_IP_UDT_RESOLVER_H_
#define UDT_IP_UDT_RESOLVER_H_

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>

#include "../connected_protocol/resolver.h"

#include "udt_query.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace ip {

template <class UDTProtocol>
class UDTResolver : public connected_protocol::Resolver<UDTProtocol> {
 public:
  using query = UDTQuery<UDTProtocol>;

 public:
  UDTResolver(boost::asio::io_service& io_service)
      : connected_protocol::Resolver<UDTProtocol>(io_service) {}
};

}  // ip


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_IP_UDT_RESOLVER_H_
