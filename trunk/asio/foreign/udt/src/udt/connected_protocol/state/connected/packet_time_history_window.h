#ifndef UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_PACKET_TIME_HISTORY_WINDOW_H_
#define UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_PACKET_TIME_HISTORY_WINDOW_H_

#include <cstdint>

#include <algorithm>
#include <chrono>
#include <numeric>

#include <boost/circular_buffer.hpp>
#include <boost/chrono.hpp>
#include <mutex>
#include "../../../../../../../../misc/sync.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace state {
namespace connected {

class PacketTimeHistoryWindow {
 private:
  using TimePoint = boost::chrono::time_point<boost::chrono::high_resolution_clock>;
  using MicrosecUnit = int_least64_t;
  using CircularBuffer = boost::circular_buffer<MicrosecUnit>;

 public:
  PacketTimeHistoryWindow(uint32_t max_arrival_size = 16,
                          uint32_t max_probe_size = 64)
      : arrival_mutex_(),
        max_packet_arrival_speed_size_(max_arrival_size),
        //arrival_interval_history_(max_arrival_size),
        arrival_interval_history_(100),
        last_arrival_(TimePoint::min()),
        probe_mutex_(),
        max_probe_interval_size_(max_probe_size),
        probe_interval_history_(max_probe_size),
        first_probe_arrival_(boost::chrono::high_resolution_clock::now()) {}

  void Init(double packet_arrival_speed = 1000.0,
            double estimated_capacity = 1000.0) {
    MicrosecUnit packet_interval(
        static_cast<MicrosecUnit>(ceil(1000000 / packet_arrival_speed)));
		/*
    for (uint32_t i = 0; i < max_packet_arrival_speed_size_; ++i) {
      arrival_interval_history_.push_back(packet_interval);
    }
		*/
		arrival_interval_history_ = packet_interval;
    MicrosecUnit probe_interval(
        static_cast<MicrosecUnit>(ceil(1000000 / estimated_capacity)));
    for (uint32_t i = 0; i < max_probe_interval_size_; ++i) {
      probe_interval_history_.push_back(probe_interval);
    }
  }

	uint_fast64_t same_t_counter{0};
  void OnArrival() {
    ::std::lock_guard<synapse::misc::mutex> lock_arrival(arrival_mutex_);
    TimePoint arrival_time(
        boost::chrono::high_resolution_clock::now());
		if (last_arrival_ != TimePoint::min()) {
			MicrosecUnit delta(DeltaTime(arrival_time, last_arrival_));
			if (delta > 0) {
				//arrival_interval_history_.push_back(delta);
				arrival_interval_history_ = .999 * arrival_interval_history_ + .001 * (static_cast<double>(delta) / ++same_t_counter);
				same_t_counter = 0;
				last_arrival_ = arrival_time;
			} else {
				++same_t_counter;
			}
		} else
			last_arrival_ = arrival_time;
  }

  void OnFirstProbe() {
    ::std::lock_guard<synapse::misc::mutex> lock_probe(probe_mutex_);
    first_probe_arrival_ = boost::chrono::high_resolution_clock::now();
  }

  void OnSecondProbe() {
    ::std::lock_guard<synapse::misc::mutex> lock_probe(probe_mutex_);
    TimePoint arrival_time(
        boost::chrono::high_resolution_clock::now());

		auto const delta(DeltaTime(arrival_time, first_probe_arrival_));
		if (delta > 0)
			probe_interval_history_.push_back();
  }

  /// @return packets per second
  double GetPacketArrivalSpeed() {
		/*
    ::std::lock_guard<synapse::misc::mutex> lock_arrival(arrival_mutex_);
    // copy values
    std::vector<MicrosecUnit> sorted_values(arrival_interval_history_.begin(),
                                            arrival_interval_history_.end());

    std::sort(sorted_values.begin(), sorted_values.end());
    MicrosecUnit median(sorted_values[sorted_values.size() / 2]);
    MicrosecUnit low_value(median / 8);
    MicrosecUnit upper_value(median * 8);
    auto it = std::copy_if(
        sorted_values.begin(), sorted_values.end(), sorted_values.begin(),
        [low_value, upper_value](MicrosecUnit current_value) {
          return current_value > low_value && current_value < upper_value;
        });
    // todo change resize
    sorted_values.resize(std::distance(sorted_values.begin(), it));

    if (sorted_values.size() > 8) {
      double sum(
          std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0));
      return (1000000.0 * sorted_values.size()) / sum;
		} else {
			std::vector<MicrosecUnit> sorted_values(arrival_interval_history_.begin(), arrival_interval_history_.end());
			if (sorted_values.size() > 3) {
				double sum(std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0));
				return (1000000.0 * sorted_values.size()) / sum;
			} else 
				return 0;
    }
		*/
		double const tmp(arrival_interval_history_);
		if (tmp > 0)
			return 1000000.0 / tmp;
		else
			return 0;
  }

  /// @return packets per second
  double GetEstimatedLinkCapacity() {
    ::std::lock_guard<synapse::misc::mutex> lock_probe(probe_mutex_);
    // copy values
    std::vector<MicrosecUnit> sorted_values(probe_interval_history_.begin(),
                                            probe_interval_history_.end());

    std::sort(sorted_values.begin(), sorted_values.end());
    MicrosecUnit median(sorted_values[sorted_values.size() / 2]);
    MicrosecUnit low_value(median / 8);
    MicrosecUnit upper_value(median * 8);
    auto it = std::copy_if(
        sorted_values.begin(), sorted_values.end(), sorted_values.begin(),
        [low_value, upper_value](MicrosecUnit current_value) {
          return current_value > low_value && current_value < upper_value;
        });
    // todo change resize
    sorted_values.resize(std::distance(sorted_values.begin(), it));

    double sum(
        std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0));

    if (sum == 0) {
      return 0;
    }

    return (1000000.0 * sorted_values.size()) / sum;
  }

 private:
  MicrosecUnit DeltaTime(const TimePoint &t1, const TimePoint &t2) {
    return boost::chrono::duration_cast<boost::chrono::microseconds>(t1 - t2)
        .count();
  }

 private:
  // delta in micro seconds
  synapse::misc::mutex arrival_mutex_;
  uint32_t max_packet_arrival_speed_size_;
  //CircularBuffer arrival_interval_history_;
  ::std::atomic<double> arrival_interval_history_;
  TimePoint last_arrival_;
  // delta in micro seconds
  synapse::misc::mutex probe_mutex_;
  uint32_t max_probe_interval_size_;
  CircularBuffer probe_interval_history_;
  TimePoint first_probe_arrival_;
};

}  // connected
}  // state
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_STATE_CONNECTED_PACKET_TIME_HISTORY_WINDOW_H_
