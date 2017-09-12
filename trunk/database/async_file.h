// a VERY ROUGH sketch at the moment... thinking out loud so to speak...

// an important note about profiling IOCP overlapped async file writing. the zero-filling effects are best tested-for when: 
// a) the file already exists
// b) a write is performed at the end of the file
// If the file is actually created during the 1st run then, even if call to SetFileValidData is ok, the preceeding pages/data are zero-shown -filled. This is not of concern to us however (i.e. even if so -- the trully async IO still appears to be in force) If in doubt, one can test-hack the underlying Asio lib to yield an error message/exception of sorts when the IOCP indicates synchronous behaviour (I have tested it through-out and it does not indicate falier of any sort)... so far so good :)

/**
	\note Minor blurb on endianness of the data, and for that matter all of the server's internal data standardisation...
	All of the data and memory layout used by the server is standardised to one layout only -- namely that of Little Endian formation (for performance reasons).
	This is only for internal data of course -- the AMQP messages are still supported with Big Endian orientation and the clients' payloads are treated as opaque so "anything goes" there.
	The server's own (meta)data is in Little Endian however -- this includes all of the index files and raw data files (e.g. non-AMQP envelope fields in the data file denoting user's payload length, index values, etc.)
	If, in future, server's architecture is to be moved to a different CPU endian layout, then the data files will first need to be converted to the target endian layout. This is done for performance reasons -- the very frequent "everyday" usage of the data is then free from any 'on the fly' conversion to-from any of the endianness issues, and if the move happes to a different endianness, then to prevent 'hidden surprises of "how come everything is running slower now"' the whole of the system is designed to "barf" due to endianness mismatch (subsequently leading to explicit "once-only" conversion/migration of data files being performed with something like perhaps some utility -- to be later written up).
	*/

#include <string>

#include <WinIoCtl.h>

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>

#include "../asio/io_service.h"
#include "../asio/async_handler_wrappers.h"
#include "../misc/log.h"
#include "../misc/misc.h"
//TODO move this endian file (it is pretty much re-written anyways)
#include "../amqp_0_9_1/foreign/copernica/endian.h"

#ifndef DATA_PROCESSORS_SYNAPSE_DATABASE_ASYNC_FILE_H
#define DATA_PROCESSORS_SYNAPSE_DATABASE_ASYNC_FILE_H


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

namespace basio = ::boost::asio;

// Note, this is essentially for faster-than-boost-filepath processing/appending to strings...
// Todo -- if in future this becomes irrelevant (e.g. performance-wise) then refactor to pass-around ::boost::filesystem::path & instead of ::std::string &
#ifdef __WIN32__
#define DATA_PROCESSORS_PATH_SEPARATOR "\\"
#else
#define DATA_PROCESSORS_PATH_SEPARATOR "/"
#endif

#ifdef __WIN32__

struct file_handle : private ::boost::noncopyable {
	HANDLE h;
#ifndef NDEBUG
	// Used in conjunction with CompareObjectHandles...but such is only supported from Windows 10.
	//HANDLE Duplicated_handle;
#endif

	file_handle(::std::string const & filepath, bool const Make_sparse)
	/**
	 * from https://support.microsoft.com/en-us/kb/156932 
	 * "The FILE_FLAG_NO_BUFFERING flag has the most effect on the behavior of the file system for asynchronous operation. This is the best way to guarantee that I/O requests are actually asynchronous. It instructs the file system to not use any cache mechanism at all."  
	 */
	: h(::CreateFile(
			filepath.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH,
			NULL)) {
		if (h == INVALID_HANDLE_VALUE)
			if (nice_performance_degradation < 0)
				throw ::std::runtime_error("CreateFile (=" + filepath + ')');
			else {
				synapse::misc::log("WARNING: PERFORMANCE DEGRADATION!!!	could not CreateFile (=" + filepath + ") with \"FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH\" mode\n", true);
				h = ::CreateFile(
					filepath.c_str(),
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ, 
					NULL,
					OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
					NULL
				);
				if (h == INVALID_HANDLE_VALUE)
					throw ::std::runtime_error("CreateFile (=" + filepath + ')');
			}
		// TODO -- refactor ctor into an Async_init call instead, then can use OVERLAPPED io for setting the sparseness
		if (Make_sparse && !::GetLastError()) { // new file was created or existing opened (make sparse only when new created, hence GetLastError() test which would return non-zero if file had already existed)...
			DWORD Tmp;
			if (!::DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &Tmp, NULL))
				throw ::std::runtime_error("FSCTL_SET_SPARSE (=" + filepath + ')');
		}

		// Used in conjunction with CompareObjectHandles...but such is only supported from Windows 10.
		//assert(::DuplicateHandle(::GetCurrentProcess(), h, ::GetCurrentProcess(), &Duplicated_handle, 0, FALSE, DUPLICATE_SAME_ACCESS));

		//synapse::misc::log("new handle " + ::boost::lexical_cast<::std::string>(h) + '\n', true);
	}
	bool Close_handle_in_destructor{true};
	~file_handle()
	{
		/* Note. This is a bit tricky -- in seems that:
		 1) windows may throw an SEH exception if CloseHandle is called erroneously (e.g. twice on the same handle).
		 2) asio async_handle thingy, once taken the ownership of the Handle, may close the handle automatically
		 ... so currently not calling CloseHandle here, but rather expecting the managing class to handle this for us...
		 */
		if (Close_handle_in_destructor) {
#ifndef NDEBUG
			DWORD Flags(HANDLE_FLAG_PROTECT_FROM_CLOSE);
			assert(::GetHandleInformation(h, &Flags));
			assert(!(Flags & HANDLE_FLAG_PROTECT_FROM_CLOSE));
			// Only from Windows 10...
			//assert(::CompareObjectHandles(h, Duplicated_handle));
#endif
			::CloseHandle(h);
		}
#ifndef NDEBUG
		// Used in conjunction with CompareObjectHandles...but such is only supported from Windows 10.
		//::CloseHandle(Duplicated_handle);
#endif
	}
};

