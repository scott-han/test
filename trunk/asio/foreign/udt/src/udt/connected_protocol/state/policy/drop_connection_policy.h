#ifndef UDT_CONNECTED_PROTOCOL_STATE_POLICY_DROP_CONNECTION_POLICY_H_
#define UDT_CONNECTED_PROTOCOL_STATE_POLICY_DROP_CONNECTION_POLICY_H_

#include <memory>


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace state {
namespace policy {

template <class Protocol>
class DropConnectionPolicy {
 private:
  using ConnectionDatagram = typename Protocol::ConnectionDatagram;
  using ConnectionDatagramPtr = std::shared_ptr<ConnectionDatagram>;
  using SocketSession = typename Protocol::socket_session;

 protected:
  void ProcessConnectionDgr(SocketSession* ,
                            ConnectionDatagramPtr ) {
    // Drop datagram
  }
};

}  // policy
}  // state
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_STATE_POLICY_DROP_CONNECTION_POLICY_H_
