#if defined(__GNUC__)  || defined(__clang__) 
#include "Archive_maker_pch.h"
#endif
#include "../../../Version.h"
#ifdef FORCE_TRUNCATION_WHILE_SUBSCRIPTION
#include <chrono>
#include <thread>
#endif
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace Archive_maker {
/**
 * To quit the applicaiton.
 */
void quit_on_request(const ::std::string& message) {
	auto const prev(::data_processors::synapse::asio::quit_requested.test_and_set());
	if (!prev) {
		synapse::misc::log("Archive_maker quits automatically (" + message + ").\n", true);
		synapse::asio::quitter::get().quit([]() {
			synapse::asio::io_service::get().stop();
			synapse::misc::log("...exiting at a service-request.\n", true);
		});
	}
}

#ifdef __WIN32__
bool static run_as_service{false};
::SERVICE_STATUS static service_status;
::SERVICE_STATUS_HANDLE static service_handle; 
char service_name[] = "Archive_maker";
bool require_stop = false;
void 
service_control_handler(DWORD control_request)
{
	switch (control_request) { 
	case SERVICE_CONTROL_STOP: 
	case SERVICE_CONTROL_SHUTDOWN: 
		require_stop = true;
		service_status.dwCurrentState = SERVICE_STOP_PENDING; 
		service_status.dwControlsAccepted = 0;
		service_status.dwWaitHint = 5 * 60 * 1000;
		::SetServiceStatus(service_handle, &service_status);
		quit_on_request("Service is asked to shutdown/stop");
	}
}
#endif

// defaults...
::std::string static host("localhost");
::std::string static port("5672");
::std::string static ssl_key;
::std::string static ssl_certificate;
unsigned static so_rcvbuf(-1);
unsigned static so_sndbuf(-1);
unsigned static concurrency_hint(0);
::boost::filesystem::path static archive_root;
uint_fast64_t static Subscribe_from(0);
::std::string static Startpoint_filepath;
::std::set<::std::string> SubscriptionS;
::std::string static Timediff_alert;
// This time difference tolerance is measured in days, if you want to change this, please also change Timediff_scale accordingly.
int static Timediff_tolerance(-1);
// The scale for Timediff_tolerance (convert it to micro seconds).
uint_fast64_t static Timediff_scale{ 24LL * 60 * 60 * 1000000 };
// The time difference checking time period (default is 3 hours)
unsigned static Check_timestamp_period{ 3 * 60 * 60 };
// When the last alarm is sent out.
uint_fast64_t static Last_alarm_time{ 0 };
// true: Archive_maker is running less than Timediff_tolerance days behind; false: it is running more than Timediff_tolerance days behind.
bool static Last_alarm_content{ false }; 
// How often the alarm will be sent out (the unit of this interval is Timediff_scale).
uint_fast64_t static Alarm_interval{ 1 * Timediff_scale };
// Report will be generated based on current timestamp or not, default is not.
int static Use_current_time{ 0 };
// The subscribe_to option.
uint_fast64_t static Subscribe_to(-1);
// Terminate the applicaiton when no more data is available (based on ACK timestamp and sequence number)
int static Terminate_when_no_more_data{ 0 };
// Total completed topics.
unsigned static Completed_topics{ 0 };
// Total topics to be subscribed (get from server).
unsigned static Total_topics_to_be_subscribed{ 0 };
// All files are scanned or not
bool static All_files_scanned{ true };
// Total subscriptions provided from command line
unsigned static Total_subscription{ 0 };
// Total files to be scanned.
unsigned static Total_file_count{ 0 };
// Total topics already subscribed.
unsigned static Subscribed_topics{ 0 };
// Path to store all save point files
::boost::filesystem::path Save_point_path;
// Days to delete local archived files
int static Days_to_delete_archived_files{ -1 };
// Days to truncate topics on server
int static Days_to_truncate_topic{ -1 };
// Archivist path
::std::string static Archivist_path;
// Synapse at
::std::string static Synapse_at;

::std::string static Log_path;
namespace basio = ::boost::asio;

template <typename Socket_type> 
struct Archive_maker : ::boost::enable_shared_from_this<Archive_maker<Socket_type>> {

	typedef ::boost::enable_shared_from_this<Archive_maker<Socket_type>> base_type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

	::boost::shared_ptr<Synapse_client_type> Synapse_client{::boost::make_shared<Synapse_client_type>()};
	/**
	 * Given the ACK is available from our Synapse server,the Archive_maker wil work like this now.
	 *
	 * After Archive_maker is started, it will scan all archived files and find the latest timestamp
	 * for each archived topic, once the latest archived timestamp is found for a particular topic,
	 * it will post a job via Synapse_client->strand to update the archived timestamp in the container.
	 * Please be advised the above task is completed asychrounisely.
	 *
	 *
	 * Once the above scan taks is started, it will subscribes to Synapse server with ACK mode only.
	 * Once Ack subscription is succeeded, it will subscribe to extra information to get total topics
	 * in this subscription. For multiple subscription cases, we will only call extra subscription 
	 * after all subscriptions are completed (subscribed).
	 *
	 * For each ACK message it received, it will post a task via Synapse_client->strand, the taks
	 * will update the latest received timestamp in Topic_statistics map, if the item is not subscribed
	 * yet, it will subscribe this topic to our Synapse server with full mode (provided that latest
	 * received timestamp and last archived timestamp are both there or the topic doesn't exist in 
	 * container at all). Otherwise it will just update the Latest_timestamp.
	 * 
	 * 
	 * Later when the callback function or the previous subscription returns and says we can start subscribing again, 
	 * we will start subscribe again.
	 *
	 * For all received message from Synapse client, we keep tracking the last archived timestamp via
	 * Synapse_client.strand, the taks is just to update the topic's statistics information. Which will 
	 * be used later for reporting.
	 *
	 * When the time difference checking timer is due, we can just scan this topic's statistics infromation and report all
	 * topics whose time difference are greater than pre-defined timediff_tolerance.
	 *
	 */
	::boost::shared_ptr<Synapse_client_type> Ack_synapse_client{::boost::make_shared<Synapse_client_type>()};
	/**
	 * We have this class is to simple asynchronous programming, after defining this class, all timestamp and sequence number
	 * related operations (updating, reading, reporting) are completed in Ack_synapse_client.strand.
	 */
	struct Topic_time_statistics {
		Topic_time_statistics(uint_fast64_t archived_timestamp, uint_fast64_t archived_sequence = 0, uint_fast64_t truncate = 0) 
			: Latest_timestamp(0)
			, Archived_timestamp(archived_timestamp)
			, Latest_sequence(0)
			, Archived_sequence(archived_sequence)
			, Truncate_timestamp(truncate)
			, Subscribed(false)
			, Completed(false)
		{
		}
		uint_fast64_t	Latest_timestamp;	// The latest timestamp for a particular topic, we get this via ACK.
		uint_fast64_t	Archived_timestamp; // The latest archived timestamp, we got this after the message is processed by Archive_maker
		uint_fast64_t	Latest_sequence;	// The latest sequence number from ACK
		uint_fast64_t	Archived_sequence;	// The last archived sequence number found in files.
		uint_fast64_t	Truncate_timestamp; // Truncate up to here
		bool			Subscribed;			// The item has been subscribed or not.
		bool			Completed;
	};
	/**
	 * A very thin MD5 and SHA256 hash wrapper.
	 *
	 */
	template <typename T, /*Hash context*/
		int I(T*),  /*Initialization function*/
		int U(T*, const void*, size_t), /*Update function*/
		int F(unsigned char*, T*), /*Final function*/
		unsigned length> /*hash digest length*/
	class Generic_Hash {
	private:
		T	CTX;
		unsigned char Buff[length];
		enum hash_status{ H_UNKNOWN, H_INIT, H_FINAL };
		hash_status cur_status{ H_UNKNOWN };
	public:
		int Init() {
			if (cur_status != H_UNKNOWN)
				throw ::std::runtime_error("It is already initialized.");
			memset(Buff, 0, length);
			int ret = I(&CTX);
			if (ret)cur_status = H_INIT;
			return ret;
		}

		int Update(const void* data, size_t len) {
			if (cur_status != H_INIT)
				throw ::std::runtime_error("Not initialized yet.");
			return U(&CTX, data, len);
		}

		int Final(unsigned char* buff = nullptr) {
			int ret = F(Buff, &CTX);
			if (ret && buff != nullptr)
				memcpy(buff, Buff, length);
			if (ret)cur_status = H_FINAL;
			return ret;
		}
		// must be called after final is called.
		::std::string HexedHash() const {
			if (cur_status != H_FINAL)
				throw ::std::runtime_error("Final must be called before calling HexedHash.");
			::std::ostringstream oss;
			oss << std::hex << std::nouppercase << std::setfill('0');
			for (unsigned k(0); k < length; ++k) {
				oss << std::setw(2) << static_cast<unsigned>(Buff[k]);
			}
			return oss.str();
		}
	};

	struct Topic_subscription {
		Topic_subscription(const ::std::string& topic, uint_fast64_t from, uint_fast64_t to = 0) 
			: Topic_name(topic)
			, Subscribe_from(from)
			, Subscribe_to(to)
		{
		}
		::std::string Topic_name;	// Topic name
		uint_fast64_t Subscribe_from; // For this topic, it should be subscribed from this point -- we found this value by scanning stored files on local disk.
		uint_fast64_t Subscribe_to;	// Subscribed to
	};

	struct Archived_file_info
	{
		Archived_file_info(const ::std::string& file, uint_fast32_t pos)
			: File_name(file), Position(pos) {}
		// Actual file name (may be [topic name]_YYYYMMDD_segment_[N] or XXXX_XXXX_XXXX_XXXX, if it is later, then 
		// [topic_name]_YYYYMMDD_segment_[N] is embedded in archived data file.
		::std::string	File_name;
		// Actual archived data started position.
		uint_fast32_t	Position;
	};

	struct Latest_archived_files {
		Latest_archived_files(const ::std::string& date, int segment, const ::std::string& file, uint_fast32_t pos)
			: Date(date)
		{
			Files.emplace(::std::piecewise_construct, ::std::forward_as_tuple(segment), ::std::forward_as_tuple(file, pos));
		}
		// To append a new file
		void Append(int segment, const ::std::string& file, uint_fast32_t pos) {
			Files.emplace(::std::piecewise_construct, ::std::forward_as_tuple(segment), ::std::forward_as_tuple(file, pos));
		}
		// To clear all files
		void Clear() {
			Files.clear();
		}
		// The date as string YYYYMMDD
		::std::string Date;
		// All segment files on this date for a pariticular topic.
		// key is the segment number, and value is the file name, which looks like topic_YYYYMMDD_Segment_n or XXXX_XXXX_XXXX_XXXX (new)
		::std::map<int, Archived_file_info> Files;
	};
	// This container is managed by Ack_synapse_client.strand, therefore no lock is required.
	::std::map<::std::string, Topic_time_statistics> Topic_statistics;
	// This container is managed by Synapse_client.strand, therefore no lock is required.
	::std::queue<Topic_subscription> Topic_subscriptions;
	// These two variables are managed by Synapse_client.strand, therefore no lock is requried.
	unsigned Subscription_fence{ 0 };
	bool Can_subscribe{ true };
	
	struct Topic_processor {
		unsigned static constexpr File_buffer_size{4 * 1024 * 1024}; // From testing, huge buffer doesn't improve performance, therefore we use a small one.
		::std::unique_ptr<char> Output_file_buffer{new char[File_buffer_size]};
		::std::unique_ptr<char> Previous_output_file_buffer{new char[File_buffer_size]};

		::std::ofstream Output_file;
		::boost::gregorian::date Output_file_date;
		::std::ofstream Previous_output_file;

		::std::string Topic_name;
		::data_processors::synapse::asio::basio::strand strand;
		::boost::filesystem::path Local_archive_root;
		uint_fast64_t Last_update_time;		
		uint_fast64_t Last_savepoint_time;
		::std::string MD5_hash;
		::boost::interprocess::ipcdetail::winapi_mutex_wrapper mutex;

		/**
		 * To get mutex name from the archive_root/topic_name
		 * 
		 * @param[name]: archive_root/topic_name
		 *
		 * @return: The string will be used as mutex name.
		 */
		static ::std::string Get_mutex_name(const ::std::string& name) {
			// We will have to convert this name to the absolute path name, otherwise we wil allow
			// two instances work on the same folder, one is relative path, the other one is absolute
			// path. But we cannot handle UNC path yet.
			::std::string mutex_name(::boost::filesystem::absolute(name).string());
			::std::replace(mutex_name.begin(), mutex_name.end(), '\\', '/');
			return mutex_name;
		}

		Topic_processor(const ::std::string& Topic_name) : Topic_name(Topic_name),
			strand(data_processors::synapse::asio::io_service::get()), Local_archive_root(archive_root), Last_update_time(1000000), Last_savepoint_time(0),
			// We have to use windows specific API CreateMutex as boost::named_mutex is based on file, 
			// which will not be removed if the application is not shutddown gracefully.
			mutex(::CreateMutex(NULL, FALSE, Get_mutex_name(Local_archive_root.string() + "/" + Topic_name).c_str()))
		{
			Output_file.rdbuf()->pubsetbuf(Output_file_buffer.get(), File_buffer_size);
			Previous_output_file.rdbuf()->pubsetbuf(Previous_output_file_buffer.get(), File_buffer_size);
			if (mutex.handle() == NULL)
				throw std::runtime_error("Failed to create mutex for topic " + Topic_name);
			if (!mutex.try_lock()) {
				throw std::runtime_error("Failed to acquire lock, cannot archive topic " + Topic_name + " to folder " + Local_archive_root.string());
			}
			Generic_Hash<MD5_CTX, MD5_Init, MD5_Update, MD5_Final, MD5_DIGEST_LENGTH> Md5;
			if (!Md5.Init())
				throw ::std::runtime_error("Failed to initialize MD5 context");
			::std::string temp(Synapse_at + "|" + Topic_name);
			if (!Md5.Update(temp.c_str(), temp.length()))
				throw ::std::runtime_error("Failed to update MD5 hash");

			if (!Md5.Final())
				throw ::std::runtime_error("Failed to finalize MD5 hash");

			MD5_hash = "_" + Md5.HexedHash();
		}

		Topic_processor(Topic_processor const &) = delete;

		void Set_output_files_state(uint_fast64_t const Timestamp) {
			if (Previous_output_file.is_open())
				Previous_output_file.close();
			auto const Current_date((data_processors::synapse::misc::epoch + ::boost::posix_time::microseconds(Timestamp)).date());
			if (Current_date != Output_file_date) {
				Output_file_date = Current_date;
				Previous_output_file.swap(Output_file);
				auto const Date_as_string(to_iso_string(Current_date));
				auto const Archive_file_prefix(Date_as_string + "_segment_");
				auto const Archive_path(Local_archive_root / Date_as_string / Topic_name);
				if (!::boost::filesystem::exists(Archive_path))
					::boost::filesystem::create_directories(Archive_path);
				::std::string temp(Synapse_at + "|" + Topic_name);
				for (unsigned i(0);;++i) {
					auto const Output_file_name(Archive_file_prefix + ::std::to_string(i) + MD5_hash);
					auto const Output_filepath(Archive_path / (Output_file_name));
					if (::boost::filesystem::exists(Output_filepath))
						continue;
					Output_file.open(Output_filepath.string(), ::std::ios_base::binary | ::std::ios_base::trunc);
					// Embed server, and topic name into archived file.
					int len(-1);
					Output_file.write(reinterpret_cast<char*>(&len), sizeof(len));
					len = static_cast<int>(temp.length());
					Output_file.write(reinterpret_cast<char*>(&len), sizeof(len));
					Output_file.write(temp.c_str(), len);
					int align_len(synapse::misc::round_up_to_multiple_of_po2<uint_fast32_t>(len, 8));
					char c[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
					Output_file.write(c, align_len - len);
					Output_file.write(reinterpret_cast<const char*>(&Timestamp), sizeof(Timestamp));
					uint64_t crc(0);
					crc = ::data_processors::synapse::misc::crc32c(crc, static_cast<uint64_t>(-1));
					crc = ::data_processors::synapse::misc::crc32c(crc, static_cast<uint64_t>(len));
					for (int m(0); m < len; ++m)
						crc = ::data_processors::synapse::misc::crc32c(crc, temp.at(m));
					crc = ::data_processors::synapse::misc::crc32c(crc, Timestamp);
					Output_file.write(reinterpret_cast<char *>(&crc), sizeof(crc));
					break;
				}
			}
		}

		void Write_output_files(void const * Data, unsigned Size) {
			if (!Output_file.is_open())
				throw ::std::runtime_error("File IO on closed ::std::ofstream, topic: " + Topic_name);
			Output_file.write(reinterpret_cast<char const *>(Data), Size);
			if (Previous_output_file.is_open())
				Previous_output_file.write(reinterpret_cast<char const *>(Data), Size);
		}

		void Flush_output_files() {
			if (Output_file.is_open())
				Output_file.flush();
			if (Previous_output_file.is_open())
				Previous_output_file.flush();
		}
	};

	struct Topic_processor_shm {
		Topic_processor* pTopic_processor;
	};
	::std::map<::std::string, Topic_processor> Topic_processor_map;
	// Topics to be truncated (as we don't care about the order of them, so we can store them in a map).
	// Must be used in Synapse_client's strand.
	::std::map<::std::string, uint_fast64_t> Topic_to_be_truncated;
	// To indicated if a topic is truncating (request is already sent to server, but not received any reponse yet).
	// Must be used in Synapse_client's strand.
	bool Topic_is_truncating{ false };
	::std::map<::std::string, ::std::string> Date_files_map;
	// map from topic to all archived date.
	::std::map<::std::string, ::std::vector<::std::string>> Check_all_topics;
	/**
	 * dtor
	 *
	 * It is here for test only as I want to track if the applicaiton is quit gracefully.
	 * We will remove it later.
	 *
	 */
	~Archive_maker()
	{
		::data_processors::synapse::misc::log("dtor is called.\n", true);
	}
	/**
	 * To save time point to disk, when Archive_maker restarts, it will load subscribe_from from this file.
	 * 
	 * @param[Topic_processor]: 
	 * @param[Message_timestamp]: Current message's timestamp.
	 *
	 * @return:
	 */
	void Save_subscribefrom() {
		// Todo, more elegance.
		uint64_t Tmp(htole64(Subscribe_from));
		uint64_t Hash(data_processors::synapse::misc::crc32c(static_cast<uint64_t>(0), Tmp));
		::std::ofstream Savepoint_file(Startpoint_filepath, ::std::ios_base::binary | ::std::ios_base::trunc);
		for (unsigned i(0); i != 10; ++i) {
			Savepoint_file.write(reinterpret_cast<char const *>(&Tmp), sizeof(Subscribe_from));
			Savepoint_file.write(reinterpret_cast<char const *>(&Hash), sizeof(Hash));
		}
	}

	::std::string Get_alert_command(const ::std::string& alert, bool& powershell) {
		auto && pos(alert.rfind('.'));
		if (pos != ::std::string::npos) {
			::std::string && ext(alert.substr(pos));
			::boost::algorithm::to_lower(ext);
			if (ext == ".ps1") {
				powershell = true;
				return "PowerShell -NoProfile -ExecutionPolicy Bypass -Command \"" + alert + " -path " + Log_path + " -content";
			}
		}
		powershell = false;
		return alert;
	}
	/**
	 * This method is to report all topics that are currently lagging behind.
	 *
	 * @param[this_]: Shared_ptr of Archive_maker, this is to ensure Archive_maker is not be destroyed while this method is executed.
	 * @param[use_current_time]: true: the application will report running late or not base on current timestamp, otherwise it will
	 *                           report running late or not base on the latest timestamp received from ACK.
	 *                           
	 *                           The parameter is currently for test purpose only, as otherwise it is hard to test some cases.
	 *
	 */
	void Report(::boost::shared_ptr<Archive_maker> const & this_, bool use_current_time = false) {
		Synapse_client->strand.post([this, this_, use_current_time]() {
			bool powershell(false);
			::std::string && reports(Get_alert_command(Timediff_alert, powershell)); 
			reports += std::string(powershell ? " \"" : " ") + "\"The following topics are running late: {";
			bool require_report(false); // To indicate if we need to send report or not.
			bool data_received(false); // To indicate if there is some data received.
			for (auto && it(Topic_statistics.begin()); it != Topic_statistics.end(); ++it) {
				if (!data_received && it->second.Archived_timestamp != 0) data_received = true;
				uint_fast64_t Current_timestamp(it->second.Latest_timestamp);
				::data_processors::synapse::misc::log("Latest timestamp for topic " + it->first + " is " + ::std::to_string(Current_timestamp) + "\n", true);
				if (use_current_time)Current_timestamp = data_processors::synapse::misc::cached_time::get();
				if (it->second.Archived_timestamp != 0 && // Ensure we've received some messages.
					Current_timestamp >= it->second.Archived_timestamp && // Ensure the result is not overflow/underflow.
					Current_timestamp - it->second.Archived_timestamp > Timediff_tolerance * Timediff_scale) { 
					require_report = true;
					reports += "[" + it->first;
					reports += ", " + ::std::to_string((Current_timestamp - it->second.Archived_timestamp) / (Timediff_tolerance * Timediff_scale));
					reports += " days behind]";
				}
			}
			if (require_report) {
				// There are some topics are running late.
				uint_fast64_t Current_timestamp(data_processors::synapse::misc::cached_time::get());
				if (Last_alarm_content || (!Last_alarm_content && (Current_timestamp - Last_alarm_time > Alarm_interval))) {
					// Only send Archive_maker running behind if the last report is GOOD (catching up) or the time span since the last
					// BAD (running behind) report is greater than the pre-defined Alarm_interval.
					require_report = true;
					reports += ("}" + ::std::string(powershell ? "\"\"\"" : "\""));
					Last_alarm_content = false;
					Last_alarm_time = Current_timestamp;
				}
				else {
					// Not satisfy sending report condition, log it only.
					require_report = false;
					::data_processors::synapse::misc::log("Archive_maker is running late, but it will NOT send alarm as the previous is just sent out at " + ::std::to_string(Last_alarm_time) + ".\n", true);
				}
			}
			else {
				if (data_received) {
					// Archive_maker is catching up.
					if (!Last_alarm_content) {
						// only send catching up report if the last report is BAD (running late).
						require_report = true;
						Last_alarm_content = true;
						Last_alarm_time = data_processors::synapse::misc::cached_time::get();
						powershell = false;
						reports = Get_alert_command(Timediff_alert, powershell);
						reports += std::string(powershell ? " \"" : " ");
						reports += "\"Archive_maker is currently running less than " + ::std::to_string(Timediff_tolerance) + " days behind." + +(powershell ? "\"\"\"" : "\"");;
					}
					else {
						// Otherwise log it only.
						require_report = false;
						::data_processors::synapse::misc::log("Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification.\n", true);
					}
				}
			}
			if (require_report) {
				// Sending report in another thread.
				synapse::asio::io_service::get().post([reports]() {
					int ret = ::system(reports.c_str());
					data_processors::synapse::misc::log("After calling system(" + reports + "), return value is " + ::std::to_string(ret) + ".\n", true);
				});
			}
		});
	}

	void Run() {
		if (archive_root.empty())
			throw ::std::runtime_error("Unspecified archive_root");
		if (SubscriptionS.empty())
			throw ::std::runtime_error("Empty subscriptions list");

		Total_subscription = SubscriptionS.size();

		if (!Startpoint_filepath.empty()) {
			::std::ifstream Savepoint_file(Startpoint_filepath, ::std::ios_base::binary);
			if (Savepoint_file) {
				uint_fast64_t Subscribe_from_in_file = 0;
				bool found = false;
				while (Savepoint_file) {
					uint64_t Savepoint;
					Savepoint_file.read(reinterpret_cast<char *>(&Savepoint), sizeof(Savepoint));
					uint64_t Hash;
					Savepoint_file.read(reinterpret_cast<char *>(&Hash), sizeof(Hash));
					if (data_processors::synapse::misc::crc32c(static_cast<uint64_t>(0), Savepoint) == Hash) {
						Subscribe_from_in_file = le64toh(Savepoint);
						found = true;
						break;
					}
				}
				if (found) {
					if (Subscribe_from != 0) {
						if (Subscribe_from < Subscribe_from_in_file) {
							throw ::std::runtime_error("Could not start subscribe from earlier time than last run.");
						}
						else {
							// Save new Subscribe_from to file.
							Save_subscribefrom();
						}
					}
				}
				else
					throw ::std::runtime_error("Savepoint file is corrupted.");
			}
			else {
				Save_subscribefrom();
			}
		}

		auto this_(base_type::shared_from_this());

		Synapse_client->Open(host, port, [this, this_](bool Error) {
			if (!Error) {
				// To set On_pending_writes_watermark call back function so we will have the notification about keep subscribing or not.
				Synapse_client->On_pending_writes_watermark = [this, this_](::boost::tribool const &val) {
					if (val) {
						Can_subscribe = false;
					}
					else if (::boost::tribool::indeterminate_value == val.value) {
						// Error, flush files
						Can_subscribe = false;
						Put_into_error_state(this_, ::std::runtime_error("On_pending_writes_watermark from Synapse_client."));
					}
					else {
						Can_subscribe = true;
						Synapse_client->strand.post([this, this_]() {
							Async_subscribe(this_);
						});
					}
				};

				// To set On_error callback function.
				Synapse_client->On_error = [this, this_]() {
					Put_into_error_state(this_, ::std::runtime_error("On_error from Synapse_client."));
				};

				Synapse_client->Open_channel(2, [this, this_](bool Error) {
					if (Error) {
						Put_into_error_state(this_, ::std::runtime_error("Open_channel 2 failed."));
					}
					else {

						auto & Channel(Synapse_client->Get_channel(1));
						Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) {
							try {
								if (pEnvelope != nullptr) {
									Channel.Stepover_incoming_message(pEnvelope, [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) {
										try {
											if (pEnvelope == nullptr) {
												Put_into_error_state(this_, ::std::runtime_error("Envolope is null in Stepover_incomming_message."));
												return;
											}
											if (!pEnvelope->User_data) {
												// We have to check if the item alredy existed or not, otherwise a temporary Topic_processor will be
												// constructed and then destructed, but during constructed, it will failed as we have a mutex there.
												auto && it(Topic_processor_map.find(pEnvelope->Topic_name));
												if (it == Topic_processor_map.end()) {
													auto ret = Topic_processor_map.emplace(pEnvelope->Topic_name, pEnvelope->Topic_name);
													it = ret.first;
												}
												pEnvelope->User_data = ::std::unique_ptr<char, void(*)(void *)>(reinterpret_cast<char*>(new Topic_processor_shm{ &it->second }), [](void * X) { delete reinterpret_cast<Topic_processor_shm*>(X); });
											}

											assert(!pEnvelope->Pending_buffers.empty());
											auto && Topic_processor(*reinterpret_cast<Archive_maker::Topic_processor_shm*>(pEnvelope->User_data.get())->pTopic_processor);
											auto && Envelope(*pEnvelope);
											assert(Envelope.Pending_buffers.front().Message_timestamp);
											assert(Envelope.Pending_buffers.front().Message_sequence_number);
											auto const & Pending_buffer(Envelope.Pending_buffers.front());
											Topic_processor.strand.post([this, this_, &Channel, &Envelope, &Topic_processor, &Pending_buffer]() {
												try {
													uint_fast64_t Timestamp(Pending_buffer.Message_timestamp);
													uint_fast64_t Sequence(Pending_buffer.Message_sequence_number);
													Topic_processor.Set_output_files_state(Timestamp);
													Synapse_client->strand.post([this, this_, &Topic_processor, Timestamp, Sequence]() {
														try {
															auto && it(Topic_statistics.find(Topic_processor.Topic_name));
															if (it != Topic_statistics.end()) {
																it->second.Archived_timestamp = Timestamp;
																it->second.Archived_sequence = Sequence;
																if (!run_as_service && !it->second.Completed && (Subscribe_to != (uint_fast64_t)-1 || Terminate_when_no_more_data != 0)) {
																	bool completed(false), subscribe_to_is_satisfied(false);
																	if (Subscribe_to != (uint_fast64_t)-1) { // --subscribe_to is provided
																		if (Terminate_when_no_more_data != 0) { // --terminate_when_no_more_data is provided
																			subscribe_to_is_satisfied = (it->second.Archived_timestamp > Subscribe_to);
																			completed = subscribe_to_is_satisfied ||
																				(it->second.Archived_timestamp == it->second.Latest_timestamp && it->second.Archived_sequence == it->second.Latest_sequence);
																		}
																		else { //--subscribe_to is provided, but --terminate_when_no_more_data is not provided
																			completed = (it->second.Archived_timestamp > Subscribe_to);
																			subscribe_to_is_satisfied = completed;
																		}
																	}
																	else { // --subscribe_to is not provided
																		if (Terminate_when_no_more_data != 0) { // --terminate_when_no_more_data is provided.
																			completed = (it->second.Archived_timestamp == it->second.Latest_timestamp && it->second.Archived_sequence == it->second.Latest_sequence);
																		}
																	}
																	if (completed) {
																		it->second.Completed = true;
																		++Completed_topics;
																		if (All_files_scanned && ((Completed_topics == ::std::max(Total_topics_to_be_subscribed, Total_subscription)) || subscribe_to_is_satisfied)) {
																			::data_processors::Archive_maker::quit_on_request("All data is received");
																		}
																	}
																}
															}
															else {
																// Oops, TODO report error or anything else?
																throw ::std::runtime_error("Could not find topic '" + Topic_processor.Topic_name + "' in Topic_statistics.");
															}
														}
														catch (::std::exception const & e) {
															Put_into_error_state(this_, e);
														}
														catch (...) {
															Put_into_error_state(this_, ::std::runtime_error("Unknow exception while updating latest timestamp."));
														}
													});
													if (Timestamp <= Subscribe_to) { // Stop archiving data when timestamp is greater than Subscribe_to
														auto Payload_buffer(Pending_buffer.Get_amqp_frame() + 3);
														assert(!(reinterpret_cast<uintptr_t>(Pending_buffer.Get_amqp_frame() + 1) % 16));
														uint32_t const Payload_size(synapse::misc::be32toh_from_possibly_misaligned_source(Payload_buffer) + 5);

														assert(Payload_buffer[Payload_size - 1] == 0xCE);

														// Todo -- this is not too fast atm...
														unsigned Hash(0);

														uint32_t Int_buffer_32(htole32(32 + Payload_size)); // Total message size.
														Hash = data_processors::synapse::misc::crc32c(static_cast<uint32_t>(Hash), Int_buffer_32);
														Topic_processor.Write_output_files(&Int_buffer_32, sizeof(Int_buffer_32));

														Int_buffer_32 = htole32(2); // Indicies size.
														Hash = data_processors::synapse::misc::crc32c(static_cast<uint32_t>(Hash), Int_buffer_32);
														Topic_processor.Write_output_files(&Int_buffer_32, sizeof(Int_buffer_32));

														uint64_t Int_buffer_64(htole64(Pending_buffer.Message_sequence_number)); // 1st index.
														Hash = data_processors::synapse::misc::crc32c(static_cast<uint64_t>(Hash), Int_buffer_64);
														Topic_processor.Write_output_files(&Int_buffer_64, sizeof(Int_buffer_64));

														Int_buffer_64 = htole64(Pending_buffer.Message_timestamp); // 2nd index.
														Hash = data_processors::synapse::misc::crc32c(static_cast<uint64_t>(Hash), Int_buffer_64);
														Topic_processor.Write_output_files(&Int_buffer_64, sizeof(Int_buffer_64));

														// Hash the bulk...
														auto const * Payload_buffer_scan(Payload_buffer);
														auto const Payload_tail_size(Payload_size % 8);
														auto const * Payload_tail_begin(Payload_buffer + Payload_size - Payload_tail_size);
														for (; Payload_buffer_scan != Payload_tail_begin; Payload_buffer_scan += 8) {
															::memcpy(&Int_buffer_64, Payload_buffer_scan, 8);
															Hash = data_processors::synapse::misc::crc32c(static_cast<uint64_t>(Hash), Int_buffer_64);
														}
														assert(Payload_tail_size < 8);

														// Hash the tail...
														if (Payload_tail_size) {
															Int_buffer_64 = 0;
															::memcpy(&Int_buffer_64, Payload_buffer_scan, Payload_tail_size);
															Hash = data_processors::synapse::misc::crc32c(static_cast<uint64_t>(Hash), Int_buffer_64);
															Topic_processor.Write_output_files(Payload_buffer, Payload_size - Payload_tail_size); // Amqp payload.
															Topic_processor.Write_output_files(&Int_buffer_64, sizeof(Int_buffer_64));
														}
														else
															Topic_processor.Write_output_files(Payload_buffer, Payload_size); // Amqp payload.

														Int_buffer_32 = htole32(Hash);
														Topic_processor.Write_output_files(&Int_buffer_32, sizeof(Int_buffer_32));

														Int_buffer_32 = 0; // Reserved zeros.
														Topic_processor.Write_output_files(&Int_buffer_32, sizeof(Int_buffer_32));

														if (Timestamp > Topic_processor.Last_savepoint_time && Timestamp - Topic_processor.Last_savepoint_time > 30 * 1000000UL) {
															Topic_processor.Flush_output_files();
															// Todo, more elegance.
															uint64_t Tmp(htole64(Topic_processor.Last_savepoint_time = Timestamp));
															uint64_t Tmp1(htole64(Sequence));
															uint64_t Hash(data_processors::synapse::misc::crc32c(static_cast<uint64_t>(0), Tmp));
															Hash = data_processors::synapse::misc::crc32c(Hash, Tmp1);
															::std::ofstream Savepoint_file(Save_point_path.string() + "\\" + Topic_processor.Topic_name, ::std::ios_base::binary | ::std::ios_base::trunc);
															for (unsigned i(0); i != 10; ++i) {
																Savepoint_file.write(reinterpret_cast<char const *>(&Tmp), sizeof(Topic_processor.Last_savepoint_time));
																Savepoint_file.write(reinterpret_cast<char const *>(&Tmp1), sizeof(Tmp1));
																Savepoint_file.write(reinterpret_cast<char const *>(&Hash), sizeof(Hash));
															}
														}
													}

													Synapse_client->strand.dispatch([this, this_, &Channel, &Envelope]() {
														try {
															//Channel.decrement_async_fence_size();
															Channel.Release_message(Envelope);
														}
														catch (::std::exception const & e) {
															Put_into_error_state(this_, e);
														}
														catch (...) {
															Put_into_error_state(this_, ::std::runtime_error("Unknow exception while Release_message."));
														}
													});
												}
												catch (::std::exception const & e) {
													Put_into_error_state(this_, e);
												}
												catch (...) {
													Put_into_error_state(this_, ::std::runtime_error("Unknow exception."));
												}
											});

										}
										catch (::std::exception const& e) {
											Put_into_error_state(this_, e);
										}
										catch (...) {
											Put_into_error_state(this_, ::std::runtime_error("Unknow exception in Stepover_incoming_message callback."));
										}
									});
								}
								else {
									// Error reaction... throw for the time being.
									throw ::std::runtime_error("On incoming message enevlope is null.");
								}
							}
							catch (::std::exception const & e) {
								Put_into_error_state(this_, e);
							}
							catch (...) {
								Put_into_error_state(this_, ::std::runtime_error("Unknow exception in On_incoming_message."));
							}
						};
						// Only start processing other tasks when Synapse_client open is succeeded.
						try {
							Start_scanning_file_and_ack_subscription(this_);
						}
						catch (::std::exception& e) {
							Put_into_error_state(this_, e);
						}
					}
				});
			}
			else {
				// Error reaction... throw for the time being.
				Put_into_error_state(this_, ::std::runtime_error("Could not open synapse_client connection."));
			}
		});
	}
	/**
	 * To put this into an error state.
	 *
	 * @param[this_]: Shared_ptr of Archive_maker to ensure Archive_maker instance is valid while this function is called.
	 * @param[e]: The actual exception.
	 *
	 * @return:
	 */
	void Put_into_error_state(const ::boost::shared_ptr<Archive_maker>& this_, ::std::exception const & e) {
		::data_processors::synapse::misc::log("Put_into_error_state, reason is: '" + ::std::string(e.what()) + "'\n", true);
		if (!::data_processors::synapse::asio::Exit_error.exchange(true, ::std::memory_order_relaxed)) {
			// Call put_into_error_state to remove all call back function objects, which are holding this_.
			Synapse_client->strand.post([this, this_, e]() { Synapse_client->put_into_error_state(e); });
			// Call put_into_error_state to remove all call back function objects, which are holding this_; call io_service::get() is to ensure our application
			// will quit when something is wrong.
			Ack_synapse_client->strand.post([this, this_, e]() { Ack_synapse_client->put_into_error_state(e); });
			::data_processors::Archive_maker::quit_on_request("Error in Archive_maker, will quit ...");
		}
	}
	/**
	 * To start scanning files on disk and subscribe to server via ACK mode
	 *
	 * @param[this_]: Shared_ptr to archive_maker to ensure it is valid while this function is called.
	 *
	 * @return
	 *
	 */
	void Start_scanning_file_and_ack_subscription(const ::boost::shared_ptr<Archive_maker>& this_) {
		// traverse all archived files to get last archived timestamp and sequence number for all topics
		// that are already archived.
		Traverse_archived_files(this_);
		Ack_synapse_client->Open(host, port, [this, this_](bool Error) {
			if (!Error) {
				// To set On_error callback function.
				Ack_synapse_client->On_error = [this, this_]() {
					Put_into_error_state(this_, ::std::runtime_error("On_error from Ack_synapse_client"));
				};

				try {
					auto & Channel(Ack_synapse_client->Get_channel(1));
					Channel.On_ack = [this, this_](::std::string const & Topic_name, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, data_processors::synapse::amqp_0_9_1::bam::Table * Amqp_headers, bool error) {
						if (!error) {
							try {
								if (Timestamp > 0 && Sequence_number > 0 && !Topic_name.empty() && Topic_name.compare("/")) {
#ifdef FORCE_TRUNCATION_WHILE_SUBSCRIPTION
									synapse::asio::io_service::get().post([this, this_, Topic_name, Timestamp, Sequence_number]() {
										::std::this_thread::sleep_for(std::chrono::seconds(15));
#endif
										Synapse_client->strand.post([this, this_, Topic_name, Timestamp, Sequence_number]() { // postpone for later execution

											auto&& ret = Topic_statistics.find(Topic_name);
											if (ret != Topic_statistics.end()) {
												// The key exists in map. We don't need to do anyhing apart from updating the timestamp and sequence number.
												Update_and_subscribe(this_, ret->second, Timestamp, Sequence_number, Topic_name, true, false);
											}
											else {
												// The topic is not in map. We need to add it to our map and then subscribe to this topic.
												::data_processors::synapse::misc::log("Got ACK for topic " + Topic_name + ", timestamp is " + ::std::to_string(Timestamp) + ".\n", true);
												auto const & result = Topic_statistics.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Topic_name), ::std::forward_as_tuple(0));
												Update_and_subscribe(this_, result.first->second, Timestamp, Sequence_number, result.first->first);
											}
										}); // end of Synapse_client->strand.post
#ifdef FORCE_TRUNCATION_WHILE_SUBSCRIPTION
									});
#endif
								}
								else {
									if (Amqp_headers != nullptr && !Topic_name.compare("/")) {
										Total_topics_to_be_subscribed = (uint64_t)(Amqp_headers->get("Current_subscription_topics_size"));
										::data_processors::synapse::misc::log("Got Current_subscription_topics_size: " + ::std::to_string(Total_topics_to_be_subscribed) + ".\n", true);
									}
									else {
										::data_processors::synapse::misc::log("Topic_name: " + Topic_name + ", Timestamp: " + ::std::to_string(Timestamp) + ", Sequence number: " + ::std::to_string(Sequence_number) + ".\n", true);
										throw ::std::runtime_error("In On_ack, neither Amqp_headers nor Topic_name is valid.");
									}
								}
							}
							catch (::std::exception& e) {
								Put_into_error_state(this_, e);
							}
							catch (...) {
								Put_into_error_state(this_, ::std::runtime_error("Unknow exception in On_ack."));
							}
						}
						else {
							Put_into_error_state(this_, ::std::runtime_error("On_ack report error."));
						}
					}; // end of On_ack call back function.
					   // Subscribe all topics to ACK mode.
					Async_subscribe(this_, Channel, SubscriptionS.begin());
				}
				catch (::std::exception& e) {
					Put_into_error_state(this_, e);
				}
				catch (...) {
					Put_into_error_state(this_, ::std::runtime_error("Unknow exception in Ack_synapse_client->Open callback."));
				}
			}
			else {
				Put_into_error_state(this_, ::std::runtime_error("Could not open Ack_synapse_client connection."));
			}
		});
	}
	/**
	 * This function is to subscribe with ACK mode only.
	 *
	 * @param[this_]: Shared_ptr of Archive_maker, to ensure Archive_maker is valid when this method is executed.
	 * @param[Channel]: An instance of AMQP channel.
	 * @param[I]: Iterator to find the topic to be subscribed.
	 *
	 * @return:
	 */

	void Async_subscribe(::boost::shared_ptr<Archive_maker> const & this_, Synapse_channel_type & Channel, ::std::set<::std::string>::const_iterator I) { 
		if (I != SubscriptionS.end()) {
			typename Synapse_channel_type::Subscription_options Options;
			Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(::boost::gregorian::date(::boost::gregorian::day_clock::universal_day()) + ::boost::gregorian::years(1));
			Options.Supply_metadata = true;
			Options.Preroll_if_begin_timestamp_in_future = true;
			Options.Skipping_period = 60000000u; // => Ack semantics
			Options.Skip_payload = true; // => Ack semantics
			Channel.Subscribe(
				[this, this_, &Channel, I](bool Error) mutable {
					// Error reaction... throw for the time being.
					if (Error)
						Put_into_error_state(this_, ::std::runtime_error("ACK subcsription failed"));
					else
						Async_subscribe(this_, Channel, ++I);
				}
				, *I, Options
			);
		}
		else {
			data_processors::synapse::amqp_0_9_1::bam::Table Fields;
			Fields.set("Current_subscription_topics_size", data_processors::synapse::amqp_0_9_1::bam::Long(1));
			Channel.Subscribe_extended_ack(
				[this, this_](bool Error) {
				if (Error)
					Put_into_error_state(this_, ::std::runtime_error("Error while subscribing for extended Report."));
			}, Fields);
		}
	}
	/**
	 * This function is to subscribe to full mode, so we can retrieve data.
	 *
	 * @param[this_]: Shared_ptr of Archive_maker, to ensure Archive_maker is valid when this method is executed.
	 *
	 * @return:
	 */
	void Async_subscribe(::boost::shared_ptr<Archive_maker> const & this_) {
		if (Can_subscribe && Subscription_fence == 0 && Topic_subscriptions.size() > 0) {
			++Subscription_fence;
			Topic_subscription subscription(Topic_subscriptions.front());
			Topic_subscriptions.pop();
			auto && it(Topic_statistics.find(subscription.Topic_name));
			assert(it != Topic_statistics.end());
			auto Truncate_timestamp(it->second.Truncate_timestamp);
			if (Truncate_timestamp != 0)
				Truncate_timestamp = Verify_truncate_timestamp(it->second.Latest_timestamp, Truncate_timestamp);
			it->second.Subscribed = true;
			auto & Channel(Synapse_client->Get_channel(1));
			// TBD, we may need to update this to use Cork subscription to let server sort data for all subscribed topics.
			typename Synapse_channel_type::Subscription_options Options;
			Options.Begin_utc = subscription.Subscribe_from;
			Options.End_utc = subscription.Subscribe_to;
			Options.Supply_metadata = true;
			Options.Preroll_if_begin_timestamp_in_future = true;
			Options.Atomic_subscription_cork = Cork_subscribe();
			if (Truncate_timestamp != 0) {
				Options._DONT_USE_JUST_YET_Sparsify_upto_timestamp = Truncate_timestamp;
				::data_processors::synapse::misc::log("Subscription with truncated parameter topic=" + subscription.Topic_name + " at " + ::std::to_string(Options._DONT_USE_JUST_YET_Sparsify_upto_timestamp) + "\n", true);
			}
			::data_processors::synapse::misc::log("Subscribe to topic:" + subscription.Topic_name + ", begin_utc: " + ::std::to_string(Options.Begin_utc) + ", truncate: " + ::std::to_string(Truncate_timestamp) + "\n", true);
			Channel.Subscribe([this, this_](bool Error) mutable {
				assert(Subscription_fence == 1);
				--Subscription_fence;
				if (Error)
					Put_into_error_state(this_, ::std::runtime_error("Full mode(non-ACK) subscription failed.")); //Need retry later.
				else {
					if (Topic_subscriptions.size() > 0) {
						Synapse_client->strand.post([this, this_]() {
							Async_subscribe(this_);
						});
					}
				}
			}, subscription.Topic_name, Options);
		}
	}
	/**
	 * To dertimine if we need to subscribe in Cork mode
	 *
	 * @param:
	 *
	 * @return: true: if not running as service and when Subscribe_to is set; false: otherwise.
	 *
	 */
	bool Cork_subscribe() {
		if (!run_as_service) {
			if (Subscribe_to != (uint_fast64_t)-1)
				return ++Subscribed_topics == Total_topics_to_be_subscribed ? false : true;
		}
		return false;
	}


	/**
	 * The method is to update Topic_time_statistics stored in map Topic_statistics, and then subscribe the topic with full mode
	 *
	 * @param[this_]: Shared_ptr of Archive_maker, to ensure when this function is executed, the Archive_maker is valid.
	 * @param[topic_stat]: Instance of Topic_time_statistics
	 * @param[time_stamp]: New timestamp to be stored.
	 * @param[sequence]: New sequence to be stored.
	 * @param[topic]: Topic name
	 * @param[latest]: Update latest related members or archived related members.
	 * @param[subscribe]: Subscribe this topic or not.
	 *
	 * @comment: Please be advised this method must be called in Ack_synapse_client.strand, otherwise the explicited synachronize
	 *           mechanism must be used accordingly.
	 *
	 */
	void Update_and_subscribe(::boost::shared_ptr<Archive_maker> const & this_, Topic_time_statistics& topic_stat, uint_fast64_t time_stamp, uint_fast64_t sequence, const ::std::string& topic, bool latest = true, bool subscribe = true) {
		if (!subscribe && !topic_stat.Subscribed) {
			if (latest) { // Update the latest timestamp and sequence number.
				subscribe = (topic_stat.Archived_timestamp != 0
					&& topic_stat.Latest_timestamp == 0 && time_stamp > 0);
			}
			else { // Update archived timestamp and sequence number.
				subscribe = (topic_stat.Archived_timestamp == 0 && time_stamp > 0
					&& topic_stat.Latest_timestamp != 0);
			}
		}
		if (latest) { // Update the latest timestamp and sequence number.
			if (topic_stat.Latest_timestamp == 0 && topic_stat.Truncate_timestamp != 0) // Topic truncation timestamp is found first
				topic_stat.Truncate_timestamp = Verify_truncate_timestamp(time_stamp, topic_stat.Truncate_timestamp);
			topic_stat.Latest_timestamp = time_stamp, topic_stat.Latest_sequence = sequence;
		}
		else { // Update archived timestamp and sequence number.
			topic_stat.Archived_timestamp = time_stamp, topic_stat.Archived_sequence = sequence;
		}
		if (subscribe) {
			auto Archive_timestamp(::std::max(topic_stat.Archived_timestamp, Subscribe_from));
			if (Archive_timestamp > 5 * 60 * 1000000)
				Archive_timestamp -= 5 * 60 * 1000000; // To ensure there is a 5 minutes overlap between two different segment files.
			// Found latest timestamp for this topic, ask Synapse_client to subscribe it.
			Synapse_client->strand.post([this, this_, topic, Archive_timestamp]() {
				Topic_subscriptions.emplace(topic, Archive_timestamp);
				if (Can_subscribe && Subscription_fence == 0) {
					Async_subscribe(this_);
				}
			});
		}
	}

	/**
	* To find the topic name from the file name. The file name looks like a.b.c.d.e_YYYYMMDD_segment_n
	* or the super long string is embedded in file.
	*
	* @param[file_name]: the file name to be processed.
	* @param[full_path]: Full path of this file.
	*
	* @return: the topic name, the file's segment number ,date, actual data starts position as string and timestamp of the 1st message as a tuple.
	*
	*/
	::std::tuple<::std::string, uint_fast32_t, ::std::string, uint_fast32_t, uint_fast64_t>
		get_topic(const std::string& file_name, const ::std::string& full_path)
	{
		::std::ifstream file(full_path, ::std::ios_base::binary);
		int length(-1);
		file.read(reinterpret_cast<char*>(&length), sizeof(length));
		if (length == -1)
		{
			char name[521];
			file.read(reinterpret_cast<char*>(&length), sizeof(length));
			assert(length > 0 && length < 513);
			int align_len(synapse::misc::round_up_to_multiple_of_po2<uint_fast32_t>(length, 8));
			assert(align_len >= length && align_len % 8 == 0);

			file.read(name, align_len);
			uint_fast64_t Msg_timestamp;
			file.read(reinterpret_cast<char*>(&Msg_timestamp), sizeof(Msg_timestamp));
			name[length] = 0;
			uint64_t Hash, Hash_cal(0);
			Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, static_cast<uint64_t>(-1));
			Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, static_cast<uint64_t>(length));
			file.read(reinterpret_cast<char*>(&Hash), sizeof(Hash));
			for (int k = 0; k < length; ++k)
				Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, name[k]);

			Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, Msg_timestamp);
			assert(Hash == Hash_cal);
			::std::string tempName(name);
			auto && pos(tempName.find_last_of('|'));
			if (length > static_cast<int>(Synapse_at.length() + 1) && pos != tempName.npos) {
				if (Hash == Hash_cal && file_name.length() > 33/*_MD5hash*/)
					return get_topic_internal(tempName.substr(pos + 1) + "_" + file_name.substr(0, file_name.length() - 33), align_len + 24, Msg_timestamp);
				else {
					return get_topic_internal(file_name);
				}
			}
			else {
				return get_topic_internal(file_name);
			}
		}
		else
		{
			return get_topic_internal(file_name);
		}
	}
	/**
	* To get topic name, file's segment number, date from the string, actual data start position is passed in and returned as well.
	*
	* @param[file_name]: string has this convention: [topic name]_YYYYMMDD_segment_[N]
	* @param[file_pos]: The actual start position of archived data.
	*
	* @return: topic name, segment number, date, actual data starts postion and the timestamp for the first message as a tuple.
	*
	*/
	::std::tuple<::std::string, uint_fast32_t, ::std::string, uint_fast32_t, uint_fast64_t>
		get_topic_internal(const std::string& file_name, uint_fast32_t file_pos = 0, uint_fast64_t timestamp = 0)
	{
		auto pos(::std::string::npos);
		// find the last '_' and ensure the result is valid.
		pos = file_name.find_last_of('_', pos);
		if (pos == ::std::string::npos || pos == 0)
			throw ::std::runtime_error("Cannot find a '_' in file name " + file_name);
		const auto & Segment(file_name.substr(pos + 1));
		auto posseg(pos);
		// find the second last '_' and ensure the result is valid.
		pos = file_name.find_last_of('_', --pos);
		if (pos == ::std::string::npos || pos == 0)
			throw ::std::runtime_error(file_name + " doesn't follow our archive file name convention.");
		if (file_name.substr(pos + 1, posseg - pos - 1) != "segment")
			throw ::std::runtime_error(file_name + " doesn't follow our archive file name convention(segment is not found).");
		// find the third last '_' and ensure the result is valid.
		auto pos1(pos);
		pos = file_name.find_last_of('_', --pos);
		if (pos == ::std::string::npos || pos == 0)
			throw ::std::runtime_error(file_name + " doesn't follow our archive file name convention.");
		// get the topic, segment number, date as string and return them as tuple.
		return{ file_name.substr(0, pos), ::std::stoi(Segment), file_name.substr(pos + 1, pos1 - pos - 1), file_pos, timestamp };
	}
	/**
	 * Send alert email about the issue during verify file hash.
	 *
	 * @param[this_]: Shared ptr to ensure Archive_maker is valid when this function is called.
	 * @param[Path]: The full path of the local file whose hash is calculating.
	 * @param[dest_path]: The full path (directory only) of the file on Archivist server
	 * @param[error]: error message.
	 *
	 */
	void Send_alert_email(::boost::shared_ptr<Archive_maker> const & this_, const ::boost::filesystem::path& Path, const ::boost::filesystem::path& dest_path, 
			const ::boost::filesystem::path& relative_path, const ::std::string& error, bool logonly = false) {
		::data_processors::synapse::misc::log("Error: " + error + ", Path: " + Path.string() + "\n", true);
		if (!logonly) {
			::data_processors::synapse::asio::io_service::get().post([this, this_, Path, dest_path, relative_path, error]() {
				// Send email here by calling into a psl file.
				bool powershell(false);
				::std::string reports(Get_alert_command(Timediff_alert, powershell));
				reports += std::string(powershell ? " \"" : " ");
				reports += "\"" + error + ", local file: " + Path.string() + ", archivist file: " + (dest_path / (relative_path.string() + ".sh256")).string();
				reports += ::std::string(powershell ? "\"\"\"" : "\"");
				// Sending report in another thread.
				synapse::asio::io_service::get().post([reports]() {
					int ret = ::system(reports.c_str());
					data_processors::synapse::misc::log("After calling system(" + reports + "), return value is " + ::std::to_string(ret) + ".\n", true);
				});
			});
		}
	}

	/**
	 * To remove archived files if the condition is met, otherwise return false.
	 *
	 * @param[Current_date]: current date as boost::gregorian:date
	 * @param[date]: Folder creation date as string YYYYMMDD
	 * @param[path]: Path of the folder to be checked.
	 * @param[diff]: Minimum of Days_to_delete_archived_files and Days_to_truncate_topic.
	 * @param[All_topics]: All topics to be removed if condition is met.
	 *
	 * @return: true if condition is met and files are removed; otehrwise return false.
	 */
	bool Remove_archived_file(const ::std::string& topic, const ::std::string& date, const ::std::string& path, long diff, const ::std::map<::std::string, ::std::vector<::std::string>>& Topics) {
		auto const & Vec(Topics.find(topic));
		assert(Vec->second.size() > 0);
		auto const & it = ::std::find(Vec->second.begin(), Vec->second.end(), date);
		assert(it != Vec->second.end());
		if (Days_to_delete_archived_files > 0 && std::distance(it, Vec->second.end()) + diff /*diff days are removed before*/ > Days_to_delete_archived_files) {
			::boost::filesystem::path Path(path);
			Path /= topic;
			::boost::filesystem::remove_all(Path.string());
			::data_processors::synapse::misc::log("All files from topic " + topic + " in folder " + path + " are removed, as they are older than " + ::std::to_string(Days_to_delete_archived_files) + " days.\n", true);
			if (::boost::filesystem::is_empty(path)) {
				// Only remove parent path if it is empty
				::boost::filesystem::remove_all(path);
				::data_processors::synapse::misc::log("Folder " + path + " is removed as well because it is empty.\n", true);
			}
			return true;
		}
		else {
			::data_processors::synapse::misc::log("Files in folder " + path + " will not be removed, as the condition is not met.\n", true);
			return false;
		}
	}
	/**
	 * To check if all files copied by archivist are identical with what we archived, if yes, we will remove 
	 * those files older than Days_to_delete_archived_files, and truncate topic older than Days_to_truncate_topic;
	 * if no, send email alert and stop further processing.
	 *
	 * @param[this_]: Shared_ptr of Archive_maker, to ensure this_ is valid when this function is running
	 *
	 */
	void Archived_file_management(::boost::shared_ptr<Archive_maker> const & this_) {
		std::map<::std::string, ::std::string> Files_map(::std::move(Date_files_map));
		std::map<::std::string, ::std::vector<::std::string>> All_topics(::std::move(Check_all_topics));
		try {
			if (Days_to_delete_archived_files <= 0 && Days_to_truncate_topic <= 0 || Days_to_delete_archived_files > 36500 || Days_to_truncate_topic > 36500 || Archivist_path.empty()) {
				::data_processors::synapse::misc::log("At least one of settings is/are invalid (Days_to_delete_archived_files = " + ::std::to_string(Days_to_delete_archived_files) + ", Days_to_truncate_topic = " + ::std::to_string(Days_to_truncate_topic) + "), archived files will not be checked.\n", true);
				return;
			}
			::data_processors::synapse::misc::log("Start archived file management, Days_to_delete_archived_files = " + ::std::to_string(Days_to_delete_archived_files) + ", Days_to_truncate_topic = " + ::std::to_string(Days_to_truncate_topic) + ".\n", true);
			unsigned temp1(Days_to_delete_archived_files), temp2(Days_to_truncate_topic);
			long diff(::std::min(temp1, temp2));
			// Remove the last diff dates for each topic, apparently for the last diff days archived files, we don't need to
			// worry about them, as they will not be removed or truncated.
			for (auto && it(All_topics.begin()); it != All_topics.end(); ++it) {
				::std::sort(it->second.begin(), it->second.end()); //To ensure they are in order.
				if (it->second.size() >= static_cast<size_t>(diff)) {
					it->second.erase(it->second.begin() + (it->second.size() - diff), it->second.end());
				}
				else {
					it->second.clear();
				}
			}
			// Now we will remove archived files and truncate topic if they are backup successfully by our Archivist.
			// As described above, this will be completed one topic after anotehr topic.
			for (auto && it(All_topics.begin()); it != All_topics.end(); ++it) {
				auto const & topic(it->first);
				uint_fast64_t max_timestamp{ 0 };
				if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
				for (auto && date_it(it->second.begin()); date_it != it->second.end(); ++date_it) {
					auto const & date(*date_it);
					if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
					::data_processors::synapse::misc::log("Topic:" + topic + ", Date: " + date + ", Path: " + Files_map[date] + "\n", true);
					::std::string dest_path(Archivist_path);

					// The destination folder like this: \\ult-backup4\h-drive\rawdata\YYYY\YYYYMM\datasrc\synapse\tyojp-netctl4\YYYYMMDD\,
					// so we need to replace date with actual value.
					::boost::replace_all(dest_path, "YYYYMMDD", date);
					::boost::replace_all(dest_path, "YYYYMM", date.substr(0, 6));
					::boost::replace_all(dest_path, "YYYY", date.substr(0, 4));
					bool running{ true };
					::data_processors::synapse::misc::log("Archivist path: " + dest_path + "\n", true);
					std::unique_ptr<char> Buff(new char[8192]);
					for (::boost::filesystem::recursive_directory_iterator archived_iterator(Files_map[date]); archived_iterator != ::boost::filesystem::recursive_directory_iterator(); ++archived_iterator) {
						if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
						auto const & Path(archived_iterator->path());
						if (::boost::filesystem::is_directory(Path)) {
							if (Path.filename().string() != topic)archived_iterator.no_push();
							continue;
						}
						auto const & Relative_path(::boost::filesystem::relative(Path, ::boost::filesystem::path(Files_map[date])));
						auto const & File_name(Path.filename().string());
						auto const & Topic_segment_and_date(get_topic(File_name, Path.string()));
						auto const & Topic_name(::std::get<0>(Topic_segment_and_date));
						auto const & Timestamp(::std::get<4>(Topic_segment_and_date));
						assert(topic == Topic_name);
						::std::ifstream f2((::boost::filesystem::path(dest_path) / (Relative_path.string() + ".sha256")).string());
						std::string archivist_hash;
						if (!f2 || !::std::getline(f2, archivist_hash)) {
							Send_alert_email(this_, Path, dest_path, Relative_path, "Could not read SHA256 hash from Archivist.", true);
							running = false;
							// file is not copied to archivist or checksum is not generated yet, so just break.
							break;
						}
						f2.close();
						::boost::erase_all(archivist_hash, " ");
						// We calculate SHA256 manually is because the file may be very big, couple of GBs or even TBs, so
						// it will take too long time to calculate this hash, and if during this procedure, our application
						// is asked to terminate, then it is not easy to break the caculation procedure if the other utility
						// is used to calculate this sha256 hash.
						Generic_Hash<SHA256_CTX, SHA256_Init, SHA256_Update, SHA256_Final, SHA256_DIGEST_LENGTH> SHA256;
						if (!SHA256.Init()) {
							Send_alert_email(this_, Path, dest_path, Relative_path, "Failed to initialize SHA256 context.");
							return;
						}
						std::ifstream f1(Path.string(), ::std::ios_base::binary);
						if (f1) {
							::std::streamsize size;
							while (1) {
								f1.read(Buff.get(), 8192);
								size = f1.gcount();
								// To check if Exit_error is set and return if it is set in case the file is huge.
								if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
								if (!SHA256.Update(Buff.get(), size)) {
									Send_alert_email(this_, Path, dest_path, Relative_path, "Failed to update SHA256 context.");
									return;
								}
								if (!f1)break;
							}
							if (!SHA256.Final()) {
								Send_alert_email(this_, Path, dest_path, Relative_path, "Failed to finalize SHA256 context.");
								return;
							}

						}
						else {
							Send_alert_email(this_, Path, dest_path, Relative_path, "Failed to open local file.");
							return;
						}
						auto && cal_hash(SHA256.HexedHash());
						if (cal_hash != archivist_hash) {
							Send_alert_email(this_, Path, dest_path, Relative_path, "Calculated hash(" + cal_hash + ") is not equal to Archivist's hash(" + archivist_hash + ").");
							return;
						}
						else {
							// record it in a map, as we may need this to truncate topics later.
							if (Timestamp > max_timestamp)max_timestamp = Timestamp;
							::data_processors::synapse::misc::log("Hash of file " + Path.string() + " is verified.\n", true);
						}
					}
					if (!running)break;
					// All files are identical, we can remove this folder and all files in this folder if it is in the remove file range.
					if (!Remove_archived_file(topic, date, Files_map[date], diff, All_topics)) {
						data_processors::synapse::misc::log("All files for topic " + topic + " in folder " + Files_map[date] + " are checked, but they will not be removed as the condition is not met.\n", true);
					}
				}
				// Truncate topics now
				::data_processors::synapse::misc::log("max_timestamp=" + ::std::to_string(max_timestamp) + "\n", true);
				if (Days_to_truncate_topic > 0 && max_timestamp > 0) {
					// Truncate topic from this timestamp.
					uint_fast64_t adjust{ 0 };
					if (Days_to_truncate_topic >= diff)
						adjust += 86400LL * (Days_to_truncate_topic - diff) * 1000000;
					::data_processors::synapse::misc::log("adjust=" + ::std::to_string(adjust) + "\n", true);
					if (max_timestamp > adjust) {
						// Truncate here
						auto && truncate_time(max_timestamp - adjust);
						::data_processors::synapse::misc::log("Truncate topic " + topic + " at " + ::std::to_string(truncate_time) + "\n", true);
						Synapse_client->strand.post([this, this_, topic, truncate_time]() { Truncate_topic(this_, topic, truncate_time); });
					}
				}
			}
		}
		catch (::std::exception & e) {
			// log only, do not shutdown application.
			::data_processors::synapse::misc::log(::std::string(e.what()) + " raised in Archive_file_management.\n", true);
		}
		catch (...) {
			::data_processors::synapse::misc::log("Unknow exception in Archive_file_management.\n", true);
		}
	}
	/**
	 * To truncate a topic by the given timestamp
	 *
	 * @param[this_], instance of Archive_maker, to ensure it is valid when this function is executing.
	 * @param[topic], topic name
	 * @param[timestamp], suggest timestamp to truncate the topic.
	 *
	 *
	 */
	void Truncate_topic(::boost::shared_ptr<Archive_maker> const & this_, ::std::string const & topic, uint_fast64_t timestamp) {
		auto && ret(Topic_statistics.find(topic));
		if (ret != Topic_statistics.end()) {
			ret->second.Truncate_timestamp = timestamp;
			if (ret->second.Subscribed) {
				// If the topic is already subscribed, then we truncte this topic directly.
				timestamp = Verify_truncate_timestamp(ret->second.Latest_timestamp, ret->second.Truncate_timestamp);
				Synapse_client->strand.post([this, this_, topic, timestamp]() {
					Topic_to_be_truncated.emplace(topic, timestamp);
					if (!Topic_is_truncating) {
						Truncate_topic_internal(this_);
					}
				});
			}
		}
	}
	/**
	 * Topic is actually truncated here (to queue topic truncation request).
	 *
	 * @param[this_]: shared_ptr of Archive_maker, to ensure instance of Archive_maker is valid when this function is executing.
	 *
	 * @remarks: the function must be called in Synapse_client's strand.
	 *
	 */
	void Truncate_topic_internal(::boost::shared_ptr<Archive_maker> const & this_) {
		if (!Topic_is_truncating && !Topic_to_be_truncated.empty()) {
			Topic_is_truncating = true;
			auto && it(Topic_to_be_truncated.begin());
			auto topic(it->first);
			auto timestamp(it->second);
			Topic_to_be_truncated.erase(it);
			auto & Channel(Synapse_client->Get_channel(2));
			::data_processors::synapse::misc::log("Truncate topic directly, topic: " + topic + ", timestamp: " + ::std::to_string(timestamp) + "\n", true);
			Channel._DONT_USE_JUST_YET_Set_topic_sparsification([this, this_, topic, timestamp](bool Error) mutable {
				assert(Topic_is_truncating);
				Topic_is_truncating = false;
				if (Error) {
					Put_into_error_state(this_, ::std::runtime_error("_DONT_USE_JUST_YET_Set_topic_sparsification returns Error."));
				}
				else {
					::data_processors::synapse::misc::log("Successfully truncate topic " + topic + " at " + ::std::to_string(timestamp) + "\n", true);
					// Next one
					Synapse_client->strand.post([this, this_]() { Truncate_topic_internal(this_); });
				}
			}, topic, 0, timestamp);
		}
	}
	/**
	 * To further check if the suggested topic truncation timestamp is valid or not.
	 *
	 * @param[Latest_timestamp]: the latest message timestamp got from server via ACK subscription.
	 * @param[Truncate_timestamp]: suggest truncate timestamp.
	 *
	 * @return: new truncate timestamp (re-calculated based on the Latest_timestamp rather than current date).
	 *
	 */
	uint_fast64_t Verify_truncate_timestamp(uint_fast64_t Latest_timestamp, uint_fast64_t Truncate_timestamp) {
		auto const && Latest_date((data_processors::synapse::misc::epoch + ::boost::posix_time::microseconds(Latest_timestamp)).date());
		auto const && Truncate_date((data_processors::synapse::misc::epoch + ::boost::posix_time::microseconds(Truncate_timestamp)).date());
		if ((Latest_date - Truncate_date).days() > Days_to_truncate_topic) {
			return Truncate_timestamp;
		}
		else {
			auto && New_date(Latest_date - ::boost::gregorian::date_duration(Days_to_truncate_topic));
			auto timestamp((::boost::posix_time::ptime(New_date, ::boost::posix_time::seconds(0)) - data_processors::synapse::misc::epoch).total_microseconds());
			if (timestamp < 0)timestamp = 0;
			return timestamp;
		}
	}
	/**
	 * To traverse all archived files and get last timestamp and sequence number from them.
	 *
	 * @param[this_]: Shared_ptr of Archive_maker, to ensure Archive_maker is valid when this method is executed.
	 *
	 * @return:
	 */
	void Traverse_archived_files(::boost::shared_ptr<Archive_maker> const & this_) {
		::std::map<::std::string/*Topic name*/, Latest_archived_files> Topic_latest_archived_files;
		// First pass -- get archived timestamp and sequence number from savepoint files.
		::std::set<::std::string> Savepoints;
		for (::boost::filesystem::recursive_directory_iterator Savepoints_iterator(Save_point_path); Savepoints_iterator != ::boost::filesystem::recursive_directory_iterator(); ++Savepoints_iterator) {
			auto const & Path(Savepoints_iterator->path());
			if (::boost::filesystem::is_regular_file(Path)) {
				::std::ifstream Savepoint_file(Path.string(), ::std::ios_base::binary);
				uint64_t Timestamp, Sequence;		
				while (Savepoint_file) {
					Savepoint_file.read(reinterpret_cast<char *>(&Timestamp), sizeof(Timestamp));
					Savepoint_file.read(reinterpret_cast<char *>(&Sequence), sizeof(Sequence));
					uint64_t Hash, Hash_calc(data_processors::synapse::misc::crc32c(static_cast<uint64_t>(0), Timestamp));
					Hash_calc = data_processors::synapse::misc::crc32c(Hash_calc, Sequence);
					Savepoint_file.read(reinterpret_cast<char *>(&Hash), sizeof(Hash));

					if (Hash_calc == Hash) {
						::std::string Topic_name(Path.filename().string());
						Topic_statistics.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Topic_name), ::std::forward_as_tuple(le64toh(Timestamp), le64toh(Sequence)));
						Savepoints.insert(Topic_name);
						::data_processors::synapse::misc::log("Find timestamp and sequence for topic: " + Topic_name + " (" + ::std::to_string(le64toh(Timestamp)) + "," + ::std::to_string(le64toh(Sequence)) + ").\n", true);
						break;
					}
				}
			}
		}
		// Get all date folder out.
		for (::boost::filesystem::recursive_directory_iterator Segment_path_iterator(archive_root); Segment_path_iterator != ::boost::filesystem::recursive_directory_iterator(); ++Segment_path_iterator) {
			if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
			auto const & Path(Segment_path_iterator->path());

			if (::boost::iequals(Path.filename().string(), "savepoints")) {
				Segment_path_iterator.no_push();
				continue;
			}
			if (::boost::filesystem::is_directory(Path)) {
				Date_files_map.emplace(Path.filename().string(), Path.string());
				Segment_path_iterator.no_push();
				continue;
			}
		}

		// convert the structure from map of date to map of topic, as each topic may have different latest archived timestamp,
		// so we will have to remove/truncate it per topic rather than per date (folder).
		// Apart from this will mess the arhived folder eventually, it is a pretty good solution.
		//
		// Do this here will remove quite a lot of troubles as once subscription is started, new topic folder may be created
		// in any date, and this is particular important while considering we will add multiple tcp connection support later.
		for (auto && archive_path(Date_files_map.begin()); archive_path != Date_files_map.end(); ++archive_path) {
			if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
			for (::boost::filesystem::recursive_directory_iterator archived_iterator(archive_path->second); archived_iterator != ::boost::filesystem::recursive_directory_iterator(); ++archived_iterator) {
				if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
				auto const & Path(archived_iterator->path());
				if (::boost::filesystem::is_directory(Path)) {
					if (Check_all_topics.find(Path.filename().string()) == Check_all_topics.end())
						Check_all_topics.emplace(Path.filename().string(), ::std::vector<::std::string>{ archive_path->first });
					else
						Check_all_topics[Path.filename().string()].emplace_back(archive_path->first);
					archived_iterator.no_push();
					continue;
				}
			}
		}
		// Second pass -- for each topic, get all of the segment files in the latest folder (Date as folder).
		for (::boost::filesystem::recursive_directory_iterator Segment_path_iterator(archive_root); Segment_path_iterator != ::boost::filesystem::recursive_directory_iterator(); ++Segment_path_iterator) {
			if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
			auto const & Path(Segment_path_iterator->path());

			if (::boost::iequals(Path.filename().string(), "savepoints")) {
				Segment_path_iterator.no_push();
				continue;
			}
			if (::boost::filesystem::is_directory(Path)) {
				continue;
			}

			auto const & File_name(Segment_path_iterator->path().filename().string());
			auto const & Topic_segment_and_date(get_topic(File_name, Path.string()));
			auto const & Topic_name(::std::get<0>(Topic_segment_and_date));
			auto const & Segment(::std::get<1>(Topic_segment_and_date));
			auto const & Date(::std::get<2>(Topic_segment_and_date));
			auto const & Position(::std::get<3>(Topic_segment_and_date));
			if (Savepoints.count(Topic_name) == 0) {
				Topic_statistics.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Topic_name), ::std::forward_as_tuple(0));
				auto const ret = Topic_latest_archived_files.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Topic_name), ::std::forward_as_tuple(Date, Segment, Path.string(), Position));
				if (!ret.second) {
					if (Date > ret.first->second.Date) {
						ret.first->second.Clear();
						ret.first->second.Date = Date;
						ret.first->second.Append(Segment, Path.string(), Position);
					}
					else if (Date == ret.first->second.Date) {
						ret.first->second.Append(Segment, Path.string(), Position);
					}
				}
			}
			else {
//				::data_processors::synapse::misc::log("Found timestamp and sequence for topic: " + Topic_name + ", will not scan its files.\n");
			}
		}
		Total_file_count = Topic_latest_archived_files.size();
		if (Total_file_count > 0)
			All_files_scanned = false;
		else {
			// only start this job when all local files are scanned (for latest timestamp and sequence number) to avoid the potential conflict.
			::data_processors::synapse::asio::io_service::get().post([this, this_]() {
				Archived_file_management(this_);
			});
		}
		// Third pass, find the timestamp and sequence number for the laste archived message(valid), and add them to Topic_statistics,
		for (auto x = Topic_latest_archived_files.begin(); x != Topic_latest_archived_files.end(); ++x) {
			auto Topic_name(x->first);
			auto&& Files(::std::move(x->second.Files));
			::data_processors::synapse::asio::io_service::get().post([this, this_, Topic_name, Files]() {
				uint_fast64_t Timestamp = 1000000;
				uint_fast64_t Sequence = 0;
				uint_fast64_t begin = ::data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::microsec_clock::universal_time());
				// Process all files in reverse order, as when the second seqgment file is created, the previous one must already existed.
				for (auto f = Files.rbegin(); f != Files.rend(); ++f) { // As the files are sorted by sequence number, therefore we process them in reverse order.
					if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return; // In error state, io_service should stop, therefore we should return asap.
					unsigned constexpr ifs_buffer_size(4 * 1024); //From my testing, the big buffer size does not improve performance.
					::std::unique_ptr<char> ifs_buffer(new char[ifs_buffer_size]);
					::std::unique_ptr<char> Message_buffer(new char[8]);
					::std::ifstream Segment_file;
					Segment_file.rdbuf()->pubsetbuf(ifs_buffer.get(), ifs_buffer_size);
					Segment_file.open(f->second.File_name, ::std::ios::binary);
					uint_fast64_t total_length{ 0 };
					if (Segment_file) {
						if (f->second.Position != 0)
							Segment_file.seekg(f->second.Position, ::std::ios_base::beg); // Advance file is Position is not equal to 0 (means topic name and related info is embedded in file)
						std::vector<uint_fast32_t> temp{ 0 }; // To record all messages' offset in file
						while (Segment_file.read(Message_buffer.get(), 8)) {
							if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return; // In error state, we should return asap.
							::data_processors::synapse::database::Message_const_ptr const Message(Message_buffer.get());
							auto const msg_size_on_disk(Message.Get_size_on_disk());
							if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= ::data_processors::synapse::database::MaxMessageSize) {
								// First pass, seek only to jump to the last message and record length of all messages.
								if (!Segment_file.seekg(msg_size_on_disk - 8, ::std::ifstream::cur))break;
								temp.push_back(msg_size_on_disk);
								total_length += msg_size_on_disk;
							}
							else {
								::data_processors::synapse::misc::log("Corruput segment(" + f->second.File_name + ").\n", true);
								break;
							}
						}
						// No any valid element in vector temp, skip this file.
						if (temp.empty()) continue;
						Message_buffer.reset(new char[*::std::max_element(temp.begin(), temp.end())]);
						Segment_file.clear(); //reset
						for (auto it = temp.rbegin(); it != temp.rend(); ++it) { // process message in reverse order as we want to find the latest archived timestamp.
							if (data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))return;
							auto const msg_size_on_disk(*it);
							total_length -= msg_size_on_disk;
							if (Segment_file.eof())Segment_file.clear();
							// Second pass, seek to the start of message in reverse order and try to get the timestamp of this message
							Segment_file.seekg(total_length + f->second.Position);
							if (Segment_file.read(Message_buffer.get(), 8)) {
								::data_processors::synapse::database::Message_const_ptr const Message(Message_buffer.get());
								auto const Extra_bytes_to_read(msg_size_on_disk - 8);
								if (!Extra_bytes_to_read || Segment_file.read(Message_buffer.get() + 8, Extra_bytes_to_read)) {
									if (Message.Get_hash() == Message.Calculate_hash()) {
										if (Message.Get_indicies_size() ==  ::data_processors::synapse::database::Message_Indicies_Size) {
											if (Message.Get_index(1)) {
												if (Timestamp < Message.Get_index(1)) {
													// Get valid timestamp and sequence number.
													Timestamp = Message.Get_index(1);
													Sequence = Message.Get_index(0);
													break; // We don't need process all other messages if we've already found a valid one.
												}
												else {
													// Current known timestamp is greater than the timestamp of the last message, therefore we don't need to
													// search all messages in this file.
													break;
												}
											}
											else {
												::data_processors::synapse::misc::log("info/error no timestamp is allowed to be zero at the moment, early exiting: " + Topic_name + ".\n", true);
												continue;
											}
										}
										else {
											::data_processors::synapse::misc::log("info/error indicies-size, early exiting: " + Topic_name + ".\n", true);
											break;
										}
									}
									else {
										::data_processors::synapse::misc::log("info/error crc32 check: " + Topic_name + ".\n", true);
										continue;
									}
								}
								else {
									::data_processors::synapse::misc::log("Corruput segment(" + f->second.File_name + ").\n", true);
									continue;
								}
							}
						}
						temp.clear();
						Segment_file.close();
					}
				}
				uint_fast64_t end = ::data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::microsec_clock::universal_time());
				::data_processors::synapse::misc::log("It took Archive_maker " + ::std::to_string((end - begin) / 1000) + " ms to process files for topic " + Topic_name + " Found timestamp is: " + ::std::to_string(Timestamp) + ".\n", true);
				Synapse_client->strand.post([this, this_, Timestamp, Sequence, Topic_name]() { // Topic_statistics is managed by Synapse_client->strand
					--Total_file_count;
					if (Total_file_count == 0) {
						All_files_scanned = true;
						// only start this job when all local files are scanned (for latest timestamp and sequence number) on avoid the potential conflict.
						::data_processors::synapse::asio::io_service::get().post([this, this_]() {
							Archived_file_management(this_);
						});
					}
					auto ret = Topic_statistics.find(Topic_name);
					if (ret != Topic_statistics.end()) {
						Update_and_subscribe(this_, ret->second, Timestamp, Sequence, Topic_name, false, false);
					}
				});
			});
		}
		Topic_latest_archived_files.clear();
	}
};

