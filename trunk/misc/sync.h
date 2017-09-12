#ifdef __WIN32__
#include <Windows.h>
#endif

#ifndef DATA_PROCESSORS_MISC_SYNC_H
#define DATA_PROCESSORS_MISC_SYNC_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc { 

struct mutex {
#ifndef SYNAPSE_SINGLE_THREADED
#if 0
	::std::atomic_flag l = ATOMIC_FLAG_INIT;
	void
	lock()
	{
		while (l.test_and_set(::std::memory_order_acquire));
	}
	void
	unlock()
	{
		l.clear(::std::memory_order_release);
	}
#endif
#ifdef __WIN32__
	::CRITICAL_SECTION cs;
	mutex(mutex &) = delete;
	mutex(mutex &&) = delete;
	mutex & operator= (mutex &) = delete;
	mutex & operator= (mutex &&) = delete;
	mutex()
	{
	#ifndef NDEBUG
		if (!::InitializeCriticalSectionAndSpinCount(&cs, static_cast<DWORD>(1u) << 14))
			throw ::std::runtime_error("InitializeCriticalSectionAndSpinCount");
	#else
		if (!::InitializeCriticalSectionEx(&cs, static_cast<DWORD>(1u) << 14, CRITICAL_SECTION_NO_DEBUG_INFO))
			throw ::std::runtime_error("InitializeCriticalSectionEx");
	#endif
	}
	void
	lock() noexcept
	{
		::EnterCriticalSection(&cs);
		// No need to add fences. Compiler cannot assume that such fence is not called in the opaque function (EnterCriticalSection), and therefore cannot reorder instructions around it...
		// ::std::atomic_thread_fence(::std::memory_order_acquire);
	}
	void
	unlock() noexcept
	{
		// ::std::atomic_thread_fence(::std::memory_order_release);
		::LeaveCriticalSection(&cs);
	}
#else
#error not yet on non-windows platforms
#endif
#else
	void lock() noexcept { }
	void unlock() noexcept { }
#endif
};

#ifdef __WIN32__
typedef mutex recursive_mutex;
#endif


#if 0
#ifdef __WIN32__
	not yet...
		instead try
		BOOL WINAPI InitializeCriticalSectionAndSpinCount(
				  _Out_  LPCRITICAL_SECTION lpCriticalSection,
					  _In_   DWORD dwSpinCount
				);
	as it allows atomically do a spinlock before waiting...

class rw_mutex
{
	//return (0 != TryAcquireSRWLockExclusive(&m_handle));

	SRWLOCK m;

	rw_mutex(rw_mutex const &) = delete;
	rw_mutex(rw_mutex &&) = delete;
	rw_mutex& operator = (rw_mutex const&) = delete;
	rw_mutex& operator = (rw_mutex &&) = delete;

public:
	rw_mutex()
	{
		::InitializeSRWLock(&m);
	}
	~rw_mutex()
	{
	}
	void 
	lock_shared()
	{
		::AcquireSRWLockShared(&m);
	}
	void 
	unlock_shared()
	{
		::ReleaseSRWLockShared(&m);
	}
	void 
	lock()
	{
		::AcquireSRWLockExclusive(&m);
	}
	void 
	unlock()
	{
		::ReleaseSRWLockExclusive(&m);
	}
};
#else
#error not yet...

// may just default to mutex (or exclusive locks) -- depending on comparative performance
class rw_mutex
{
	::pthread_rwlock_t m;

	rw_mutex(rw_mutex const &) = delete;
	rw_mutex(rw_mutex &&) = delete;
	rw_mutex& operator = (rw_mutex const&) = delete;
	rw_mutex& operator = (rw_mutex &&) = delete;

public:
	rw_mutex()
	: ::pthread_rwlock_init(&m, NULL) {
	}
	~rw_mutex()
	{
		::pthread_rwlock_destroy(&m);
	}
	void 
	lock_shared()
	{
		::pthread_rwlock_rdlock(&m);
	}
	void 
	unlock_shared()
	{
		::pthread_rwlock_unlock(&m);
	}
	void 
	lock()
	{
		::pthread_rwlock_wrlock(&m);
	}
	void 
	unlock()
	{
		::pthread_rwlock_unlock(&m);
	}
};
#endif
#endif