struct scoped_page_allocator : 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
synapse::misc::Test_new_delete<scoped_page_allocator>,
#endif
private ::boost::noncopyable {

	char unsigned * pages{nullptr};
	decltype(synapse::Large_page_preallocator)::Unsigned_type Deallocation_handle;
	decltype(synapse::Large_page_preallocator)::Unsigned_type Bytes_size;
	bool Pages_locked;
#ifndef NDEBUG
	::std::atomic_size_t Singleton_test{0};
#endif

	// TODO -- use pool allocation/caching of either the buffers OR the whole of the containing ranges!
	unsigned alloc(decltype(synapse::Large_page_preallocator)::Unsigned_type Bytes_size) {
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		assert(pages == nullptr);
		pages = synapse::Large_page_preallocator.Allocate(Bytes_size, Deallocation_handle, Pages_locked);
		return this->Bytes_size = Bytes_size;
	}

	void free() {
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		assert(pages != nullptr);
		synapse::Large_page_preallocator.Deallocate(pages, Deallocation_handle, Bytes_size, Pages_locked);
		pages = nullptr;
	}

	~scoped_page_allocator()
	{
		if (pages != nullptr)
			synapse::Large_page_preallocator.Deallocate(pages, Deallocation_handle, Bytes_size, Pages_locked);
#ifndef NDEBUG
		pages = nullptr;
#endif
	}
};

struct Overlapped_object_handle {
	::OVERLAPPED Overlapped;
	HANDLE Event{::CreateEvent(NULL, FALSE, FALSE, NULL)};
	basio::windows::object_handle Object;
	Overlapped_object_handle(::boost::asio::strand & Strand) : Object(Strand.get_io_service(), Event)
	{
		if (Event == NULL)
			throw ::std::runtime_error("NULL event returned by CreateEvent");
	}
	// makes sure that overlapped struct is prepared to raw windows api, as well as making sure that IOCP notification (completion packets) are not being queued...
	::OVERLAPPED *
	Reset_overlapped_no_iocp()
	{
		::ZeroMemory(&Overlapped, sizeof(Overlapped));
		Overlapped.hEvent = reinterpret_cast<decltype(Event)>(reinterpret_cast<uintptr_t>(Event) | 0x1); // Set low-order bit to prevent IOCP notification queueing
		return &Overlapped;
	}
};

class Async_random_access_handle_wrapper {

	database::file_handle File_handle;
	basio::windows::random_access_handle Async_file_handle;

	::std::unique_ptr<Overlapped_object_handle> Sparsification_handle;

public:
	Async_random_access_handle_wrapper(Async_random_access_handle_wrapper const &) = delete; 
	void operator = (Async_random_access_handle_wrapper const &) = delete; 

	uint_fast64_t last_known_size_on_filesystem{0};

	Async_random_access_handle_wrapper(::boost::asio::strand & Strand, ::std::string const & File_path, bool const Make_sparse = false)
	: File_handle(File_path, Make_sparse), Async_file_handle(Strand.get_io_service(), File_handle.h) {

		if (Make_sparse == true)
			Sparsification_handle.reset(new Overlapped_object_handle(Strand));
	}

	~Async_random_access_handle_wrapper()
	{
		File_handle.Close_handle_in_destructor = !Async_file_handle.is_open();
	}

	basio::windows::random_access_handle &
	Get() noexcept
	{
		return Async_file_handle;
	}

	decltype(Sparsification_handle) & Get_sparsification_handle() {
		return Sparsification_handle;
	}
};