void static default_parse_arg(int const argc, char* argv[], int & i) {
	if (i == argc - 1)
		throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
	else if (!::strcmp(argv[i], "--log_root")) {
		Log_path = argv[i + 1];
		data_processors::synapse::misc::log.Set_output_root(argv[++i]);
	}
	else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
	if (!::strcmp(argv[i], "--ssl_key"))
		ssl_key = argv[++i];
	else if (!::strcmp(argv[i], "--ssl_certificate"))
		ssl_certificate = argv[++i];
	else if (!::strcmp(argv[i], "--so_rcvbuf"))
		so_rcvbuf = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--so_sndbuf"))
		so_sndbuf = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--concurrency_hint"))
		concurrency_hint = ::boost::lexical_cast<unsigned>(argv[++i]);
	else if (!::strcmp(argv[i], "--archive_root"))
		archive_root = argv[++i];
	else if (!::strcmp(argv[i], "--subscribe_from"))
		Subscribe_from = data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::from_iso_string(argv[++i]));
	else if (!::strcmp(argv[i], "--subscription"))
		SubscriptionS.emplace(argv[++i]);
	else if (!::strcmp(argv[i], "--startpoint_filepath"))
		Startpoint_filepath = argv[++i];
	else if (!::strcmp(argv[i], "--timediff_tolerance")) //Time difference tolerance, default is 1, measured in Timediff_scale (default is in days).
		Timediff_tolerance = ::std::stoi(argv[++i]);
	else if (!::strcmp(argv[i], "--timediff_alert")) //command to be executed when timestamp of received message is far behind current time.
		Timediff_alert = argv[++i];
	else if (!::strcmp(argv[i], "--check_timestamp_period")) //How often the time difference check will happen, default is 3 hours.
		Check_timestamp_period = ::std::stoul(argv[++i]);
	else if (!::strcmp(argv[i], "--alarm_interval")) // How often the alarm will be sent out, default is 1 day.
		Alarm_interval = ::std::stoull(argv[++i]) * 1000000;
	else if (!::strcmp(argv[i], "--use_current_time")) // Report is generated based on current time or not, default is not -- use ACK from Synapse server.
		Use_current_time = ::std::stoi(argv[++i]);
	else if (!::strcmp(argv[i], "--subscribe_to"))
		Subscribe_to = data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::from_iso_string(argv[++i]));
	else if (!::strcmp(argv[i], "--days_to_delete_archived_file"))
		Days_to_delete_archived_files = ::std::stoi(argv[++i]);
	else if (!::strcmp(argv[i], "--days_to_truncate_topic"))
		Days_to_truncate_topic = ::std::stoi(argv[++i]);
	else if (!::strcmp(argv[i], "--archivist_path"))
		Archivist_path = argv[++i];
	else
		throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');
}


