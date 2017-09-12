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
unsigned static constexpr Total_Messages_Size{375};

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
	void Publish() {
		Message = ::boost::make_shared<Message_Type>();
		Message->Set_currency("blah");
		Synapse_Client = ::boost::make_shared<Synapse_Client_Type>();
		Synapse_Client->On_error = [](){
			throw ::std::runtime_error("Publisher On_error (not expected)");
		};
		Synapse_Client->Open(host, port, [this](bool Error) mutable {
			if (!Error)
				Synapse_Client->Open_channel(2, [this](bool Error) mutable {
					if (!Error) {
						auto & Channel(Synapse_Client->Get_channel(2)); 
						Channel.Set_as_publisher("a");
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
					else {
						Synapse_Client->Soft_Close([this](bool Error){
							if (Error)
								throw ::std::runtime_error("Publisher error on Soft_Close");
							Synapse_Client->Close();
						});
					}
				} else
					throw ::std::runtime_error("Publisher Boo in publish.");
			}, Message.get(), static_cast<Waypoints_Type *>(nullptr), false, ::std::string(), 0, 1000000ull * 60 * 60 * 24 * 10 * Processed_Messages_Size  + 1
		);
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

		Tester->Publish();

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
	} catch (::std::exception const & e) {
		data_processors::synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
		::std::cerr << "oops: " << e.what() << ::std::endl;
		return -1;
	}
}

}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[]) {
	return ::data_processors::Test::Run(argc, argv);
}

