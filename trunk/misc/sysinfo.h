#include <Windows.h>

#include <atomic>

#include "Cache_Info.h"

#ifndef DATA_PROCESSORS_MISC_SYSINFO_H
#define DATA_PROCESSORS_MISC_SYSINFO_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc { 

#ifdef __WIN32__
unsigned static inline
get_physical_cores_size()
{
	DWORD buf_len(0);
	DWORD rc(::GetLogicalProcessorInformation(NULL, &buf_len));
	if (rc == FALSE && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		::std::unique_ptr<SYSTEM_LOGICAL_PROCESSOR_INFORMATION[]> Info_array(new SYSTEM_LOGICAL_PROCESSOR_INFORMATION[buf_len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)]);
		rc = ::GetLogicalProcessorInformation(Info_array.get(), &buf_len);
		if (rc == TRUE && !(buf_len % sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION))) {
			unsigned Cores_Size(0);
			unsigned Numa_Nodes_Size(0);
			int i(-1);
			do {
				switch(Info_array[++i].Relationship) {
				case LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore:
					++Cores_Size;
					break;
				case LOGICAL_PROCESSOR_RELATIONSHIP::RelationNumaNode:
					++Numa_Nodes_Size;
					break;
				default:
					break;
				}
			} while (buf_len -= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
			auto const Total_Cores_Size(Cores_Size * Numa_Nodes_Size);
#ifndef NDEBUG
			synapse::misc::log("system cores: " + ::std::to_string(Total_Cores_Size) + "\n", true);
#endif
			return Total_Cores_Size;
		}
	}
	throw ::std::runtime_error("GetLogicalProcessorInformation");
}

uint_fast64_t static inline Get_system_memory_size() {
	::MEMORYSTATUSEX Rv;
	Rv.dwLength = sizeof(Rv);
	if (!::GlobalMemoryStatusEx(&Rv))
		throw ::std::runtime_error("GetPhysicallyInstalledSystemMemory, error: " + ::std::to_string(::GetLastError()));
	return Rv.ullTotalPhys;
}

#else
#error not yet
#endif

unsigned static inline
concurrency_hint() 
{
	return
#ifndef SYNAPSE_SINGLE_THREADED
			synapse::misc::get_physical_cores_size()
#else
			1
#endif
			;
}

::boost::posix_time::ptime const static epoch(::boost::posix_time::from_iso_string("19700101T000000"));

uint_fast64_t const static Ticks_To_Micros_Denominator(::boost::posix_time::time_duration::ticks_per_second() / 1000000);
template <typename Ticks_provider>
inline uint_fast64_t static Get_micros_from_duration(Ticks_provider && Duration) {
	assert(::boost::posix_time::time_duration::ticks_per_second() >= 1000000);
	return Duration.ticks() / Ticks_To_Micros_Denominator;
}


inline uint_fast64_t static Get_micros_since_epoch(::boost::posix_time::ptime const & Value
	#if !defined(__WIN32__) || defined(DATA_PROCESSORS_NO_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME)
		= ::boost::posix_time::microsec_clock::universal_time()
	#endif
) {
	assert(Value >= misc::epoch);
	return Get_micros_from_duration(Value - misc::epoch);
}

inline uint_fast64_t static Get_micros_since_epoch(::boost::gregorian::date const & Value) {
	return Get_micros_since_epoch(::boost::posix_time::ptime(Value));
}

inline uint_fast64_t static Get_micros_since_epoch(unsigned Year, unsigned Month, unsigned Day) {
	return Get_micros_since_epoch(::boost::gregorian::date(Year, Month, Day));
}

#if defined(__WIN32__) && !defined(DATA_PROCESSORS_NO_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME) 
// Some functions and static variables are explicitly not inside a function (because static function-internal variables incur atomic evaluation as well as branch performance hit).
// Normally it is ok, but with getting timestamp call, such could be done very frequently. TODO -- apply this to other time-based functions and non-timing code throughout.
inline FARPROC static Find_Get_System_Time_Precise_As_File_Time() {
	auto Library_Handle(::LoadLibrary("Kernel32.dll"));
	if (Library_Handle)
		return ::GetProcAddress(Library_Handle, "GetSystemTimePreciseAsFileTime");
	else
		return nullptr;
}
void static (WINAPI * Get_System_Time_Precise_As_File_Time) (LPFILETIME) ((void (WINAPI *) (LPFILETIME))Find_Get_System_Time_Precise_As_File_Time());

inline ::FILETIME static Get_Epoch_As_File_Time() {
	::SYSTEMTIME const Epoch_System_Time{1970, 1, 0, 1, 0, 0, 0, 0};
	::FILETIME Epoch_File_Time;
	// Using epoch (1970) via native Win32 SYSTEMTIME API as no presumption is made about whether value offsets from 1601 returned by GetSystemTimePreciseAsFileTime include leap seconds.
	if (!::SystemTimeToFileTime(&Epoch_System_Time, &Epoch_File_Time))
		throw ::std::runtime_error("SystemTimeToFileTime");
	return Epoch_File_Time;
}
::FILETIME const static Epoch_File_Time{Get_Epoch_As_File_Time()};