// NOTE: Transparent async behaviour -- users of this class are expected to manage callback-presence-lifespan, thread-safety and so on...
// The lifespan management can be done via "shared_from_this()", in the user code, injecting the 'callback' object of this class.
template <unsigned PageSize>
class unmanaged_async_file
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
: public synapse::misc::Test_new_delete<unmanaged_async_file<PageSize>>
#endif
{


	template <unsigned, typename> 
	friend class async_duplex_buffered_file;

	template <unsigned, typename> 
	friend class async_file;

	typedef void (unmanaged_async_file::*on_io_callback_type) (::boost::system::error_code const &, ::std::size_t); 
	typedef void * (unmanaged_async_file::*alloc_callback_type) (unsigned); 
	typedef void (unmanaged_async_file::*free_callback_type) (void *); 

	::boost::asio::strand & Strand; 

	synapse::asio::basic_alloc_type<500> allocator; // TODO -- was 256, now up due to VS2015 debug usage... TODO see if release mode is different and perhaps can lower it based on the target compiler being used...
	void * handler_alloc(unsigned size) { return allocator.alloc(size); }
	void handler_free(void * ptr) { return allocator.dealloc(ptr); }

	template<on_io_callback_type Callback>
	using bind_type = synapse::asio::handler_type_5<unmanaged_async_file, on_io_callback_type, alloc_callback_type, free_callback_type, Callback, &unmanaged_async_file::handler_alloc, &unmanaged_async_file::handler_free>;

	uint_fast32_t reserved_size;

	Async_random_access_handle_wrapper & async_handle_wrapper;

	scoped_page_allocator alloc;

	uint_fast64_t _buffer_offset = 0;
	uint_fast32_t _buffer_size = 0;
	uint_fast32_t first_data_byte_i = 0;

	// TODO -- in future correlate between page size and phys disk sector size (to make sure that page size is align-able w.r.t. disk sector size)

	void 
	do_callback(::boost::system::error_code const &e, ::std::size_t) noexcept
	{
		Strand.post([this, e]() {
			assert(async_io_in_progress.exchange(false, ::std::memory_order_relaxed) == true);
			try {
				if (e)
					async_error = true;
				if (callback) {
					::std::function<void(bool)> callback_tmp;
					::std::swap(callback, callback_tmp);
					assert(!callback);
					callback_tmp(async_error);
				}
				if (e)
					throw ::std::runtime_error(e.message());
			} catch (::std::exception const & e) {
					async_error = true;
					synapse::misc::log(::std::string("do_callback (=") + e.what() + ')');
			} catch (...) {
					async_error = true;
					synapse::misc::log("do_callback (=unknown exception)");
			}
		});
	}

#ifndef NDEBUG
	bool 
	check_invariants() const
	{
		// op. precedence rules are there to be leveraged :)
		return async_error == true || buffer_capacity() >= buffer_size() && buffer_capacity() <= reserved_size && first_data_byte_i < PageSize;
	}
#endif

	bool async_error = false;
	::std::function<void(bool)> callback; // todo better optimized et. al. (i.e. not a functor but more explicit perhaps)

protected:
	void
	do_file_size_preallocation(uint_fast64_t const & size)
	{
		::LARGE_INTEGER li;
		li.QuadPart = size;
		/*
			Note as to why SetFileValidData is no longer used (was present in previous revisions).
			Thinking is that it may not be really needed. In that async-io was observed to occur (when 'appending' to file semantically) even without calling the SetFileValidData. 
			The SetEndOfFile tends to achieve the 'quick' extension of file size, whilst (even if present then still already subsequent-to/after the SetEndOfFile) the SetFileValidData appearing to act only for preventing zero-filling... but such zero-filling is, apparently, done only when writing 'after' the void (from last Valid_data_length to the currently written-to location)... as noted by someones (not authoritative of course) answer from:
				http://stackoverflow.com/questions/12228042/what-does-setfilevaliddata-doing-what-is-the-difference-with-setendoffile 
				" When you use SetEndOfFile to increase the length of a file, the logical file length changes and the necessary disk space is allocated, but no data is actually physically written to the disk sectors corresponding to the new part of the file. The valid data length remains the same as it was.
				This means you can use SetEndOfFile to make a file very large very quickly, and if you read from the new part of the file you'll just get zeros. The valid data length increases when you write actual data to the new part of the file.
				That's fine if you just want to reserve space, and will then be writing data to the file sequentially. But if you make the file very large and immediately write data near the end of it, zeros need to be written to the new part of the file, which will take a long time. If you don't actually need the file to contain zeros, you can use SetFileValidData to skip this step; the new part of the file will then contain random data from previously deleted files."
			... so if writing 'sequentially' to the data, then SetFileValidData may not be needed.
			Which is a nice bonus as it (SetFileValidData) is incompatible with sparse files... whilst also incurring additional system call during regularl writing events. 
			Moreover security-breach issue also appears to be implicitly resolved.
		*/
		if (!::SetFilePointerEx(async_handle_wrapper.Get().native_handle(), li, NULL, FILE_BEGIN) || !::SetEndOfFile(async_handle_wrapper.Get().native_handle()))
			throw ::std::runtime_error("do_file_size_preallocation from(=" + ::std::to_string(async_handle_wrapper.last_known_size_on_filesystem) + ") to(=" + ::std::to_string(size) + ')');
		async_handle_wrapper.last_known_size_on_filesystem = size;
	}

	uint_fast64_t
	get_last_known_size_on_filesystem() 
	{
		return async_handle_wrapper.last_known_size_on_filesystem;
	}

public:

#ifndef NDEBUG
	bool initialised{false};
#endif

	char unsigned *
	buffer()
	{
		return alloc.pages;
	}
	char unsigned const *
	buffer() const
	{
		return alloc.pages;
	}

	char unsigned * 
	buffer_begin()
	{
		return alloc.pages + first_data_byte_i; 
	}

	char unsigned const * 
	buffer_begin() const
	{
		return alloc.pages + first_data_byte_i; 
	}

	uint_fast32_t 
	buffer_capacity() const
	{
		return reserved_size - first_data_byte_i;
	}

	unsigned
	buffer_alignment_offset() const 
	{
		return first_data_byte_i;
	}

	uint_fast64_t
	buffer_offset() const
	{
		return _buffer_offset;
	}

	uint_fast32_t
	buffer_size() const
	{
		return _buffer_size;
	}

	uint_fast32_t
	reserved_capacity() const
	{
		return reserved_size;
	}

#ifndef NDEBUG
	::std::atomic_size_t Singleton_test{0};
#endif

	void
	ensure_reserved_capacity_no_less_than(uint_fast32_t const & x)
	{
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		assert(async_io_in_progress.load(::std::memory_order_relaxed) == false);
		assert(!(x % PageSize));
		if (reserved_size < x) {
			alloc.free();
			reserved_size = alloc.alloc(x);
		}
	}

	void
	ensure_reserved_capacity_no_greater_than(uint_fast32_t const & x)
	{
#ifndef NDEBUG
		synapse::misc::Scoped_single_thread_test sst(Singleton_test, __FILE__, __LINE__);
#endif
		assert(async_io_in_progress.load(::std::memory_order_relaxed) == false);
		assert(!(x % PageSize));
		if (reserved_size > x) {
			alloc.free();
			reserved_size = alloc.alloc(x);
		}
	}

#ifndef NDEBUG
	::std::atomic_bool async_io_in_progress{false};
#endif

	// read portion of file into the start of the buffer
	// args are file-byte specific (not in relation to buffer start)
	template <typename T>
	void
	async_read(uint_fast64_t const offset, uint_fast32_t const size, T && callback)
	{
		assert(check_invariants());
		assert(size <= buffer_capacity());

		assert(!this->callback);
		this->callback = ::std::forward<T>(callback);

		_buffer_offset = offset;
		_buffer_size = size;
		uint_fast64_t const begin(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(offset, PageSize));
		uint_fast64_t const end(synapse::misc::round_up_to_multiple_of_po2<uint_fast64_t>(offset + size, PageSize));
		first_data_byte_i = offset - begin;
		assert(end - begin <= reserved_size);
		assert(!(begin % PageSize));
		assert(!((end - begin) % PageSize));

		assert(async_io_in_progress.exchange(true, ::std::memory_order_relaxed) == false);

		basio::async_read_at(async_handle_wrapper.Get(), begin, basio::buffer(alloc.pages, end - begin), bind_type<&unmanaged_async_file::do_callback>(*this));
		assert(check_invariants());
	}

	// does not do any io, simply sets the buffer's mapping to be associated with certain section of file
	// it is up to calling code to ensure that (e.g. when writing out) the specified offset does not cause pre(leading) pagesize amount to be clobbered (not preserved)
	void
	set_buffer_offset(uint_fast64_t const offset)
	{
		_buffer_offset = offset;
		first_data_byte_i = offset - synapse::misc::round_down_to_multiple_of_po2<decltype(offset)>(offset, PageSize);
		assert(check_invariants());
	}

	// does not do any io, simply sets the buffer's mapping to be associated with certain section of file
	void
	set_buffer_size(uint_fast32_t const size)
	{
		assert(size <= buffer_capacity());
		_buffer_size = size;
		assert(check_invariants());
	}

	void
	set_buffer(uint_fast64_t const offset, uint_fast32_t const size)
	{
		_buffer_offset = offset;
		first_data_byte_i = offset - synapse::misc::round_down_to_multiple_of_po2<decltype(offset)>(offset, PageSize);
		assert(size <= buffer_capacity());
		_buffer_size = size;
		assert(check_invariants());
	}

	// write portion of the buffer into the file
	// args are file-byte specific (not in relation to buffer start), but current buffer contents must be already covering the relevant section of the file (i.e. _buffer_offset ought to be correct)
	template <typename T>
	void
	async_write(uint_fast64_t const offset, uint_fast32_t const size, T && callback)
	{
		assert(check_invariants());
		assert(offset >= _buffer_offset);
		assert(offset < _buffer_offset + buffer_capacity());
		assert(offset + size <= _buffer_offset + buffer_capacity());

		assert(!this->callback);
		this->callback = ::std::forward<T>(callback);

		uint_fast32_t const buffer_begin(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(offset - _buffer_offset + first_data_byte_i, PageSize));
		uint_fast64_t const begin(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(offset, PageSize));
		uint_fast64_t const end(synapse::misc::round_up_to_multiple_of_po2<uint_fast64_t>(offset + size, PageSize));
		assert(end > begin);
		assert(end - begin <= reserved_size);
		assert(!(begin % PageSize));
		assert(!(buffer_begin % PageSize));
		assert(!((end - begin) % PageSize));

		assert(async_io_in_progress.exchange(true, ::std::memory_order_relaxed) == false);

		// schedule writing of data
		basio::async_write_at(async_handle_wrapper.Get(), begin, basio::buffer(alloc.pages + buffer_begin, end - begin), bind_type<&unmanaged_async_file::do_callback>(*this));
		assert(check_invariants());
	}

	/*
		TODO -- in future unite and reuse explictly contained overlapped_ptr (currently used only in sparsification mechanisms) with the normal asyinc read-write io. 
		This way may explicitly GetLastError for testing the async nature of processing. 
	*/
#ifndef NDEBUG
	::std::atomic_bool Sparsification_in_progress{false};
#endif
	template <typename Callback_type>
	void
	Async_sparsify_beginning(uint_fast64_t const Previous_sparseness_end, uint_fast64_t const Sparseness_end, Callback_type && Callback)
	{
		assert(check_invariants());
		assert(Sparsification_in_progress.exchange(true, ::std::memory_order_relaxed) == false);
		assert(async_handle_wrapper.Get_sparsification_handle());

		/*
			Note: this (FSCTL_SET_ZERO_DATA) appears to be a strictly CPU-bound process. Thusly, it is (more likely than not) implemented in kernel/NTFS driver as a synchronous call (despite 'OVERLAPPED'	setup).
			Ideally, even the CPU-bound calls would have been broken-down into an async-version counterparts (i.e. doing things by smaller chunks thereby yielding coop-like multitasking) -- so that LIFO thread-context minimization would have been possible... alas most-likely the best possible outcome at this stage is to do this CPU-bound (blocking) call on a separate thread. Given that one ought to have more 'awaiting' threads in a thread-pool than there are CPU/cores (i.e. the whole IOCP setup philosophy) this is achieved by simply not doing things on the same 'strand' (in this case via calling io_service()::post())...
		 */
		Strand.get_io_service().post([this, Previous_sparseness_end, Sparseness_end, Callback]() mutable {
			assert(Sparsification_in_progress.load(::std::memory_order_relaxed) == true);
			assert(async_handle_wrapper.Get_sparsification_handle());

			// TODO more assertions w.r.t. where current data-io pointer is, file size, etc. and the to-be-sparsed data...

			uint_fast64_t const Adjusted_previous_sparseness_end(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(Previous_sparseness_end, PageSize));
			assert(!(Adjusted_previous_sparseness_end % PageSize));

			uint_fast64_t const Adjusted_sparseness_end(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(Sparseness_end, PageSize));
			assert(!(Adjusted_sparseness_end % PageSize));

			if (Adjusted_sparseness_end > Adjusted_previous_sparseness_end) {

				::FILE_ZERO_DATA_INFORMATION Meta_zero_data;
				if (static_cast<decltype(Adjusted_sparseness_end)>(::std::numeric_limits<decltype(Meta_zero_data.BeyondFinalZero.QuadPart)>::max()) < Adjusted_sparseness_end) {
					// TODO -- introduce exception try catch instead of this...
					synapse::misc::log("Async_sparsify_beginning in async_file has target values outside of the numeric range of system API", true);
					assert(Sparsification_in_progress.exchange(false, ::std::memory_order_relaxed) == true);
					Strand.post([Callback]() mutable {
						Callback(true);
					});
					return;
				}
				Meta_zero_data.FileOffset.QuadPart = 0;
				Meta_zero_data.BeyondFinalZero.QuadPart = Adjusted_sparseness_end;

				synapse::misc::log("SPARSIFYING begin byte " + ::std::to_string(Adjusted_previous_sparseness_end) 
					+ " end byte " + ::std::to_string(Adjusted_sparseness_end) 
					+ " size(MB) " + ::std::to_string((Adjusted_sparseness_end - Adjusted_previous_sparseness_end) / 1000000.) + '\n');

#ifndef NDEBUG
				auto const Chrono_begin(::std::chrono::high_resolution_clock::now());
#endif
				auto const Sync_done(::DeviceIoControl(async_handle_wrapper.Get().native_handle(), FSCTL_SET_ZERO_DATA, &Meta_zero_data, sizeof(Meta_zero_data), NULL, 0, NULL, async_handle_wrapper.Get_sparsification_handle()->Reset_overlapped_no_iocp())); 
#ifndef NDEBUG
				auto const Chrono_end(::std::chrono::high_resolution_clock::now());
#endif
				if (!Sync_done) {
					auto const Error_code(::GetLastError());
					if (Error_code != ERROR_IO_PENDING) {
						assert(Sparsification_in_progress.exchange(false, ::std::memory_order_relaxed) == true);
						Strand.post([Callback]() mutable {
							Callback(true);
						});
						return;
					}
					synapse::misc::log("WOOOOOOOWWWWWOWWOWOWOWOW", true);
				} 
#ifndef NDEBUG
				else
					synapse::misc::log("warning, sync-io in FSCTL_SET_ZERO_DATA. took: " + ::std::to_string(::std::chrono::duration_cast<::std::chrono::microseconds>(Chrono_end - Chrono_begin).count())+ "\n", true);
#endif
				auto const Sparsified_size_delta(Adjusted_sparseness_end - Adjusted_previous_sparseness_end);
				async_handle_wrapper.Get_sparsification_handle()->Object.async_wait([Callback, Sparsified_size_delta, this](::boost::system::error_code const & e) mutable {
					assert(Sparsification_in_progress.exchange(false, ::std::memory_order_relaxed) == true);
					database::Database_size.fetch_sub(Sparsified_size_delta, ::std::memory_order_relaxed);
					Strand.post([Callback, e]() mutable {
						Callback(e);
					});
				});
			} else {
				// Todo, figure out why not just post to strand directly (what was the reasoning, and does it still apply).
				Strand.get_io_service().post([Callback, this]() mutable {
					assert(Sparsification_in_progress.exchange(false, ::std::memory_order_relaxed) == true);
					Strand.post([Callback]() mutable {
						Callback(false);
					});
				});
			}
		});
		assert(check_invariants());
	}
	//

	unmanaged_async_file(::boost::asio::strand & Strand, Async_random_access_handle_wrapper & async_handle_wrapper, uint_fast32_t const reserve_size) 
	: Strand(Strand), async_handle_wrapper(async_handle_wrapper) {
		// note, not doing basio::use_service<basio::detail::io_service_impl>(synapse::asio::io_service::get()).register_handle(Native_handle, e) for the sparsification mechanisms because async_handle_wrapper above should already register the native handle in question

		assert(!(reserve_size % PageSize));
		this->reserved_size = alloc.alloc(reserve_size);

		::LARGE_INTEGER li;
		if (!::GetFileSizeEx(async_handle_wrapper.Get().native_handle(), &li))
			throw ::std::runtime_error("get_last_known_size_on_filesystem " + ::boost::lexical_cast<::std::string>(::GetLastError()));
		async_handle_wrapper.last_known_size_on_filesystem = li.QuadPart;
	}
};

