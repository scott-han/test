#ifndef UDT_CONNECTED_PROTOCOL_SEQUENCE_GENERATOR_H_
#define UDT_CONNECTED_PROTOCOL_SEQUENCE_GENERATOR_H_

#include <cstdint>
#include <cstdlib>

#include <chrono>

#include <boost/chrono.hpp>

#include <mutex>
#include "../../../../../../misc/sync.h"

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <uint32_t MaxValue> // todo -- static assert to be of (x + 1) == power of 2
class SequenceGenerator {
 public:
  using SeqNumber = uint32_t;

 public:
  SequenceGenerator()
	: mutex_(), current_(0) {
    boost::random::mt19937 gen(
			static_cast<SeqNumber>(
				::boost::chrono::duration_cast<boost::chrono::nanoseconds>(::boost::chrono::high_resolution_clock::now().time_since_epoch()).count()
			)
		);
    ::boost::random::uniform_int_distribution<uint32_t> dist(0, MaxValue);
    current_ = dist(gen);
  }

  SeqNumber Previous() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    current_ = Dec(current_);
    return current_;
  }

  SeqNumber Next() {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    current_ = Inc(current_);
    return current_;
  }

  void set_current(SeqNumber current) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
		current_ = current;
  }

  SeqNumber current() const {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    return current_;
  }

  SeqNumber static
	Inc(SeqNumber seq_num) {
		static_assert(::std::is_unsigned<SeqNumber>::value, "SeqNumber must be unsigned");
		return ++seq_num & MaxValue;
  }

  SeqNumber static
	Dec(SeqNumber seq_num) {
		static_assert(::std::is_unsigned<SeqNumber>::value, "SeqNumber must be unsigned");
		return --seq_num & MaxValue;
  }

	::std::make_signed<SeqNumber>::type static
	distance(SeqNumber a, SeqNumber b)
	{
		static_assert(::std::is_unsigned<SeqNumber>::value, "SeqNumber must be unsigned");
		// todo -- a quick hack for the time being
		static_assert(MaxValue >> 1 <= 1 + ::std::numeric_limits<SeqNumber>::max() - static_cast<SeqNumber>(::std::numeric_limits<::std::make_signed<SeqNumber>::type>::min()), "signed version of SeqNumber is of unexpected range, need to recompile with different templated consts etc.");
		auto const rv((b - a) & MaxValue);
		if (rv > MaxValue >> 1)
			return -((a - b) & MaxValue);
		else 
			return rv;
	}

#if 0
  /// @return positive if lhs > rhs, negative if lhs < rhs
  int Compare(SeqNumber lhs, SeqNumber rhs) const {
    return (static_cast<uint32_t>(std::abs(static_cast<int>(lhs - rhs))) <
            threshold_compare_)
               ? (lhs - rhs)
               : (rhs - lhs);
  }

  int32_t SeqLength(int32_t first, int32_t last) const {
    return (first <= last) ? (last - first + 1)
                           : (last - first + max_value_ + 2);
  }

  int32_t SeqOffset(int32_t first, int32_t last) const {
    if (std::abs(first - last) < static_cast<int32_t>(threshold_compare_)) {
      return last - first;
    }

    if (first < last) {
      return last - first - static_cast<int32_t>(max_value_) - 1;
    }

    return last - first + static_cast<int32_t>(max_value_) + 1;
  }
#endif

 private:
  mutable synapse::misc::mutex mutex_;
  SeqNumber current_;
};

}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_SEQUENCE_GENERATOR_H_