inline bool static Is_True_Microsecond_Clock_Present() {
	return misc::Get_System_Time_Precise_As_File_Time != nullptr;
}

inline uint_fast64_t static Get_micros_since_epoch() {
	if (misc::Get_System_Time_Precise_As_File_Time) {
		::FILETIME Now_File_Time;
		misc::Get_System_Time_Precise_As_File_Time(&Now_File_Time);
		return ((static_cast<uint_fast64_t>(Now_File_Time.dwHighDateTime) << 32 | Now_File_Time.dwLowDateTime) - (static_cast<uint_fast64_t>(Epoch_File_Time.dwHighDateTime) << 32 | Epoch_File_Time.dwLowDateTime)) / 10;
	} else { 
		return Get_micros_since_epoch(::boost::posix_time::microsec_clock::universal_time());
	}
}

#endif


// Todo: later move into separate header... allows for extern or static linkage by the user app
// Ideally should not be needed (could be put into the singleton pattern in cached_time class as static var) but MSVC (sigh) has issue with anonymous (unnamed) namespaces and precompiled headers... so will get multiple definitions error at link time (even if trying to build a single-translation unit app/whole-program target).
class cached_time;
static cached_time * cached_time_instance{nullptr};
///\about the idea is to prevent frequent calls to system-level mechanisms, yet offer parametric/program-wide timing resolution/access
class cached_time {

	::boost::asio::deadline_timer caching_timer;
	unsigned const microsecond_refresh_hint;
	::std::atomic_uint_fast64_t value{0};

	cached_time(unsigned const microsecond_refresh_hint)
	: caching_timer(synapse::asio::io_service::get()), microsecond_refresh_hint(microsecond_refresh_hint)
	{
		assert(value.is_lock_free() == true);
#ifndef NDEBUG
		uint_fast64_t constexpr cached_flag_bit(static_cast<uint_fast64_t>(1) << 62);
		assert(value < cached_flag_bit);
#endif
	}

	// using 2 bits for flag... still over 125 years time-span even if using nanosecond resolution, should be good enough...
	uint_fast64_t 
	get_() 
	{
		uint_fast64_t constexpr solo_flag_bit(static_cast<uint_fast64_t>(1) << 63);
		uint_fast64_t constexpr cached_flag_bit(static_cast<uint_fast64_t>(1) << 62);
		uint_fast64_t const Original_Value(value.load(::std::memory_order_relaxed));
		if (!(cached_flag_bit & Original_Value)) { // Not cached, must update.
			if (!(Original_Value & solo_flag_bit)) { // Opportunistic test to see if someone already has the solo right...
				uint_fast64_t const Previous_Value(value.fetch_or(solo_flag_bit, ::std::memory_order_relaxed));
				if (!(Previous_Value & solo_flag_bit)) { // Got the solo right to do the update.
					auto const Previous_Time_Value(~(solo_flag_bit | cached_flag_bit) & Previous_Value);
					::boost::posix_time::ptime const Now_UTC(::boost::posix_time::microsec_clock::universal_time());
					auto Now_Time_Value(misc::Get_micros_since_epoch(Now_UTC));
					assert(Now_Time_Value < cached_flag_bit);
					if (Now_Time_Value < Previous_Time_Value)
						Now_Time_Value = Previous_Time_Value;
					value.store(Now_Time_Value | (solo_flag_bit | cached_flag_bit), ::std::memory_order_relaxed);
					caching_timer.expires_at(Now_UTC + ::boost::posix_time::microseconds(microsecond_refresh_hint));
					caching_timer.async_wait([this](::boost::system::error_code const &){
						#ifdef _MSC_VER
							// msvc is not very good to say the least... will await proper clang-codegn support, so this is 'hack in the meantime'
							uint_fast64_t constexpr solo_flag_bit(static_cast<uint_fast64_t>(1) << 63);
							uint_fast64_t constexpr cached_flag_bit(static_cast<uint_fast64_t>(1) << 62);
						#endif
						value.fetch_and(~(solo_flag_bit | cached_flag_bit), ::std::memory_order_relaxed);
					});
					return Now_Time_Value;
				} 
			} 
				#ifdef SYNAPSE_SINGLE_THREADED
					assert(0); // Should not ever get to this point in single-threaded build.
				#endif
				while(!0) { // Spinlock.
					uint_fast64_t const Latest_Value(value.load(::std::memory_order_relaxed));
					if ((~solo_flag_bit & Latest_Value) != (~solo_flag_bit & Original_Value))
						return ~(solo_flag_bit | cached_flag_bit) & Latest_Value;
				}
		}
		// still cached, fast path...
		return ~(solo_flag_bit | cached_flag_bit) & Original_Value;
	}

public:

	void static
	init(unsigned const microsecond_refresh_hint) // in microseconds
	{
		assert(cached_time_instance == nullptr);
		cached_time_instance = new cached_time(microsecond_refresh_hint);
	}

	uint_fast64_t static
	get() // in microseconds
	{
		assert(cached_time_instance != nullptr);
		return cached_time_instance->get_();
	}

};



}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
