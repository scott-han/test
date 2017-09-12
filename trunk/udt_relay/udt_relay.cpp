/*
	 NOTE -- for the time being this is NOT lockless, performance-oriented, code. this relay/tunnel is mainly for getting data across large-distance and large BDP WAN networks (not intended to compete at LAN levels).
	 a future TODO is to do what Synapse does -- namely use lockless progressing of writing to a buffer, advancing atomic 'head pointer' and allowing reading strand/socket to catch up at its own leasure...

	 Currently however only the 'queue-posting' (back and forth) processing is used (to save time on coding/hacking and getting to actually testing for International connectivity issues). Such queue-posting ought to only happen, ideally, when the 'atomic head pointer' has been reached by the reader, etc.

	 Moreover, it ought to be possible to 'buffer-up/accumulated' small messages (so as not to pay too much of an overhead for small packets), but this is true for a general protocol-level functioning... may be will improve on later...

	 So, in other words, current code is not too fast at all by LAN standards.

	 Also, thu UDT library itself (3rd party) may not be coded 100% in the optimized way. The scoped locks could be migrated from mutex to hybrid spin-locked-CriticalSection(Ex) (on Windows of course -- it allows to tune spinlok prior to jumping into the kernel on contention). Even more so -- locks way be better left off eliminated altogether and exchanged for 'strand' semantics (so something like a multixplexer would have its own strand, etc.) ... all of this of course takes development time, so will postpone till later... 

	 Having said this, Synapse can be already refactored to talk UDT and is fast by LAN standards, so there is also an option to make amqp clients (such as mirror maker) talk udt directly if/when needed thereby bypassing the need for a relay altogether.
*/

#include <iostream>

#ifdef __WIN32__
#ifdef __MINGW64__
#define _assert assert_hack
// essentially a hack so that we flush the err stream -- when running as service the assertion failiers are not printed out... if app is build on mingw64 (in wheir wassert.c they don't call fflush on stderr before calling abort...)
extern "C" {
void __cdecl
assert_hack(const char * expression, const char * file, unsigned line_number)
{
	::std::cerr << "ASSERTION FAILIER (custom hack)!\n";
	if (expression)
		::std::cerr << "expression: " << expression << '\n';
	if (file)
		::std::cerr << "file: " << file << '\n';
	::std::cerr << "line_number: " << line_number << '\n';
	::std::cerr.flush();
	abort ();
}
}
#endif
#endif

#include "../misc/sysinfo.h"
#include "../asio/server.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace udt_relay {

// defaults...
unsigned constexpr static asio_buffer_size(1024 * 1024 * 8);
::std::string static protocol;
::std::string static host("localhost");
::std::string static port("5672");
::std::string static peer_protocol;
::std::string static peer_host("localhost");
::std::string static peer_port("8672");
unsigned static listen_size(1000);
unsigned static so_rcvbuf(-1);
unsigned static so_sndbuf(-1);
unsigned static concurrency_hint(0);
bool error{false};

template <typename SocketType, typename ServerConnection, unsigned MaxBufferSize>
struct client_connection final : synapse::asio::connection_1<SocketType, MaxBufferSize, 0, client_connection<SocketType, ServerConnection, MaxBufferSize>, ServerConnection> {

	typedef synapse::asio::connection_1<SocketType, MaxBufferSize, 0, client_connection<SocketType, ServerConnection, MaxBufferSize>, ServerConnection> base_type;

	using base_type::shared_from_this;
	using base_type::put_into_error_state;
	using base_type::strand;
	using base_type::data;
	using base_type::bytes;

	::boost::shared_ptr<ServerConnection> server;

	client_connection()
	: base_type(1) // could be zero really (a pedantic clarification todo for later on in time)
	{ 
		base_type::get_socket().open();
		if (so_rcvbuf != static_cast<decltype(so_rcvbuf)>(-1))
			base_type::get_socket().set_option(::boost::asio::socket_base::receive_buffer_size(so_rcvbuf)); // 8192
		if (so_sndbuf != static_cast<decltype(so_sndbuf)>(-1))
			base_type::get_socket().set_option(::boost::asio::socket_base::send_buffer_size(so_sndbuf)); // 8192
	}

	//~~~ dtor
	~client_connection()
	{
	}
	//```

	void
	put_into_error_state(::std::exception const & e) 
	{
		if (base_type::get_async_error() == false) {
			base_type::put_into_error_state(e);
			server->async_put_into_error_state(e);
			server.reset();
		}
	}

	void
	async_put_into_error_state(::std::exception const & e)
	{
		auto this_(shared_from_this());
		strand.dispatch([this_, this, e]() {
			put_into_error_state(e);
		});
	}

	void
	on_connect()
	{
		assert(strand.running_in_this_thread());
		base_type::template read_some<&client_connection::on_read_some>(MaxBufferSize);
		server->async_read_some();
	}

