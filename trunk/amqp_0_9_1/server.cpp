#if defined(__GNUC__)  || defined(__clang__) 
#include "server_pch.h"
#endif

#include "../Version.h"

#include <iostream>

#include <boost/algorithm/string.hpp>

#if defined(_MSC_VER) && !defined(__clang__)
void Non_conforming_msvc_hack() {
	::std::exception_ptr exptr(::std::current_exception());
	try {
		::std::rethrow_exception(exptr);
	} catch (::std::exception const & e) {
		data_processors::synapse::misc::log(::std::string("Uncaught excetpion: ") + e.what());
	} catch (...) {
		data_processors::synapse::misc::log("Uncaught non ::std::excetpion... ");
	}
	data_processors::synapse::misc::log("Most likely an exception was thrown from a 'noexcept' method... like C++11 destructor.");
	data_processors::synapse::misc::log.flush();
	abort();
}
#endif

#ifdef __WIN32__
#ifdef __MINGW64__
// essentially a hack so that we flush the err stream -- when running as service the assertion failiers are not printed out... if app is build on mingw64 (in wheir wassert.c they don't call fflush on stderr before calling abort...)
extern "C" {
void __cdecl
_assert(const char * expression, const char * file, unsigned line_number)
{
	data_processors::synapse::misc::log("ASSERTION FAILIER (custom hack)!\n");
	if (expression)
		data_processors::synapse::misc::log(::std::string("expression: ") + expression + '\n');
	if (file)
		data_processors::synapse::misc::log(::std::string("file: ") + file + '\n');
	data_processors::synapse::misc::log("line_number: " + ::std::to_string(line_number) + '\n');
	data_processors::synapse::misc::log.flush();
	abort ();
}
}
#endif

#if 0
// for multimedia scheduler class
#include <Windef.h>
#include <Avrt.h>
#endif
#endif

#include <chrono>

#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/signal_set.hpp>

