#include <iostream>
#include <random>
#include <chrono>
#include <atomic>
#include <thread>

#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

#include "../amqp_0_9_1/foreign/copernica/endian.h"

#ifndef DATA_PROCESSORS_SYNAPSE_MISC_H
#define DATA_PROCESSORS_SYNAPSE_MISC_H

#include "sync.h"

#if defined(__GNUC__) || defined (__clang__)
#define DP_BUILTIN_ASSUME_ALIGNED(expression, aligned_size) __builtin_assume_aligned(expression, aligned_size)
#define DP_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define DP_BUILTIN_ASSUME_ALIGNED(expression, aligned_size) (__assume(reinterpret_cast<uintptr_t>(expression) % aligned_size == 0), (expression))
#define DP_RESTRICT __restrict
#else
#error note yet, implement non-GCC version of code please, or fallback to unassuming(=possibly slower) code:
#define DP_BUILTIN_ASSUME_ALIGNED(expression, aligned_size) expression
#endif


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { 
	
struct Size_alarm_error : ::std::runtime_error { using ::std::runtime_error::runtime_error; };

namespace synapse { namespace misc {

void static inline Nag_User_Style_1() {

	::std::string User_Response;

	::std::cout << "ARE YOU SUPER DUPER SURE? Anything but 'Yes' will be interpreted as a negative." << ::std::endl;
	if (!::std::getline(::std::cin, User_Response) || User_Response != "Yes")
		throw ::std::runtime_error("Could not get a positive response.");

	::std::cout << "\nREALLY? Anything but 'Yes!' will be interpreted as a negative." << ::std::endl;
	if (!::std::getline(::std::cin, User_Response) || User_Response != "Yes!")
		throw ::std::runtime_error("Could not get a positive response.");

	::std::default_random_engine Random_Engine(::std::chrono::system_clock::now().time_since_epoch().count());
	::std::uniform_int_distribution<int> Distribution(1000, 10000);
	auto const Pin(::std::to_string(Distribution(Random_Engine)));
	::std::cout << "\nWell... ok then... just to be sure though...\nenter the following pin: " + Pin << ::std::endl;
	if (!::std::getline(::std::cin, User_Response) || User_Response != Pin)
		throw ::std::runtime_error("Pins dont match");
}

bool static inline CLI_Option_Parse_Host_Port(char const * const CLI_Name, int & i, char * argv[], ::std::string & host, ::std::string & port) {
	if (!::strcmp(argv[i], CLI_Name)) {
		char const * tmp(::strtok(argv[++i], ":"));
		if (tmp == nullptr)
			throw ::std::runtime_error("--synapse_at value(=" + ::std::string(argv[i]) + ") but need 'host:port'");
		else 
			host = tmp;
		tmp = ::strtok(nullptr, ":");
		if (tmp == nullptr)
			throw ::std::runtime_error("--synapse_at value(=" + ::std::string(argv[i]) + ") but need 'host:port'");
		else
			port = tmp;
		return true;
	} else 
		return false;
}

struct Host_Port_Result_Type {
	bool OK;
	::std::string Host;
	::std::string Port;
	operator bool() const {
		return OK;
	}
	bool operator !() const {
		return !OK;
	}
};

Host_Port_Result_Type static inline CLI_Option_Parse_Host_Port(char const * const CLI_Name, int & i, char * argv[]) {
	::std::string Host, Port;
	return {CLI_Option_Parse_Host_Port(CLI_Name, i, argv, Host, Port), Host, Port};
}

#ifdef __WIN32__
void static inline Set_Thread_Processor_Affinity(uint64_t const Mask) {
	uint64_t Unused;
	uint64_t System_Mask;
	if (!::GetProcessAffinityMask(::GetCurrentProcess(), &Unused, &System_Mask))
		throw ::std::runtime_error("GetProcessAffinityMask failed");
	if (~System_Mask & Mask)
		throw ::std::runtime_error("Supplied Mask is conflicting with system-allowed affinity (must be a subset)");
	if (!::SetThreadAffinityMask(::GetCurrentThread(), Mask))
		throw ::std::runtime_error("SetThreadAffinityMask failed");
}
#endif

template <typename Return_type>
Return_type static Parse_string_as_suffixed_integral_size(::std::string String) {
	static_assert(::std::is_unsigned<Return_type>::value == true, "Parse_string_as_suffixed_integral_size Return_type must be unsigend");
	static_assert(::std::numeric_limits<Return_type>::max() >= static_cast<uint32_t>(1u) << 30, "Return_type in Parse_string_as_suffixed_integral_size is too small");
	static_assert(sizeof(Return_type) * 8 > 20, "Parse_string_as_suffixed_integral_size undefined behavior shift size is greater or equal to the bitwidth destination.");
	assert(String.empty() == false);
	::boost::trim(String);
	auto && Last_character(String.back());
	Return_type Multiplier(1u);
	switch (::toupper(Last_character)) {
		case 'G' :
			Multiplier <<= 10;
		case 'M' :
			Multiplier <<= 20;
			String.pop_back();
	}
	auto const User_supplied_value(::boost::lexical_cast<Return_type>(String));
	if (User_supplied_value > ::std::numeric_limits<Return_type>::max() / Multiplier)
		throw ::std::runtime_error("Parse_string_as_suffixed_integral_size user supplied value is too large");
	return  User_supplied_value * Multiplier;
}

template <typename Return_type>
Return_type static Parse_string_as_suffixed_integral_duration(::std::string String) {
	static_assert(::std::is_unsigned<Return_type>::value == true, "Parse_string_as_suffixed_integral_microseconds_duration Return_type must be unsigend");
	static_assert(::std::numeric_limits<Return_type>::max() >= 24ull * 60u * 60u * 1000000u, "Return_type in Parse_string_as_suffixed_integral_microseconds_duration is too small");
	static_assert(sizeof(Return_type) * 8 > 20, "Parse_string_as_suffixed_integral_microseconds_duration undefined behavior shift size is greater or equal to the bitwidth destination.");
	assert(String.empty() == false);
	::boost::trim(String);
	auto && Last_character(String.back());
	Return_type Multiplier(1u);
	switch (::toupper(Last_character)) {
		case 'D' :
			Multiplier *= 24u;
		case 'H' :
			Multiplier *= 60u;
		case 'M' :
			Multiplier *= 60u;
		case 'S' :
			Multiplier *= 1000000u;
			String.pop_back();
	}
	auto const User_supplied_value(::boost::lexical_cast<Return_type>(String));
	if (User_supplied_value > ::std::numeric_limits<Return_type>::max() / Multiplier)
		throw ::std::runtime_error("Parse_string_as_suffixed_integral_microseconds_duration user supplied value is too large");
	return  User_supplied_value * Multiplier;
}


#ifndef NDEBUG
struct Scoped_single_thread_test {
	::std::atomic_size_t & x;
	char const * Assertion_source_filename;
	int Assertion_source_linenumber;
	Scoped_single_thread_test(::std::atomic_size_t & x, char const * Assertion_source_filename, int const Assertion_source_linenumber) : x(x), Assertion_source_filename(Assertion_source_filename), Assertion_source_linenumber(Assertion_source_linenumber) {
		#ifndef SYNAPSE_SINGLE_THREADED
		auto const This_thread_id(::std::this_thread::get_id());
		::std::hash<::std::thread::id> Thread_id_hasher;
		auto const This_thread_id_hash(Thread_id_hasher(This_thread_id));
		auto const Previos_access_thread_id_hash(x.exchange(This_thread_id_hash, ::std::memory_order_relaxed));
		if (Previos_access_thread_id_hash != 0 && Previos_access_thread_id_hash != This_thread_id_hash) {
			::std::cerr << ::std::string("Scoped_single_thread_test CTOR ASSERTION FAILIER file: ") + Assertion_source_filename +  " line: " + ::std::to_string(Assertion_source_linenumber) + " self " + ::std::to_string(This_thread_id_hash) + " current " + ::std::to_string(Previos_access_thread_id_hash) + '\n';
			::std::cerr.flush();
			assert(0);
		}
		#endif
	}
	~Scoped_single_thread_test() {
		#ifndef SYNAPSE_SINGLE_THREADED
		auto const This_thread_id(::std::this_thread::get_id());
		::std::hash<::std::thread::id> Thread_id_hasher;
		auto const This_thread_id_hash(Thread_id_hasher(This_thread_id));
		auto const Previos_access_thread_id_hash(x.exchange(0, ::std::memory_order_relaxed));
		if (Previos_access_thread_id_hash != 0 && Previos_access_thread_id_hash != This_thread_id_hash) {
			::std::cerr << ::std::string("Scoped_single_thread_test DTOR ASSERTION FAILIER file: ") + Assertion_source_filename +  " line: " + ::std::to_string(Assertion_source_linenumber) + " self " + ::std::to_string(This_thread_id_hash) + " current " + ::std::to_string(Previos_access_thread_id_hash) + '\n';
			::std::cerr.flush();
			assert(0);
		}
		#endif
	}
};

template <typename Deriving>
struct Test_new_delete {
	static void * operator new (::std::size_t sz) {
		return ::std::memset(::operator new(sz), 0xff, sz);
	}
	~Test_new_delete() {
		::std::memset(static_cast<Deriving*>(this), 0xff, sizeof(Deriving));
	}
};

#endif


template <typename Type>
Type constexpr static inline
is_po2(Type const value)
{
	static_assert(::std::is_unsigned<Type>::value == true, "is_po2 must called with values of unsigned type");
	assert(value);
	return value & value - 1 ? false : true;
}

template <typename Type>
Type constexpr static inline
po2_mask(Type const value)
{
	return !is_po2(value) ? throw ::std::runtime_error("not power of 2 value supplied") : ~(value - 1);
}

template <typename uint_type>
uint_type constexpr static inline
round_up_to_multiple_of_po2(uint_type const x, uint_type const Po2Size)
{
	return (x + (Po2Size - 1)) & misc::po2_mask<uint_type>(Po2Size);
}

template <typename uint_type>
uint_type constexpr static inline
round_down_to_multiple_of_po2(uint_type const x, uint_type const Po2Size)
{
	return x & misc::po2_mask<uint_type>(Po2Size);
}

template <typename uint_type>
bool constexpr static inline
is_multiple_of_po2(uint_type const x, uint_type const Po2Size)
{
	return !(x & (Po2Size - 1));
}

// Most significant bit index utilities.
#if defined(__GNUC__) || defined (__clang__)
inline uint_fast8_t static Get_most_significant_bit_index(uint16_t Value) {
	assert(Value);
	return sizeof(unsigned int) * 8 - 1 - __builtin_clz(Value);
}
inline uint_fast8_t static Get_most_significant_bit_index(uint32_t Value) {
	assert(Value);
	return sizeof(unsigned long) * 8 - 1 - __builtin_clzl(Value);
}
inline uint_fast8_t static Get_most_significant_bit_index(uint64_t Value) {
	assert(Value);
	return sizeof(unsigned long long) * 8 - 1 - __builtin_clzll(Value);
}
inline uint_fast8_t static Get_least_significant_bit_index(uint16_t Value) {
	assert(Value);
	return __builtin_ctz(Value);
}
inline uint_fast8_t static Get_least_significant_bit_index(uint32_t Value) {
	assert(Value);
	return __builtin_ctzl(Value);
}
inline uint_fast8_t static Get_least_significant_bit_index(uint64_t Value) {
	assert(Value);
	return __builtin_ctzll(Value);
}
inline uint_fast8_t static Get_Set_Bits_Size(uint64_t Value) {
	return __builtin_popcountll(Value);
}
#elif defined(_MSC_VER)
inline uint_fast8_t static Get_most_significant_bit_index(uint32_t Value) {
	assert(Value);
	unsigned long Msb;
	_BitScanReverse(&Msb, Value);
	return Msb;
}
inline uint_fast8_t static Get_most_significant_bit_index(uint64_t Value) {
	assert(Value);
	unsigned long Msb;
	_BitScanReverse64(&Msb, Value);
	return Msb;
}
inline uint_fast8_t static Get_least_significant_bit_index(uint32_t Value) {
	assert(Value);
	unsigned long Msb;
	_BitScanForward(&Msb, Value);
	return Msb;
}
inline uint_fast8_t static Get_least_significant_bit_index(uint64_t Value) {
	assert(Value);
	unsigned long Msb;
	_BitScanForward64(&Msb, Value);
	return Msb;
}
inline uint_fast8_t static Get_Set_Bits_Size(uint64_t Value) {
	return __popcnt64(Value);
}
#else
#error "Nope, not yet implemented."
#endif

// CRC32 utilities.
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__) && defined(__SSE4_2__)
///\note probably uses bit-reflection, so the boost version (below) has extra params (true) in template args...
uint64_t static inline
crc32c(uint64_t const & a, uint64_t const & b)
{
	return __builtin_ia32_crc32di(a, b);
}

unsigned static inline
crc32c(unsigned const & a, unsigned const & b)
{
	return __builtin_ia32_crc32si(a, b);
}

unsigned static inline
crc32c(unsigned const & a, unsigned char & b)
{
	return __builtin_ia32_crc32qi(a, b);
}

#elif defined(_MSC_VER)
uint64_t static inline
crc32c(uint64_t const & a, uint64_t const & b)
{
	return _mm_crc32_u64(a, b);
}

unsigned static inline
crc32c(unsigned const & a, unsigned const & b)
{
	return _mm_crc32_u32(a, b);
}

unsigned static inline
crc32c(unsigned const & a, unsigned char & b)
{
	return _mm_crc32_u8(a, b);
}

#else
#error not yet, implement hardware-specific optimization or fallback like ::boost::crc_optimal<32, 0x1EDC6F41, 0, 0, true, true> crc
#endif

// Strict-aliasing safety. TODO -- to be revisited later on with greater elegance etc.
#if defined(__GNUC__) || defined(__clang__)
	template <typename T>
	T const & 
	Get_alias_safe_value(void const * Memory) 
	{
		assert(!(reinterpret_cast<uintptr_t>(Memory) % sizeof(T)));
		T const * __attribute__((__may_alias__)) Rv = static_cast<T const *>(DP_BUILTIN_ASSUME_ALIGNED(Memory, sizeof(T)));
		return *Rv;
	}
	template <typename T>
	T const 
	Atomic_load_alias_safe_value(void const * Memory) 
	{
		assert(!(reinterpret_cast<uintptr_t>(Memory) % sizeof(T)));
		T const * __attribute__((__may_alias__)) Rv = static_cast<T const *>(DP_BUILTIN_ASSUME_ALIGNED(Memory, sizeof(T)));
		return misc::atomic_load_relaxed(Rv);
	}
#elif defined(_MSC_VER)
#pragma message ("Issue of Visual C++ and strict-aliasing is a mess... Better NOT to use Visual Studio at all...")
	template <typename T>
	T 
	Get_alias_safe_value(void const * Memory) 
	{
		assert(!(reinterpret_cast<uintptr_t>(Memory) % sizeof(T)));
		T Rv; 
		T * DP_RESTRICT Destination(&Rv);
		T const * DP_RESTRICT Source(static_cast<T const *>(DP_BUILTIN_ASSUME_ALIGNED(Memory, sizeof(T))));
		::memcpy(Destination, Source, sizeof(T));
		return Rv;
	}
#pragma optimize("", off)
	template <typename T>
	T const 
	Atomic_load_alias_safe_value(void const * Memory) 
	{
		assert(!(reinterpret_cast<uintptr_t>(Memory) % sizeof(T)));
		T const * DP_RESTRICT Source(static_cast<T const *>(DP_BUILTIN_ASSUME_ALIGNED(Memory, sizeof(T))));
		return misc::atomic_load_relaxed(Source);
	}
#pragma optimize("", on)
#endif

// Byte (re)ordering utils.
#if DP_ENDIANNESS == LITTLE
uint16_t static inline
be16toh_from_possibly_misaligned_source(void const * const x)
{
	static_assert(alignof(uint16_t) == 2, "alignof uint16_t");
	if (reinterpret_cast<uintptr_t>(x) & 0x1) {
		uint16_t rv;
		auto const y(reinterpret_cast<char *>(&rv));
		auto const x_(static_cast<char const *>(x));
		y[0] = x_[1];
		y[1] = x_[0];
		return rv;
	} else
		return be16toh(misc::Get_alias_safe_value<uint16_t>(x));
}

uint32_t static inline
be32toh_from_possibly_misaligned_source(void const * const x)
{
	static_assert(alignof(uint32_t) == 4, "alignof uint32_t");
	if (reinterpret_cast<uintptr_t>(x) & 0x3) {
		uint32_t rv;
		auto const y(reinterpret_cast<char *>(&rv));
		auto const x_(static_cast<char const *>(x));
		y[0] = x_[3];
		y[1] = x_[2];
		y[2] = x_[1];
		y[3] = x_[0];
		return rv;
	} else
		return be32toh(misc::Get_alias_safe_value<uint32_t>(x));
}

uint64_t static inline
be64toh_from_possibly_misaligned_source(void const * const x)
{
	static_assert(alignof(uint64_t) == 8, "alignof uint64_t");
	if (reinterpret_cast<uintptr_t>(x) & 0x7) {
		uint64_t rv;
		auto const y(reinterpret_cast<char *>(&rv));
		auto const x_(static_cast<char const *>(x));
		y[0] = x_[7];
		y[1] = x_[6];
		y[2] = x_[5];
		y[3] = x_[4];
		y[4] = x_[3];
		y[5] = x_[2];
		y[6] = x_[1];
		y[7] = x_[0];
		return rv;
	} else
		return be64toh(misc::Get_alias_safe_value<uint64_t>(x));
}
#else
#error not yet, implement byte reversal for possibly misaligned sources on your h/w endian orientation
#endif

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
