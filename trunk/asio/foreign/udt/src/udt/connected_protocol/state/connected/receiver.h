#ifndef UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_RECEIVER_H_
#define UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_RECEIVER_H_

#include <cstdint>

#include <atomic>
#include <map>
#include <queue>
#include <set>

#include <boost/asio/io_service.hpp>
#include <boost/asio/basic_waitable_timer.hpp>

#include <boost/chrono.hpp>
#include <mutex>
#include "../../../../../../../../misc/sync.h"

#include "../../../common/error/error.h"
#include "../../cache/connection_info.h"
#include "../../io/buffers.h"
#include "../../io/read_op.h"
#include "../../sequence_generator.h"
#include "ack_history_window.h"
#include "packet_time_history_window.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace state {
namespace connected {

template <class Protocol, class ConnectedState>
class Receiver {
 private:
  using Clock = typename Protocol::clock;
  using Timer = typename Protocol::timer;
  using TimePoint = typename Protocol::time_point;
  using ReadOpsQueue =
      std::queue<io::basic_pending_stream_read_operation<Protocol> *>;
  using PacketSequenceNumber = uint32_t;
  using AckSequenceNumber = uint32_t;
  using SocketSession = typename Protocol::socket_session;
  using DataDatagram = typename Protocol::DataDatagram;
  using DataDatagramPtr = std::shared_ptr<DataDatagram>;
  using AckDatagram = typename Protocol::AckDatagram;
  using AckDatagramPtr = std::shared_ptr<AckDatagram>;
  using NAckDatagram = typename Protocol::NAckDatagram;
  using HAckDatagram = typename Protocol::HAckDatagram;
  using NAckDatagramPtr = std::shared_ptr<NAckDatagram>;
  using ReceivedDatagramsMap = std::map<PacketSequenceNumber, DataDatagram, ::std::function<bool(PacketSequenceNumber const &, PacketSequenceNumber const &)>>;

 public:
  Receiver(typename SocketSession::Ptr p_session)
      : mutex_(),
        p_session_(p_session),
        p_state_(nullptr),
        lrsn_(0),
        read_ops_mutex_(),
        read_ops_queue_(),
        max_received_size_(8192),
        packets_received_mutex_(),
        last_buffer_seq_(0),
        packet_history_window_(1024 * 8),
        ack_history_window_(),
        last_exp_reset_timestamp_(Clock::now()),
        exp_count_(0),
        largest_acknowledged_seq_number_(0),
        last_ack2_seq_number_(0),
        last_ack2_timestamp_(Clock::now()),
        last_ack_number_(0),
        last_ack_timestamp_(Clock::now()) {}

  void Init(typename ConnectedState::Ptr p_state,
            PacketSequenceNumber initial_packet_seq_num) {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    p_state_ = p_state;
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    last_exp_reset_timestamp_ = Clock::now();

    lrsn_ = initial_packet_seq_num - 1;
    largest_ack_number_acknowledged_ = initial_packet_seq_num;
    last_ack_number_ = initial_packet_seq_num;
    last_buffer_seq_ = initial_packet_seq_num - 1;
    last_ack_timestamp_ = Clock::now();
    last_ack2_timestamp_ = Clock::now();
    const auto &connection_info = p_session->connection_info();

    if (connection_info.packet_arrival_speed() > 0 &&
        connection_info.estimated_link_capacity() > 0) {
      packet_history_window_.Init(connection_info.packet_arrival_speed(),
                                  connection_info.estimated_link_capacity());
    } else {
      packet_history_window_.Init();
    }
  }

  void Stop() {
    CloseReadOpsQueue();
    p_state_.reset();
  }

