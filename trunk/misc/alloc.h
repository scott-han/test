#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include <psapi.h>

#include <cstddef>
#include <atomic>
#include <boost/pool/pool.hpp>
#include <boost/integer/static_log2.hpp>

#include "misc.h"

#ifndef DATA_PROCESSORS_MISC_ALLOC_H
#define DATA_PROCESSORS_MISC_ALLOC_H

#ifdef __MINGW64__
#include <ntdef.h>
#include <ntstatus.h>
#else
#include <Windows.h>
typedef LONG NTSTATUS;
#define STATUS_PRIVILEGE_NOT_HELD ((NTSTATUS)0xC0000061)
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022)
#define NT_SUCCESS(Status)              (((NTSTATUS)(Status)) >= 0)
#endif

#ifdef __WIN32__
#if defined(__MINGW64__)
extern "C" {
long __stdcall ZwSetSystemInformation(int, void*, unsigned long);
}
#else 
NTSTATUS static (WINAPI * ZwSetSystemInformation) (INT, PVOID, ULONG) (nullptr);
bool static Load_ntdll() {
	HMODULE Ntdll(::LoadLibrary("ntdll.dll"));
	if (Ntdll == nullptr) {
		::data_processors::synapse::misc::log("Absolutely cannot continue. LoadLibrary on ntdll.dll", true);
		exit(1);
	}
	ZwSetSystemInformation = (NTSTATUS (WINAPI *)(INT, PVOID, ULONG))::GetProcAddress(Ntdll, "ZwSetSystemInformation");
	if (ZwSetSystemInformation == nullptr) {
		::data_processors::synapse::misc::log("Absolutely cannot continue. GetProcAddress on ZwSetSystemInformation", true);
		exit(1);
	}
	return true;
}
bool const static Ntdll_loaded(Load_ntdll());
#endif
#endif

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace misc { namespace alloc {

struct Memory_alarm_breach_reaction_throw {};
struct Memory_alarm_breach_reaction_no_throw {};
template <typename Unsigned>
class Scoped_contributor_to_virtual_allocated_memory_size {
	// Note -- important for it not to subtract itself if over the limit, let the calling code do that.
	bool static Contribute_to_virtual_allocated_memory_size(Unsigned Contribution_amount) noexcept {
		if (alloc::Virtual_allocated_memory_size.fetch_add(Contribution_amount, ::std::memory_order_relaxed) 
			#if defined(DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_CHANNEL_1) || defined(DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_STREAM_1) || defined(DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_1)
				+ Contribution_amount
			#endif
			< alloc::Memory_alarm_threshold
		)
			return true;
		else 
			return false;
	}
	Unsigned Accumulated_contribution_size{0};
public:

	Scoped_contributor_to_virtual_allocated_memory_size() = default;
	Scoped_contributor_to_virtual_allocated_memory_size(Unsigned Initial_contribution_size, Memory_alarm_breach_reaction_no_throw) : Accumulated_contribution_size(Initial_contribution_size) {
		Contribute_to_virtual_allocated_memory_size(Initial_contribution_size);
	}
	Scoped_contributor_to_virtual_allocated_memory_size(Unsigned Initial_contribution_size, Memory_alarm_breach_reaction_throw, char const * Exception_text) : Accumulated_contribution_size(Initial_contribution_size) {
		if (!Contribute_to_virtual_allocated_memory_size(Initial_contribution_size)) {
			assert(alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) >= Initial_contribution_size);
			alloc::Virtual_allocated_memory_size.fetch_sub(Initial_contribution_size, ::std::memory_order_relaxed);
			throw data_processors::Size_alarm_error(Exception_text);
		}
	}
	~Scoped_contributor_to_virtual_allocated_memory_size() {
		if (Accumulated_contribution_size) {
			assert(alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) >= Accumulated_contribution_size);
			alloc::Virtual_allocated_memory_size.fetch_sub(Accumulated_contribution_size, ::std::memory_order_relaxed);
		}
	}
	void Forget() {
		Accumulated_contribution_size = 0;
	}
	void Increment_contribution_size(Unsigned const Additional_contribution_size, char const * Exception_text) {
		assert(Additional_contribution_size);
		assert(::std::numeric_limits<decltype(alloc::Virtual_allocated_memory_size.load())>::max() - alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) >= Additional_contribution_size );
		if (Contribute_to_virtual_allocated_memory_size(Additional_contribution_size))
			Accumulated_contribution_size += Additional_contribution_size;
		else {
			assert(alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) >= Additional_contribution_size);
			alloc::Virtual_allocated_memory_size.fetch_sub(Additional_contribution_size, ::std::memory_order_relaxed);
			throw data_processors::Size_alarm_error(Exception_text);
		} 
	}
	void Decrement_contribution_size(Unsigned const Amount) {
		assert(Amount);
		assert(Accumulated_contribution_size >= Amount);
		Accumulated_contribution_size -= Amount;
	}
};

template <unsigned Pools_size, unsigned Starting_chunk_size>
unsigned static Select_pool(unsigned Bytes_size) {
	assert(Bytes_size);
	auto Msb(synapse::misc::Get_most_significant_bit_index(Bytes_size));
	assert(Msb < sizeof(unsigned) * 8);
	if (1u << Msb < Bytes_size)
		++Msb;
	Msb -= ::std::min<unsigned>(::boost::static_log2<Starting_chunk_size>::value, Msb);
	return Msb;
}

