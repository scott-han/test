#ifndef UDT_CONNECTED_PROTOCOL_RESOLVER_QUERY_H_
#define UDT_CONNECTED_PROTOCOL_RESOLVER_QUERY_H_

#include <cstdint>


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class Protocol>
class ResolverQuery {
 public:
  using protocol_type = Protocol;
  using SocketId = uint32_t;
  using NextLayer = typename Protocol::next_layer_protocol;
  using NextLayerQuery = typename NextLayer::resolver::query;

 public:
  ResolverQuery(const NextLayerQuery& next_layer_query, SocketId socket_id = 0)
      : next_layer_query_(next_layer_query), socket_id_(socket_id) {}

  ResolverQuery(const ResolverQuery& other)
      : next_layer_query_(other.next_layer_query_),
        socket_id_(other.socket_id_) {}

  NextLayerQuery next_layer_query() const { return next_layer_query_; }

  SocketId socket_id() const { return socket_id_; }

 protected:
  NextLayerQuery next_layer_query_;
  SocketId socket_id_;
};

}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_RESOLVER_QUERY_H_
