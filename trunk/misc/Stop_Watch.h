#ifndef DATA_PROCESSORS_MISC_STOP_WATCH_H
#define DATA_PROCESSORS_MISC_STOP_WATCH_H

#include <Windows.h>
#include <stdint.h>
#include <stdexcept>
#include <limits>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc { 

// Todo -- may be make it more into a singleton type later on
class Stopwatch_Type {
	uint_fast64_t static Get_Performance_Counter_Frequency() {
		LARGE_INTEGER Return_Value;
		if (::QueryPerformanceFrequency(&Return_Value) && Return_Value.QuadPart > 0)
			return Return_Value.QuadPart;
		else
			throw ::std::runtime_error("QueryPerformanceFrequency");
	}
	uint_fast64_t const Frequency{Get_Performance_Counter_Frequency()};
public:
	uint_fast64_t static Ping() {
		LARGE_INTEGER Return_Value;
		if (::QueryPerformanceCounter(&Return_Value))
			return Return_Value.QuadPart;
		else
			throw ::std::runtime_error("QueryPerformanceCounter");
	}
	uint_fast64_t Get_Delta_As_Microseconds_With_Auto_Correction(uint_fast64_t const & Begin, uint_fast64_t const & End) const {
		if (End > Begin) {
			auto const Delta(End - Begin);
			if (::std::numeric_limits<uint_fast64_t>::max() >> 20 > Delta)
				return  Delta * 1000000 / Frequency;
			else
				throw ::std::runtime_error("Delta range is too great.");
		} else
			return 0;
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