template <bool Is_mutable>
class Basic_data_meta_ptr {
	template <bool, bool> struct Memory_type_traits;
	template<bool Ignored> struct Memory_type_traits<true, Ignored> { typedef void type; };
	template<bool Ignored> struct Memory_type_traits<false, Ignored> { typedef void const type; };

	unsigned constexpr static version_number{2};

	typename Memory_type_traits<Is_mutable, true>::type * Memory;

public:

#if DP_ENDIANNESS != LITTLE
#error current code only supports LITTLE_ENDIAN archs. -- extending to BIG_ENDIAN is trivial but there wasnt enough time at this stage...
#endif

	unsigned constexpr static size{80};
	static_assert(!(size % 8), "meta size ought to be multiple of 64bits"); 

	void
	initialise() const
	{
		assert(Memory);
		new (Memory) uint64_t(version_number);
		set_data_size(0);
		Set_maximum_sequence_number(0);
		set_latest_timestamp(0);
		Set_begin_byte_offset(0);
		Set_target_data_consumption_limit(-1);
		Set_requested_sparsify_upto_timestamp(0);
		Set_processed_sparsify_upto_timestamp(0);
		Set_sparsify_upto_byte_offset(0);
		rehash();
	}
	void static
	initialise_(Basic_data_meta_ptr meta)
	{
		meta.initialise();
	}
	uint_fast64_t
	get_data_size() const
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1);
	}
	void
	set_data_size(uint_fast64_t const & size) const
	{
		new (reinterpret_cast<uint64_t*>(Memory) + 1) uint64_t(size);
	}
	void
	Set_maximum_sequence_number(uint_fast64_t const & size) const
	{
		new (reinterpret_cast<uint64_t*>(Memory) + 2) uint64_t(size);
	}
	uint_fast64_t
	Get_maximum_sequence_number() const 
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 2);
	}
	void
	set_latest_timestamp(uint_fast64_t const & timestamp) const
	{
		new (reinterpret_cast<uint64_t*>(Memory) + 3) uint64_t(timestamp); 
	}
	uint_fast64_t
	get_latest_timestamp() const 
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 3);
	}

	void
	Set_begin_byte_offset(uint_fast64_t const & Begin_byte_offset) const
	{
		new (reinterpret_cast<uint64_t*>(Memory) + 4) uint64_t(Begin_byte_offset); 
	}
	uint_fast64_t
	Get_begin_byte_offset() const 
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 4);
	}

	void Set_target_data_consumption_limit(uint_fast64_t const & Target_limit) const {
		new (reinterpret_cast<uint64_t*>(Memory) + 5) uint64_t(Target_limit); 
	}

	uint_fast64_t Get_target_data_consumption_limit() const {
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 5);
	}

	void 
	Set_requested_sparsify_upto_timestamp(uint_fast64_t const & Timestamp) const {
		new (reinterpret_cast<uint64_t*>(Memory) + 6) uint64_t(Timestamp); 
	}

	uint_fast64_t 
	Get_requested_sparsify_upto_timestamp() const {
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 6);
	}

	void 
	Set_processed_sparsify_upto_timestamp(uint_fast64_t const & Timestamp) const {
		new (reinterpret_cast<uint64_t*>(Memory) + 7) uint64_t(Timestamp); 
	}

	uint_fast64_t 
	Get_processed_sparsify_upto_timestamp() const {
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 7);
	}

	void 
	Set_sparsify_upto_byte_offset(uint_fast64_t const & Offset) const {
		new (reinterpret_cast<uint64_t*>(Memory) + 8) uint64_t(Offset); 
	}

	uint_fast64_t 
	Get_sparsify_upto_byte_offset() const {
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 8);
	}

	void
	verify_hash() const
	{
		uint64_t hash(synapse::misc::crc32c(synapse::misc::Get_alias_safe_value<uint64_t>(Memory), synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1)));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 5));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 6));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 7));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 8));
		if ((synapse::misc::Get_alias_safe_value<uint64_t>(Memory) & 0xffffffff) != version_number || hash != synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 9))
			throw ::std::runtime_error("Basic_data_meta_ptr hash verification in file has failed");
	}
	void
	rehash() const
	{
		uint64_t hash(synapse::misc::crc32c(synapse::misc::Get_alias_safe_value<uint64_t>(Memory), synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1)));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 5));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 6));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 7));
		new (reinterpret_cast<uint64_t*>(Memory) + 9) uint64_t(synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 8)));
	}

	typedef Basic_data_meta_ptr<false> Constant_type;

	Basic_data_meta_ptr(typename Memory_type_traits<Is_mutable, true>::type * Memory)
	: Memory(Memory)
	{
	}

	template <bool> friend class Basic_data_meta_ptr;

	template <bool X>
	Basic_data_meta_ptr(Basic_data_meta_ptr<X> const & Construct_from) 
	: Memory(Construct_from.Memory) {
	}

	template <bool X>
	bool operator == (Basic_data_meta_ptr<X> const & Compare_with) const
	{
		return Memory == Compare_with.Memory;
	}

};