#ifndef NDEBUG
struct Debug_boost_pool : ::boost::pool<> {
	typedef ::boost::pool<> Base_type;
	using Base_type::Base_type;
	void * malloc() {
		return ::std::memset(Base_type::malloc(), 0xff, get_requested_size());
	}
	void * ordered_malloc(size_type N) {
		return ::std::memset(Base_type::ordered_malloc(N), 0xff, N * get_requested_size());
	}
	void free(void * const X) {
		::std::memset(X, 0xff, get_requested_size());
		Base_type::free(X);
	}
	void free(void * const X, size_type const N) {
		::std::memset(X, 0xff, N * get_requested_size());
		Base_type::free(X, N);
	}
};
#endif

template <unsigned Pools_size = 8, unsigned Starting_chunk_size = 16>
struct basic_segregated_pools_1 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
: synapse::misc::Test_new_delete<basic_segregated_pools_1<Pools_size, Starting_chunk_size>>
#endif
{
	static_assert(::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1 < sizeof(unsigned) * 8, "Undefined bitshift amount");
	unsigned constexpr static Max_chunk_size{1u << ::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1};

#ifndef NDEBUG
	::std::atomic_size_t Singleton_test{0};
	typedef Debug_boost_pool Pool_type;
#else
	typedef ::boost::pool<> Pool_type;
#endif

	template <typename T>
	struct pool_selector {
		template <unsigned ChunkSize, unsigned Index, bool WithinLimits = (Index < Pools_size), bool End = (sizeof(T) <= ChunkSize)>
		struct impl {
			typedef typename impl<ChunkSize + ChunkSize, Index + 1>::info info;
		};
		template <unsigned ChunkSize, unsigned Index>
		struct impl<ChunkSize, Index, true, true> {
			struct info {
				unsigned constexpr static
				index()
				{
					return Index;
				}
				unsigned constexpr static
				chunk_size()
				{
					return ChunkSize;
				}
			};
		};
		template <unsigned ChunkSize, unsigned Index, bool End>
		struct impl<ChunkSize, Index, false, End> {
		};
		typedef typename impl<Starting_chunk_size, 0>::info info;
	};
	template <unsigned ChunkSize, unsigned Index, bool WithinLimits = (Index < Pools_size)>
	struct pool_initializer
	{
		void static
		init(char * pools)
		{
			new (pools + Index * sizeof(Pool_type)) Pool_type(ChunkSize);
			pool_initializer<ChunkSize + ChunkSize, Index + 1>::init(pools);
		}
		void static
		deinit(char * pools)
		{
			reinterpret_cast<Pool_type*>(pools + Index * sizeof(Pool_type))->Pool_type::~Pool_type();
			pool_initializer<ChunkSize + ChunkSize, Index + 1>::deinit(pools);
		}
	};
	template <unsigned ChunkSize, unsigned Index>
	struct pool_initializer<ChunkSize, Index, false>
	{
		void static init(void *) { }
		void static deinit(void *) { }
	};
	typename ::std::aligned_storage<sizeof(Pool_type) * Pools_size>::type pools;
	basic_segregated_pools_1()
	{
		pool_initializer<Starting_chunk_size, 0>::init(reinterpret_cast<char*>(&pools));
	}
	~basic_segregated_pools_1()
	{
		pool_initializer<Starting_chunk_size, 0>::deinit(reinterpret_cast<char*>(&pools));
	}
	Pool_type & 
	operator[] (unsigned i) 
	{
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		return reinterpret_cast<Pool_type &>(reinterpret_cast<char *>(&pools)[i * sizeof(Pool_type)]);
	}

/*
	Note.
	It may be desirable to deprecate this 'make_shared' because generally anything like this is only of use if/when allocation is frequent enough... but if so, then ::boost::shared_ptr itself will cause heap allocation. However it is not that simple because of weak_ptr retaining the hold on the control-block and so the scope of the overall 'basic_pools' object cannot go out of scope too early (e.g. when other threads may be using the said weak_ptr).
	It is still better than 2 separate allocations but may not be hugely better in comparison to something like ::boost::alloc_shared with heap allocator. Albeit trying to find free space on the heap for smaller chunks of memory will be lesser affected by memory fragmentation (i.e. more candidates will be available) so the code is still retained here.
	The mechanism for longer-surviving pool allocators could well be to keep track of how much was still not-freed by the time containing object (e.g. connection) is being  deleted... and then pass the object lifescope to the more persistent container (like topic, which controls the ranges that may still keep weak_ptr refereces to the control block)...
	... this, however, is a future todo due to the development-time constrains.
	In the future (when architecturally refactoring for more resiliently-surviving pool allocation object) may try instead use a different alloc (e.g. Basic_alloc_2) which will (at a price of not using compile-time pool_selector<T>::info::index() but rather a few instructions such as hardware bitscanning, etc.) pick the sub-pool where only one allocation will need to take place... and without any need for 'ordered_malloc'. One would have to watch out for weak_ptr usage however.
	The basic_alloc_1 is still of use (compile-time selection of subpool), but mainly for cases where non-vector/non-ordered-malloc allocations may be needed (e.g. may be like a ::std::list).

*/
	template<typename T, typename... Args>
	::boost::shared_ptr<T> make_shared(Args&&... args) {
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		return ::boost::shared_ptr<T>(New_and_construct<T>(::std::forward<Args>(args)...), [this](T * p) {
			assert(p != nullptr);
			Destruct_and_free(p);
		});
	}

	template<typename T, typename... Args>
	::std::unique_ptr<T, ::std::function<void(T *)>> Make_unique(Args&&... args) {
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		return ::std::unique_ptr<T, ::std::function<void(T *)>>(New_and_construct<T>(::std::forward<Args>(args)...), [this](T * p) {
			assert(p != nullptr);
			Destruct_and_free(p);
		});
	}

	template<typename T, typename... Args>
	T * New_and_construct(Args&&... args) {
		return new ((*this)[basic_segregated_pools_1::pool_selector<T>::info::index()].malloc()) T(::std::forward<Args>(args)...);
	}

	template<typename T>
	void Destruct_and_free(T * p) {
		assert(p != nullptr);
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		p->~T();
		(*this)[basic_segregated_pools_1::pool_selector<T>::info::index()].free(p, 
			synapse::misc::round_up_to_multiple_of_po2<unsigned>(sizeof(T), basic_segregated_pools_1::pool_selector<T>::info::chunk_size()) / basic_segregated_pools_1::pool_selector<T>::info::chunk_size()
		);
	}

	::std::unique_ptr<char unsigned, ::std::function<void(char unsigned *)>> Make_unique_buffer(unsigned Bytes_size) {
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		assert(Bytes_size);
		auto const Selected_pool_index(Select_pool<Pools_size, Starting_chunk_size>(Bytes_size));
		char unsigned * Allocated;
		if (Selected_pool_index < Pools_size) { // most frequent casses
			assert((*this)[Selected_pool_index].get_requested_size() >= Bytes_size);
			Allocated = static_cast<char unsigned*>((*this)[Selected_pool_index].malloc());
		} else { // rare cases... if user supplies appropriate template parameters
			// TODO try taking this out (should be available in class scope already?)
			static_assert(::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1 < sizeof(unsigned) * 8, "Undefined bitshift amount");
			unsigned constexpr static Max_chunk_size(1u << (::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1));
			assert((*this)[Pools_size - 1].get_requested_size() == Max_chunk_size);
			Allocated = static_cast<char unsigned*>((*this)[Pools_size - 1].ordered_malloc(
				synapse::misc::round_up_to_multiple_of_po2<unsigned>(Bytes_size, Max_chunk_size) / Max_chunk_size
			));
		}
		return ::std::unique_ptr<char unsigned, ::std::function<void(char unsigned *)>>(Allocated, 
			[this, Selected_pool_index, Bytes_size](char unsigned * p) {
				assert(p != nullptr);
#ifndef NDEBUG
				synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
				if (Selected_pool_index < Pools_size) { // most frequent casses
					assert((*this)[Selected_pool_index].get_requested_size() >= Bytes_size);
					(*this)[Selected_pool_index].free(p);
				} else { // rare cases... if user supplies appropriate template parameters
					// TODO try taking this out (should be available in class scope already?)
					static_assert(::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1 < sizeof(unsigned) * 8, "Undefined bitshift amount");
					unsigned constexpr static Max_chunk_size(1u << (::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1));
					assert((*this)[Pools_size - 1].get_requested_size() == Max_chunk_size);
					(*this)[Pools_size - 1].free(p, 
						synapse::misc::round_up_to_multiple_of_po2<unsigned>(Bytes_size, Max_chunk_size) / Max_chunk_size
					);
				}
			}
		);
	}

};