#include "../misc/sysinfo.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { 
namespace misc { namespace alloc {
#ifndef SYNAPSE_SINGLE_THREADED
alignas(data_processors::synapse::misc::Cache_line_size) 
#endif
::std::atomic_uint_fast64_t static Virtual_allocated_memory_size{0};
uint_fast64_t static Memory_alarm_threshold{static_cast<uint_fast64_t>(data_processors::synapse::misc::Get_system_memory_size() * .9)};
}}
// Placing those here to fill-up spacial efficiency of cacheline above...
namespace synapse { namespace amqp_0_9_1 {
// defaults...
::std::string static host("localhost");
::std::string static port("5672");
::std::string static ssl_host("localhost");
::std::string static ssl_port("5671");
::std::string static ssl_key;
::std::string static ssl_certificate;
bool static Realtime_priority(false);
bool static Tcp_loopback_fast_path(false);
uint_fast64_t static Time_Refresh_Resolution(100 * 1000);
unsigned static Periodic_Logging_Interval(2);
}}
namespace synapse { namespace database {
#ifndef SYNAPSE_SINGLE_THREADED
alignas(data_processors::synapse::misc::Cache_line_size) 
#endif
::std::atomic_uint_fast64_t static Database_size{0};
uint_fast64_t static Database_alarm_threshold{static_cast<uint_fast64_t>(-1)};
int static nice_performance_degradation = 0;
}
namespace amqp_0_9_1 {
unsigned constexpr static page_size = 4096;
}
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

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

#include "../database/topic.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 {
unsigned constexpr static target_buffer_size = 4 * 1024 * 1024;
unsigned constexpr static asio_buffer_size = 8192 * 8;
unsigned constexpr static max_supported_channels_end = 8192;
typedef synapse::database::topic_factory<synapse::amqp_0_9_1::target_buffer_size, synapse::amqp_0_9_1::page_size> topics_type;
::std::unique_ptr<topics_type> static topics;

// more defaults...
// TODO -- better static_assert to make sure that socket's buffer size is larger than database MaxMessageSize with a bit of extra for various end octet, etc.
unsigned static listen_size(1000);
unsigned static so_rcvbuf(-1);
unsigned static so_sndbuf(-1);
unsigned static IO_Service_Concurrency(0);
unsigned static IO_Services_Size(1);
unsigned static IO_Service_Concurrency_Reserve_Multiplier(8);
uint_fast64_t static Reserved_memory_size(0);
uint_fast64_t static Processor_Affinity(0);
bool static Busy_Poll(false);
::std::string static database_root;
::boost::filesystem::path static parent_path;

uint_fast64_t static Start_Of_Run_Timestamp{0};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "server.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 {

#ifdef __WIN32__
bool static run_as_service{false};
::SERVICE_STATUS static service_status;
::SERVICE_STATUS_HANDLE static service_handle; 
char service_name[] = "synapse_server";
void 
service_control_handler(DWORD control_request)
{
	switch (control_request) { 
	case SERVICE_CONTROL_STOP: 
	case SERVICE_CONTROL_SHUTDOWN: 
		service_status.dwCurrentState = SERVICE_STOP_PENDING; 
		service_status.dwControlsAccepted = 0;
		service_status.dwWaitHint = 5 * 60 * 1000;
		::SetServiceStatus(service_handle, &service_status);
		synapse::asio::quitter::get().quit([](){
			synapse::asio::io_service::Stop();
			synapse::misc::log("...exiting at a service-request.\n", true);
		});
	}
}
#endif

void static
default_parse_arg(int const argc, char* argv[], int & i)
{
	if (!::strcmp(argv[i], "--realtime_priority"))
		Realtime_priority = true;
	else if (!::strcmp(argv[i], "--tcp_loopback_fast_path"))
		Tcp_loopback_fast_path = true;
	else if  (!::strcmp(argv[i], "--Busy_Poll"))
		Busy_Poll = true;
	else if (i == argc - 1)
		throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
	else if (!::strcmp(argv[i], "--nice_performance_degradation"))
		synapse::database::nice_performance_degradation = ::boost::lexical_cast<int>(argv[++i]);
	else if (!::strcmp(argv[i], "--log_root"))
		synapse::misc::log.Set_output_root(argv[++i]);
	else if (!synapse::misc::CLI_Option_Parse_Host_Port("--listen_on", i, argv, host, port))
	if (!synapse::misc::CLI_Option_Parse_Host_Port("--ssl_listen_on", i, argv, ssl_host, ssl_port))
	if (!::strcmp(argv[i], "--ssl_key"))
		ssl_key = argv[++i];
	else if (!::strcmp(argv[i], "--ssl_certificate"))
		ssl_certificate = argv[++i];
	else if (!::strcmp(argv[i], "--listen_size"))
		listen_size = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--so_rcvbuf"))
		so_rcvbuf = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--so_sndbuf"))
		so_sndbuf = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--IO_Service_Concurrency"))
#ifndef SYNAPSE_SINGLE_THREADED
		IO_Service_Concurrency = ::boost::lexical_cast<unsigned>(argv[++i]);
#else
		throw ::std::runtime_error("--IO_Service_Concurrency is not supported in this single-threaded build");
#endif
	else if (!::strcmp(argv[i], "--IO_Services_Size")) {
#ifndef SYNAPSE_SINGLE_THREADED
		IO_Services_Size = ::boost::lexical_cast<unsigned>(argv[++i]);

		if (!IO_Services_Size || !synapse::misc::is_po2(IO_Services_Size))
			throw ::std::runtime_error("--IO_Services_Size cannot be less than 1 and must be a power of 2 value");
#else
		throw ::std::runtime_error("--IO_Services_Size is not supported in this single-threaded build");
#endif
	} else if (!::strcmp(argv[i], "--IO_Service_Concurrency_Reserve_Multiplier")) {
#ifndef SYNAPSE_SINGLE_THREADED
		IO_Service_Concurrency_Reserve_Multiplier = ::boost::lexical_cast<unsigned>(argv[++i]);

		if (!IO_Service_Concurrency_Reserve_Multiplier)
			throw ::std::runtime_error("--IO_Service_Concurrency_Reserve_Multiplier cannot be less than 1");
#else
		throw ::std::runtime_error("--IO_Service_Concurrency_Reserve_Multiplier is not supported in this single-threaded build");
#endif
	} else if (!::strcmp(argv[i], "--database_root"))
		database_root = argv[++i];
	else if (!::strcmp(argv[i], "--reserve_memory_size"))
		Reserved_memory_size = synapse::misc::Parse_string_as_suffixed_integral_size<uint_fast64_t>(argv[++i]);
	else if (!::strcmp(argv[i], "--memory_alarm_threshold"))
		data_processors::misc::alloc::Memory_alarm_threshold = synapse::misc::Parse_string_as_suffixed_integral_size<uint_fast64_t>(argv[++i]);
	else if (!::strcmp(argv[i], "--database_alarm_threshold"))
		data_processors::synapse::database::Database_alarm_threshold = synapse::misc::Parse_string_as_suffixed_integral_size<uint_fast64_t>(argv[++i]);
	else if (!::strcmp(argv[i], "--processor_affinity"))
		Processor_Affinity = ::boost::lexical_cast<uint_fast64_t>(argv[++i]);
	else if (!::strcmp(argv[i], "--Time_Refresh_Resolution"))
		Time_Refresh_Resolution = ::boost::lexical_cast<uint_fast64_t>(argv[++i]);
	else if (!::strcmp(argv[i], "--Periodic_Logging_Interval"))
		Periodic_Logging_Interval = ::boost::lexical_cast<uint_fast64_t>(argv[++i]);
	else
		throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');
}

void static
run_(int argc, char* argv[]) 
{
	try{
#ifdef __WIN32__
		::std::atomic_thread_fence(::std::memory_order_acquire);
		if (run_as_service == true) {
			// allow sys-admins to override some parameters via the services applet in control panel
			assert(argc);
			for (int i(1); i != argc; ++i)
				default_parse_arg(argc, argv, i);
			service_handle = ::RegisterServiceCtrlHandler(service_name, (LPHANDLER_FUNCTION)service_control_handler); 
			if (!service_handle) 
				throw ::std::runtime_error("could not register windows service control handler");

			::memset(&service_status, 0, sizeof(service_status));
			service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
			service_status.dwCurrentState = SERVICE_RUNNING; 
			service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

			::SetServiceStatus(service_handle, &service_status);
		}
#endif

		synapse::misc::log(Version_string_with_newline, true);

		if (Processor_Affinity)
			data_processors::synapse::misc::Set_Thread_Processor_Affinity(Processor_Affinity);

		synapse::Large_page_preallocator.Initialize(Reserved_memory_size);

		if (!IO_Service_Concurrency)
			IO_Service_Concurrency = synapse::misc::concurrency_hint();

		// create based on chosen runtime parameters
		asio::io_service::init(IO_Service_Concurrency, IO_Services_Size);

		// Protect. 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates
		if (Time_Refresh_Resolution < 100 * 1000)
			Time_Refresh_Resolution = 100 * 1000; 

		synapse::misc::cached_time::init(Time_Refresh_Resolution);
		amqp_0_9_1::Start_Of_Run_Timestamp = synapse::misc::cached_time::get();
		synapse::misc::log("Finest-possible deployed Time_Refresh_Resolution(=" + ::std::to_string(Time_Refresh_Resolution) + ")\n", true);

		// Protect. 2 seconds
		if (Periodic_Logging_Interval < 2) 
			Periodic_Logging_Interval = 2;
		synapse::misc::log("Periodic_Logging_Interval(=" + ::std::to_string(Periodic_Logging_Interval) + ")\n", true);

		// database directory...
		{
			boost::filesystem::path path;
			if (database_root.empty() == false)
				path = database_root;
			else
				path = parent_path.parent_path() / ("." + parent_path.filename().string() + ".tmp");

			if (synapse::database::Database_alarm_threshold == static_cast<uint_fast64_t>(-1)) 
				synapse::database::Database_alarm_threshold = ::boost::filesystem::space(::boost::filesystem::absolute(path).root_path()).capacity * .9;

			if (!::boost::filesystem::exists(path))
				::boost::filesystem::create_directories(path);
			topics.reset(new amqp_0_9_1::topics_type(path.string()));
		}

		// Automatic quitting handling
		// ... signal based (mostly for unix, but also on Windows when ran from proper consoles (not msys or cygwin which intercept handling of CTRL-C and play-around w.r.t. expected behaviour))
		synapse::asio::Autoquit_on_signals Signal_quitter;
#if !defined(GDB_STDIN_BUG) && !defined(SYNAPSE_SINGLE_THREADED)
		// the following quitting handling is for user-based keypress interaction which should work on any platform (windows, linux etc.). 
		synapse::asio::Autoquit_on_keypress Keypress_quitter;
#endif

		synapse::misc::log("Listening in cleartext on " + host + ':' + port + '\n', true);
		typedef synapse::asio::server_1<amqp_0_9_1::connection_1<synapse::asio::tcp_socket, amqp_0_9_1::asio_buffer_size>> server_type;
		::boost::shared_ptr<server_type> server(new server_type(host, port, listen_size, so_rcvbuf, so_sndbuf, Tcp_loopback_fast_path));
		server->add_quit_handler();
		server->async_accept();

		if (!ssl_key.empty() || !ssl_certificate.empty()) {
			if (ssl_host.empty() || ssl_port.empty() || ssl_key.empty() || ssl_certificate.empty())
				throw ::std::runtime_error("not all SSL options have been specified. mandatory:{--ssl_key, --ssl_certificate} optional: --ssl_listen_on");

			synapse::misc::log("Listening in SSL on " + host + ':' + port + '\n', true);
			synapse::asio::io_service::init_ssl_context();

			auto && ssl(synapse::asio::io_service::get_ssl_context());
			ssl.use_certificate_file(ssl_certificate, basio::ssl::context::pem);
			ssl.use_private_key_file(ssl_key, basio::ssl::context::pem);
	
			typedef synapse::asio::server_1<amqp_0_9_1::connection_1<synapse::asio::ssl_tcp_socket, amqp_0_9_1::asio_buffer_size>> ssl_server_type;
			::boost::shared_ptr<ssl_server_type> ssl_server(new ssl_server_type(ssl_host, ssl_port, listen_size, so_rcvbuf, so_sndbuf, Tcp_loopback_fast_path));
			ssl_server->add_quit_handler();
			ssl_server->async_accept();
		}

		if (Realtime_priority && (!::SetPriorityClass(::GetCurrentProcess(), REALTIME_PRIORITY_CLASS) || !::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)))
			throw ::std::runtime_error("SetPriorityClass(=REALTIME_PRIORITY_CLASS) || SetThreadPriority(=THREAD_PRIORITY_TIME_CRITICAL)");

		#ifndef SYNAPSE_SINGLE_THREADED
		//::SetThreadAffinityMask(::GetCurrentThread(), 1);
		::std::vector<::std::thread> pool;
		auto const Threads_Per_IO_Service(IO_Service_Concurrency * IO_Service_Concurrency_Reserve_Multiplier);
		assert(IO_Services_Size);
		pool.reserve(IO_Services_Size * Threads_Per_IO_Service - 1);
		// unsigned thread_i(0);
		for (unsigned IO_Service_Index(0); IO_Service_Index != IO_Services_Size; ++IO_Service_Index) {
			for (unsigned i(IO_Service_Index ? 0 : 1); i != Threads_Per_IO_Service; ++i) {
				pool.emplace_back([
					//thread_i, 
					IO_Service_Index
				]()->void{
					#if defined(_MSC_VER) && !defined(__clang__)
						// Note -- this is not really correct at all. It is non-conforming Visual C++ stuff:
						// https://connect.microsoft.com/VisualStudio/Feedback/Details/1353306
						// and even then e.what() (albeit ok, implementation defined) is not very informative as compared to GCC (in VS just says 'bad exception').
						::std::set_terminate(Non_conforming_msvc_hack);
					#endif
					#ifdef __WIN32__
						if (Processor_Affinity)
							data_processors::synapse::misc::Set_Thread_Processor_Affinity(Processor_Affinity);
						if (Realtime_priority && !::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
							throw ::std::runtime_error("SetThreadPriority(=THREAD_PRIORITY_TIME_CRITICAL)");
						//::SetThreadAffinityMask(::GetCurrentThread(), (1 << (thread_i % IO_Service_Concurrency)));
						//DWORD taskIndex = 0;
						//::AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
					#endif
					synapse::asio::run(IO_Service_Index, Busy_Poll);
				});
				//++thread_i;
			}	
		}
		#endif

		::boost::asio::deadline_timer t(synapse::asio::io_service::get(), ::boost::posix_time::seconds(Periodic_Logging_Interval));
		::std::chrono::high_resolution_clock::time_point then(::std::chrono::high_resolution_clock::now());
		uint_fast64_t prev_written(0);
		uint_fast64_t prev_read(0);
		uint_fast64_t prev_read_payload(0);
		uint_fast64_t Previous_Written_Size(0);
		::std::function<void(::boost::system::error_code const &)> on_reporting_timeout = [&t, &on_reporting_timeout, &then, &prev_written, &prev_read, &prev_read_payload, &Previous_Written_Size](::boost::system::error_code const & ec){
			if (!ec) {
				::std::chrono::high_resolution_clock::time_point const now(::std::chrono::high_resolution_clock::now());
				uint_fast64_t const microseconds(::std::chrono::duration_cast< ::std::chrono::microseconds >(now - then).count());
				if (microseconds) {
					then = now;

					::std::string report;

					uint_fast64_t static Previous_Written_Size_Delta(-1);
					uint_fast64_t tmp_counter(synapse::asio::Written_Size.load(::std::memory_order_relaxed));
					auto const Written_Size_Delta(tmp_counter - Previous_Written_Size);
					Previous_Written_Size = tmp_counter;

					uint_fast64_t static prev_written_delta(-1);
					tmp_counter = synapse::amqp_0_9_1::total_written_messages.load(::std::memory_order_relaxed);
					auto const written_delta(tmp_counter - prev_written);
					if (written_delta != prev_written_delta) {
						report += "write Hz(=" + ::std::to_string((prev_written_delta = written_delta) * 1000000 / microseconds) + ')';
						// Putting it here for raw writes, because otherwise it would get triggered even on the heartbeat IO thereby producing a lot of log lines...
						if (Written_Size_Delta != Previous_Written_Size_Delta)
							report += " written raw MBps(=" + ::std::to_string(static_cast<double>(Previous_Written_Size_Delta = Written_Size_Delta) / microseconds) + ')';
					}
					prev_written = tmp_counter;

					uint_fast64_t static prev_read_delta(-1);
					tmp_counter = synapse::amqp_0_9_1::total_read_messages.load(::std::memory_order_relaxed);
					auto const read_delta(tmp_counter - prev_read);
					if (read_delta != prev_read_delta)
						report += " read Hz(=" + ::std::to_string((prev_read_delta = read_delta) * 1000000 / microseconds) + ')';
					prev_read = tmp_counter;

					uint_fast64_t static prev_read_payload_delta(-1);
					tmp_counter = synapse::amqp_0_9_1::total_read_payload.load(::std::memory_order_relaxed);
					auto const read_payload_delta(tmp_counter - prev_read_payload);
					if (read_payload_delta != prev_read_payload_delta)
						report += " read MBps(=" + ::std::to_string(static_cast<double>(prev_read_payload_delta = read_payload_delta) / microseconds) + ')';
					prev_read_payload = tmp_counter;

					if (synapse::asio::bad_buffering == true) {
						report += " bad buffering";
						synapse::asio::bad_buffering = false;
					}

					uint_fast64_t static Prev_memory_state_approximation(-1);
					tmp_counter = data_processors::misc::alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed);
					if (tmp_counter != Prev_memory_state_approximation)
						report += " Memory state approximation (MB=" + ::std::to_string(tmp_counter * 0.000001) + ')';
					Prev_memory_state_approximation = tmp_counter;

					uint_fast64_t static Prev_database_state_approximation(-1);
					tmp_counter = database::Database_size.load(::std::memory_order_relaxed);
					if (tmp_counter != Prev_database_state_approximation)
						report += " Database state approximation (MB=" + ::std::to_string(tmp_counter * 0.000001) + ')';
					Prev_database_state_approximation = tmp_counter;

					uint_fast64_t static Previous_Connections_Size(0);
					auto const Connections_Size(synapse::asio::Created_Connections_Size.load(::std::memory_order_relaxed) - synapse::asio::Destroyed_Connections_Size.load(::std::memory_order_relaxed));
					if (Connections_Size != Previous_Connections_Size && Connections_Size)
						report += " connections(=" + ::std::to_string((Previous_Connections_Size = Connections_Size) - 1) + ')'; // less by one because server always news one to setup pending async_accept() call

					if (report.empty() == false)
						synapse::misc::log(report + '\n', true);
				}

				t.expires_at(t.expires_at() + ::boost::posix_time::seconds(Periodic_Logging_Interval));
				t.async_wait(on_reporting_timeout);
			}
		};
		t.async_wait(on_reporting_timeout);
		synapse::asio::run(0, Busy_Poll);

#ifndef SYNAPSE_SINGLE_THREADED
		for (auto & i: pool)
			i.join();
#endif
	}
	catch (::std::exception const & e)
	{
#ifdef __WIN32__
		if (run_as_service == true)
			service_status.dwWin32ExitCode = -1;
#endif
		synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
		synapse::asio::Exit_error = true;
	}
#ifdef __WIN32__
	if (run_as_service == true) {
		service_status.dwCurrentState = SERVICE_STOPPED; 
		service_status.dwControlsAccepted = 0;
		::SetServiceStatus(service_handle, &service_status);
	}
#endif
	synapse::misc::log("Bye bye. Memory estimated(ish) status(MB): " + ::std::to_string(data_processors::misc::alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) * 0.000001) + " Database estimated(ish) status(MB): " + ::std::to_string(synapse::database::Database_size.load(::std::memory_order_relaxed) * 0.000001) + '\n', true);
}