	double static constexpr packet_arrival_speed_scaler_throttle{0.9};
	double static constexpr packet_arrival_speed_scaler_throttle_inv{1. / packet_arrival_speed_scaler_throttle};
	double packet_arrival_speed_scaler{1};
	void
	on_nack_timer()
	{
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

		::std::cerr << "in nack timer" << ::std::endl;
		auto const threshold(max_received_size_ >> 2);
		bool packets_received_empty;
		uint_fast32_t remaining_buffer_size;
		{
			::std::lock_guard<synapse::misc::mutex> lock_packets_received(packets_received_mutex_);
			if (packets_received_.empty())
				packets_received_empty = true;
			else 
				packets_received_empty = false;
			//remaining_buffer_size = max_received_size_ - ::std::min(max_received_size_, static_cast<uint32_t>(packets_received_.size()));
		}
		if (packets_received_empty == false) {
			::std::lock_guard<synapse::misc::mutex> lock(mutex_);
			bool nack_sent(false);
			::std::cerr << "in nack -- loss size " << loss_list_.size() << ::std::endl;
			if (!loss_list_.empty()) {
				::std::cerr << "critical recovery NACK SENDING" << ::std::endl;
				// critical recovery -- packets are accumulating but lost ones are preventing further processing...
				auto prev_i(*loss_list_.begin());
				auto range_begin_i(prev_i);

				double const loss_ratio(static_cast<double>(loss_list_.size()) / threshold);
				const auto &packet_seq_gen = p_session->packet_seq_gen();

				::std::cerr << "loss ratio " << loss_ratio << ::std::endl;
				//::std::cerr << "remaining buffer " << remaining_buffer_size << ::std::endl;
				::std::cerr << "threshold " << threshold << ::std::endl;
				::std::cerr << "dist on loss " << packet_seq_gen.distance(range_begin_i, lrsn_) << ::std::endl;
				::std::cerr << ::std::endl;

				uint32_t additional_info(NAckDatagram::Header::NO_ADDITIONAL_INFO);
				if (packet_history_window_.GetPacketArrivalSpeed() > 0)
					additional_info = htonl(static_cast<uint32_t>(packet_history_window_.GetPacketArrivalSpeed() * packet_arrival_speed_scaler));

				for (auto i(loss_list_.begin());  
					i != loss_list_.end() && 
					(packet_seq_gen.distance(range_begin_i, lrsn_) > static_cast<int32_t>(threshold))
					; 
					++i
				) {
					if (*i - prev_i > 1) {
						auto p_nack_dgr = std::make_shared<NAckDatagram>();
						if (prev_i != range_begin_i)
							p_nack_dgr->payload().AddLossRange(range_begin_i, prev_i);
						else
							p_nack_dgr->payload().AddLossPacket(range_begin_i);
						::std::cerr << "SENT loss packet" << ::std::endl;
						p_session->AsyncSendControlPacket(*p_nack_dgr, NAckDatagram::Header::NACK, additional_info, [p_session, p_nack_dgr](boost::system::error_code const &, std::size_t) {});
						nack_sent = true;
						range_begin_i = *i;
					}
					prev_i = *i;
				}
				if (packet_seq_gen.distance(range_begin_i, lrsn_) > static_cast<int32_t>(max_received_size_)) {
					auto p_nack_dgr = std::make_shared<NAckDatagram>();
					if (prev_i != range_begin_i)
						p_nack_dgr->payload().AddLossRange(range_begin_i, prev_i);
					else
						p_nack_dgr->payload().AddLossPacket(range_begin_i);
					::std::cerr << "SENT loss packet" << ::std::endl;
					p_session->AsyncSendControlPacket(*p_nack_dgr, NAckDatagram::Header::NACK, additional_info, [p_session, p_nack_dgr](boost::system::error_code const &, std::size_t) {});
					nack_sent = true;
				}
			}

			if (nack_sent == false) {
				packet_arrival_speed_scaler = ::std::min(packet_arrival_speed_scaler * packet_arrival_speed_scaler_throttle_inv, 1.);
				if (packet_history_window_.GetPacketArrivalSpeed() > 0) {
					auto p_nack_dgr = std::make_shared<HAckDatagram>();
					p_session->AsyncSendControlPacket(*p_nack_dgr, HAckDatagram::Header::HACK, htonl(static_cast<uint32_t>(packet_history_window_.GetPacketArrivalSpeed())), [p_session, p_nack_dgr](boost::system::error_code const &, std::size_t) {});
				}
			} else {
				packet_arrival_speed_scaler = ::std::max(packet_arrival_speed_scaler * packet_arrival_speed_scaler_throttle, .5);
			}
			::std::cerr << "PACKET ARRIVAL SCALER " << packet_arrival_speed_scaler << ::std::endl;
			::std::cerr << "rate:  " << 1000000. / GetPacketArrivalSpeed() << ::std::endl;

		} 
	}