// STL-compatible, mainly good for non-reserve/non-batch allocations (uses ordered_malloc, yet is able to leverage compile-time selection of sub-pool). 
template <typename T, unsigned Pools_size = 8, unsigned Starting_chunk_size = 16>
struct basic_alloc_1 {

	typedef basic_segregated_pools_1<Pools_size, Starting_chunk_size> Pools_type;
	unsigned constexpr static Chunk_size{Pools_type::template pool_selector<T>::info::chunk_size()};
	unsigned constexpr static Pool_index{Pools_type::template pool_selector<T>::info::index()};

	Pools_type & sp;

	typedef T value_type;

	basic_alloc_1(Pools_type & sp) noexcept 
	: sp(sp) {
	}

	template <typename U>
	basic_alloc_1(basic_alloc_1<U, Pools_size, Starting_chunk_size> const & other) noexcept
	: sp(other.sp) {
	}

	T * allocate(::std::size_t n) {
		static_assert(synapse::misc::is_po2(Chunk_size), "chunk_size in pool allocator must always be power of two");
		assert(sp[Pool_index].get_requested_size() >= Chunk_size);
		if (n == 1)
			return static_cast<T*>(sp[Pool_index].malloc());
		else 
			return static_cast<T*>(sp[Pool_index].ordered_malloc(synapse::misc::round_up_to_multiple_of_po2<unsigned>(sizeof(T) * n, Chunk_size) / Chunk_size));
	}

	void deallocate(T * p, ::std::size_t n) {
		assert(sp[Pool_index].get_requested_size() >= Chunk_size);
		if (p != nullptr)
			sp[Pool_index].free(p, synapse::misc::round_up_to_multiple_of_po2<unsigned>(sizeof(T) * n, Chunk_size) / Chunk_size);
	}