// a quick atomics hack -- because so far ther's no atomic load/store on generic, non-atomic data in the c++ standard...
#if defined(__GNUC__) || defined(__clang__)
template <typename T>
T static
atomic_load_relaxed(T const *x)
{
	return __atomic_load_n(x, __ATOMIC_RELAXED);
}
template <typename T>
void static
atomic_store_relaxed(T *x, T const & val)
{
	__atomic_store_n(x, val, __ATOMIC_RELAXED);
}

template <typename T>
T static
atomic_exchange_relaxed(T *x, T const & val)
{
	return __atomic_exchange_n (x, val, __ATOMIC_RELAXED);
}

template <typename T>
T static
atomic_fetch_or_relaxed(T * x, T const & val)
{
	return __atomic_fetch_or(x, val, __ATOMIC_RELAXED);
}

template <typename T>
void static
atomic_store_release(T *x, T const & val)
{
	__atomic_store_n(x, val, __ATOMIC_RELEASE);
}

template <typename T>
T static
atomic_exchange_release(T *x, T const & val)
{
	return __atomic_exchange_n (x, val, __ATOMIC_RELEASE);
}

template <typename T>
T static
atomic_fetch_or_release(T * x, T const & val)
{
	return __atomic_fetch_or(x, val, __ATOMIC_RELEASE);
}

template <typename T>
T static
atomic_load_sequentially_consistent(T const *x)
{
	return __atomic_load_n(x, __ATOMIC_SEQ_CST);
}

#elif defined(_MSC_VER)

#pragma message ("Better NOT to use Visual Studio at all...")

char unsigned static
atomic_load_relaxed(char unsigned const * x)
{
	// "no fence" version is in windows 8 and above only, visual c++ is really suboptimal... GCC ought to be better.
	return ::InterlockedOr8(reinterpret_cast<char *>(const_cast<char unsigned *>(x)), 0);
}
uint64_t static
atomic_load_relaxed(uint64_t const * x)
{
	return ::InterlockedOr64NoFence(reinterpret_cast<int64_t *>(const_cast<uint64_t *>(x)), 0);
}

void static
atomic_store_relaxed(char unsigned * x, char unsigned const & val)
{
	// "no fence" version is in windows 8 and above only, visual c++ is really suboptimal... GCC ought to be better.
	::InterlockedExchange8(reinterpret_cast<char *>(x), val);
}

char unsigned static
atomic_fetch_or_relaxed(char unsigned * x, char unsigned const & val)
{
	// "no fence" version is in windows 8 and above only, visual c++ is really suboptimal... GCC ought to be better.
	return ::InterlockedOr8(reinterpret_cast<char *>(x), val);
}

void static
atomic_store_release(char unsigned * x, char unsigned const & val)
{
	// "no fence" version is in windows 8 and above only, visual c++ is really suboptimal... GCC ought to be better.
	::InterlockedExchange8(reinterpret_cast<char *>(x), val);
}

char unsigned static
atomic_fetch_or_release(char unsigned * x, char unsigned const & val)
{
	// "no fence" version is in windows 8 and above only, visual c++ is really suboptimal... GCC ought to be better.
	return ::InterlockedOr8(reinterpret_cast<char *>(x), val);
}

char unsigned static
atomic_load_sequentially_consistent(char unsigned const * x)
{
	// "no fence" version is in windows 8 and above only, visual c++ is really suboptimal... GCC ought to be better.
	return ::InterlockedOr8(reinterpret_cast<char *>(const_cast<char unsigned *>(x)), 0);
}
uint64_t static
atomic_load_sequentially_consistent(uint64_t const * x)
{
	return ::InterlockedOr64(reinterpret_cast<int64_t *>(const_cast<uint64_t *>(x)), 0);
}


#else
#error not yet -- need to implement atomic load/store on generic/non-atomic memory addersses on your compiler/system...
#endif

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
