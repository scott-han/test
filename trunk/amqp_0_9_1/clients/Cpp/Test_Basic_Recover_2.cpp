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

#include "Basic_client_1.h"

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
unsigned static constexpr Total_Topics_Size{7};
unsigned static constexpr Total_Messages_Size{35000};
unsigned constexpr static Corrupt_At_Messages[] = { 17000, 23000 };

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
	unsigned Processed_Topics_Size{0};
	unsigned Processed_Messages_Size{0};
	void Publish() {
		Message = ::boost::make_shared<Message_Type>();
		Message->Set_currency("blah");
		Synapse_Client = ::boost::make_shared<Synapse_Client_Type>();
		Synapse_Client->On_error = [](){
			throw ::std::runtime_error("Publisher On_error (not expected)");
		};
		Synapse_Client->Open(host, port, [this](bool Error) mutable {
			if (!Error)
				Publish_Topic();
			else 
				throw ::std::runtime_error("Publisher Boo on Open.");
		}, 50);
	}

	void Publish_Topic() {
		Synapse_Client->Open_channel(2 + Processed_Topics_Size, [this](bool Error) mutable {
			if (!Error) {
				auto & Channel(Synapse_Client->Get_channel(2 + Processed_Topics_Size)); 
				Channel.Set_as_publisher(::std::to_string(Processed_Topics_Size));
				Publish_Message();
			} else 
				throw ::std::runtime_error("Publisher Error in callback on Open_channel");
		});
	}

	void Publish_Message() {
		auto & Channel(Synapse_Client->Get_channel(2 + Processed_Topics_Size)); 
		Channel.Publish([this](bool Error) mutable {
				if (!Error) {
					if (++Processed_Messages_Size < Total_Messages_Size || ++Processed_Topics_Size < Total_Topics_Size) {
						if (Processed_Messages_Size == Total_Messages_Size) {
							Processed_Messages_Size = 0;
							Publish_Topic();
						} else
							Publish_Message();
					} else {
						Synapse_Client->Soft_Close([this](bool Error){
							if (Error)
								throw ::std::runtime_error("Publisher error on Soft_Close");
							Synapse_Client->Close();
						});
					}
				} else
					throw ::std::runtime_error("Publisher Boo in publish.");
			}, Message.get(), static_cast<Waypoints_Type *>(nullptr), false, ::std::string(), 0, Processed_Messages_Size + 1
		);
	}

	void Corrupt() {
		for (unsigned Topic_Number(0); Topic_Number != Total_Topics_Size; ++Topic_Number) {
			auto const Filepath((Topics_Path / ::std::to_string(Topic_Number) / "data").string());
			::std::fstream File;
			File.open(Filepath.c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary); // if using ofstream the file is truncated by default and this gets rid of the sparse attribute (at least on windows)...
			uint32_t Message_Size;
			if (!File.read(reinterpret_cast<char *>(&Message_Size), sizeof(Message_Size)))
				throw ::std::runtime_error("Corrupter. reading 1st int on file errored out");
			Message_Size = ::data_processors::synapse::misc::round_up_to_multiple_of_po2<decltype(Message_Size)>(Message_Size, 8);

			::std::unique_ptr<char[]> Message_Buffer(new char[Message_Size]);

			for (auto const &  Corrupt_Message : Corrupt_At_Messages) {
				if (!File.seekg(Corrupt_Message * Message_Size))
					throw ::std::runtime_error("Corrupter could not read seek to the tobe corrupted message");
				if (!File.read(Message_Buffer.get(), Message_Size))
					throw ::std::runtime_error("Corrupter could not read the tobe corrupted message");
				// Simple bit-flipping content corrpution.
				for (unsigned i(0); i != Message_Size; ++i)
					Message_Buffer[i] = ~Message_Buffer[i];
				if (!File.seekp(Corrupt_Message * Message_Size))
					throw ::std::runtime_error("Corrupter could not write seek to the tobe corrupted message");
				if (!File.write(Message_Buffer.get(), Message_Size))
					throw ::std::runtime_error("Corrupter could not write the corrupted message");
			}
		}
	}

	// Todo, a very basic test for the time being. Later on expand on verifying topic-speciffic timestamp progressions, etc.
	void Subscribe() { 
		Synapse_Client = ::boost::make_shared<Synapse_Client_Type>();
		Synapse_Client->Open(host, port, [this](bool Error) {
			if (!Error) {
				Synapse_Client->Get_channel(1).On_incoming_message = [this](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope) {
					Synapse_Client->Get_channel(1).template Process_incoming_message<Waypoints_Type, Message_Type>(Message_Envelope,
						[this](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope){
							if (Message_Envelope == nullptr)
								throw ::std::runtime_error("Subsriber On_incoming_message should not supply nullptr Message_Envelope");

							bool Message_Should_Not_Have_Delta_Poison(true);
							auto const Message_Number(Message_Envelope->Get_message_timestamp() - 1);
							for (auto const &  Corrupt_Message : Corrupt_At_Messages) {
								if (Corrupt_Message == Message_Number)
									throw ::std::runtime_error("Message is not expected (should be corrupted).");
								else if (Corrupt_Message + 1 == Message_Number) {
									Message_Should_Not_Have_Delta_Poison = false;
									if (!Message_Envelope->Message->State_Of_Dom_Node.Is_Delta_Poison_Edge_On())
										throw ::std::runtime_error("Message is expected to have delta-poison set.");
								}
							}
							if (!Message_Number) { // because 1st message in a topic is implicitly expected to be delta-poisoned anyways...
								if (!Message_Envelope->Message->State_Of_Dom_Node.Is_Delta_Poison_Edge_On())
									throw ::std::runtime_error("First message in streaming is implicitly expected to have delta-poison set.");
							} else if (Message_Should_Not_Have_Delta_Poison && Message_Envelope->Message->State_Of_Dom_Node.Is_Delta_Poison_Edge_On())
								throw ::std::runtime_error("Message is not expected to have delta-poison set. Message_Number " + ::std::to_string(Message_Number));

							if (++Processed_Messages_Size == (Total_Messages_Size - sizeof(Corrupt_At_Messages) / sizeof(::std::remove_reference<decltype(Corrupt_At_Messages[0])>::type)) * Total_Topics_Size) 
								Synapse_Client->Close();
							else 
								Synapse_Client->Get_channel(1).Release_message(*Message_Envelope);

							data_processors::synapse::misc::log("Processed_Messages_Size " + ::std::to_string(Processed_Messages_Size) + '\n', true);

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

		enum Action {None, Publish, Corrupt, Subscribe};
		Action Run_as{None};

		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
			if (!::strcmp(argv[i], "--Topics_Path"))
				Topics_Path = argv[++i];
			else if (!::strcmp(argv[i++], "--Action")) {
				if (!::strcmp(argv[i], "Publish"))
					Run_as = Publish;
				else if (!::strcmp(argv[i], "Corrupt"))
					Run_as = Corrupt;
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
		case Corrupt :
		Tester->Corrupt();
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