	template <class U>
	struct rebind { using other = basic_alloc_1<U, Pools_size, Starting_chunk_size>; };
};
template <typename T, typename U, unsigned Pools_size, unsigned Starting_chunk_size>
bool 
operator == (basic_alloc_1<T, Pools_size, Starting_chunk_size> const &, basic_alloc_1<U, Pools_size, Starting_chunk_size> const &)
{
	return true;
}
template <typename T, typename U, unsigned Pools_size, unsigned Starting_chunk_size>
bool 
operator != (basic_alloc_1<T, Pools_size, Starting_chunk_size> const &, basic_alloc_1<U, Pools_size, Starting_chunk_size> const &)
{
	return false;
}

// STL-compatible, mainly good for alloc_shared calls as well as containers that use reservation/batch-preallocation (does not use ordered_malloc upto 2^(log2(Starting_chunk_size) + Pools_size - 1) ish... but also uses runtime determination when selecting which sub-pool to use during both allocation and deallocation). 
// Note: do watch out if using weak_ptr with this one (via say allocate_shared et.al.) because control block may still need to be in scope long after the pointed-to-user-object is semantically out of scope.
template <typename T, unsigned Pools_size = 8, unsigned Starting_chunk_size = 16>
struct Basic_alloc_2 {

	typedef basic_segregated_pools_1<Pools_size, Starting_chunk_size> Pools_type;
	Pools_type & sp;

	typedef T value_type;

	Basic_alloc_2(Pools_type & sp) noexcept 
	: sp(sp) {
	}

	template <typename U>
	Basic_alloc_2(Basic_alloc_2<U, Pools_size, Starting_chunk_size> const & other) noexcept
	: sp(other.sp) {
	}

	T * allocate(::std::size_t n) {
		auto const Bytes_size(n * sizeof(T));
		auto const Selected_pool_index(Select_pool<Pools_size, Starting_chunk_size>(Bytes_size));
		if (Selected_pool_index < Pools_size) { // most frequent casses
			assert(sp[Selected_pool_index].get_requested_size() >= Bytes_size);
			return static_cast<T*>(sp[Selected_pool_index].malloc());
		} else { // rare cases... if user supplies appropriate template parameters
			// TODO try taking this out (should be available in class scope already?)
			static_assert(::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1 < sizeof(unsigned) * 8, "Undefined bitshift amount");
			unsigned constexpr static Max_chunk_size(1u << (::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1));
			assert(sp[Pools_size - 1].get_requested_size() == Max_chunk_size);
			return static_cast<T*>(sp[Pools_size - 1].ordered_malloc(
				synapse::misc::round_up_to_multiple_of_po2<unsigned>(Bytes_size, Max_chunk_size) / Max_chunk_size
			));
		}
	}

	void deallocate(T * p, ::std::size_t n) {
		if (p != nullptr) {
			auto const Bytes_size(n * sizeof(T));
			auto const Selected_pool_index(Select_pool<Pools_size, Starting_chunk_size>(Bytes_size));
			if (Selected_pool_index < Pools_size) { // most frequent casses
				assert(sp[Selected_pool_index].get_requested_size() >= Bytes_size);
				sp[Selected_pool_index].free(p);
			} else { // rare cases... if user supplies appropriate template parameters
				// TODO try taking this out (should be available in class scope already?)
				static_assert(::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1 < sizeof(unsigned) * 8, "Undefined bitshift amount");
				unsigned constexpr static Max_chunk_size(1u << (::boost::static_log2<Starting_chunk_size>::value + Pools_size - 1));
				assert(sp[Pools_size - 1].get_requested_size() == Max_chunk_size);
				sp[Pools_size - 1].free(p, 
					synapse::misc::round_up_to_multiple_of_po2<unsigned>(Bytes_size, Max_chunk_size) / Max_chunk_size
				);
			}
		}
	}

	template <class U>
	struct rebind { using other = Basic_alloc_2<U, Pools_size, Starting_chunk_size>; };
};
template <typename T, typename U, unsigned Pools_size, unsigned Starting_chunk_size>
bool 
operator == (Basic_alloc_2<T, Pools_size, Starting_chunk_size> const &, Basic_alloc_2<U, Pools_size, Starting_chunk_size> const &)
{
	return true;
}
template <typename T, typename U, unsigned Pools_size, unsigned Starting_chunk_size>
bool 
operator != (Basic_alloc_2<T, Pools_size, Starting_chunk_size> const &, Basic_alloc_2<U, Pools_size, Starting_chunk_size> const &)
{
	return false;
}

// Windows large page memory PRE-allocation support.
// Expected to be used in a singleton(ish) fashion (i.e. will not try to deallocate pre-initialized block of memory).

// Todo: further testing on the limits of numeric capacity (i.e. against overflows).
template <unsigned Page_size, typename Unsigned = uint64_t>
struct Large_page_preallocator {

	typedef Unsigned Unsigned_type;

	Unsigned constexpr static Chunk_size{4 * 1024 * 1024};
	static_assert(synapse::misc::is_po2(Chunk_size), "Chunk_size must be power of two.");