int static
run(int argc, char* argv[])
{
	try
	{
		// CLI...
		parent_path = argv[0];
		for (int i(1); i != argc; ++i) {
#ifdef __WIN32__
			if (!::strcmp(argv[i], "--run_as_service"))
				run_as_service = true;
			else 
#endif
			if (!::strcmp(argv[i], "--version")) {
				::std::cerr << Version_string_with_newline;
				return 0;
			} else
				default_parse_arg(argc, argv, i);
		}

#ifdef __WIN32__
		if (run_as_service == true) {
			::SERVICE_TABLE_ENTRY dispatch_table[2];
			::memset(dispatch_table, 0, sizeof(dispatch_table));
			dispatch_table[0].lpServiceName = ::data_processors::synapse::amqp_0_9_1::service_name;
			dispatch_table[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)::data_processors::synapse::amqp_0_9_1::run_;
			::std::atomic_thread_fence(::std::memory_order_release);
			return !::StartServiceCtrlDispatcher(dispatch_table);
		}
#endif
		run_(0, nullptr); // dummy for the sake of windows nonsense...
	}
	catch (::std::exception const & e)
	{
		synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
		synapse::asio::Exit_error = true;
	}
	return synapse::asio::Exit_error;
}

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[])
{
	#if !defined(NDEBUG) && defined(__WIN32__)
		#ifndef DATA_PROCESSORS_SYNAPSE_TEST_ALLOW_SILENT_CRASH
			::SetErrorMode(0);
		#else
			::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
			#ifdef _MSC_VER
				_set_abort_behavior(0, _WRITE_ABORT_MSG);
			#endif
		#endif
	#endif
	#if defined(_MSC_VER) && !defined(__clang__)
		::std::set_terminate(Non_conforming_msvc_hack);
	#endif
	return ::data_processors::synapse::amqp_0_9_1::run(argc, argv);
}
