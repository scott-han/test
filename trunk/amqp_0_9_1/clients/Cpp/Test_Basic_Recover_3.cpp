/*
If a crash occurs and metadata has not been committed to yet, then various 'begin key/data' offsets may be pointing to the invalid (zeroes/sparse/deleted) data. Basic recover needs to detect and deal with this. SYN-206 (aka TDC-206).

Overall approach...

Publish a lot of messages (size-wise)
Each message sequence number and timestamp should be far apart form the preceeding message to cause sufficient number of entries in the indicies
Stop server (nicely)
Just zero-out some starting data for all index files as well as data file
Re-write the metadata files (change starting byte offset for data; initial index as appropriate for indicies – all pointing to some amount of offset from start location in the underlying file but knowingly into the zeroed-out content; then recalculate checksum and write-out the metafiles).
Run basic recover – inplace and from eof pointing to the valid data (not zeroed out)
Start server and subscribe
Verify that subscriber sees the last-published messages (timestamp/sequence number).

More specifics...

During the Publish stage:
Publish 1000 messages. Each message advances:
Timestamp by 2 sec (i.e. > quantization value of time index)
Sequence number by 2000 (i.e. > quantization value of sequence number index)
Message size is > 1MB (i.e. definitely larger than message_boundary index quantization value)

During Corruption stage:
Verify that each index has no less than 1000 entries (based on index size and key size – both obtained from metadata file)
Also verify that read metadata values for index quantization are less than those we used to publish with (i.e. advancing our timestamp etc. – asserting the above bullet points before the "Corruption" stage.
Zero every data file up to 50% in size (1st 50%)
Re-write metadata 'begin' values to point to 25% from the start of the file/index/etc.

During Recovery stage:
Run basic_recover inline, and 25% from eof (75% from start that is), based on timestamp value.
*/

#include <iostream>
#include <atomic>
#include <chrono>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/integer/static_log2.hpp>

#include "Basic_client_1.h"

