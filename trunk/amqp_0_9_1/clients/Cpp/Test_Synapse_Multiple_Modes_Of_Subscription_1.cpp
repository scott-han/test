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

namespace basio = ::boost::asio;

typedef ::DataProcessors::Waypoints::waypoints Waypoints_Type;
typedef ::DataProcessors::XRate::Rate Rate_Type;

template <typename Socket_Type> 
struct Tester_Type : ::boost::enable_shared_from_this<Tester_Type<Socket_Type>> {

	typedef ::boost::enable_shared_from_this<Tester_Type<Socket_Type>> Tester_Base_Type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_Type> Synapse_Client_Type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_Client_Type> Synapse_Channel_Type;

	::boost::shared_ptr<Synapse_Client_Type> Subscriber;
	::boost::shared_ptr<Synapse_Client_Type> Publisher;

	::boost::asio::deadline_timer Timer{data_processors::synapse::asio::io_service::get()};

	/*
		 Connect with default mode
		 wait a few sec
		 check that no topic was created
		 Publish
		 Ensure that message was obtained
	*/
	void Run() { // Start with topic A
		::std::string Log_Prefix("Subscribing to non-existing topic in default mode to topic 'a' ");
		data_processors::synapse::misc::log(Log_Prefix + '\n');
		Subscriber = ::boost::make_shared<Synapse_Client_Type>();
		Subscriber->Open(host, port, [this, Log_Prefix](bool Error) {
			if (!Error) {
				Subscriber->Get_channel(1).On_incoming_message = [this, Log_Prefix](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope) {
					if (Message_Envelope == nullptr)
						throw ::std::runtime_error(Log_Prefix + "On_incoming_message should not supply nullptr Message_Envelope");
					data_processors::synapse::misc::log(Log_Prefix + "has seen message... (expected)\n");
					Subscriber->Close();
					Topic_B();
				};
				Subscriber->Get_channel(1).Subscribe(
					[this, Log_Prefix](bool Error) {
						if (Error) 
							throw ::std::runtime_error(Log_Prefix + "error in subscribe callback");
						else {
							Timer.expires_from_now(::boost::posix_time::seconds(3));
							Timer.async_wait([this, Log_Prefix](::boost::system::error_code const &Error){
								if (Error)
									throw ::std::runtime_error(Log_Prefix + "error in timer before issuing Publish_Message");
								if (::boost::filesystem::exists(Topics_Path / "a"))
									throw ::std::runtime_error(Log_Prefix + "topic should not exist");
								data_processors::synapse::misc::log(Log_Prefix + "topic does not exist (expected)\n");
								Publish_Message("a");
							});
						}
					}
					, "a", typename Synapse_Channel_Type::Subscription_options()
				);
			} else 
				throw ::std::runtime_error(Log_Prefix + "Boo on Open in Subscriber.");
		}, 50);
	}

	void Publish_Message(::std::string const & Topic_Name) {
		::std::string Log_Prefix("Publishing a message  to topic " + Topic_Name + ' ');
		Publisher = ::boost::make_shared<Synapse_Client_Type>();
		Publisher->On_error = [Log_Prefix](){
			throw ::std::runtime_error(Log_Prefix + "On_error (not expected)");
		};
		Publisher->Open(host, port, [this, Log_Prefix, Topic_Name](bool Error) mutable {
			if (!Error) {
				Publisher->Open_channel(2, [this, Log_Prefix, Topic_Name](bool Error) mutable {
					if (!Error) {
						auto & Channel(Publisher->Get_channel(2)); 
						Channel.Set_as_publisher(Topic_Name);
						auto Rate(::boost::make_shared<Rate_Type>());
						Rate->Set_currency("blah");
						data_processors::synapse::misc::log(Log_Prefix + "publishing a message\n");
						Channel.Publish([this, Log_Prefix](bool Error) mutable {
								if (!Error)
									Publisher->Soft_Close([this, Log_Prefix](bool Error){
										if (Error)
											throw ::std::runtime_error(Log_Prefix + "error on Soft_Close");
										Publisher->Close();
									});
								else
									throw ::std::runtime_error(Log_Prefix + "Boo in publish.");
							}, Rate, static_cast<Waypoints_Type *>(nullptr)
						);
					} else 
						throw ::std::runtime_error(Log_Prefix + "Error in callback on Open_channel");
				});
			} else 
				throw ::std::runtime_error(Log_Prefix + "Boo on Open.");
		}, 50);
	}