typedef Basic_data_meta_ptr<true> Data_meta_ptr;
typedef Basic_data_meta_ptr<false> Data_meta_const_ptr;

template <unsigned PageSize, typename MetaTraits>
class async_file : 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
public synapse::misc::Test_new_delete<async_file<PageSize, MetaTraits>>, 
#endif
public synapse::database::unmanaged_async_file<PageSize> 
{
#ifndef NDEBUG
	static void * operator new (::std::size_t sz) {
		return synapse::misc::Test_new_delete<async_file<PageSize, MetaTraits>>::operator new(sz);
	}
#endif

	// TODO -- later on do the constexpr round-up MetaTraits::size to the PageSize and then in async_open deal with meta_file filesystem size based on MeteTraits::size rather than currently-simplified shortcut of a hack 'single PageSize' fixed size amount.
	static_assert(PageSize >= MetaTraits::size, "size of metadata cannot be greater than PageSize");

	typedef synapse::database::unmanaged_async_file<PageSize> base_type;

	synapse::database::unmanaged_async_file<PageSize> & meta_file; // TODO -- consider having a templated buffer-size file class variant...

public:
	async_file(::boost::asio::strand & Strand, synapse::database::unmanaged_async_file<PageSize> & Meta_file, Async_random_access_handle_wrapper & Async_file_handle_wrapper, uint_fast32_t const reserve_size)
	: base_type(Strand, Async_file_handle_wrapper, reserve_size), meta_file(Meta_file) {
    assert(base_type::async_handle_wrapper.Get().native_handle() != meta_file.async_handle_wrapper.Get().native_handle());
	}

	MetaTraits
	get_meta()
	{
		assert(meta_file.initialised == true && meta_file.buffer() == meta_file.buffer_begin());
		return MetaTraits(meta_file.buffer());
	}

	typename MetaTraits::Constant_type   
	get_meta() const
	{
		assert(meta_file.initialised == true && meta_file.buffer() == meta_file.buffer_begin());
		return typename MetaTraits::Constant_type(meta_file.buffer());
	}

	template <typename T, typename MetaInitialiseCallbackType>
	void
	async_open(T && callback, MetaInitialiseCallbackType && meta_initialise_callback)
	{
		assert(meta_file.initialised == false);
		uint_fast64_t init_file_size(meta_file.get_last_known_size_on_filesystem()); // note -- ok to do so here because there is a guarantee-by-design that initially, there will be only one file that is opened fully (callback is invoked) before other files referring to the same filename get constructed
		if (init_file_size < PageSize)
			meta_file.do_file_size_preallocation(PageSize);
		if (!base_type::async_error) {
			meta_file.async_read(0, PageSize, [this, init_file_size, callback(::std::forward<T>(callback)), meta_initialise_callback](bool error) mutable {
				if (!error) {
					if (!base_type::async_error) {
						if (init_file_size >= PageSize) { 
							assert(base_type::Strand.running_in_this_thread());
							assert(meta_file.buffer() == meta_file.buffer_begin());
							assert(!(reinterpret_cast<uintptr_t>(meta_file.buffer_begin()) % PageSize));
							#ifndef NDEBUG
								meta_file.initialised = true;
							#endif
							get_meta().verify_hash();
							callback(false);
						} else {
							#ifndef NDEBUG
								meta_file.initialised = true;
							#endif
							meta_initialise_callback(get_meta());
							#ifndef NDEBUG
								meta_file.initialised = false;
							#endif
							meta_file.set_buffer_offset(0);
							meta_file.set_buffer_size(MetaTraits::size);
							assert(meta_file.get_last_known_size_on_filesystem() >= PageSize);
							meta_file.async_write(0, MetaTraits::size, 
								[this, callback(::std::move(callback))](bool error) mutable {
									if (!error && !base_type::async_error) {
										#ifndef NDEBUG
											meta_file.initialised = true;
										#endif
										callback(false);
									} else {
										base_type::async_error = true;
										callback(true);
										// TODO convert all of the code to try/catch semantics to make it more consistent with other source files
									}
								}
							);
						}
					}
				} else {
					base_type::async_error = true;
					callback(true);
				}
			});
		} else
			callback(true);
	}

	template <typename T>
	void async_write_meta(T && callback) {
		meta_file.set_buffer_offset(0);
		meta_file.set_buffer_size(MetaTraits::size);
		get_meta().rehash();
		assert(get_meta().get_data_size() <= base_type::get_last_known_size_on_filesystem());
		assert(meta_file.get_last_known_size_on_filesystem() >= PageSize);
		meta_file.async_write(0, MetaTraits::size, 
			[this, callback(::std::forward<T>(callback))](bool error) mutable {
				if (!error && !base_type::async_error) {
					callback(false);
				} else {
					base_type::async_error = true;
					callback(true);
					// TODO convert all of the code to try/catch semantics to make it more consistent with other source files
				}
			}
		);
	}

	template <typename T>
	void
	async_write(uint_fast64_t const offset, uint_fast32_t const size, T && callback)
	{
		uint_fast64_t const tmp(offset + size);
		if (tmp > base_type::get_last_known_size_on_filesystem())
			base_type::do_file_size_preallocation(synapse::misc::round_up_to_multiple_of_po2<decltype(tmp)>(tmp + base_type::reserved_size, PageSize));
		assert(get_meta().get_data_size() <= base_type::get_last_known_size_on_filesystem());
		if (!base_type::async_error) {
			base_type::async_write(offset, size, [this, tmp, callback(::std::forward<T>(callback))](bool error) mutable {
				assert(base_type::Strand.running_in_this_thread());
				if (!error) {
					if (!base_type::async_error) {
						if (tmp > get_meta().get_data_size()) {
							get_meta().set_data_size(tmp);
							async_write_meta(::std::move(callback));
						} else
							callback(false);
					}
				} else {
					base_type::async_error = true;
					callback(true);
				}
			});
		} else 
			callback(true);
	}
};