void static run_(int argc, char* argv[]) {
	try {
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

		synapse::misc::log(">>>>>>> BUILD info: " BUILD_INFO " main source: " __FILE__ ". date: " __DATE__ ". time: " __TIME__ ".\n", true);
		// To adjust maximum allowed concurrently open files at stdio level, this is just a temporary solution,
		// in the future, the Archive_maker will subscribe and archive topics in batch (for example, 10 only,
		// once one is completed, it will unsubscribe it and subscribe a new topic).
		_setmaxstdio(2048);
		::data_processors::synapse::misc::log("After calling _setmaxstdio with 2048 as argument, now the allowed concurrent open files is: " + ::std::to_string(_getmaxstdio()) + "\n", true);

		if (!((Timediff_tolerance > 0 && Timediff_alert.length() > 0) || (Timediff_tolerance <= 0 && Timediff_alert.length() == 0))) {
			// Timediff_tolerance and Timediff_alert must be both valid or neither is valid.
			throw ::std::runtime_error("Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set.");
		}

		data_processors::synapse::misc::log("Archive maker starting.\n", true);
		if (archive_root.string().empty()) {
			throw ::std::runtime_error("Archive_root must be provided.");
		}
		if (!::boost::filesystem::exists(archive_root)) {
			if (!::boost::filesystem::create_directories(archive_root)) {
				data_processors::synapse::misc::log("Could not create directories (" + archive_root.string() + ").\n", true);
				throw ::std::runtime_error("Failed to create directory (" + archive_root.string() + ").");
			}
		}
		Save_point_path = archive_root;
		Save_point_path /= "savepoints";
		if (!::boost::filesystem::exists(Save_point_path)) {
			if (!::boost::filesystem::create_directory(Save_point_path)) {
				data_processors::synapse::misc::log("Could not create directory (" + Save_point_path.string() + ").\n", true);
				throw ::std::runtime_error("Failed to create directory (" + Save_point_path.string() + ").");
			}
		}
		if (!concurrency_hint)
			concurrency_hint = data_processors::synapse::misc::concurrency_hint();

		// create based on chosen runtime parameters
		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		// Automatic quitting handling
		data_processors::synapse::asio::Autoquit_on_signals Signal_quitter;
#ifndef GDB_STDIN_BUG
		data_processors::synapse::asio::Autoquit_on_keypress Keypress_quitter;
#endif
		::boost::shared_ptr<Archive_maker<data_processors::synapse::asio::ssl_tcp_socket>> ssl_server;
		::boost::shared_ptr<Archive_maker<data_processors::synapse::asio::tcp_socket>> tcp_server;
		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);
		if (!ssl_key.empty() || !ssl_certificate.empty()) {
			if (ssl_key.empty() || ssl_certificate.empty())
				throw ::std::runtime_error("not all SSL options have been specified. mandatory:{--ssl_key, --ssl_certificate} optional: --ssl_listen_on");

			data_processors::synapse::misc::log("Listening in SSL on " + host + ':' + port + '\n', true);
			data_processors::synapse::asio::io_service::init_ssl_context();

			auto && ssl(data_processors::synapse::asio::io_service::get_ssl_context());
			ssl.use_certificate_file(ssl_certificate, basio::ssl::context::pem);
			ssl.use_private_key_file(ssl_key, basio::ssl::context::pem);

			::boost::make_shared<Archive_maker<data_processors::synapse::asio::ssl_tcp_socket>>()->Run();
			ssl_server->Run();
		}
		else {
			tcp_server = ::boost::make_shared<Archive_maker<data_processors::synapse::asio::tcp_socket>>();
			tcp_server->Run();
		}

		// Start a timer, which is to check the difference between message received and current time.
		::boost::asio::deadline_timer t(synapse::asio::io_service::get(), ::boost::posix_time::seconds(Check_timestamp_period));

		::std::function<void(::boost::system::error_code const &)> on_reporting_timeout = 
			[&t, &on_reporting_timeout, &tcp_server, &ssl_server]
		(::boost::system::error_code const & ec) {
			if (!ec) {
				if (tcp_server)
					tcp_server->Report(tcp_server, Use_current_time == 0 ? false : true);
				else
					ssl_server->Report(ssl_server, Use_current_time == 0 ? false : true);

				t.expires_at(t.expires_at() + ::boost::posix_time::seconds(Check_timestamp_period));
				t.async_wait(on_reporting_timeout);
			}
		};
		if (Timediff_tolerance > 0) // Only start this timer when Timediff_tolerance is set.
			t.async_wait(on_reporting_timeout);

		::std::vector<::std::thread> pool(concurrency_hint * 8 - 1);
		for (auto && i : pool) {
			i = ::std::thread([]()->void{
				data_processors::synapse::asio::run();
			});
		}

		data_processors::synapse::asio::run();

		for (auto & i: pool)
			i.join();
	} catch (::std::exception const & e) {
#ifdef __WIN32__
		if (run_as_service == true)
			service_status.dwWin32ExitCode = -1;
#endif
		data_processors::synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
		data_processors::synapse::asio::Exit_error = true;
	}