	Unsigned constexpr static Maximum_index_allocation_size{Chunk_size * sizeof(Unsigned) * 8};
	static_assert(synapse::misc::is_po2(Maximum_index_allocation_size), "Maximum_index_allocation_size must be power of two.");

	struct 
	#ifndef SYNAPSE_SINGLE_THREADED
	alignas(data_processors::synapse::misc::Cache_line_size / 2) // false sharing vs. spacial efficiency/hotness
	#endif
	Cache_aligned_atomic_unsigned {
		::std::atomic<Unsigned> Value;
	};
	::std::unique_ptr<Cache_aligned_atomic_unsigned, ::std::function<void(Cache_aligned_atomic_unsigned*)>> Indicies;

	char unsigned * Block{nullptr};

	Unsigned Block_size;

	Unsigned Indicies_size
#ifndef NDEBUG
	{0}
#endif
	;

private:

	SIZE_T static Initialize_large_page_size() {
		::SYSTEM_INFO sysinfo;
		::GetSystemInfo(&sysinfo);
		if (sysinfo.dwPageSize != Page_size)
			throw ::std::runtime_error("Page_size value ain't cool");
		SIZE_T const rv(::GetLargePageMinimum());
		static_assert(::std::is_unsigned<decltype(rv)>::value, "SIZE_T ought to be unsigned");
		if (!rv || rv < Page_size || rv & ~synapse::misc::po2_mask<decltype(rv)>(Page_size) || rv & rv - 1 || Chunk_size & ~synapse::misc::po2_mask<decltype(rv)>(rv)) // precedence rules of c++ standard make it right
			throw ::std::runtime_error("Unexpected large page value.");
		return rv;
	}
	SIZE_T const Large_page_size{Initialize_large_page_size()};

	char unsigned * Regular_virtual_alloc(unsigned const & Bytes_size, bool & Pages_locked) {
		assert(Pages_locked == false);
		char unsigned * Allocated(static_cast<char unsigned*>(::VirtualAlloc(NULL, Bytes_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)));
		if (Allocated == nullptr)
			throw ::std::runtime_error("VirtualAlloc err: " + ::std::to_string(::GetLastError()));
		if (data_processors::synapse::database::nice_performance_degradation < 11) {
			if (::VirtualLock(Allocated, Bytes_size)) 
				Pages_locked = true;
			else {
				synapse::misc::log("WARNING: initial 'VirtualLock' attempt failed. error " + ::std::to_string(::GetLastError()) + "\n", true);
				auto const Current_process_handle(::GetCurrentProcess());
				::PROCESS_MEMORY_COUNTERS Memory_counters;
				if (::GetProcessMemoryInfo(Current_process_handle, &Memory_counters, sizeof(Memory_counters))) {
					auto const Suggested_min(Memory_counters.PagefileUsage + 1024 * 1024); 
					auto Suggested_max(Memory_counters.PeakWorkingSetSize);
					if (Suggested_min > Suggested_max)
						Suggested_max = Suggested_min;
					if (::SetProcessWorkingSetSize(Current_process_handle, Suggested_min, Suggested_max)) { 
						if (::VirtualLock(Allocated, Bytes_size))
							Pages_locked = true;
						else
							synapse::misc::log("WARNING: final 'VirtualLock' attempt failed. error " + ::std::to_string(::GetLastError()) + " minimum size attemted: " + ::std::to_string(Suggested_min) + " maximum size attempted " + ::std::to_string(Suggested_max) + '\n', true);
					} else 
						synapse::misc::log("WARNING: subsequent to failed 'VirtualLock' attempt to call SetProcessWorkingSetSize failed. error " + ::std::to_string(::GetLastError()) + " minimum size attemted: " + ::std::to_string(Suggested_min) + " maximum size attempted " + ::std::to_string(Suggested_max) + '\n', true);
				} else 
					synapse::misc::log("WARNING: subsequent to failed 'VirtualLock' attempt to call GetProcessMemoryInfo failed. error " + ::std::to_string(::GetLastError()) + "\n", true);
			}
		}
		assert(!(reinterpret_cast<uintptr_t>(Allocated) % Page_size));
		return Allocated;
	}

	/**
	Performance degradation niceness:

	-50 Throw at any failed attempt

	-40 Try to allocate large pages by issuing clearance of working sets, etc. Throw if eventually failed to allocate large pages.

	-30 As per -40 only return nullptr without throwing.

	-20 As per -30 try to allocate regular pages. Throw if regular pages allocation fails.

	-1 Try to allocate large pages by issuing clearance of working sets, logging each failed attempt. Eventually allocate from regular space (non large pages).

	0 Same as -1 only dont log.

	10 Try to allocate large pages, if fails then do not try to clear working tests but simply allocate from regular pages.

	20 Dont try anything with large pages.

	*/

