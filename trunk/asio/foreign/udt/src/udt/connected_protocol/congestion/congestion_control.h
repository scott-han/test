#ifndef UDT_CONNECTED_PROTOCOL_CONGESTION_CONGESTION_CONTROL_H_
#define UDT_CONNECTED_PROTOCOL_CONGESTION_CONGESTION_CONTROL_H_

#include <memory>

#include <boost/chrono.hpp>

#include "../sequence_generator.h"
#include "../cache/connection_info.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace congestion {

template <class Protocol>
class CongestionControl {
 private:
  using Clock = typename Protocol::clock;
  using TimePoint = typename Protocol::time_point;

 public:
  using ConnectionInfo = connected_protocol::cache::ConnectionInfo;
  using PacketSequenceNumber = uint32_t;
  using SocketSession = typename Protocol::socket_session;
  using SendDatagram = typename Protocol::SendDatagram;
  using DataDatagram = typename Protocol::DataDatagram;
  using AckDatagram = typename Protocol::AckDatagram;
  using NAckDatagram = typename Protocol::NAckDatagram;

  CongestionControl(ConnectionInfo *p_connection_info)
      : p_connection_info_(p_connection_info),
        window_flow_size_(connected_protocol::cache::ConnectionInfo::min_window_size),
        max_window_size_(0),
        sending_period_(Protocol::MIN_OUTPUT_PERIOD),
        slow_start_phase_(true),
        loss_phase_(false),
        last_ack_number_(0),
        avg_nack_num_(1),
        nack_count_(1),
        dec_count_(1),
        last_dec_sending_period_(1.0),
        last_send_seq_num_(0),
        dec_random_(1),
        last_update_(Clock::now()) 
				, last_sent_packet_time(TimePoint::min())
	{}

  void Init(PacketSequenceNumber init_packet_seq_num,
            uint32_t max_window_size) {
    last_dec_seq_num_ = init_packet_seq_num - 1;
    last_ack_number_ = init_packet_seq_num - 1;
    max_window_size_ = max_window_size;
    last_update_ = Clock::now();
  }

	void 
	on_full_window()
	{
		last_sent_packet_time = TimePoint::min();
	}

	::std::atomic<double> packet_sending_period;
	::std::atomic<double> last_packet_sending_period;
	::std::atomic<uint_fast64_t> same_t_counter{0};
  void OnPacketSent(const SendDatagram &) 
	{
		if (!slow_start_phase_) {
			auto const now(Clock::now());
			if (last_sent_packet_time != TimePoint::min()) {
				auto const delta(boost::chrono::duration_cast<boost::chrono::microseconds>(now - last_sent_packet_time));
				if (delta.count() > 0) {
					packet_sending_period = .998 * packet_sending_period + .002 * (static_cast<double>(delta.count()) / ++same_t_counter);
					same_t_counter = 0;
					last_sent_packet_time = now;
				} else {
					++same_t_counter;
				}
			} else
				last_sent_packet_time = now;
		}
	}