  void OnDataDatagram(DataDatagram *p_datagram) {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    const auto &packet_seq_gen = p_session->packet_seq_gen();
    auto &header = p_datagram->header();
    PacketSequenceNumber packet_seq_num = header.packet_sequence_number();

		// ::std::cerr << "in receiver on datagram, seq " << packet_seq_num << ::std::endl;

    // Save packet arrival time in receiver history window
    packet_history_window_.OnArrival();

    // Register first packet probe arrival
    if (packet_seq_num % 16 == 0) {
      packet_history_window_.OnFirstProbe();
    }

    // Register second packet probe arrival
    if (packet_seq_num % 16 == 1) {
      packet_history_window_.OnSecondProbe();
    }


    {
      ::std::lock_guard<synapse::misc::mutex> lock_packets_received(packets_received_mutex_);
			auto &begin_pair = *(packets_received_.begin());
			auto first_seq_num_received_buffer = begin_pair.first;

			auto const dist(packet_seq_gen.distance(last_buffer_seq_, packet_seq_num));
      if (dist <= 0 || packets_received_.count(packet_seq_num)) {
        // packet already processed
				//::std::cerr << "in receiver on datagram  already processed, distance  " << dist << ::std::endl;
        return;
      }
      if (!packets_received_.empty()) {
				auto const dist(packet_seq_gen.distance(first_seq_num_received_buffer, packet_seq_num));
        if (dist > static_cast<int32_t>(max_received_size_ << 2)) {
          // drop -> no more buffer space available
					::std::cerr << "in receiver on datagram  dropping out of buffer dist " << dist << ::std::endl;
          return;
        }
      }
    }

    {
      ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
			auto const lrsn(lrsn_.load());
			auto const dist(packet_seq_gen.distance(lrsn, packet_seq_num));
      if (dist > 1) {
				auto const lrst_inc(packet_seq_gen.Inc(lrsn));
        uint32_t i = lrst_inc;
        while (i != packet_seq_num) {
          loss_list_.insert(i);
          i = packet_seq_gen.Inc(i);
        }

#if 0
        auto p_nack_dgr = std::make_shared<NAckDatagram>();
        if (lrst_inc != packet_seq_gen.Dec(packet_seq_num)) {
          p_nack_dgr->payload().AddLossRange(lrst_inc, packet_seq_gen.Dec(packet_seq_num));
        } else {
          p_nack_dgr->payload().AddLossPacket(lrst_inc);
        }
        // send nack datagram
        p_session->AsyncSendControlPacket(
            *p_nack_dgr, NAckDatagram::Header::NACK,
            NAckDatagram::Header::NO_ADDITIONAL_INFO,
            [p_session, p_nack_dgr](const boost::system::error_code &,
                                    std::size_t) {});
#endif

      } else if (dist <= 0) {
        loss_list_.erase(packet_seq_num);
      }
      if (dist > 0)
        lrsn_ = packet_seq_num;
    }

    {
      ::std::lock_guard<synapse::misc::mutex> lock_packets_received(packets_received_mutex_);
      packets_received_[packet_seq_num] = std::move(*p_datagram);
    }

    p_session->get_io_service().post(boost::bind(
        &Receiver::HandleQueues, this, boost::system::error_code(), p_state_));
  }

  void StoreAck(AckSequenceNumber ack_seq_num, PacketSequenceNumber ack_number,
                bool light_ack) {
    ack_history_window_.StoreAck(ack_seq_num, ack_number);
    if (!light_ack) {
      last_ack_timestamp_ = Clock::now();
    }
  }

  bool AckAck(AckSequenceNumber ack_seq_num,
              PacketSequenceNumber *p_packet_seq_num,
              boost::chrono::microseconds *p_rtt) {
    return ack_history_window_.Acknowledge(ack_seq_num, p_packet_seq_num,
                                           p_rtt);
  }

