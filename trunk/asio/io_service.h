#include <atomic>
#include <thread>
#include <list>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "../misc/log.h"
#include "../misc/misc.h"
#include "../misc/Cache_Info.h"

#ifndef DATA_PROCESSORS_SYNAPSE_ASIO_IO_SERVICE_H
#define DATA_PROCESSORS_SYNAPSE_ASIO_IO_SERVICE_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio {

namespace basio = ::boost::asio;

namespace io_service {
	// Todo, if wanting to use the codebase in a non-header-only lib, then will need to move this into a conditional header (then used/or-explicitly-declared-instead by the main.cpp in some fashion).
	::std::vector<::std::unique_ptr<basio::io_service>> static IO_Services;
	::std::vector<::std::unique_ptr<basio::io_service::work>> static IO_Services_Work;
	basio::ssl::context static * ssl_context_instance{nullptr};

	void static inline init(unsigned concurrency_hint, unsigned IO_Services_Size = 1u) {
		assert(IO_Services_Size);
		assert(misc::is_po2(IO_Services_Size));
		assert(IO_Services.empty());
		IO_Services.reserve(IO_Services_Size);
		if (IO_Services_Size != 1u)
			IO_Services_Work.reserve(IO_Services_Size);
		for (unsigned i(0); i != IO_Services_Size; ++i) {
			IO_Services.emplace_back(new basio::io_service(concurrency_hint));
			if (IO_Services_Size != 1u)
				IO_Services_Work.emplace_back(new basio::io_service::work(*IO_Services.back()));
		}
	}

	bool static inline Is_initialized() {
		return !IO_Services.empty();
	}

	basio::io_service static inline & get(unsigned const IO_Service_Index) {
		assert(IO_Services.size() > IO_Service_Index);
		return *IO_Services[IO_Service_Index];
	}

	basio::io_service static inline & get() {
		assert(!IO_Services.empty());
		assert(misc::is_po2(IO_Services.size()));
		#ifndef SYNAPSE_SINGLE_THREADED
		alignas(data_processors::synapse::misc::Cache_line_size) 
		#endif
		::std::atomic_uint static IO_Service_Index{static_cast<unsigned>(-1)};
		return io_service::get(++IO_Service_Index & IO_Services.size() - 1);
	}

	void static inline init_ssl_context(basio::ssl::context::method m = ::boost::asio::ssl::context::sslv23) {
		assert(!IO_Services.empty());
		assert(ssl_context_instance == nullptr);
		// TODO -- may be also have a pool of SSL contexts! (no time to re-test/implement at this point).
		ssl_context_instance = new basio::ssl::context(io_service::get(), m);
	}

	basio::ssl::context static inline & get_ssl_context() {
		assert(ssl_context_instance != nullptr);
		return *ssl_context_instance;
	}

	void static inline Stop() {
		for (auto && IO_Service_Work : IO_Services_Work) 
			IO_Service_Work.reset();
		for (auto && IO_Service : IO_Services)
			IO_Service->stop();
	}
}


void static inline
run(unsigned const IO_Service_Index = 0, bool const Busy_Poll = false)
{
	// todo -- think about further refactoring and do not loop, as may end up expecting all of the exceptions to be really handled in individual handlers
	while (!0) {
		try {
			auto && Service(io_service::get(IO_Service_Index));
			#ifdef DATA_PROCESSORS_BOOST_ASIO_HACKS
				// This will likely be the default in future...
				Service.impl_.Busy_Poll = Busy_Poll;
				Service.run();
			#else
				// Workaround for the time being (until the above is ready). May need explicit stopping of the IO mechanisms, as opposed to exiting naturally if/when no more work remains...
				if (!Busy_Poll)
					Service.run();
				else 
					do Service.poll(); while (!Service.stopped());
			#endif
			break;
		} catch (::std::bad_alloc const &) {
			throw;
		} catch (::std::exception const & e) {
			synapse::asio::Exit_error = true;
			synapse::misc::log(::std::string("exception: ") + e.what() + '\n', true);
		} catch (...) {
			synapse::asio::Exit_error = true;
			synapse::misc::log("unknown exception\n", true);
		}
		synapse::asio::io_service::Stop();
	}
}

bool static Quitter_Deinitialization_Fiasco{false};
class quitter {
public:
	struct Scoped_Membership_Registration_Type;
	~quitter() {
		Quitter_Deinitialization_Fiasco = true;
	}

private:
	::boost::asio::strand strand{io_service::get()};
	::std::list<::std::function<void (Scoped_Membership_Registration_Type &&)>> on_quit_handlers;
	::boost::asio::deadline_timer t{io_service::get()};
	unsigned Is_Quitting_In_Progress{false};

	template <typename CallbackType>
	void
	monitor_quitting_progress(CallbackType && callback)
	{
		assert(strand.running_in_this_thread());
		assert(Is_Quitting_In_Progress);
		if (quit_fence_size || on_quit_handlers.empty() == false) {
			for (auto & i : on_quit_handlers)
				i(Scoped_Membership_Registration_Type());
			on_quit_handlers.clear();
			assert(on_quit_handlers.empty() == true);
			t.expires_from_now(::boost::posix_time::seconds(1));
			t.async_wait(strand.wrap([this, callback(::std::forward<CallbackType>(callback))](::boost::system::error_code const &) mutable {
				monitor_quitting_progress(::std::move(callback));
			}));
		} else {
			callback();
		}
	}

public:
	quitter static &
	get()
	{
		quitter static q;
		return q;
	}

	::std::atomic<uint_fast64_t> quit_fence_size{0};

