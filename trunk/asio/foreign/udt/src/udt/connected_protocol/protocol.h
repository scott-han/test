#ifndef UDT_CONNECTED_PROTOCOL_PROTOCOL_H_
#define UDT_CONNECTED_PROTOCOL_PROTOCOL_H_

#include <cstdint>

#include <string>

#include <boost/asio/io_service.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/detail/socket_types.hpp>

#include <boost/chrono.hpp>

#include "congestion/congestion_control.h"

#include "datagram/basic_datagram.h"
#include "datagram/basic_header.h"
#include "datagram/basic_payload.h"
#include "datagram/empty_component.h"

#include "endpoint.h"
#include "flow.h"
#include "multiplexer.h"
#include "multiplexers_manager.h"
#include "resolver.h"

#include "socket_session.h"
#include "acceptor_session.h"

#include "stream_socket_service.h"
#include "socket_acceptor_service.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class NextLayer, template <class> class CongestionControlAlg = congestion::CongestionControl>
class Protocol {

 public:
  using next_socket_type = typename NextLayer::socket;
  using next_endpoint_type = typename NextLayer::endpoint;

  using next_layer_protocol = NextLayer;

  // Sessions
  using socket_session = SocketSession<Protocol>;
  using acceptor_session = AcceptorSession<Protocol>;
  using endpoint_context_type = uint32_t;

  // Clock
  using clock = boost::chrono::high_resolution_clock;
  using time_point = clock::time_point;
  using timer = boost::asio::basic_waitable_timer<clock>;

  // Socket options
  enum socket_options { TIMEOUT_DELAY };

  enum : uint32_t {
    MTU = 1450, //  508, 1472 1500
    MAXIMUM_WINDOW_FLOW_SIZE = 25600,
    MAX_PACKET_SEQUENCE_NUMBER = 0x7FFFFFFF,
		MAX_ACK_SEQUENCE_NUMBER = 0xFFFF, // 0x1FFFFFFF,
    MAX_MSG_SEQUENCE_NUMBER = 0x1FFFFFFF,
		MIN_OUTPUT_PERIOD = 8
  };

  enum : uint32_t { PACKET_SIZE_CORRECTION = 28 };

  using timeout_option_type =
      boost::asio::detail::socket_option::integer<BOOST_ASIO_OS_DEF(SOL_SOCKET),
                                                  TIMEOUT_DELAY>;

  using endpoint = Endpoint<Protocol>;

  using resolver = Resolver<Protocol>;

  using multiplexer_manager = MultiplexerManager<Protocol>;
  using multiplexer = Multiplexer<Protocol>;
  using flow = Flow<Protocol>;

  using congestion_control = CongestionControlAlg<Protocol>;

  using socket =
      boost::asio::basic_stream_socket<Protocol,
                                       stream_socket_service<Protocol>>;
  using acceptor =
      boost::asio::basic_socket_acceptor<Protocol,
                                         socket_acceptor_service<Protocol>>;

  // Datagram types
  using EmptyPayload = datagram::EmptyComponent;

  // Datagram header types
  using GenericHeader = datagram::basic_GenericHeader;
  using DataHeader = datagram::basic_DataHeader;
  using ControlHeader = datagram::basic_ControlHeader;

  // Datagram payload types
  using ConnectionPayload = datagram::basic_ConnectionPayload;
  using AckPayload = datagram::basic_AckPayload;
  using NAckPayload = datagram::basic_NAckPayload<MTU - GenericHeader::size>;
  using MessageDropRequestPayload = datagram::basic_MessageDropRequestPayload;
  using GenericReceivePayload =
      datagram::BufferPayload<MTU - GenericHeader::size>;
  using SendPayload =
      datagram::ConstBufferSequencePayload<MTU - GenericHeader::size>;

  // Generic datagram type
  using GenericReceiveDatagram =
      datagram::basic_Datagram<GenericHeader, GenericReceivePayload>;

  // Control datagram types
  using ConnectionDatagram =
      datagram::basic_Datagram<ControlHeader, ConnectionPayload>;
  using KeepAliveDatagram =
      datagram::basic_Datagram<ControlHeader, EmptyPayload>;
  using AckDatagram = datagram::basic_Datagram<ControlHeader, AckPayload>;
  using NAckDatagram = datagram::basic_Datagram<ControlHeader, NAckPayload>;
  using HAckDatagram = datagram::basic_Datagram<ControlHeader, EmptyPayload>;
  using ShutdownDatagram =
      datagram::basic_Datagram<ControlHeader, EmptyPayload>;
  using AckOfAckDatagram =
      datagram::basic_Datagram<ControlHeader, EmptyPayload>;
  using MessageDropRequestDatagram =
      datagram::basic_Datagram<ControlHeader, MessageDropRequestPayload>;

  using GenericControlDatagram =
      datagram::basic_Datagram<ControlHeader, GenericReceivePayload>;

  // Data datagram
  using DataDatagram =
      datagram::basic_Datagram<DataHeader, GenericReceivePayload>;
  using SendDatagram =
      datagram::basic_Datagram<DataHeader, GenericReceivePayload>;
  using ReceiveDatagram = DataDatagram;

 public:
  static MultiplexerManager<Protocol> multiplexers_manager_;
};

template <class NextLayer, template <class> class CongestionControlAlg>
MultiplexerManager<Protocol<NextLayer, CongestionControlAlg>>
    Protocol<NextLayer, CongestionControlAlg>::multiplexers_manager_;
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_PROTOCOL_H_
