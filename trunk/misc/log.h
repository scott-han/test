#include <mutex>

#include <stdio.h>

#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>

#include "sync.h"

#ifndef DATA_PROCESSORS_MISC_LOG_H
#define DATA_PROCESSORS_MISC_LOG_H

#ifndef WINDOWS_EVENT_LOGGING_SOURCE_ID
	#define WINDOWS_EVENT_LOGGING_SOURCE_ID "Synapse"
#endif


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc { 

struct Logger { 
#ifdef LOG_ON
private:
	struct Scoped_file_handle {
		Scoped_file_handle(Scoped_file_handle const &) = delete;
		void operator=(Scoped_file_handle const &) = delete;
		HANDLE h;
		Scoped_file_handle()
		: h(INVALID_HANDLE_VALUE) {
		}
		void Close() {
			if (h != INVALID_HANDLE_VALUE)
				::CloseHandle(h);
		}
		void Reset(::std::string const & filepath) {
			Close();
			h = ::CreateFile(filepath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (h == INVALID_HANDLE_VALUE)
				throw ::std::runtime_error("CreateFile (=" + filepath + ')');
			if (::SetFilePointer(h, 0, NULL, FILE_END) == INVALID_SET_FILE_POINTER && ::GetLastError() != NO_ERROR) 
				throw ::std::runtime_error("SetFilePointer (=" + filepath + ')');
		}
		~Scoped_file_handle() {
			Close();
		}
	} File_handle;

	misc::mutex m;
	::std::string accumulated;
	::boost::filesystem::path Output_root;
	::boost::gregorian::date Output_date{1400, 1, 1};

	void Set_output_filepath(::boost::posix_time::ptime const & Time) {
		assert(!Output_root.empty());
		Set_output_filepath(Time.date());
	}

	void Set_output_filepath(::boost::gregorian::date const & Incoming_date) {
		assert(!Output_root.empty());
		if (Incoming_date > Output_date) {
			auto const New_filepath(Output_root / (to_iso_string(Output_date = Incoming_date) + "_log.txt"));
			if (::boost::filesystem::exists(New_filepath))
				throw ::std::runtime_error("Target name " + New_filepath.string() + " for log file already exists (should not though -- perhaps was created by external sources whilst synapse was already running?)");
			File_handle.Reset(New_filepath.string());
			#ifdef BUILD_INFO
				accumulated += Version_string_with_newline;
			#endif
		}
	}

public:


	void Set_output_root(::std::string const & Root) {
		// Register event viewer source (TODO more error checking and subsequent appropriate handling).
		HKEY key;
		// TODO -- currently using another app until we get our own resources compiled-in (no time to tweak now).
		// similar to faking with calling eventcreate.exe i.e.
		// eventcreate.exe /L APPLICATION /so Synapse /t ERROR /id 1 /d "The error message text."
		if (::RegCreateKeyEx(HKEY_LOCAL_MACHINE,"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" WINDOWS_EVENT_LOGGING_SOURCE_ID, 0, 0, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, 0, &key, 0) == ERROR_SUCCESS) {
			char unsigned Tmp_path[] = "%SystemRoot%\\System32\\EventCreate.exe";
			::RegSetValueEx(key, "EventMessageFile", 0, REG_EXPAND_SZ, Tmp_path, sizeof(Tmp_path));
			DWORD Tmp(0x7);
			::RegSetValueEx(key, "TypesSupported", 0, REG_DWORD, reinterpret_cast<char unsigned *>(&Tmp), sizeof(Tmp));
			Tmp = 0x1;
			::RegSetValueEx(key, "CustomSource", 0, REG_DWORD, reinterpret_cast<char unsigned *>(&Tmp), sizeof(Tmp));
			::RegCloseKey(key);
		}
		try {
			::boost::filesystem::create_directories(Root);
			auto const Now(::boost::posix_time::microsec_clock::universal_time().date());
			for (::boost::filesystem::directory_iterator i(Root); i != ::boost::filesystem::directory_iterator(); ++i) {
				auto const & Filename(i->path().filename().string());
				try {
					auto const & Log_date(::boost::gregorian::from_undelimited_string(Filename.substr(0, 8)));
					if (Log_date > Now)
						throw ::std::runtime_error("future log file. inferred logfile date (=" + to_simple_string(Log_date) + ')');
				} catch (::std::exception const & e) {
					throw ::std::runtime_error(::std::string(WINDOWS_EVENT_LOGGING_SOURCE_ID " cannot proceed with starting (=") + e.what() + ") Current UTC date(=" + to_simple_string(Now) + ") Log filename (=" + Filename + ')'); 
				}
			}
			File_handle.Reset(((Output_root = Root) / (to_iso_string(Output_date = Now) + "_log.txt")).string());
		} catch (::std::exception const & e) {
			Write_event_viewer(::std::string(WINDOWS_EVENT_LOGGING_SOURCE_ID " Logger exception: ") + e.what());
			throw;
		} catch (...) {
			Write_event_viewer(WINDOWS_EVENT_LOGGING_SOURCE_ID " Logger unknown exception");
			throw;
		}
	}

	/**
	 * \about may be executed by multiple threads at once
   * Todo -- in future ADD ASYNC VERSION of this.
	 */
	template <typename T>
	void inline operator()(T && x, bool flush = false) noexcept;

	void inline flush() noexcept;
private:
	void Write_file(::std::string const & Data) {
		assert(!Output_root.empty());
		// Todo, more error checking and handling.
		DWORD Tmp;
		::WriteFile(File_handle.h, Data.data(), Data.size(), &Tmp, NULL);
	}
	void Write_event_viewer(::std::string const & Data) {
		::std::cerr << Data; ::std::cerr.flush();
		auto Event_log_handle(::RegisterEventSource(0, WINDOWS_EVENT_LOGGING_SOURCE_ID));
		if (Event_log_handle != NULL) {
			auto Tmp(Data.c_str());
			::ReportEvent(Event_log_handle, EVENTLOG_ERROR_TYPE, 0, 0x2, 0, 1, 0, &Tmp, 0);
			::DeregisterEventSource(Event_log_handle);
		}
	}
#else
	// TODO -- this is probably too generic -- will need to allow some logging in contexts such as release build, whilst allowing for more logging in debug builds... will think about it some more...
	template <typename T> void inline operator()(T &&, bool = false) const noexcept {}
	void inline flush() const noexcept {}
	void Set_output_root(::std::string const &) const noexcept {}
#endif
} static log;

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