// TODO rather an extremely quick and qurky and hacky type of a thingy :)
// make it more elegant later on (e.g. share the scoped_handle between reader and writer instead of duplicating it)
template <unsigned PageSize, typename MetaTraits>
class async_duplex_buffered_file {
	Async_random_access_handle_wrapper Async_meta_file_handle_wrapper;
	synapse::database::unmanaged_async_file<PageSize> meta_file;
	Async_random_access_handle_wrapper Async_file_handle_wrapper;
	synapse::database::unmanaged_async_file<PageSize> reading_file;
	synapse::database::unmanaged_async_file<PageSize> writing_file;

#ifndef NDEBUG
	bool initialised{false};
#endif

	bool async_error = false;
	void
	put_into_error_state(::std::exception const & e) 
	{
		synapse::misc::log(::std::string("exception(=") + e.what() + ")\n");
		async_error = true;
	}

public:
	async_duplex_buffered_file(::boost::asio::strand & Strand, ::std::string const & meta_filepath, ::std::string const & filepath, uint_fast32_t const read_reserve_size, uint_fast32_t const write_reserve_size)
	: Async_meta_file_handle_wrapper(Strand, meta_filepath, false), meta_file(Strand, Async_meta_file_handle_wrapper, PageSize), Async_file_handle_wrapper(Strand, filepath, true), reading_file(Strand, Async_file_handle_wrapper, read_reserve_size), writing_file(Strand, Async_file_handle_wrapper, write_reserve_size) {
	}