  uint32_t AvailableReceiveBufferSize() {
    //::std::lock_guard<synapse::misc::mutex> lock(packets_received_mutex_);
    //return max_received_size_ - ::std::min(max_received_size_, static_cast<uint32_t>(packets_received_.size()));
    return max_received_size_;
  }

  double GetPacketArrivalSpeed() {
    return packet_history_window_.GetPacketArrivalSpeed() * packet_arrival_speed_scaler;
  }

  double GetEstimatedLinkCapacity() {
    return packet_history_window_.GetEstimatedLinkCapacity();
  }

  void IncExpCounter() { exp_count_ = exp_count_.load() + 1; }

  void ResetExpCounter() {
    ::std::lock_guard<synapse::misc::mutex> lock_exp(mutex_);
    exp_count_ = 1;
    last_exp_reset_timestamp_ = Clock::now();
  }

  uint64_t exp_count() { return exp_count_.load(); }

  bool HasTimeout() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    return exp_count_.load() > 16 &&
           boost::chrono::duration_cast<boost::chrono::seconds>(
               Clock::now() - last_exp_reset_timestamp_)
                   .count() > 10;
  }

  void PushReadOp(io::basic_pending_stream_read_operation<Protocol> *read_op) {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    {
      ::std::lock_guard<synapse::misc::mutex> lock_read_ops(read_ops_mutex_);
      read_ops_queue_.push(read_op);
    }
    p_session->get_io_service().post(boost::bind(
        &Receiver::HandleQueues, this, boost::system::error_code(), p_state_));
  }

