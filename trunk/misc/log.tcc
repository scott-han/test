/*
Needed because of circular catch22 dependency: quitter in io_service uses log, log uses quitter(quit_silently) and io_service(exit_error). TODO will make it more elegant.
*/

#ifndef DATA_PROCESSORS_MISC_LOG_IMPL
#define DATA_PROCESSORS_MISC_LOG_IMPL

#ifndef WINDOWS_EVENT_LOGGING_SOURCE_ID
	#error "header inclusions error, WINDOWS_EVENT_LOGGING_SOURCE_ID should have been defined by now"
#endif

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc { 

#ifdef LOG_ON

#ifdef TEST_SYNAPSE_LOG_ROTATION_1
uint_fast64_t static Increment_log_rotation_test_offset() {
	::std::atomic_uint_fast64_t static Value{0};
	return ++Value;
}
#endif

/**
 * \about may be executed by multiple threads at once
 * Todo -- in future ADD ASYNC VERSION of this.
 */
template <typename T>
void inline Logger::operator()(T && x, bool flush) noexcept {
	try {
		auto const Utc_time(::boost::posix_time::microsec_clock::universal_time()
#ifdef TEST_SYNAPSE_LOG_ROTATION_1
			+ ::boost::posix_time::hours(Increment_log_rotation_test_offset())
#endif
		);
		auto const Local_time(::boost::date_time::c_local_adjustor<decltype(Utc_time)>::utc_to_local(Utc_time));
		::std::string const logged_time("local=" + ::boost::posix_time::to_simple_string(Local_time) + " universal=" + ::boost::posix_time::to_simple_string(Utc_time) + ' ');

		::std::lock_guard<misc::mutex> l(m);
		if (flush == false) {
			accumulated += ::std::move(logged_time) + ::std::forward<T>(x);
		} else {
			if (!Output_root.empty()) {
				Set_output_filepath(Utc_time);
				Write_file(accumulated + ::std::move(logged_time) + ::std::forward<T>(x));
			} else {
				::std::cerr << accumulated + ::std::move(logged_time) + ::std::forward<T>(x);
				::std::cerr.flush();
			}
			accumulated.clear();
		}
	} catch (::std::exception const & e) {
		Write_event_viewer(::std::string(WINDOWS_EVENT_LOGGING_SOURCE_ID " Logger exception: ") + e.what());
		synapse::asio::Exit_error = true;
		if (synapse::asio::io_service::Is_initialized())
			synapse::asio::quitter::get().Quit_silently();
	} catch (...) {
		Write_event_viewer(WINDOWS_EVENT_LOGGING_SOURCE_ID " Logger unknown exception");
		synapse::asio::Exit_error = true;
		if (synapse::asio::io_service::Is_initialized())
			synapse::asio::quitter::get().Quit_silently();
	}
}

void inline Logger::flush() noexcept {
	try {
		::std::lock_guard<misc::mutex> l(m);
		if (!Output_root.empty()) {
			Set_output_filepath(::boost::posix_time::microsec_clock::universal_time());
			Write_file(accumulated);
		} else {
			::std::cerr << accumulated;
			::std::cerr.flush();
		}
		accumulated.clear();
	} catch (::std::exception const & e) {
		Write_event_viewer(::std::string(WINDOWS_EVENT_LOGGING_SOURCE_ID " Logger exception: ") + e.what());
		synapse::asio::Exit_error = true;
		synapse::asio::quitter::get().Quit_silently();
	} catch (...) {
		Write_event_viewer(WINDOWS_EVENT_LOGGING_SOURCE_ID " Logger unknown exception");
		synapse::asio::Exit_error = true;
		synapse::asio::quitter::get().Quit_silently();
	}
}
#endif

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