	struct Scoped_Membership_Registration_Type {
		bool Is_Valid_Iterator{false};
		decltype(on_quit_handlers)::const_iterator In_Membership_Iterator;
		Scoped_Membership_Registration_Type(decltype(on_quit_handlers)::const_iterator const & Iterator) : Is_Valid_Iterator(true), In_Membership_Iterator(Iterator) {
		}
		Scoped_Membership_Registration_Type() = default;
		Scoped_Membership_Registration_Type(Scoped_Membership_Registration_Type && X) : Is_Valid_Iterator(X.Is_Valid_Iterator), In_Membership_Iterator(X.In_Membership_Iterator) {
			X.Is_Valid_Iterator = false;
		}
		Scoped_Membership_Registration_Type & operator=(Scoped_Membership_Registration_Type && X) {
			Is_Valid_Iterator = X.Is_Valid_Iterator;
			In_Membership_Iterator = X.In_Membership_Iterator;
			X.Is_Valid_Iterator = false;
			return *this;
		}
		~Scoped_Membership_Registration_Type() {
			if (Is_Valid_Iterator && !Quitter_Deinitialization_Fiasco) {
				auto & Quitter(quitter::get());
				Quitter.strand.post([&Quitter, Iterator(In_Membership_Iterator)]() {
					if (!Quitter.on_quit_handlers.empty()) {
						Quitter.on_quit_handlers.erase(Iterator);
					}
				});
			}
		}
		Scoped_Membership_Registration_Type(Scoped_Membership_Registration_Type const &);
		Scoped_Membership_Registration_Type & operator=(Scoped_Membership_Registration_Type const &);
		operator bool() const {
			return Is_Valid_Iterator;
		}
	};

	template <typename CallbackType>
	void 
	add_quit_handler(CallbackType && handler)
	{
		strand.post([this, handler(::std::forward<CallbackType>(handler))]() mutable {
			if (!Is_Quitting_In_Progress) {
				on_quit_handlers.emplace_front(::std::move(handler));
				on_quit_handlers.front()(Scoped_Membership_Registration_Type(on_quit_handlers.cbegin()));
			} else
				handler(Scoped_Membership_Registration_Type());
		});
	}

	template <typename CallbackType>
	void
	quit(CallbackType && callback)
	{
		strand.post([this, callback(::std::forward<CallbackType>(callback))]() mutable {
			if (!Is_Quitting_In_Progress) {
				Is_Quitting_In_Progress = true;
				synapse::misc::log("Initiating safe exit...\n", true);
				synapse::misc::log("...awaiting safe exit conditions\n", true);
				t.expires_from_now(::boost::posix_time::seconds(1));
				t.async_wait(strand.wrap([this, callback(::std::move(callback))](::boost::system::error_code const &) mutable {
					monitor_quitting_progress(::std::move(callback));
				}));
			}
		});
	}

	// Mainly for the logger (catch 22) if logger itself cannot do the logging it wants the whole app to stop... but would be nice not to terminate abbruptly and give rest of the stuff a chance to 'save' topic data if possible...
	void Quit_silently() {
		strand.post([this]() mutable {
			if (!Is_Quitting_In_Progress) {
				Is_Quitting_In_Progress = true;
				t.expires_from_now(::boost::posix_time::seconds(1));
				t.async_wait(strand.wrap([this](::boost::system::error_code const &) mutable {
					monitor_quitting_progress([](){});
				}));
			}
		});
	}
};

// Quitting handling types...

// Todo: make it more robust w.r.t. non-singular (multiple translation units) build targets... will essentially force to create own lib sub-packages...
::std::atomic_flag static quit_requested = ATOMIC_FLAG_INIT;

// SIGNAL-based handling may be used on unix systems when prcesses are runnig as services/daemons as well as on native windows console (cmd.exe) with something like Ctrl-C
struct Autoquit_on_signals {

	::boost::asio::signal_set quit_signals{synapse::asio::io_service::get(), SIGINT, SIGTERM};

	Autoquit_on_signals() {
		// ... signal based (mostly for unix, but also on Windows when ran from proper consoles (not msys or cygwin which intercept handling of CTRL-C and play-around w.r.t. expected behaviour))
		quit_signals.async_wait([](::boost::system::error_code const & e, int){
			if (!e) {
				auto const prev(quit_requested.test_and_set());
				if (prev == false) {
					synapse::asio::quitter::get().quit([](){
						synapse::asio::io_service::Stop();
						synapse::misc::log("...exiting on signal.\n", true);
					});
				}
			}
		});
	}
};

// The following quitting handling is for user-based keypress interaction which should work on any platform (windows, linux etc.). 
#ifndef SYNAPSE_SINGLE_THREADED

::std::thread static quit_keyboard_monitor;
struct Autoquit_on_keypress {
	void static Cleanup_On_Exit() {
		if (quit_keyboard_monitor.joinable()) {
			quit_requested.test_and_set();
			#ifdef __WIN32__
				::CloseHandle(::GetStdHandle(STD_INPUT_HANDLE));
			#else
				#error "Not yet. Implement closing std::cin from a thread first."
			#endif
			quit_keyboard_monitor.join();
		}
	}
	Autoquit_on_keypress() {
		// Cant really detach for as long as the detached thread is using static vars.
		//quit_keyboard_monitor.detach();
		if (::atexit(Cleanup_On_Exit))
			throw ::std::runtime_error("Autoquit_on_keypress() atexit registration");
		quit_keyboard_monitor = ::std::thread([](){
			char ch;
			while (::std::cin.get(ch)) {
				if (ch == '!') {
					auto const prev(quit_requested.test_and_set());
					if (prev == false) {
						synapse::asio::quitter::get().quit([](){
							synapse::asio::io_service::Stop();
							synapse::misc::log("...exiting on keypress interaction.\n", true);
						});
					}
					break;
				}
			}
		});
	}
};

#endif

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../misc/log.tcc"
#endif