  void OnAck(const AckDatagram &ack_dgr, const SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER> &packet_seq_gen) {
    double syn_interval = static_cast<double>(p_connection_info_->syn_interval());
    double rtt = static_cast<double>(p_connection_info_->rtt().count());
    double packet_data_size = static_cast<double>(p_connection_info_->packet_data_size());
    double estimated_link_capacity = p_connection_info_->estimated_link_capacity();
    double packet_arrival_speed = p_connection_info_->packet_arrival_speed();

    PacketSequenceNumber ack_number(GetSequenceNumber(ack_dgr.payload().max_packet_sequence_number()));

		auto const dist(packet_seq_gen.distance(last_ack_number_.load(), ack_number));
		if (dist <= 0)
			return;
		last_ack_number_ = ack_number;

    TimePoint current_time = Clock::now();

		::std::cerr << "before slow starting phase " << slow_start_phase_ << ::std::endl;
    //if (boost::chrono::duration_cast<boost::chrono::microseconds>(current_time - last_update_).count() < p_connection_info_->syn_interval()) {
    if (!slow_start_phase_ && boost::chrono::duration_cast<boost::chrono::microseconds>(current_time - last_update_).count() < rtt) {
      return;
    }

    last_update_ = current_time;

    if (slow_start_phase_.load()) {
			// todo -- a bit of a fudge (firstly w.r.t. max(2 tmp) and also about 'dist' -- original code used 'dist + 1' essentially
			auto const tmp(window_flow_size_.load() + (static_cast<double>(dist)));
      window_flow_size_ = ::std::min(static_cast<double>(connected_protocol::cache::ConnectionInfo::min_window_size), tmp);
		::std::cerr << "on ack slow phase max window size " << max_window_size_ << ", current size " << window_flow_size_ << ::std::endl;
      if (window_flow_size_.load() > max_window_size_ >> 2) {
        slow_start_phase_ = false;
        if (packet_arrival_speed > 0) {
          last_packet_sending_period = packet_sending_period = (1000000.0 / packet_arrival_speed);
        } else {
          //sending_period_ = ((rtt + syn_interval) / window_flow_size_);
          last_packet_sending_period = packet_sending_period = (rtt + syn_interval) / max_window_size_;
        }
				::std::cerr << "sending period in slow start set to " << sending_period_ << ::std::endl;
				assert(!::std::signbit(sending_period_));
      }
    } else {
      UpdateWindowFlowSize();
    }

    if (slow_start_phase_.load()) {
      //p_connection_info_->set_window_flow_size(window_flow_size_.load());
      //p_connection_info_->set_sending_period(sending_period_.load());

      return;
    }

    if (loss_phase_.load()) {
      loss_phase_ = false;
      return;
    }

    double min_inc = 0.01;
		if (packet_data_size > 0)
			min_inc = 1. / Protocol::MTU;
    double inc(0.0);

#if 0

#if 1
		// another hack
		if (packet_arrival_speed > 0) {
			if (packet_arrival_speed > estimated_link_capacity)
				estimated_link_capacity = packet_arrival_speed;
		}
#endif

    double B = estimated_link_capacity - (1000000.0 / (sending_period_.load()));
    if ((sending_period_.load() > last_dec_sending_period_.load()) &&
        ((estimated_link_capacity / 9) < B)) {
      B = estimated_link_capacity / 9;
    }
    if (B <= 0) {
      inc = min_inc;
    } else {
      inc = pow(10.0, ceil(log10(B * packet_data_size * 8.0))) * 0.0000015 /
            packet_data_size;
      if (inc < min_inc) {
        inc = min_inc;
      }
    }
		::std::cerr << "a sending rate " << sending_period_ << ::std::endl;
    sending_period_ = (sending_period_.load() * syn_interval) /
                      (sending_period_.load() * inc + syn_interval);
#endif

#if 1
		// another hack
		if (packet_arrival_speed > 0) {

			double const arrival_period(1000000.0 / packet_arrival_speed);

			if (last_packet_sending_period > arrival_period * .875)
				sending_period_ = ::std::max<double>(sending_period_ * .875, Protocol::MIN_OUTPUT_PERIOD);
			else if (last_packet_sending_period < arrival_period * 1.125)
				sending_period_ = ::std::min(sending_period_ * 1.125, arrival_period * 3);

			last_packet_sending_period = packet_sending_period.load();

			::std::cerr << "b sending rate " << sending_period_ << ", arrival rate " << arrival_period << ", actual sending rate " << last_packet_sending_period << ::std::endl;
		}
#endif


    //p_connection_info_->set_sending_period(sending_period_.load());
  }

	void 
	OnHack(uint32_t packet_arrival_speed)
	{

		auto const now(Clock::now());
    double rtt = static_cast<double>(p_connection_info_->rtt().count());
		if (boost::chrono::duration_cast<boost::chrono::microseconds>(now - last_update_).count() < rtt) {
			return;
		}
		last_update_ = now;

		auto arrival_period = (1000000.0 / packet_arrival_speed);
		if (last_packet_sending_period > arrival_period * .875)
			sending_period_ = ::std::max<double>(sending_period_ * .875, Protocol::MIN_OUTPUT_PERIOD);
		else if (last_packet_sending_period < arrival_period * 1.125)
			sending_period_ = ::std::min(sending_period_ * 1.125, arrival_period * 3);

	::std::cerr << "d sending rate " << sending_period_ << ", arrival rate " << arrival_period << ", actual sending rate " << last_packet_sending_period << ::std::endl;

		last_packet_sending_period = packet_sending_period.load();
	}