	MetaTraits 
	get_meta()
	{
		assert(initialised == true && meta_file.buffer() == meta_file.buffer_begin());
		return MetaTraits(meta_file.buffer());
	}

	typename MetaTraits::Constant_type  
	get_meta() const
	{
		assert(initialised == true && meta_file.buffer() == meta_file.buffer_begin());
		return typename MetaTraits::Constant_type(meta_file.buffer());
	}

	template <typename T, typename MetaInitialiseCallbackType>
	void
	async_open(T && callback, MetaInitialiseCallbackType && meta_initialise_callback) noexcept
	{
		assert(initialised == false);
		try {
			uint_fast64_t const init_file_size(meta_file.get_last_known_size_on_filesystem());
			if (init_file_size < PageSize)
				meta_file.do_file_size_preallocation(PageSize);
			if (!async_error) {
				meta_file.async_read(0, PageSize, [this, init_file_size, callback, meta_initialise_callback](bool error) mutable noexcept {
					try {
						if (!error && !async_error) {
							static_assert(PageSize >= 9, "page size does not make sense");
							if (init_file_size >= PageSize) { 
								assert(!(reinterpret_cast<uintptr_t>(meta_file.buffer_begin()) % PageSize));
								#ifndef NDEBUG
									initialised = true;
								#endif
								get_meta().verify_hash();
								callback(false);
							} else {
								#ifndef NDEBUG
									initialised = true;
								#endif
								meta_initialise_callback(get_meta());
								#ifndef NDEBUG
									initialised = false;
								#endif
								meta_file.set_buffer_offset(0);
								meta_file.set_buffer_size(MetaTraits::size);
								assert(meta_file.get_last_known_size_on_filesystem() >= PageSize);
								meta_file.async_write(0, MetaTraits::size, [this, callback](bool error) mutable noexcept {
									try {
										if (!error && !async_error) {
											#ifndef NDEBUG
												initialised = true;
											#endif
											callback(false);
										} else 
											throw ::std::runtime_error("writing initial metadata in async_open in async_duplex_buffered_file");
									} catch (::std::exception const & e) { 
										put_into_error_state(e); 
										callback(true);
									}
								});
							}
						} else
							throw ::std::runtime_error("meta_file async_read in async_duplex_buffered_file");
					} catch (::std::exception const & e) { 
						put_into_error_state(e); 
						callback(true);
					}
				});
			} else
				throw ::std::runtime_error("async_duplex_buffered_file is already in error in async_open");
		} catch (::std::exception const & e) { 
			put_into_error_state(e); 
			callback(true);
		}
	}