	void
	on_read_some()
	{
		assert(bytes);
		server->async_write(data, bytes);
	}

	void
	async_write(char unsigned * bfr, unsigned bytes)
	{
		auto this_(shared_from_this());
		strand.dispatch([this_, this, bfr, bytes]() {
			base_type::template write<&ServerConnection::on_client_written>(server.get(), bfr, bytes);
		});
	}

	void
	on_server_written()
	{
		auto this_(shared_from_this());
		strand.dispatch([this_, this]() {
			base_type::template read_some<&client_connection::on_read_some>(MaxBufferSize);
		});
	}

};

template <typename MySocketType, typename PeerSocketType, unsigned MaxBufferSize>
struct server_connection final : synapse::asio::connection_1<MySocketType, MaxBufferSize, 0, server_connection<MySocketType, PeerSocketType, MaxBufferSize>, client_connection<PeerSocketType, server_connection<MySocketType, PeerSocketType, MaxBufferSize>, MaxBufferSize>> {

	typedef synapse::asio::connection_1<MySocketType, MaxBufferSize, 0, server_connection, client_connection<PeerSocketType, server_connection, MaxBufferSize>> base_type;

	using base_type::shared_from_this;
	using base_type::put_into_error_state;
	using base_type::strand;
	using base_type::data;
	using base_type::bytes;

	typedef client_connection<PeerSocketType, server_connection, MaxBufferSize> client_type;

	::boost::shared_ptr<client_type> client{new client_type};

	server_connection()
	: base_type(1) // could be zero really (a pedantic clarification todo for later on in time)
	{ 
	}

	~server_connection()
	{
	}

	void
	put_into_error_state(::std::exception const & e) 
	{
		if (base_type::get_async_error() == false) {
			base_type::put_into_error_state(e);
			client->async_put_into_error_state(e);
			client.reset();
		}
	}

	void
	async_put_into_error_state(::std::exception const & e)
	{
		auto this_(shared_from_this());
		strand.dispatch([this_, this, e]() {
			put_into_error_state(e);
		});
	}

	void
	on_connect()
	{
		assert(strand.running_in_this_thread());
		base_type::on_connect();
		client->server = shared_from_this();
		client->async_connect(client, peer_host, peer_port);
	}

	void
	async_read_some()
	{
		auto this_(shared_from_this());
		strand.dispatch([this_, this]() {
			base_type::template read_some<&server_connection::on_read_some>(MaxBufferSize);
		});
	}

	void
	on_read_some()
	{
		assert(bytes);
		client->async_write(data, bytes);
	}

	void
	async_write(char unsigned * bfr, unsigned bytes)
	{
		auto this_(shared_from_this());
		strand.dispatch([this_, this, bfr, bytes]() {
			base_type::template write<&client_type::on_server_written>(client.get(), bfr, bytes);
		});
	}

	void
	on_client_written()
	{
		async_read_some();
	}
};

// TODO -- note a lot of this code is shared (verbatim) with the synapse server.cpp (like 'run as service' management, as well as single-threaded deployment etc.) -- so may be move into the shared library (if this udt_relay and/or mirror_maker will become of use)...

// quitting handling
::std::atomic_flag static quit_requested = ATOMIC_FLAG_INIT;

#ifdef __WIN32__
bool static run_as_service{false};
::SERVICE_STATUS static service_status;
::SERVICE_STATUS_HANDLE static service_handle; 
char service_name[] = "udt_relay";
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
			synapse::asio::io_service::get().stop();
			synapse::misc::log("...exiting at a service-request.\n", true);
		});
	}
}
#endif

void static
default_parse_arg(int const argc, char* argv[], int & i)
{
	if (i == argc - 1)
		throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
	else if (!::strcmp(argv[i], "--protocol"))
		protocol = argv[++i];
	else if (!::strcmp(argv[i], "--peer_protocol"))
		peer_protocol = argv[++i];
	else if (!synapse::misc::CLI_Option_Parse_Host_Port("--listen_on", i, argv, host, port))
	if (!synapse::misc::CLI_Option_Parse_Host_Port("--peer_at", i, argv, peer_host, peer_port))
	if (!::strcmp(argv[i], "--listen_size"))
		listen_size = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--so_rcvbuf"))
		so_rcvbuf = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--so_sndbuf"))
		so_sndbuf = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--concurrency_hint"))
#ifndef SYNAPSE_SINGLE_THREADED
		concurrency_hint = ::boost::lexical_cast<unsigned>(argv[++i]);
#else
		throw ::std::runtime_error("--concurrency_hint is not supported in this single-threaded build");
#endif
	else
		throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');
}