	/*
		 Connect with Error_If_Missing for missing topic B
		 Expect for connection to error out (incoming message null, or on-error, etc.)
		 boost filesystem exists should return false
	*/
	void Topic_B() {
		::std::string Log_Prefix("Subscribing to non-existing topic in error mode to topic 'b' ");
		data_processors::synapse::misc::log(Log_Prefix + '\n');
		Subscriber = ::boost::make_shared<Synapse_Client_Type>();
		Subscriber->Open(host, port, [this, Log_Prefix](bool Error) {
			if (!Error) {
				Subscriber->Get_channel(1).On_incoming_message = [this, Log_Prefix](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope) {
					if (Message_Envelope != nullptr)
						throw ::std::runtime_error(Log_Prefix + "On_incoming_message should not supply valid Message_Envelope");
					data_processors::synapse::misc::log(Log_Prefix + "received nullptr for Message_Envelope (expected)\n");
					Timer.expires_from_now(::boost::posix_time::seconds(3));
					Timer.async_wait(Subscriber->strand.wrap([this, Log_Prefix](::boost::system::error_code const &Error){
						if (Error)
							throw ::std::runtime_error(Log_Prefix + "error in timer before checking topic non-existence");
						if (::boost::filesystem::exists(Topics_Path / "b"))
							throw ::std::runtime_error(Log_Prefix + "topic should not exist");
						data_processors::synapse::misc::log(Log_Prefix + "verified topic does not exist (expected)\n");
						Subscriber->Close();
						Topic_C();
					}));
				};
				typename Synapse_Channel_Type::Subscription_options Options;
				Options.Subscription_To_Missing_Topics_Mode = decltype(Options.Subscription_To_Missing_Topics_Mode)::Error_If_Missing;
				Subscriber->Get_channel(1).Subscribe([](bool){}, "b", Options);
			} else 
				throw ::std::runtime_error(Log_Prefix + "Boo on Open in Subscriber.");
		}, 50);
	}

	/*
		 Connect with Create_If_Missing for missing topic C
		 wait a few sec
		 check that topic was indeed created
		 Publish
		 Ensure that message was obtained
	*/
	void Topic_C() { 
		::std::string Log_Prefix("Subscribing to non-existing topic in default mode to topic 'c' ");
		data_processors::synapse::misc::log(Log_Prefix + '\n');
		Subscriber = ::boost::make_shared<Synapse_Client_Type>();
		Subscriber->Open(host, port, [this, Log_Prefix](bool Error) {
			if (!Error) {
				Subscriber->Get_channel(1).On_incoming_message = [this, Log_Prefix](data_processors::synapse::amqp_0_9_1::Message_envelope * Message_Envelope) {
					if (Message_Envelope == nullptr)
						throw ::std::runtime_error(Log_Prefix + "On_incoming_message should not supply nullptr Message_Envelope");
					data_processors::synapse::misc::log(Log_Prefix + "has seen message... (expected)\n");
					Subscriber->Close();
				};
				typename Synapse_Channel_Type::Subscription_options Options;
				Options.Subscription_To_Missing_Topics_Mode = decltype(Options.Subscription_To_Missing_Topics_Mode)::Create_If_Missing;
				Subscriber->Get_channel(1).Subscribe(
					[this, Log_Prefix](bool Error) {
						if (Error) 
							throw ::std::runtime_error(Log_Prefix + "error in subscribe callback");
						else {
							Timer.expires_from_now(::boost::posix_time::seconds(3));
							Timer.async_wait([this, Log_Prefix](::boost::system::error_code const &Error){
								if (Error)
									throw ::std::runtime_error(Log_Prefix + "error in timer before issuing Publish_Message");
								if (!::boost::filesystem::exists(Topics_Path / "c"))
									throw ::std::runtime_error(Log_Prefix + "topic should exist");
									data_processors::synapse::misc::log(Log_Prefix + "verified topic does exist (expected)\n");
								Publish_Message("c");
							});
						}
					}
					, "c", Options
				);
			} else 
				throw ::std::runtime_error(Log_Prefix + "Boo on Open in Subscriber.");
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
			if (!::strcmp(argv[i], "--Topics_Path"))
				Topics_Path = argv[++i];
			else
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

