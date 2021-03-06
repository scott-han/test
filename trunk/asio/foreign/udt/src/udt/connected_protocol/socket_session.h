#ifndef UDT_CONNECTED_PROTOCOL_SOCKET_SESSION_H_
#define UDT_CONNECTED_PROTOCOL_SOCKET_SESSION_H_

#include <cstdint>

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>

#include <boost/asio/io_service.hpp>
#include <boost/asio/socket_base.hpp>

#include <boost/chrono.hpp>
#include <mutex>
#include "../../../../../../misc/sync.h"

#include "io/connect_op.h"
#include "io/write_op.h"
#include "io/read_op.h"

#include "cache/connection_info.h"
#include "cache/connections_info_manager.h"

#include "sequence_generator.h"
#include "state/base_state.h"
#include "state/closed_state.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class Protocol>
class SocketSession
    : public std::enable_shared_from_this<SocketSession<Protocol>> {
 private:
  using Endpoint = typename Protocol::endpoint;
  using EndpointPtr = std::shared_ptr<Endpoint>;
  using NextLayerEndpoint = typename Protocol::next_layer_protocol::endpoint;

 private:
  using MultiplexerPtr = std::shared_ptr<typename Protocol::multiplexer>;
  using FlowPtr = std::shared_ptr<typename Protocol::flow>;
  using AcceptorSession = typename Protocol::acceptor_session;
  using ConnectionInfo = cache::ConnectionInfo;
  using ConnectionInfoPtr = cache::ConnectionInfo::Ptr;
  using ClosedState = state::ClosedState<Protocol>;
  using BaseStatePtr =
      typename connected_protocol::state::BaseState<Protocol>::Ptr;

 private:
  using TimePoint = typename Protocol::time_point;
  using Clock = typename Protocol::clock;
  using Timer = typename Protocol::timer;

 private:
  using ConnectionDatagram = typename Protocol::ConnectionDatagram;
  using ConnectionDatagramPtr = std::shared_ptr<ConnectionDatagram>;
  using ControlDatagram = typename Protocol::GenericControlDatagram;
  using ControlHeader = typename ControlDatagram::Header;
  using SendDatagram = typename Protocol::SendDatagram;
  using DataDatagram = typename Protocol::DataDatagram;
  using AckDatagram = typename Protocol::AckDatagram;
  using AckOfAckDatagram = typename Protocol::AckOfAckDatagram;
  using PacketSequenceNumber = uint32_t;
  using SocketId = uint32_t;

 public:
  using Ptr = std::shared_ptr<SocketSession>;

 public:
  static Ptr Create(MultiplexerPtr p_multiplexer, FlowPtr p_flow) {
    Ptr p_session(
        new SocketSession(std::move(p_multiplexer), std::move(p_flow)));
    p_session->Init();

    return p_session;
  }

  ~SocketSession() {}

  void set_next_remote_endpoint(const NextLayerEndpoint& next_remote_ep) {
    if (next_remote_endpoint_ == NextLayerEndpoint()) {
      next_remote_endpoint_ = next_remote_ep;
      p_connection_info_cache_ =
          connections_info_manager_.GetConnectionInfo(next_remote_ep);
      auto connection_cache = p_connection_info_cache_.lock();
      if (connection_cache) {
        connection_info_ = *connection_cache;
      }
    }
  }

  const NextLayerEndpoint& next_remote_endpoint() {
    return next_remote_endpoint_;
  }

  void set_next_local_endpoint(const NextLayerEndpoint& next_local_ep) {
    if (next_local_endpoint_ == NextLayerEndpoint()) {
      next_local_endpoint_ = next_local_ep;
    }
  }

  const NextLayerEndpoint& next_local_endpoint() {
    return next_local_endpoint_;
  }

  bool IsClosed() {
    auto p_state = p_state_;
    return state::BaseState<Protocol>::CLOSED == p_state->GetType();
  }

  void Close() {
    auto p_state = p_state_;
    p_state->Close();
  }

  void PushReadOp(io::basic_pending_stream_read_operation<Protocol>* read_op) {
    auto p_state = p_state_;
    p_state->PushReadOp(read_op);
  }

  void PushWriteOp(io::basic_pending_write_operation* write_op) {
    auto p_state = p_state_;
    p_state->PushWriteOp(write_op);
  }

  void PushConnectionDgr(ConnectionDatagramPtr p_connection_dgr) {
    auto p_state = p_state_;
    p_state->OnConnectionDgr(p_connection_dgr);
  }

  void PushControlDgr(ControlDatagram* p_control_dgr) {
    auto p_state = p_state_;
    p_state->OnControlDgr(p_control_dgr);
  }

  void PushDataDgr(DataDatagram* p_datagram) {
    auto p_state = p_state_;
    p_state->OnDataDgr(p_datagram);
  }

  bool HasPacketToSend() {
    auto p_state = p_state_;
    return p_state->HasPacketToSend();
  }

  SendDatagram* NextScheduledPacket() {
    auto p_state = p_state_;
    return p_state->NextScheduledPacket();
  }

  boost::chrono::nanoseconds NextScheduledPacketTime() {
    auto p_state = p_state_;
    return p_state->NextScheduledPacketTime();
  }

  // High priority sending : use for control packet only
  template <class Datagram, class Handler>
  void AsyncSendControlPacket(Datagram& datagram,
                              typename ControlHeader::type type,
                              uint32_t additional_info, Handler handler) {
    FillControlHeader(&(datagram.header()), type, additional_info);
    p_multiplexer_->AsyncSendControlPacket(datagram, next_remote_endpoint_,
                                           handler);
  }

  template <class Datagram, class Handler>
  void AsyncSendPacket(Datagram* p_datagram, Handler handler) {
    p_multiplexer_->AsyncSendDataPacket(p_datagram, next_remote_endpoint_,
                                        handler);
  }

  void AsyncSendPackets() {
    p_flow_->RegisterNewSocket(this->shared_from_this());
  }

  // State management
  void SetAcceptor(AcceptorSession* p_acceptor) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(acceptor_mutex_);
    p_acceptor_ = p_acceptor;
  }

  void RemoveAcceptor() {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(acceptor_mutex_);
    p_acceptor_ = nullptr;
  }

  // Change session's current state
  void ChangeState(BaseStatePtr p_new_state) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    auto p_state = p_state_;
    if (p_state) {
      p_state->Stop();
    }
    p_state_ = std::move(p_new_state);
    p_state_->Init();
    NotifyAcceptor();
  }

  void Unbind() {
    boost::system::error_code ec;
    p_multiplexer_->RemoveSocketSession(next_remote_endpoint_, socket_id_);
  }

  typename connected_protocol::state::BaseState<Protocol>::type GetState() {
    return p_state_->GetType();
  }

  SocketId socket_id() const { return socket_id_; }

  void set_socket_id(SocketId socket_id) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    socket_id_ = socket_id;
  }

  SocketId remote_socket_id() const { return remote_socket_id_; }

  void set_remote_socket_id(SocketId remote_socket_id) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    remote_socket_id_ = remote_socket_id;
  }

  uint32_t syn_cookie() const { return syn_cookie_; }

  void set_syn_cookie(uint32_t syn_cookie) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    syn_cookie_ = syn_cookie;
  }

  SequenceGenerator<Protocol::MAX_MSG_SEQUENCE_NUMBER>* get_p_message_seq_gen() { return &message_seq_gen_; }

  const SequenceGenerator<Protocol::MAX_MSG_SEQUENCE_NUMBER>& message_seq_gen() const { return message_seq_gen_; }

  SequenceGenerator<Protocol::MAX_ACK_SEQUENCE_NUMBER>* get_p_ack_seq_gen() { return &ack_seq_gen_; }

  const SequenceGenerator<Protocol::MAX_ACK_SEQUENCE_NUMBER>& ack_seq_gen() const { return ack_seq_gen_; }

  SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER>* get_p_packet_seq_gen() { return &packet_seq_gen_; }

  const SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER>& packet_seq_gen() const { return packet_seq_gen_; }

  void set_timeout_delay(uint32_t delay) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    timeout_delay_ = delay;
  }

  uint32_t timeout_delay() const { return timeout_delay_; }

  void set_start_timestamp(const TimePoint& start) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    start_timestamp_ = start;
  }

  const TimePoint& start_timestamp() const { return start_timestamp_; }

  uint32_t max_window_flow_size() const { return max_window_flow_size_; }

  void set_max_window_flow_size(uint32_t max_window_flow_size) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    max_window_flow_size_ = max_window_flow_size;
  }

  uint32_t window_flow_size() const { 
		return window_flow_size_; 
	}

  void set_window_flow_size(uint32_t window_flow_size) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    window_flow_size_ = window_flow_size;
  }

  uint32_t init_packet_seq_num() const { return init_packet_seq_num_; }

  void set_init_packet_seq_num(uint32_t init_packet_seq_num) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    init_packet_seq_num_ = init_packet_seq_num;
  }

  const ConnectionInfo& connection_info() const { return connection_info_; }

  ConnectionInfo* get_p_connection_info() { return &connection_info_; }

  void UpdateCacheConnection() {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_session(mutex_);
    auto connection_cache = p_connection_info_cache_.lock();
    if (connection_cache) {
      connection_cache->Update(connection_info_);
    }
  }

  boost::asio::io_service& get_io_service() {
    return p_multiplexer_->get_io_service();
  }

 private:
  SocketSession(MultiplexerPtr p_multiplexer, FlowPtr p_fl)
	: 
		mutex_(),
		p_multiplexer_(std::move(p_multiplexer)),
		p_flow_(std::move(p_fl)),
		acceptor_mutex_(),
		p_acceptor_(nullptr),
		p_state_(ClosedState::Create(p_multiplexer_->get_io_service())),
		socket_id_(0),
		remote_socket_id_(0),
		next_local_endpoint_(),
		next_remote_endpoint_(),
		syn_cookie_(0),
		max_window_flow_size_(0),
		window_flow_size_(0),
		init_packet_seq_num_(0),
		timeout_delay_(30),
		start_timestamp_(Clock::now()),
		connection_info_(),
		p_connection_info_cache_()
	{
	}

  void Init() {
    // initialize local endpoint with multiplexer's one
    boost::system::error_code ec;
    next_local_endpoint_ = p_multiplexer_->local_endpoint(ec);
  }

  void FillControlHeader(ControlHeader* p_control_header,
                         typename ControlHeader::type type,
                         uint32_t additional_info) {
    p_control_header->set_flags(type);
    p_control_header->set_additional_info(additional_info);
    p_control_header->set_destination_socket(remote_socket_id_);
    p_control_header->set_timestamp(static_cast<uint32_t>(
        boost::chrono::duration_cast<boost::chrono::microseconds>(
            Clock::now() - start_timestamp_)
            .count()));
  }

  void NotifyAcceptor() {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(acceptor_mutex_);
    if (p_acceptor_) {
      p_acceptor_->Notify(this);
    }
  }

 private:
  synapse::misc::recursive_mutex mutex_;
  MultiplexerPtr p_multiplexer_;
 public:
  FlowPtr p_flow_;
  synapse::misc::recursive_mutex acceptor_mutex_;
  AcceptorSession* p_acceptor_;
  BaseStatePtr p_state_;
  SocketId socket_id_;
  SocketId remote_socket_id_;
  NextLayerEndpoint next_local_endpoint_;
  NextLayerEndpoint next_remote_endpoint_;
  uint32_t syn_cookie_;
  uint32_t max_window_flow_size_;
  uint32_t window_flow_size_;
  PacketSequenceNumber init_packet_seq_num_;
  SequenceGenerator<Protocol::MAX_MSG_SEQUENCE_NUMBER> message_seq_gen_;
  SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER> packet_seq_gen_;
  SequenceGenerator<Protocol::MAX_ACK_SEQUENCE_NUMBER> ack_seq_gen_;
  /// Timeout delay in seconds
  uint32_t timeout_delay_;
  TimePoint start_timestamp_;
  // Connection cache
  ConnectionInfo connection_info_;
  std::weak_ptr<ConnectionInfo> p_connection_info_cache_;

 private:
  static cache::ConnectionsInfoManager<Protocol> connections_info_manager_;
};

template <class Protocol>
cache::ConnectionsInfoManager<Protocol>
    SocketSession<Protocol>::connections_info_manager_;

}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_SOCKET_SESSION_H_