  void 
	OnLoss(const NAckDatagram &nack_dgr, const connected_protocol::SequenceGenerator<Protocol::MAX_PACKET_SEQUENCE_NUMBER> &seq_gen) {
    auto const loss_list(nack_dgr.payload().GetLossPackets());
		assert(loss_list.empty() == false);
    PacketSequenceNumber first_loss_list_seq = GetSequenceNumber(loss_list[0]);
    PacketSequenceNumber last_loss_list_seq = GetSequenceNumber(loss_list.back());

    double syn_interval = static_cast<double>(p_connection_info_->syn_interval());
    double rtt = static_cast<double>(p_connection_info_->rtt().count());


		//::std::cerr << "on loss last ack before parket arvw spped " << ::std::endl;
#if 1
			auto const dist(seq_gen.distance(last_ack_number_, last_loss_list_seq));
			if (dist <= 0)
				return;

			double packet_arrival_speed = p_connection_info_->packet_arrival_speed();
			double const packet_arrival_speed_(ntohl(nack_dgr.header().additional_info()));
			if (packet_arrival_speed_ > 0)
				packet_arrival_speed = packet_arrival_speed_;

      if (packet_arrival_speed > 0) {

				auto arrival_period = (1000000.0 / packet_arrival_speed);

				if (slow_start_phase_) {
					slow_start_phase_ = false;
					last_packet_sending_period = packet_sending_period = arrival_period;
				}

				//::std::cerr << "on loss last ack: " << last_ack_number_ << ", loss: " << last_loss_list_seq << ::std::endl;

				auto const now(Clock::now());
				if (boost::chrono::duration_cast<boost::chrono::microseconds>(now - last_update_).count() < rtt) {
					return;
				}
				last_update_ = now;

			if (last_packet_sending_period > arrival_period * .875)
				sending_period_ = ::std::max<double>(sending_period_ * .875, Protocol::MIN_OUTPUT_PERIOD);
			else if (last_packet_sending_period < arrival_period * 1.125)
				sending_period_ = ::std::min(sending_period_ * 1.125, arrival_period * 3);

		::std::cerr << "c sending rate " << sending_period_ << ", arrival rate " << arrival_period << ", actual sending rate " << last_packet_sending_period << ::std::endl;

				last_packet_sending_period = packet_sending_period.load();

      } else {
				if (slow_start_phase_) {
					slow_start_phase_ = false;
				}
			}
			return;
#endif

    if (slow_start_phase_.load()) {
      slow_start_phase_ = false;

      if (packet_arrival_speed > 0) {
        sending_period_ = (1000000.0 / packet_arrival_speed);
			::std::cerr << "on loss is slow start, received packet spee " << sending_period_ << ::std::endl;
        return;
      }
			if (window_flow_size_ == 0 || window_flow_size_ > max_window_size_)
				sending_period_ = (rtt + syn_interval) / max_window_size_;
			else
				sending_period_ = (rtt + syn_interval) / window_flow_size_;
			::std::cerr << "on loss is slow start, inferred packet spee " << sending_period_ << ::std::endl;
    }

    loss_phase_ = true;

    if (seq_gen.distance(last_dec_seq_num_.load(), first_loss_list_seq) > 0) {
      last_dec_sending_period_ = sending_period_.load();
      sending_period_ = sending_period_.load() * 1.125;

      avg_nack_num_ = static_cast<uint32_t>(
          ceil(avg_nack_num_.load() * 0.875 + nack_count_.load() * 0.125));
      nack_count_ = 1;
      dec_count_ = 1;
      last_dec_seq_num_ = last_send_seq_num_.load();
      srand(last_dec_seq_num_.load());
      dec_random_ = static_cast<uint32_t>(ceil(
          avg_nack_num_.load() * (static_cast<double>(rand()) / RAND_MAX)));
      if (dec_random_ < 1) {
        dec_random_ = 1;
      }
    } else {
      nack_count_ = nack_count_.load() + 1;
      if (dec_count_.load() < 5 &&
          0 == (nack_count_.load() % dec_random_.load())) {
        sending_period_ = sending_period_.load() * 1.125;
        last_dec_seq_num_ = last_send_seq_num_.load();
      }
      dec_count_ = dec_count_.load() + 1;
    }
		::std::cerr << "c sending rate " << sending_period_ << ::std::endl;
  }

  void OnPacketReceived(const DataDatagram &) {}

  void OnTimeout() {}

  void OnClose() {}

  void UpdateLastSendSeqNum(PacketSequenceNumber last_send_seq_num) {
    last_send_seq_num_ = last_send_seq_num;
  }

  void UpdateWindowFlowSize() {
    double syn_interval =
        static_cast<double>(p_connection_info_->syn_interval());
    double rtt = static_cast<double>(p_connection_info_->rtt().count());
    double packet_arrival_speed = p_connection_info_->packet_arrival_speed();

		assert(!::std::signbit(syn_interval));
		assert(!::std::signbit(rtt));
		assert(!::std::signbit(packet_arrival_speed));

    window_flow_size_ = (packet_arrival_speed / 1000000.0) * (rtt + syn_interval) + connected_protocol::cache::ConnectionInfo::min_window_size;

    //p_connection_info_->set_window_flow_size(window_flow_size_.load());
  }

  boost::chrono::nanoseconds sending_period() const {
    return boost::chrono::nanoseconds(
        static_cast<long long>(ceil(sending_period_.load() * 1000)));
  }

  uint32_t window_flow_size() const {
    return static_cast<uint32_t>(ceil(window_flow_size_.load()));
  }

 private:
  PacketSequenceNumber GetSequenceNumber(PacketSequenceNumber seq_num) {
    return seq_num & 0x7FFFFFFF;
  }

 private:
  ConnectionInfo *p_connection_info_;
  std::atomic<double> window_flow_size_;
  uint32_t max_window_size_;
  // in nanosec
  std::atomic<double> sending_period_;
  std::atomic<bool> slow_start_phase_;
  std::atomic<bool> loss_phase_;
  std::atomic<PacketSequenceNumber> last_ack_number_;
  std::atomic<uint32_t> avg_nack_num_;
  std::atomic<uint32_t> nack_count_;
  std::atomic<uint32_t> dec_count_;
  std::atomic<PacketSequenceNumber> last_dec_seq_num_;
  // in nanosec
  std::atomic<double> last_dec_sending_period_;
  std::atomic<PacketSequenceNumber> last_send_seq_num_;
  std::atomic<uint32_t> dec_random_;
  TimePoint last_update_;

  TimePoint last_sent_packet_time;
};

}  // congestion
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_CONGESTION_CONGESTION_CONTROL_H_