  PacketSequenceNumber AckNumber(const SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER> &packet_seq_gen) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    if (loss_list_.empty()) {
      return packet_seq_gen.Inc(lrsn_.load());
    } else {
      return *(loss_list_.begin());
    }
  }

  void set_largest_acknowledged_seq_number(
      PacketSequenceNumber largest_acknowledged_seq_number) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    largest_acknowledged_seq_number_ = largest_acknowledged_seq_number;
  }

  PacketSequenceNumber largest_acknowledged_seq_number() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    return largest_acknowledged_seq_number_;
  }

  void set_largest_ack_number_acknowledged(
      PacketSequenceNumber largest_ack_number_acknowledged) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    largest_ack_number_acknowledged_ = largest_ack_number_acknowledged;
  }

  PacketSequenceNumber largest_ack_number_acknowledged() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    return largest_ack_number_acknowledged_;
  }

  void set_last_ack2_seq_number(AckSequenceNumber last_ack2_seq_number) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    last_ack2_seq_number_ = last_ack2_seq_number;
    last_ack2_timestamp_ = Clock::now();
  }

  void set_last_ack_number(PacketSequenceNumber last_ack_number) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    last_ack_number_ = last_ack_number;
  }

  PacketSequenceNumber last_ack_number() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    return last_ack_number_;
  }

  TimePoint last_ack_timestamp() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    return last_ack_timestamp_;
  }

 private:
  void HandleQueues(const boost::system::error_code &ec,
                    typename ConnectedState::Ptr ) {
    auto p_session = p_session_.lock();
    if (!p_session || !p_state_) {
      return;
    }

    ::std::lock_guard<synapse::misc::mutex> packet_received_lock(packets_received_mutex_);
    ::std::lock_guard<synapse::misc::mutex> read_ops_lock_(read_ops_mutex_);

    if (read_ops_queue_.empty() || packets_received_.empty()) {
      return;
    }

    if (ec) {
      CloseReadOpsQueue();
      return;
    }


    const auto &packet_seq_gen = p_session->packet_seq_gen();

    io::fixed_const_buffer_sequence packets_buffer;

    auto begin_it = packets_received_.begin();
    auto current_packet_it = begin_it;
    auto next_packet_it = begin_it;

		//::std::cerr << ("handling packet in receiver, last seq " + ::std::to_string(last_buffer_seq_) + ", current " + ::std::to_string(current_packet_it->first) ) << ::std::endl;
    if (last_buffer_seq_ != packet_seq_gen.Dec(current_packet_it->first)) {
      // wait the next seq number
      return;
    }

    while (current_packet_it != packets_received_.end()) {
      current_packet_it->second.payload().GetConstBuffers(&packets_buffer);
      ++next_packet_it;
      // end reached or gap in sequence number
      if (next_packet_it == packets_received_.end() ||
          (current_packet_it->first !=
           packet_seq_gen.Dec(next_packet_it->first))) {
        break;
      }
      current_packet_it = next_packet_it;
    }

    io::basic_pending_stream_read_operation<Protocol> *read_op =
        read_ops_queue_.front();
    read_ops_queue_.pop();

    std::size_t copied(read_op->fill_buffer(packets_buffer));
    std::size_t offset(copied);
    std::size_t buffer_size(0);
    auto packet_it = begin_it;

    // clean packets_received set
    while (packet_it != packets_received_.end() && offset > 0) {
      auto &payload = packet_it->second.payload();
      buffer_size = payload.GetSize();
      if (offset >= buffer_size) {
        // packet consumed entirely
        offset -= buffer_size;
        last_buffer_seq_ = packet_it->first;
        packet_it = packets_received_.erase(packet_it);
      } else {
        // partial consuming
				::std::cerr << "interesting partial consuming???" << ::std::endl;
        last_buffer_seq_ = packet_seq_gen.Dec(packet_it->first);
        payload.SetOffset(static_cast<uint32_t>(offset));
        offset = 0;
      }
    }

    auto do_complete =
        [read_op, ec, copied]() { read_op->complete(ec, copied); };

    p_session->get_io_service().post(::std::move(do_complete));
  }

  void CloseReadOpsQueue() {
    auto p_session = p_session_.lock();
    if (!p_session) {
      return;
    }

    ::std::lock_guard<synapse::misc::mutex> lock_read_ops(read_ops_mutex_);
    // Unqueue read ops queue and callback with error code
    io::basic_pending_stream_read_operation<Protocol> *p_read_op;
    while (!read_ops_queue_.empty()) {
      p_read_op = read_ops_queue_.front();
      read_ops_queue_.pop();
      auto do_complete = [p_read_op]() {
        boost::system::error_code ec(udt::common::error::operation_canceled,
                                     udt::common::error::get_error_category());
        p_read_op->complete(ec, 0);
      };
      p_session->get_io_service().dispatch(std::move(do_complete));
    }
  }

 private:
  // mutex
  synapse::misc::mutex mutex_;
  // session
  std::weak_ptr<SocketSession> p_session_;
  // owner state
  typename ConnectedState::Ptr p_state_;

  // packet largest received sequence number
  std::atomic<PacketSequenceNumber> lrsn_;

  // packets loss list, sorted by seq_number increased order
  std::set<PacketSequenceNumber, ::std::function<bool(PacketSequenceNumber const &, PacketSequenceNumber const &)>> loss_list_{
		[](PacketSequenceNumber const & a, PacketSequenceNumber const & b){
			return SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER>::distance(a, b) > 0 ? true : false;
		}
	};

  // Read ops queue
  synapse::misc::mutex read_ops_mutex_;
  ReadOpsQueue read_ops_queue_;

  // packets received
  uint32_t max_received_size_;
  synapse::misc::mutex packets_received_mutex_;
  ReceivedDatagramsMap packets_received_{
		[](PacketSequenceNumber const & a, PacketSequenceNumber const & b){
			return SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER>::distance(a, b) > 0 ? true : false;
		}
	};


  PacketSequenceNumber last_buffer_seq_;

  // packet history window (arrival time of data packet)
  PacketTimeHistoryWindow packet_history_window_;

  // ack history window
  AckHistoryWindow ack_history_window_;

  // last exp counter reset
  TimePoint last_exp_reset_timestamp_;
  // consecutive expired timeout : timeout > 16
  std::atomic<uint64_t> exp_count_;

  PacketSequenceNumber largest_acknowledged_seq_number_;
  // largest ack number acknowledged by ACK2
  PacketSequenceNumber largest_ack_number_acknowledged_;
  // last ack2 sent back
  AckSequenceNumber last_ack2_seq_number_;
  // last ack2 timestamp
  TimePoint last_ack2_timestamp_;
  // last ack number
  PacketSequenceNumber last_ack_number_;
  // last ack timestamp
  TimePoint last_ack_timestamp_;
};

}  // connected
}  // state
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_RECEIVER_H_
