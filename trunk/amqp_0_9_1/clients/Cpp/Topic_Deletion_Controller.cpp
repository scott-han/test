#include <iostream>
#include <string>

#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>

#include "Basic_client_1.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

namespace data_processors { namespace synapse { namespace Topic_Deletion_Controller {

::std::string static host("localhost");
::std::string static port("5672");
::std::string static Topic_Name;

namespace basio = ::boost::asio;

typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<data_processors::synapse::asio::tcp_socket> Synapse_Client_Type;

::boost::shared_ptr<Synapse_Client_Type> Synapse_Client;

int static Run(int argc, char * argv[]) {
	try {
		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
			if (!::strcmp(argv[i], "--Topic_Name"))
				Topic_Name = argv[++i];
			else
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		if (Topic_Name.empty())
			throw ::std::runtime_error("Must supply valid --Topic_Name");

		data_processors::synapse::asio::io_service::init(1);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		::std::cout << "\nConnection to " + host + ':' + port + "\nTopic_Name to be deleted: " + Topic_Name + '\n' << ::std::endl;

		synapse::misc::Nag_User_Style_1();

		::std::cout << "\nFine... processing now\n" << ::std::endl;

		Synapse_Client = ::boost::make_shared<Synapse_Client_Type>();
		Synapse_Client->Open(host, port, [](bool Error) {
			if (!Error) {
				Synapse_Client->Get_channel(1)._DONT_USE_JUST_YET_Set_topic_sparsification([](bool Error){
					if (Error)
						throw ::std::runtime_error("Synapse_Client could not send request for the deletion of topic");
					Synapse_Client->Soft_Close([](bool Error){
						if (Error)
							throw ::std::runtime_error("Synapse_Client could not Soft_Close");
						data_processors::synapse::misc::log("All done ok ... here's hoping you were sure :) :) :)\n", true);
						Synapse_Client->Close();
					});
				}, Topic_Name, 0, 0, 0, true);
			} else 
				throw ::std::runtime_error("Synapse_Client could not Open.");
		}, 50);

		data_processors::synapse::asio::run();

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

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[])
{
	return ::data_processors::synapse::Topic_Deletion_Controller::Run(argc, argv);
}

