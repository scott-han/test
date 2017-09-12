#ifndef UDT_CONNECTED_PROTOCOL_FLOW_H_
#define UDT_CONNECTED_PROTOCOL_FLOW_H_

#include <cstdint>

#include <chrono>
#include <set>
#include <memory>

#include <boost/asio/buffer.hpp>

#include <boost/bind.hpp>

#include <boost/system/error_code.hpp>
#include <mutex>
#include "../../../../../../misc/sync.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class Protocol>
class Flow : public std::enable_shared_from_this<Flow<Protocol>> {
 public:
  using NextEndpointType = typename Protocol::next_layer_protocol::endpoint;
  using Datagram = typename Protocol::SendDatagram;
  using DatagramPtr = std::shared_ptr<Datagram>;

  struct DatagramAddressPair {
    DatagramPtr p_datagram;
    NextEndpointType remote_endpoint;
  };

 private:
  using Timer = typename Protocol::timer;
  using Clock = typename Protocol::clock;
  using TimePoint = typename Protocol::time_point;
  using SocketSession = typename Protocol::socket_session;

  struct CompareSessionPacketSendingTime {
    bool operator()(std::weak_ptr<SocketSession> p_lhs,
                    std::weak_ptr<SocketSession> p_rhs) {
      auto p_shared_lhs = p_lhs.lock();
      auto p_shared_rhs = p_rhs.lock();
      if (!p_shared_lhs || !p_shared_rhs) {
        return true;
      }

      return p_shared_lhs->NextScheduledPacketTime() <
             p_shared_rhs->NextScheduledPacketTime();
    }
  };

  using SocketsContainer =
      std::set<std::weak_ptr<SocketSession>, CompareSessionPacketSendingTime>;

 public:
  using Ptr = std::shared_ptr<Flow>;

 public:
  static Ptr Create(boost::asio::io_service& io_service) {
    return Ptr(new Flow(io_service));
  }

  ~Flow() {}

  void RegisterNewSocket(typename SocketSession::Ptr p_session) {
    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock(mutex_);
      auto inserted = socket_sessions_.insert(p_session);
      if (inserted.second) {
        // relaunch timer since there is a new socket
        StopPullSocketQueue();
      }
    }

    StartPullingSocketQueue();
  }

 private:
  Flow(boost::asio::io_service& io_service)
      : io_service_(io_service),
        mutex_(),
        socket_sessions_(),
        next_packet_timer_(io_service),
        pulling_(false),
        sent_count_(0) {}

  void StartPullingSocketQueue() {
    if (::std::atomic_exchange(&pulling_, true)) {
      return;
		}
    PullSocketQueue();
  }

  void PullSocketQueue(bool no_busy_loop = false) {
    typename SocketsContainer::iterator p_next_socket_expired_it;

    if (!pulling_.load()) {
      return;
    }

    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock_socket_sessions(mutex_);
      if (socket_sessions_.empty()) {
        this->StopPullSocketQueue();
        return;
      }

      p_next_socket_expired_it = socket_sessions_.begin();

      auto p_next_socket_expired = (*p_next_socket_expired_it).lock();
      if (!p_next_socket_expired) {
        io_service_.post(
            boost::bind(&Flow::PullSocketQueue, this->shared_from_this(), false));
        return;
      }

      auto next_scheduled_packet_interval =
          p_next_socket_expired->NextScheduledPacketTime();

timeout___ = next_scheduled_packet_interval.count();

      if (next_scheduled_packet_interval.count() <= Protocol::MIN_OUTPUT_PERIOD && no_busy_loop == false) {
        // Resend immediatly
        boost::system::error_code ec;
        ec.assign(udt::common::error::success,
                  udt::common::error::get_error_category());
        io_service_.post(boost::bind(&Flow::WaitPullSocketHandler,
                                     this->shared_from_this(), ec));
        return;
      }

			next_packet_timer_.expires_from_now(next_scheduled_packet_interval);
      next_packet_timer_.async_wait(boost::bind(&Flow::WaitPullSocketHandler,
                                                this->shared_from_this(), _1));
    }
  }

  // When no data is available
  void StopPullSocketQueue() {
    pulling_ = false;
    boost::system::error_code ec;
    next_packet_timer_.cancel(ec);
  }

 public:
	int_fast64_t timeout___{0};
	uint_fast64_t blah{0};
  void WaitPullSocketHandler(const boost::system::error_code& ec) {
		++blah;
#if 0
    if (ec) {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock_socket_sessions(mutex_);
      StopPullSocketQueue();
      return;
    }
#else
    if (!pulling_ || ec) {
      return;
		}
#endif

    typename SocketSession::Ptr p_session;
    Datagram* p_datagram;
    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock_socket_sessions(mutex_);

      if (socket_sessions_.empty()) {
        StopPullSocketQueue();
        return;
      }

      typename SocketsContainer::iterator p_session_it;
      p_session_it = socket_sessions_.begin();

      p_session = (*p_session_it).lock();

      if (!p_session) {
        socket_sessions_.erase(p_session_it);
        PullSocketQueue();
        return;
      }
      socket_sessions_.erase(p_session_it);
      p_datagram = p_session->NextScheduledPacket();

      if (p_session->HasPacketToSend()) {
        socket_sessions_.insert(p_session);
      }

    }

    if (p_datagram && p_session) {
      auto self = this->shared_from_this();
      p_session->AsyncSendPacket(
          p_datagram, [p_datagram](const boost::system::error_code& ,
                                   std::size_t ) {
					});
			PullSocketQueue();
    } else
			PullSocketQueue(true);

  }

 private:
  boost::asio::io_service& io_service_;

  synapse::misc::recursive_mutex mutex_;
  // sockets list
  SocketsContainer socket_sessions_;

  Timer next_packet_timer_;
 public:
  std::atomic<bool> pulling_;
  std::atomic<uint32_t> sent_count_;
};

}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_FLOW_H_