template <typename ServerType>
void
start_server()
{
	::boost::shared_ptr<ServerType> server(new ServerType(host, port, listen_size, so_rcvbuf, so_sndbuf));
	server->add_quit_handler();
	server->async_accept();
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

		if (protocol.empty() || peer_protocol.empty())
			throw ::std::runtime_error("--protocol and --peer_protocol are both mandatory options");

		synapse::misc::log(">>>>>>> BUILD info: " BUILD_INFO " main source: " __FILE__ ". date: " __DATE__ ". time: " __TIME__ ".\n", true);

		if (!concurrency_hint)
			concurrency_hint = synapse::misc::concurrency_hint();

		// create based on chosen runtime parameters
		asio::io_service::init(concurrency_hint);
		synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		// quitting handling
		// ... signal based (mostly for unix, but also on Windows when ran from proper consoles (not msys or cygwin which intercept handling of CTRL-C and play-around w.r.t. expected behaviour))
		::boost::asio::signal_set quit_signals(synapse::asio::io_service::get(), SIGINT, SIGTERM);
		quit_signals.async_wait([](::boost::system::error_code const & e, int){
			if (!e) {
				auto const prev(quit_requested.test_and_set());
				if (prev == false) {
					synapse::asio::quitter::get().quit([](){
						synapse::asio::io_service::get().stop();
						synapse::misc::log("...exiting on signal.\n", true);
					});
				}
			}
		});
#ifndef GDB_STDIN_BUG
		// the following quitting handling is for user-based keypress interaction which should work on any platform (windows, linux etc.). 
		// the above SIGNAL-based handling still remains as such may be used on unix systems when prcesses are runnig as services/daemons
		::std::thread quit_keyboard_monitor([](){
			char ch;
			while (::std::cin.get(ch)) {
				if (ch == '!') {
					auto const prev(quit_requested.test_and_set());
					if (prev == false) {
						synapse::asio::quitter::get().quit([](){
							synapse::asio::io_service::get().stop();
							synapse::misc::log("...exiting on keypress interaction.\n", true);
						});
					}
				}
			}
		});
		quit_keyboard_monitor.detach();
#endif

		//
		if (protocol == "tcp" && peer_protocol == "udt")
			start_server<synapse::asio::server_1<udt_relay::server_connection<synapse::asio::tcp_socket, synapse::asio::udt_socket<>, udt_relay::asio_buffer_size>>>();
		else if (protocol == "udt" && peer_protocol == "tcp")
			start_server<synapse::asio::server_1<udt_relay::server_connection<synapse::asio::udt_socket<>, synapse::asio::tcp_socket, udt_relay::asio_buffer_size>>>();
		else
			throw ::std::runtime_error("unsupported protocol/peer_protocol combination");

#ifndef SYNAPSE_SINGLE_THREADED
		//::SetThreadAffinityMask(::GetCurrentThread(), 1);
		::std::vector< ::std::thread> pool(concurrency_hint * 3 - 1);
		//unsigned thread_i(1);
		for (auto & i : pool) {
			i = ::std::thread([//thread_i, 
			]()->void{

#ifdef __WIN32__
				//::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
				//::SetThreadAffinityMask(::GetCurrentThread(), (1 << (thread_i % concurrency_hint)));
				//DWORD taskIndex = 0;
				//::AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
#endif
				asio::run();
			});
			//++thread_i;
		}
#endif

		asio::run();

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
		::std::cerr << "oops: " << e.what() << ::std::endl;
		error = true;
	}
#ifdef __WIN32__
	if (run_as_service == true) {
		service_status.dwCurrentState = SERVICE_STOPPED; 
		service_status.dwControlsAccepted = 0;
		::SetServiceStatus(service_handle, &service_status);
	}
#endif
	synapse::misc::log("Bye bye.\n", true);
}

int static
run(int argc, char* argv[])
{
	try
	{
		// CLI...
		for (int i(1); i != argc; ++i) {
#ifdef __WIN32__
			if (!::strcmp(argv[i], "--run_as_service"))
				run_as_service = true;
#endif
			else
				default_parse_arg(argc, argv, i);
		}

#ifdef __WIN32__
		if (run_as_service == true) {
			::SERVICE_TABLE_ENTRY dispatch_table[2];
			::memset(dispatch_table, 0, sizeof(dispatch_table));
			dispatch_table[0].lpServiceName = ::data_processors::synapse::udt_relay::service_name;
			dispatch_table[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)::data_processors::synapse::udt_relay::run_;
			::std::atomic_thread_fence(::std::memory_order_release);
			return !::StartServiceCtrlDispatcher(dispatch_table);
		}
#endif
		run_(0, nullptr); // dummy for the sake of windows nonsense...
	}
	catch (::std::exception const & e)
	{
		synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
		::std::cerr << "oops: " << e.what() << ::std::endl;
	}
	return !error;
}
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[])
{
	return ::data_processors::synapse::udt_relay::run(argc, argv);
}
