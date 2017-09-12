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

namespace basio = ::boost::asio;

typedef ::DataProcessors::Waypoints::waypoints Waypoints_Type;
typedef ::DataProcessors::XRate::Rate Rate_Type;

template <typename Socket_Type> 
struct Tester_Type : ::boost::enable_shared_from_this<Tester_Type<Socket_Type>> {

	typedef ::boost::enable_shared_from_this<Tester_Type<Socket_Type>> Tester_Base_Type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_Type> Synapse_Client_Type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_Client_Type> Synapse_Channel_Type;

	::boost::shared_ptr<Synapse_Client_Type> Subscriber;
	::boost::shared_ptr<Synapse_Client_Type> Publisher_A, Publisher_B;

	::boost::asio::deadline_timer Timer{data_processors::synapse::asio::io_service::get()};

	/*
		Subscribe.
		Publish with one client (allowing self-disconnection).
		Ensure that message was obtained.
		Publish with another client (disconnecting 1st, disallowing self-disconnection).
		Ensure that message was obtained.
		Publish with 3rd client (trying to disconnect 2nd) -- should error out.
	*/
	unsigned Received_Messages_Size{0};
	void Run() { 
		::std::string Log_Prefix("Subscribing. ");
		data_processors::synapse::misc::log(Log_Prefix + '\n');
		Subscriber = ::boost::make_shared<Synapse_Client_Type>();
		Subscriber->Open(host, port, [this, Log_Prefix](bool Error) {
			if (!Error) {
				Subscriber->Get_channel(1).On_incoming_message = [this, Log_Prefix](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope) {
					if (Message_Envelope == nullptr)
						throw ::std::runtime_error(Log_Prefix + "On_incoming_message should not supply nullptr Message_Envelope");
					data_processors::synapse::misc::log(Log_Prefix + "has seen message... (expected)\n", true);

					Subscriber->strand.post([this, Message_Envelope]() mutable {
						Subscriber->Get_channel(1).Release_message(*Message_Envelope);
					});

					switch (++Received_Messages_Size) {
					case 1 :
					Publisher_B = ::boost::make_shared<Synapse_Client_Type>();
					Publisher_B->On_error = [](){
						throw ::std::runtime_error("Publisher_B has seen error... (NOT expected)");
					};
					Publish_Message("Publisher_B ", "a", Publisher_B, true, false);
					break;
					case 2 :
					Publisher_A->strand.post([Scoped_Publisher_A(Publisher_A)](){Scoped_Publisher_A->Close();});
					Publisher_A = ::boost::make_shared<Synapse_Client_Type>();
					Publisher_A->On_error = [this](){
						assert(Publisher_A->strand.running_in_this_thread());
						data_processors::synapse::misc::log("Publisher_A, second run, has seen error... (expected)\n", true);
						Publisher_A->Close();
						Publisher_B->strand.post([this](){Publisher_B->Soft_Close([this](bool){Publisher_B->Close();});});
						Subscriber->strand.post([this](){Subscriber->Close();});
					};
					Publish_Message("Publisher_A, second run, ", "a", Publisher_A, true, false);
					break;
					}
				};

				Subscriber->Get_channel(1).Subscribe(
					[this, Log_Prefix](bool Error) {
						if (Error) 
							throw ::std::runtime_error(Log_Prefix + "error in subscribe callback");
						else {
							Publisher_A = ::boost::make_shared<Synapse_Client_Type>();
							Publisher_A->On_error = [](){
								data_processors::synapse::misc::log("Publisher_A has seen error... (expected)\n");
							};
							Publish_Message("Publisher_A ", "a", Publisher_A, false, true);
						}
					}
					, "a", typename Synapse_Channel_Type::Subscription_options()
				);
			} else 
				throw ::std::runtime_error(Log_Prefix + "Boo on Open in Subscriber.");
		}, 50);
	}

	void Publish_Message(::std::string Log_Prefix, ::std::string const & Topic_Name, ::boost::shared_ptr<Synapse_Client_Type> & Publisher, bool const Replace_Previous_Publisher, bool const Allow_Replacing_Self) {
		Log_Prefix += Topic_Name + ' ';
		Publisher->Open(host, port, [this, Log_Prefix, Topic_Name, Replace_Previous_Publisher, Allow_Replacing_Self, Publisher](bool Error) mutable {
			if (!Error) {
				Publisher->Open_channel(2, [this, Log_Prefix, Topic_Name, Replace_Previous_Publisher, Allow_Replacing_Self, Publisher](bool Error) mutable {
					if (!Error) {
						auto & Channel(Publisher->Get_channel(2)); 
						Channel.Set_as_publisher(Topic_Name);
						auto Rate(::boost::make_shared<Rate_Type>());
						Rate->Set_currency("blah");
						data_processors::synapse::misc::log(Log_Prefix + "publishing a message\n");
						Channel.Publish([](bool){}, Rate, static_cast<Waypoints_Type *>(nullptr), false, ::std::string(), 0, 0, 0, false, false, Replace_Previous_Publisher, Allow_Replacing_Self);
					} else 
						throw ::std::runtime_error(Log_Prefix + "Error in callback on Open_channel");
				});
			} else 
				throw ::std::runtime_error(Log_Prefix + "Boo on Open.");
		}, 50);
	}

};

int static Run(int argc, char * argv[]) {
	try {

		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);
		auto Tester(::boost::make_shared<Tester_Type<data_processors::synapse::asio::tcp_socket>>());
		Tester->Run();

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

