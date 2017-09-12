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

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

unsigned static constexpr Total_Topics_Size{300};

namespace data_processors { namespace Test {

// defaults...
::std::string static host("localhost");
::std::string static port("5672");

namespace basio = ::boost::asio;

template <typename Socket> 
struct Tester : ::boost::enable_shared_from_this<Tester<Socket>> {

	typedef ::boost::enable_shared_from_this<Tester<Socket>> Tester_Base;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket> Synapse_Client;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_Client> Synapse_Channel_Type;

	::boost::shared_ptr<Synapse_Client> Subscriber;

	::boost::asio::deadline_timer Timer{data_processors::synapse::asio::io_service::get()};
	unsigned End_Of_Run_Pause{38777};

	void End_Run() {
		Subscriber->Soft_Close([this](bool Error){
			if (!Error) {
				Subscriber->Close();
				Subscriber.reset();
				if (End_Of_Run_Pause > 29000) {
					Timer.expires_from_now(::boost::posix_time::milliseconds(End_Of_Run_Pause -= 333));
					data_processors::synapse::misc::log("Async sleeping for " + ::std::to_string(End_Of_Run_Pause) + '\n', true);
					Timer.async_wait([this](::boost::system::error_code const &Error){
						if (!Error)
							Run();
					});
				}
			}
		});
	}

	void Subscribe(unsigned Topics_Size) {
		typename Synapse_Channel_Type::Subscription_options Options;
		Options.Subscription_To_Missing_Topics_Mode = decltype(Options.Subscription_To_Missing_Topics_Mode)::Create_If_Missing;
		Subscriber->Get_channel(1).Subscribe(
			[this, Topics_Size](bool Error) {
				if (Error) 
					throw ::std::runtime_error("Error in subcsribe callback");
				else if (Topics_Size)
					Subscribe(Topics_Size - 1);
				else
					End_Run();
			}
			, ::std::to_string(Topics_Size), Options
		);
	}

	void Run() {
		Subscriber = ::boost::make_shared<Synapse_Client>();
		Subscriber->Open(host, port, [this](bool Error) {
			if (!Error) {
				Subscriber->Get_channel(1).On_incoming_message = [](data_processors::synapse::amqp_0_9_1::Message_envelope *) {};
				Subscribe(Total_Topics_Size);
			} else 
				throw ::std::runtime_error("Boo on Open in Subscriber.");
		}, 50);
	}

};

int static Run(int , char* []) {
	try {

		auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);
		auto Tester(::boost::make_shared<Tester<data_processors::synapse::asio::tcp_socket>>());
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
		return 0;
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