	char unsigned * Virtual_alloc(Unsigned & Bytes_size, int const Nice_performance_degradation, bool & Pages_locked) {
		assert(Pages_locked == false);
		Bytes_size = synapse::misc::round_up_to_multiple_of_po2<Unsigned>(Bytes_size, Large_page_size);
		alloc::Scoped_contributor_to_virtual_allocated_memory_size<Unsigned> Scoped_contributor_to_virtual_allocated_memory_size(Bytes_size,
			alloc::Memory_alarm_breach_reaction_throw(), "Memory_alarm_threshold in Virtual_alloc breached."
		);
		char unsigned * Allocated
		#ifndef NDEBUG
			(nullptr)
		#endif
		;
		if (Nice_performance_degradation < 11) {
			Allocated = static_cast<char unsigned*>(::VirtualAlloc(NULL, Bytes_size, MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (Allocated == nullptr) {
				if (Nice_performance_degradation < 1) {
					if (Nice_performance_degradation < 0)
						synapse::misc::log("WARNING: initial 'VirtualAlloc with MEM_LARGE_PAGES' attempt failed due to Windows not being able to find enough large-pages memory... will try to free-up some at the expense of other apps...\n", true);

					auto retry_allocating_large_memory_pages = [Nice_performance_degradation, &Bytes_size, &Allocated](NTSTATUS const status, char const * const retry_name, bool last_retry) noexcept {
						#pragma GCC diagnostic push
						#pragma GCC diagnostic ignored "-Wterminate"
						if (status == STATUS_PRIVILEGE_NOT_HELD || status == STATUS_ACCESS_DENIED) {
							::std::string msg("PERFORMANCE DEGRADATION!!! not enough privileges to clear memory lists and working sets, option " + ::std::string(retry_name));
							if (Nice_performance_degradation < -49)
								throw ::std::runtime_error(::std::move(msg));
							else if (Nice_performance_degradation < 0)
								synapse::misc::log("WARNING: " + ::std::move(msg) + '\n', true);
						} else if (!NT_SUCCESS(status)) {
							::std::string msg("PERFORMANCE DEGRADATION!!! error in clearing memory lists and working sets, option " + ::std::string(retry_name));
							if (Nice_performance_degradation < -49)
								throw ::std::runtime_error(::std::move(msg));
							else if (Nice_performance_degradation < 0)
								synapse::misc::log("WARNING: " + ::std::move(msg) + '\n', true);
						} else {
							// now try to allocate again... the prefromance mode
							Allocated = static_cast<char unsigned*>(::VirtualAlloc(NULL, Bytes_size, MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
							if (Allocated == nullptr) {
								::std::string msg("PERFORMANCE DEGRADATION!!!	could not VirtualAlloc with MEM_LARGE_PAGES, option " + ::std::string(retry_name));
								if (Nice_performance_degradation < -49 && last_retry == true)
									throw ::std::runtime_error(::std::move(msg) + " err: " + ::std::to_string(::GetLastError()));
								else if (Nice_performance_degradation < 0)
									synapse::misc::log("WARNING: " + ::std::move(msg)  + '\n', true);
							}
						}
						#pragma GCC diagnostic pop
					};

					::std::function<void()> large_memory_allocation_retries[4];

					large_memory_allocation_retries[0] = [&retry_allocating_large_memory_pages](){
						struct {
							ULONG_PTR current_size{0};
							ULONG_PTR peak_size{0};
							ULONG_PTR page_fault_count{0};
							ULONG_PTR minimum_working_set{static_cast<decltype(minimum_working_set)>(-1)};
							ULONG_PTR maximum_working_set{static_cast<decltype(minimum_working_set)>(-1)};
							ULONG_PTR transition_shared_pages{0};
							ULONG_PTR peak_transition_shared_pages{0};
							ULONG_PTR unused[2]{0};
						} system_cache_information;
						retry_allocating_large_memory_pages(::ZwSetSystemInformation(0x15, &system_cache_information, sizeof(system_cache_information)), "EmtpySystemCacheWorkingSet", false);
					};
					large_memory_allocation_retries[1] = [&retry_allocating_large_memory_pages](){
						DWORD cmd(2);
						// TODO -- later the synapse-s own code should use 'VirtualLock()' on existing non-data (i.e. code structures, etc.) code to prevent emty-working-ests affecting itself...
						// ... on the other hand, even own working-set pages can cause fragmentation of RAM and thunsly limit own ability to obtain MEM_LARGE_PAGES allocations... so may be emptying the working sets (inclusive of own)
						// is somewhat reasonable...
						retry_allocating_large_memory_pages(::ZwSetSystemInformation(80, &cmd, sizeof(DWORD)), "MemoryEmptyWorkingSets", false);
					};
					large_memory_allocation_retries[2] = [&retry_allocating_large_memory_pages](){
						DWORD cmd(5);
						retry_allocating_large_memory_pages(::ZwSetSystemInformation(80, &cmd, sizeof(DWORD)), "MemoryPurgeLowPriorityStandbyList", false);
					};
					large_memory_allocation_retries[3] = [&retry_allocating_large_memory_pages](){
						DWORD cmd(4);
						retry_allocating_large_memory_pages(::ZwSetSystemInformation(80, &cmd, sizeof(DWORD)), "MemoryPurgeStandbyList", true);
					};

					for (auto && attempt : large_memory_allocation_retries) {
						attempt();
						if (Allocated != nullptr)
							break;
					}
					if (Allocated == nullptr) {
						if (Nice_performance_degradation < -39)
							throw ::std::runtime_error("Could not allocate large memory pages.");
						else if (Nice_performance_degradation < -29)
							return nullptr;
						else {
							if (Nice_performance_degradation < 0)
								synapse::misc::log("WARNING: PERFORMANCE DEGRADATION!!!	COULD NOT VirtualAlloc with MEM_LARGE_PAGES, will fallback to regular-sized pages (NO MORE OPTIONS TO TRY)\n", true);
							Allocated = Regular_virtual_alloc(Bytes_size, Pages_locked);
						}
					}
#ifndef NDEBUG
					else assert(!(reinterpret_cast<uintptr_t>(Allocated) % Large_page_size));
#endif
				} else
					Allocated = Regular_virtual_alloc(Bytes_size, Pages_locked);
			}
#ifndef NDEBUG
			else assert(!(reinterpret_cast<uintptr_t>(Allocated) % Large_page_size));
#endif
		} else 
			Allocated = Regular_virtual_alloc(Bytes_size, Pages_locked);
		assert(!(reinterpret_cast<uintptr_t>(Allocated) % Page_size));
		assert(Allocated != nullptr);
		Scoped_contributor_to_virtual_allocated_memory_size.Forget();
		return Allocated;
	}
public:

	void Initialize(Unsigned Bytes_size = 0) {
		assert(Block == nullptr);
		assert(!Indicies);
		assert(!Indicies_size);

		struct multi_priveleges_type {
			DWORD               PrivilegeCount;
			LUID_AND_ATTRIBUTES Privileges[3];
		};
		multi_priveleges_type privs;
		privs.PrivilegeCount = 3;
		privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		privs.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;
		privs.Privileges[2].Attributes = SE_PRIVILEGE_ENABLED;

		HANDLE tok;
		if (
				!::LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &privs.Privileges[0].Luid) 
				|| !::LookupPrivilegeValue(NULL, SE_PROF_SINGLE_PROCESS_NAME, &privs.Privileges[1].Luid) 
				|| !::LookupPrivilegeValue(NULL, SE_INCREASE_QUOTA_NAME, &privs.Privileges[2].Luid) 
				|| !::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok) 
				|| !::AdjustTokenPrivileges(tok, FALSE, (PTOKEN_PRIVILEGES)&privs, 0, NULL, NULL) 
				|| ::GetLastError() != ERROR_SUCCESS
				|| !::CloseHandle(tok)
			 )
			if (data_processors::synapse::database::nice_performance_degradation < -39)
				throw ::std::runtime_error("getting SE_LOCK_MEMORY_NAME and SE_PROFILE_SINGLE_PROCESS_NAME (need for large memorp pages support feature)");
			else if (data_processors::synapse::database::nice_performance_degradation < 1)
				synapse::misc::log("WARNING: PERFORMANCE DEGRADATION!!!	could not activate SE_LOCK_MEMORY_NAME and SE_PROFILE_SINGLE_PROCESS_NAME\n", true);

		if (Bytes_size) {
			static_assert(Maximum_index_allocation_size > Chunk_size, "Maximum_index_allocation_size should be > Chunk_size");
			static_assert(!(Maximum_index_allocation_size % Chunk_size), "Maximum_index_allocation_size should be divisible by Chunk_size");
			Block_size = synapse::misc::round_up_to_multiple_of_po2<Unsigned>(Bytes_size, Chunk_size);
			bool Unused(false);
			Block = Virtual_alloc(Block_size, -30, Unused);
			if (Block != nullptr) {
				assert(!Unused);
				assert(!(reinterpret_cast<uintptr_t>(Block) % Large_page_size));
				Indicies_size = synapse::misc::round_up_to_multiple_of_po2<Unsigned>(Block_size, Maximum_index_allocation_size) / Maximum_index_allocation_size; // overrunning on purpose, thats OK -- reusing partial index bits at the tail of the allocation pool.
				// TODO consider keeping another counter for 'constructed'indicies size so that if any throw half-way, then  deallocator would be safe-enough to do this also... having said this the currently-managed types are not throwing...
				Indicies = decltype(Indicies)(static_cast<Cache_aligned_atomic_unsigned*>(
					#ifdef __WIN32__
						_aligned_malloc
					#else
						#error "Not yet -- aligned_alloc/free deployment"
					#endif
					(sizeof(Cache_aligned_atomic_unsigned) * Indicies_size, sizeof(Cache_aligned_atomic_unsigned))), 
					[this](Cache_aligned_atomic_unsigned * Memory) {
						for (Unsigned I(0); I != Indicies_size; ++I)
							Indicies.get()[I].~Cache_aligned_atomic_unsigned();
						#ifdef __WIN32__
							_aligned_free(Memory);
						#else
							#error "Not yet -- aligned_alloc/free deployment"
						#endif
					}
				);		
				for (unsigned i(0); i != Indicies_size; ++i) {
					new (Indicies.get() + i) Cache_aligned_atomic_unsigned;
					Indicies.get()[i].Value.store(0, ::std::memory_order_relaxed);
				}
			}
		}
	}

	char unsigned * Allocate(Unsigned & Bytes_size, Unsigned & Bytes_size_as_bitmask_in_index, bool & Pages_locked) {
#ifndef NDEBUG
		Bytes_size_as_bitmask_in_index = 0;
#endif
		Pages_locked = false;
		if (Bytes_size >= Large_page_size) {
			if (Block != nullptr && Bytes_size < Block_size && Bytes_size < Maximum_index_allocation_size) {
				auto const Chunk_aligned_bytes_size(synapse::misc::round_up_to_multiple_of_po2<Unsigned>(Bytes_size, Chunk_size));
				assert(Chunk_aligned_bytes_size <= Maximum_index_allocation_size);
				assert(Bytes_size <= Chunk_aligned_bytes_size);
				Unsigned const Bits_in_bitmask(Chunk_aligned_bytes_size >> ::boost::static_log2<Chunk_size>::value);
				assert(Bits_in_bitmask);
				assert(Bits_in_bitmask * Chunk_size == Chunk_aligned_bytes_size);
				assert(Bits_in_bitmask <= sizeof(Unsigned) * 8);

				Unsigned const Parked_bytes_size_as_bitmask_in_index(Bits_in_bitmask < sizeof(Unsigned) * 8 ? (static_cast<Unsigned>(1u) << Bits_in_bitmask) - 1 : -1);
				
				for (unsigned Retries(0); Retries != 3; ++Retries) {
					for (Unsigned i(0); i != Indicies_size; ++i) {
						auto && Index(Indicies.get()[i].Value);
						auto Index_value(Index.load(::std::memory_order_relaxed));
						if (Index_value == static_cast<Unsigned>(-1))
							continue;

						auto const Lsb(synapse::misc::Get_least_significant_bit_index(~Index_value));
						assert(Lsb < sizeof(Unsigned) * 8);
						Bytes_size_as_bitmask_in_index = Parked_bytes_size_as_bitmask_in_index << Lsb;
						assert(Bytes_size_as_bitmask_in_index);
						Unsigned const Index_offset(Maximum_index_allocation_size * i);
						for (Unsigned Within_index_offset(Lsb * Chunk_size); ; Within_index_offset += Chunk_size) {
							auto const Within_index_size_consumption(Within_index_offset + Chunk_aligned_bytes_size);
							if (Within_index_size_consumption > Maximum_index_allocation_size || Index_offset + Within_index_size_consumption > Block_size)
								break;
							assert(Within_index_offset < Maximum_index_allocation_size);
							assert(Within_index_size_consumption <= Maximum_index_allocation_size);
							assert(Bytes_size_as_bitmask_in_index);
							if (!(Bytes_size_as_bitmask_in_index & Index_value)) { 
								Index_value = Index.fetch_or(Bytes_size_as_bitmask_in_index, ::std::memory_order_relaxed);
								auto const Matching_mask(Bytes_size_as_bitmask_in_index & Index_value);
								if (!Matching_mask) {
									Bytes_size = Chunk_aligned_bytes_size;
									assert(Index_offset + Within_index_offset + Bytes_size <= Block_size);
									return Block + Index_offset + Within_index_offset;
								} else { // Must return my bits back to zero...
									auto const Tmp(~(Bytes_size_as_bitmask_in_index ^ Matching_mask));
									Index_value = Index.fetch_and(Tmp, ::std::memory_order_relaxed) & Tmp;
								}
							}
							Bytes_size_as_bitmask_in_index <<= 1;
						}
					}
				}
			}
#ifndef NDEBUG
			Bytes_size_as_bitmask_in_index = 0;
#endif
			// If could not get from preallocated block then try directly from RAM/system.
			return Virtual_alloc(Bytes_size, data_processors::synapse::database::nice_performance_degradation, Pages_locked);
		} else {
			assert(!(Bytes_size % Page_size));
			alloc::Scoped_contributor_to_virtual_allocated_memory_size<Unsigned> Scoped_contributor_to_virtual_allocated_memory_size(Bytes_size,
				alloc::Memory_alarm_breach_reaction_throw(), "Memory_alarm_threshold in Allocate breached."
			);
			auto Result(Regular_virtual_alloc(Bytes_size, Pages_locked));
			Scoped_contributor_to_virtual_allocated_memory_size.Forget();
			return Result;
		}
	}

	void Deallocate(char unsigned * Address, Unsigned const Bytes_size_as_bitmask_in_index, Unsigned const Bytes_size, bool const Pages_locked) noexcept {
		assert(Address != nullptr);
		if (Bytes_size >= Large_page_size) {
			// If allocated from block
			Unsigned Offset;
			if (Block != nullptr && Address >= Block && (Offset = Address - Block) < Block_size) {
				assert(Offset + Bytes_size <= Block_size);
				assert(Bytes_size_as_bitmask_in_index);
				assert(Offset >> ::boost::static_log2<Maximum_index_allocation_size>::value < Indicies_size);
				auto && Index(Indicies.get()[Offset >> ::boost::static_log2<Maximum_index_allocation_size>::value].Value);
#ifndef NDEBUG
				auto const Index_value = 
#endif
				Index.fetch_and(~Bytes_size_as_bitmask_in_index, ::std::memory_order_relaxed);
				assert((Bytes_size_as_bitmask_in_index & Index_value) == Bytes_size_as_bitmask_in_index);

				return;
			} 
		} 
		assert(!Bytes_size_as_bitmask_in_index);
		assert(Block == nullptr || Address < Block || Address >= Block + Block_size);
		if (Pages_locked)
			::VirtualUnlock(Address, Bytes_size);
		::VirtualFree(Address, 0, MEM_RELEASE);
		assert(alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) >= Bytes_size);
		alloc::Virtual_allocated_memory_size.fetch_sub(Bytes_size, ::std::memory_order_relaxed);
	}
};


}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
