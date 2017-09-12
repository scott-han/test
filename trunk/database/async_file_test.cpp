#include <atomic>
#include <boost/enable_shared_from_this.hpp> // TODO -- when newer gcc comes out try with std::shared_ptr et.al.
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "../Version.h"

#include "../amqp_0_9_1/foreign/copernica/endian.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { 
namespace misc { namespace alloc {
::std::atomic_uint_fast64_t static Virtual_allocated_memory_size{0};
uint_fast64_t static Memory_alarm_threshold{static_cast<uint_fast64_t>(-1)};
}}
namespace synapse { 
namespace asio {
::std::atomic_bool static Exit_error{false};
}
namespace database {
#if defined(__GNUC__) && !defined(__clang__)
	// otherwise clang complains of unused variable, GCC however wants it to be present.
	::std::atomic_uint_fast64_t static Database_size{0};
#endif
int static nice_performance_degradation = 0;
}
namespace amqp_0_9_1 {
unsigned constexpr static page_size = 4096;
}
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../asio/io_service.h"
#include "../misc/sysinfo.h"
#include "../misc/alloc.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { 
// Todo: better inclusion style (no time now).
data_processors::misc::alloc::Large_page_preallocator<amqp_0_9_1::page_size> static Large_page_preallocator;
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "async_file.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

// example of likely usage in terms of lifescope etc.
template <unsigned PageSize>
struct range : public ::boost::enable_shared_from_this<range<PageSize> > {
	typedef ::boost::enable_shared_from_this<range<PageSize> > base_type;
	Async_random_access_handle_wrapper Async_meta_file_handle;
	database::unmanaged_async_file<PageSize> Meta_file;
	Async_random_access_handle_wrapper Async_file_handle;
	database::async_file<PageSize, database::Data_meta_ptr> file;

	range(::boost::asio::strand & Strand, ::std::string const & filesizepath, ::std::string const & filepath)
	: Async_meta_file_handle(Strand, filesizepath, false), Meta_file(Strand, Async_meta_file_handle, PageSize), Async_file_handle(Strand, filepath, true), file(Strand, Meta_file, Async_file_handle, 1ul << 24) {
		::std::cerr << "range ctor\n";
	}

	unsigned anticipated_size;
	void
	whatever()
	{
		// example of keeping self in scope via async_file's callbacks
		auto me(base_type::shared_from_this());
		auto on_write = [this, me](bool error) 
		{
			if (error == true) 
				throw ::std::runtime_error("on_write file should not be in error");
			if (anticipated_size != file.get_meta().get_data_size())
				throw ::std::runtime_error("file.get_meta().get_data_size() does not match. want(=" + ::boost::lexical_cast< ::std::string>(anticipated_size) + ") got(=" + ::boost::lexical_cast< ::std::string>(file.get_meta().get_data_size()) + ')');
			if (file.buffer_size() != file.get_meta().get_data_size())
				throw ::std::runtime_error("at this stage buffer size should be the same as the file size");
		};
		auto on_open = [this, me, on_write](bool error)
		{
			::std::cerr << "error " << error << ::std::endl;
			::std::cerr << "size " << file.get_meta().get_data_size() << ::std::endl;
			if (error == true) 
				throw ::std::runtime_error("file should not be in error");
			if (file.get_meta().get_data_size()) 
				throw ::std::runtime_error("initial file size should be 0");
			if (file.buffer_size()) 
				throw ::std::runtime_error("initial buffer size should be 0");
			char unsigned * x = file.buffer_begin();
			if (x == nullptr) 
				throw ::std::runtime_error("buffer_data should not be null");
			anticipated_size = file.buffer_capacity();
			for (unsigned i(0); i != file.buffer_capacity(); ++i) {
				x[i] = 'a';
			}
			file.set_buffer_offset(0);
			file.set_buffer_size(file.buffer_capacity());
			file.async_write(0, file.buffer_capacity(), on_write);
		};
		file.async_open(on_open, &database::Data_meta_ptr::initialise_); // or, could also do something like "bind(&range::open, shared_from_this())" 
	}
	~range()
	{
		::std::cerr << "range dtor\n";
	}
};

int static
run(int, char* argv[])
{
	try
	{
		boost::filesystem::path path(argv[0]);
		::std::string const filename('.' + path.filename().string());

		boost::filesystem::path file_size_path(path.parent_path() / (filename + ".tmp.meta"));
		if (::boost::filesystem::exists(file_size_path))
			::boost::filesystem::remove(file_size_path);
		boost::filesystem::path file_path(path.parent_path() / (filename + ".tmp"));
		if (::boost::filesystem::exists(file_path))
			::boost::filesystem::remove(file_path);

		synapse::Large_page_preallocator.Initialize();
		synapse::asio::io_service::init(synapse::misc::concurrency_hint());
		::boost::asio::strand Strand(synapse::asio::io_service::get());

		typedef range<synapse::amqp_0_9_1::page_size> range_type;
		::boost::shared_ptr<range_type> r(new range_type(Strand, file_size_path.string(), file_path.string()));
		::std::cerr << "about to reset the main/containing pointer to range object\n";
		r->whatever();
		r.reset();
		::std::cerr << "io_service about to run\n";
		synapse::asio::run();
		::std::cerr << "io_service done running\n";
		return synapse::asio::Exit_error;
	}
	catch (::std::exception const & e)
	{
		::std::cerr << "oops: " << e.what() << "\n";
		return 1;
	}
	catch (...)
	{
		::std::cerr << "oops: uknown exception\n";
		return 1;
	}
}
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[])
{
	return ::data_processors::synapse::database::run(argc, argv);
}