#ifdef __WIN32__
	if (run_as_service == true) {
		if (require_stop) {
			service_status.dwCurrentState = SERVICE_STOPPED;
			service_status.dwControlsAccepted = 0;
			::SetServiceStatus(service_handle, &service_status);
		}
		else {
			if (::data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))
			{
				throw ::std::runtime_error("To force our service restart.");
			}
		}
	}
#endif
	data_processors::synapse::misc::log("Bye bye.\n", true);
}

int static run(int argc, char* argv[]) {
	try {
		// CLI...
		for (int i(1); i != argc; ++i) {
#ifdef __WIN32__
			if (!::strcmp(argv[i], "--run_as_service"))
				run_as_service = true;
			else
#endif
			if (!::strcmp(argv[i], "--terminate_when_no_more_data"))
				Terminate_when_no_more_data = 1;
			else
				default_parse_arg(argc, argv, i);
		}
		Synapse_at = host + ":" + port;
#ifdef __WIN32__
		if (run_as_service == true) {
			::SERVICE_TABLE_ENTRY dispatch_table[2];
			::memset(dispatch_table, 0, sizeof(dispatch_table));
			dispatch_table[0].lpServiceName = ::data_processors::Archive_maker::service_name;
			dispatch_table[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)::data_processors::Archive_maker::run_;
			::std::atomic_thread_fence(::std::memory_order_release);
			return !::StartServiceCtrlDispatcher(dispatch_table);
		}
#endif
		run_(0, nullptr); // dummy for the sake of windows nonsense...
	} catch (::std::exception const & e) {
		data_processors::synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
	}
	return data_processors::synapse::asio::Exit_error;
}

}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[]) {
	#if !defined(NDEBUG) && defined(__WIN32__)
		::SetErrorMode(0);
	#endif
	return ::data_processors::Archive_maker::run(argc, argv);
}

