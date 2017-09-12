#ifndef UDT_CONNECTED_PROTOCOL_DATAGRAM_EMPTY_COMPONENT_H_
#define UDT_CONNECTED_PROTOCOL_DATAGRAM_EMPTY_COMPONENT_H_

#include "../io/buffers.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace datagram {

class EmptyComponent {
 public:
  using ConstBuffers = io::fixed_const_buffer_sequence;
  using MutableBuffers = io::fixed_mutable_buffer_sequence;
  enum { size = 0 };

 public:
  EmptyComponent() {}
  ~EmptyComponent() {}

  ConstBuffers GetConstBuffers() const { return ConstBuffers(); }
  void GetConstBuffers(ConstBuffers*) const {}

  MutableBuffers GetMutableBuffers() { return MutableBuffers(); }
  void GetMutableBuffers(MutableBuffers*) {}
};

}  // datagram
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_DATAGRAM_EMPTY_COMPONENT_H_