	template <typename T>
	void
	async_read(uint_fast64_t const offset, uint_fast32_t const size, T && callback) noexcept
	{
		try {
			if (!async_error)
				reading_file.async_read(offset, size, callback);
			else
				throw ::std::runtime_error("async_read in async_duplex_buffered_file");
		} catch (::std::exception const & e) { 
			put_into_error_state(e); 
			callback(true);
		}
	}

	char unsigned const * 
	reading_buffer_begin() const 
	{
		return reading_file.buffer_begin(); 
	}

	uint_fast32_t 
	reading_buffer_capacity() const
	{
		return reading_file.buffer_capacity();
	}

	uint_fast64_t
	reading_buffer_offset() const
	{
		return reading_file.buffer_offset();
	}

	uint_fast32_t
	reading_buffer_size() const
	{
		return reading_file.buffer_size();
	}

	template <typename T>
	void async_write_meta(T && callback) noexcept {
		meta_file.set_buffer_offset(0);
		meta_file.set_buffer_size(MetaTraits::size);
		get_meta().rehash();
		assert(get_meta().get_data_size() <= writing_file.get_last_known_size_on_filesystem());
		assert(meta_file.get_last_known_size_on_filesystem() >= PageSize);
		meta_file.async_write(0, MetaTraits::size, [this, callback(::std::forward<T>(callback))](bool error) mutable noexcept {
			try {
				if (!error && !async_error) {
					callback(false);
				} else 
					throw ::std::runtime_error("writing metadata in async_write in async_duplex_buffered_file");
			} catch (::std::exception const & e) { 
				put_into_error_state(e); 
				callback(true);
			}
		});
	}

	template <typename T>
	void
	async_write(uint_fast64_t const offset, uint_fast32_t const size, T && callback) noexcept
	{
		try {
			uint_fast64_t const tmp(offset + size);
			if (tmp > writing_file.get_last_known_size_on_filesystem())
				writing_file.do_file_size_preallocation(synapse::misc::round_up_to_multiple_of_po2<decltype(tmp)>(tmp + writing_file.reserved_capacity(), PageSize));
			assert(get_meta().get_data_size() <= writing_file.get_last_known_size_on_filesystem());
			if (!async_error) {
				writing_file.async_write(offset, size, [this, tmp, callback](bool error) mutable noexcept {
					try {
						if (!error && !async_error) {
							if (tmp > get_meta().get_data_size()) {
								get_meta().set_data_size(tmp);
								async_write_meta(callback);
							} else
								callback(false);
						} else 
							throw ::std::runtime_error("writing_file in async_write in async_duplex_buffered_file");
					} catch (::std::exception const & e) { 
						put_into_error_state(e); 
						callback(true);
					}
				});
			} else 
				throw ::std::runtime_error("async_write in async_duplex_buffered_file");
		} catch (::std::exception const & e) { 
			put_into_error_state(e); 
			callback(true);
		}
	}

	char unsigned * 
	writing_buffer() 
	{
		return writing_file.buffer(); 
	}

	char unsigned * 
	writing_buffer_begin() 
	{
		return writing_file.buffer_begin(); 
	}

	uint_fast32_t 
	writing_buffer_capacity() const
	{
		return writing_file.buffer_capacity();
	}

	unsigned
	writing_buffer_alignment_offset() const
	{
		return writing_file.buffer_alignment_offset();
	}

	uint_fast64_t
	writing_buffer_offset() const
	{
		return writing_file.buffer_offset();
	}

	uint_fast32_t
	writing_buffer_size() const
	{
		return writing_file.buffer_size();
	}

	uint_fast32_t
	writing_reserved_capacity() const
	{
		return writing_file.reserved_capacity();
	}

	void
	set_writing_buffer_offset(uint_fast64_t const offset)
	{
		writing_file.set_buffer_offset(offset);
	}

	void
	set_writing_buffer_size(uint_fast32_t const size)
	{
		writing_file.set_buffer_size(size);
	}

	void
	set_writing_buffer(uint_fast64_t const offset, uint_fast32_t const size)
	{
		writing_file.set_buffer(offset, size);
	}


	template <typename Callback_type>
	void
	Async_sparsify_beginning(uint_fast64_t const Sparseness_begin, uint_fast64_t const Sparseness_end, Callback_type && Callback)
	{
		writing_file.Async_sparsify_beginning(Sparseness_begin, Sparseness_end, ::std::forward<Callback_type>(Callback));
	}
};

#else

// unfortunately, currently only on Windows... (will later revise boost::asio integration with Linux's native (in-kernel) AIO async io for files opened with O_DIRECT
#error non-windows targets need to implement async. file io semantics (ideally integrated with boost.asio in some way)

#endif


}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