// For Corrupt part of the testing and metadata re-writing
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors {  
namespace synapse { 
namespace database {
#if defined(__GNUC__) && !defined(__clang__)
	// otherwise clang complains of unused variable, GCC however wants it to be present.
	::std::atomic_uint_fast64_t static Database_size{0};
#endif
unsigned constexpr static page_size = 4096;
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../../../misc/alloc.h"
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse {  
// Todo: better inclusion style (no time now).
data_processors::misc::alloc::Large_page_preallocator<synapse::database::page_size> static Large_page_preallocator;
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "../../../database/message.h"
#include "../../../database/index.h"
// ... metadata rewriting and corrupt testiing

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/Waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/XRate_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

namespace data_processors { namespace Test {

::std::string static host("localhost");
::std::string static port("5672");
::boost::filesystem::path static Topics_Path;
::boost::filesystem::path static Topic_Path;
::std::string static Topic_Name{"a"};
unsigned static constexpr Total_Messages_Size{1000};

namespace basio = ::boost::asio;

typedef ::DataProcessors::Waypoints::waypoints Waypoints_Type;
typedef ::DataProcessors::XRate::Rate Message_Type;

template <typename Socket_Type> 
struct Tester_Type : ::boost::enable_shared_from_this<Tester_Type<Socket_Type>> {

	typedef ::boost::enable_shared_from_this<Tester_Type<Socket_Type>> Tester_Base_Type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_Type> Synapse_Client_Type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_Client_Type> Synapse_Channel_Type;

	::boost::shared_ptr<Synapse_Client_Type> Synapse_Client;

	::boost::shared_ptr<Message_Type> Message;
	unsigned Processed_Messages_Size{0};
	unsigned Last_Message_Timestamp{0};
	unsigned Last_Message_Sequence_Number{0};
	unsigned constexpr static Timestamp_Advance{2000000};
	unsigned constexpr static Sequence_Number_Advance{2000};
	unsigned constexpr static Minimum_Message_Size{1024 * 1024};
	void Publish() {
		Message = ::boost::make_shared<Message_Type>();
		Message->Set_currency("a");
		static_assert(data_processors::synapse::misc::is_po2(Minimum_Message_Size), "Minimum_Message_Size must be power of 2");
		for (unsigned i(0); i != ::boost::static_log2<Minimum_Message_Size>::value; ++i)
			Message->Set_currency(Message->Get_currency() + Message->Get_currency());
		Synapse_Client = ::boost::make_shared<Synapse_Client_Type>();
		Synapse_Client->On_error = [](){
			throw ::std::runtime_error("Publisher On_error (not expected)");
		};
		Synapse_Client->Open(host, port, [this](bool Error) mutable {
			if (!Error)
				Synapse_Client->Open_channel(2, [this](bool Error) mutable {
					if (!Error) {
						auto & Channel(Synapse_Client->Get_channel(2)); 
						Channel.Set_as_publisher(Topic_Name);
						Publish_Message();
					} else 
						throw ::std::runtime_error("Publisher Error in callback on Open_channel");
				});
			else 
				throw ::std::runtime_error("Publisher Boo on Open.");
		}, 50);
	}

	void Publish_Message() {
		auto & Channel(Synapse_Client->Get_channel(2)); 
		Channel.Publish([this](bool Error) mutable {
				if (!Error) {
					if (++Processed_Messages_Size != Total_Messages_Size)
						Publish_Message();
					else
						Synapse_Client->Soft_Close([this](bool Error){
							if (Error)
								throw ::std::runtime_error("Publisher error on Soft_Close");
							Synapse_Client->Close();
						});
				} else
					throw ::std::runtime_error("Publisher Boo in publish.");
			}, Message.get(), static_cast<Waypoints_Type *>(nullptr), false, ::std::string(), Last_Message_Sequence_Number += Sequence_Number_Advance, Last_Message_Timestamp += Timestamp_Advance 
		);
	}

	template <unsigned Minimum_Quantize_Size>
	struct Index_Processing_Helper_Type {
		::std::string File_path;
		::std::string Meta_path;
		::std::fstream File; 
		::std::unique_ptr<char> Buffer_meta; 
		data_processors::synapse::database::Index_meta_ptr Meta; 
		::std::fstream File_meta;
		Index_Processing_Helper_Type(::std::string const & File_path, ::std::string const & Meta_path) : File_path(File_path), Meta_path(Meta_path), Buffer_meta(new char[data_processors::synapse::database::Index_meta_ptr::size]), Meta(Buffer_meta.get()) {
			assert(::boost::filesystem::exists(File_path));
			assert(::boost::filesystem::exists(Meta_path));

			File_meta.open(Meta_path.c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary);

			File_meta.read(Buffer_meta.get(), data_processors::synapse::database::Index_meta_ptr::size);
			Meta.verify_hash();
			if (!File_meta || 1ull << Meta.get_quantise_shift_mask() >= Minimum_Quantize_Size || Meta.Get_start_of_file_key() == static_cast<decltype(Meta.Get_start_of_file_key()) const>(-1) || Meta.get_data_size() < Meta.get_key_entry_size() * Total_Messages_Size)
				throw ::std::runtime_error("Index helper could not validate correct metadata: " + Meta_path);

			// Point to approx. 25% after start
			Meta.Set_begin_key(Meta.Get_start_of_file_key() + (Meta.get_data_size() / (Meta.get_key_entry_size() * 4) << Meta.get_quantise_shift_mask()));
			Meta.rehash();

			File_meta.seekp(0);
			File_meta.write(Buffer_meta.get(), data_processors::synapse::database::Index_meta_ptr::size);
			if (!File_meta)
				throw ::std::runtime_error("Index helper could not write corrupted metadata: " + Meta_path);

			assert(::boost::filesystem::file_size(File_path) >= Meta.get_data_size());

			// Zero-out approx. 1st half of the data.
			File.open(File_path.c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary);
			auto const To_Zero_Size(Meta.get_data_size() >> 1);
			assert(To_Zero_Size);
			::std::unique_ptr<char> Zero_Data(new char[To_Zero_Size]); 
			::memset(Zero_Data.get(), 0, To_Zero_Size);
			File.seekp(0);
			File.write(Zero_Data.get(), To_Zero_Size);
			if (!File)
				throw ::std::runtime_error("Index helper could not write zeroed data: " + File_path);
		}
	};

	void Corrupt_And_Recover() {
		{ // This will automatically close all files before running basic_recover at the end...
			Index_Processing_Helper_Type<Minimum_Message_Size> Message_Boundaries_Helper((Topic_Path / "index_0_data").string(), (Topic_Path / "index_0_data_meta").string());
			// Note: we are no longer indexing sequence number (index_1_data etc.)
			Index_Processing_Helper_Type<Timestamp_Advance> Timestamp_Helper((Topic_Path / "index_2_data").string(), (Topic_Path / "index_2_data_meta").string());

			::std::string File_path((Topic_Path / "data").string());
			::std::string Meta_path((Topic_Path / "data_meta").string());
			::std::fstream File; 
			::std::unique_ptr<char> Buffer_meta(new char[data_processors::synapse::database::Data_meta_ptr::size]); 
			data_processors::synapse::database::Data_meta_ptr Meta(Buffer_meta.get()); 
			::std::fstream File_meta;

			assert(::boost::filesystem::exists(File_path));
			assert(::boost::filesystem::exists(Meta_path));

			File_meta.open(Meta_path.c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary);

			File_meta.read(Buffer_meta.get(), data_processors::synapse::database::Data_meta_ptr::size);
			Meta.verify_hash();
			if (!File_meta || !Meta.get_data_size() || Meta.get_data_size() < Minimum_Message_Size * Total_Messages_Size)
				throw ::std::runtime_error("Main data could not validate correct metadata: " + Meta_path);

			assert(::boost::filesystem::file_size(File_path) >= Meta.get_data_size());

			// Point to 25% on main data.
			Meta.Set_begin_byte_offset(Meta.get_data_size() >> 2);
			Meta.rehash();

			File_meta.seekp(0);
			File_meta.write(Buffer_meta.get(), data_processors::synapse::database::Index_meta_ptr::size);
			if (!File_meta)
				throw ::std::runtime_error("Could not write corrupted metadata: " + Meta_path);

			// Zero out 1st 50% of data
			File.open(File_path.c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary);
			auto const To_Zero_Size(Meta.get_data_size() >> 1);
			assert(To_Zero_Size);
			::std::unique_ptr<char> Zero_Data(new char[To_Zero_Size]); 
			::memset(Zero_Data.get(), 0, To_Zero_Size);
			File.seekp(0);
			File.write(Zero_Data.get(), To_Zero_Size);
			if (!File)
				throw ::std::runtime_error("Could not write zeroed data: " + File_path);
		}

		// Recover at 75% from the data (should still be vaild...)
		if (::system(("database\\basic_recover.exe --src " + Topics_Path.string() + " --dst " + Topics_Path.string() + " --recover_from_eof " +  ::std::to_string((Total_Messages_Size >> 2) * Timestamp_Advance) + " > Basic_Recover_Log.txt 2>&1").c_str()))
			throw ::std::runtime_error("Could not run basic_recover");
	}

	void Subscribe() { 
		Synapse_Client = ::boost::make_shared<Synapse_Client_Type>();
		Synapse_Client->Open(host, port, [this](bool Error) {
			if (!Error) {
				Synapse_Client->Get_channel(1).On_incoming_message = [this](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope) {
					Synapse_Client->Get_channel(1).template Process_incoming_message<Waypoints_Type, Message_Type>(Message_Envelope,
						[this](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope){
							if (Message_Envelope == nullptr)
								throw ::std::runtime_error("Subsriber On_incoming_message should not supply nullptr Message_Envelope");

							// For non-first messages there should not be delta poison being set.
							if (Message_Envelope->Message->State_Of_Dom_Node.Read_Counter > 1 && Message_Envelope->Message->State_Of_Dom_Node.Is_Delta_Poison_Edge_On()) 
								throw ::std::runtime_error("Message is not expected to have delta-poison set.");

							if (Message_Envelope->Get_message_sequence_number() == Total_Messages_Size * Sequence_Number_Advance && Message_Envelope->Get_message_timestamp() == Total_Messages_Size * Timestamp_Advance) {
								data_processors::synapse::misc::log("Subscriber verified latest message OK.\n", true);
								Synapse_Client->Close();
							} else 
								Synapse_Client->Get_channel(1).Release_message(*Message_Envelope);
						}
					);
				};
				typename Synapse_Channel_Type::Subscription_options Options;
				Options.Begin_utc = 1;
				Options.Supply_metadata = true;
				Synapse_Client->Get_channel(1).Subscribe(
					[](bool Error) {
						if (Error) 
							throw ::std::runtime_error("Subscriber error in subscribe callback");
					}
					, "#", Options
				);
			} else 
				throw ::std::runtime_error("Subscriber Boo on Open in Synapse_Client.");
		}, 50);
	}

};

int static Run(int argc, char * argv[]) {
	try {

		enum Action {None, Publish, Corrupt_And_Recover, Subscribe};
		Action Run_as{None};

		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
			if (!::strcmp(argv[i], "--Topics_Path")) {
				Topics_Path = argv[++i];
				Topic_Path = Topics_Path / Topic_Name;
			} else if (!::strcmp(argv[i++], "--Action")) {
				if (!::strcmp(argv[i], "Publish"))
					Run_as = Publish;
				else if (!::strcmp(argv[i], "Corrupt_And_Recover"))
					Run_as = Corrupt_And_Recover;
				else if (!::strcmp(argv[i], "Subscribe"))
					Run_as = Subscribe;
				else
					throw ::std::runtime_error("unknown --Action value(=" + ::std::string(argv[i]) + ')');
			} else
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);
		auto Tester(::boost::make_shared<Tester_Type<data_processors::synapse::asio::tcp_socket>>());

		switch (Run_as) {
		case Publish :
		Tester->Publish();
		break;
		case Corrupt_And_Recover :
		Tester->Corrupt_And_Recover();
		break;
		case Subscribe :
		Tester->Subscribe();
		break;
		case None :
			throw ::std::runtime_error("--Action option must be specified");
		}

		::std::vector<::std::thread> pool(concurrency_hint * 8 - 1);
		for (auto && i : pool) {
			i = ::std::thread([]()->void{
				data_processors::synapse::asio::run();
			});
		}

		data_processors::synapse::asio::run();

		for (auto & i: pool)
			i.join();

		data_processors::synapse::misc::log("Bye bye.\n", true);
		return synapse::asio::Exit_error;
	}
	catch (::std::exception const & e)
	{
		data_processors::synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
		::std::cerr << "oops: " << e.what() << ::std::endl;
		return -1;
	}
}

}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[])
{
	return ::data_processors::Test::Run(argc, argv);
}

