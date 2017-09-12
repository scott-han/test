#include <iostream>
#include <atomic>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/deadline_timer.hpp>

#include "Basic_client_1.h"

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace Test {

unsigned static constexpr Total_messages{5000};

// defaults...
::std::string static host("localhost");
::std::string static port("5672");

namespace basio = ::boost::asio;

enum class Testing_stage { Undefined, Publish, Corrupt, Subscribe };
Testing_stage static Current_stage{Testing_stage::Undefined};

template <typename Socket_type> 
struct Tester : ::boost::enable_shared_from_this<Tester<Socket_type>> {

	typedef ::boost::enable_shared_from_this<Tester<Socket_type>> base_type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

	typedef DataProcessors::Waypoints::waypoints Waypoints_type;
	typedef DataProcessors::Contests4::Contests Contests_type;
	typedef DataProcessors::Contests4::Contest Contest_type;

	enum Status {Initial, Seen_non_delta_before_poison, Delta_poisoned, Done};
	struct My_contests_reactor : data_processors::Federated_serialisation::Field_inotify<::std::string, Contest_type> {

		struct Contest_Stats : ::data_processors::Federated_serialisation::User_data_type {
			Status Progress{Initial};
			void Ping(Contest_type const & Contest) noexcept {
				if (Progress == Initial && Contest.State_Of_Dom_Node.Has_Seen_Non_Delta())
					Progress = Seen_non_delta_before_poison;
				else if (Progress == Seen_non_delta_before_poison && Contest.State_Of_Dom_Node.Is_Delta_Poison_Edge_On())
					Progress = Delta_poisoned;
				else  if (Progress == Delta_poisoned && Contest.State_Of_Dom_Node.Has_Seen_Non_Delta())
					Progress = Done;
				else if (Progress != Done && Contest.State_Of_Dom_Node.Read_Counter > Total_messages * .8)
					throw ::std::runtime_error("Contest reactor should probably have seen all the delta statuses in the subscription mode.");
			}
		};

		void On_new(::std::string const &, Contest_type & Contest) {
			Contest.User_data.reset(new Contest_Stats);
		}
		void On_add(::std::string const &, Contest_type & Contest) {
			static_cast<Contest_Stats *>(Contest.User_data.get())->Ping(Contest);
		}
		void On_modify(::std::string const &, Contest_type & Contest) {
			static_cast<Contest_Stats *>(Contest.User_data.get())->Ping(Contest);
		}
		void On_delete(::std::string const &, Contest_type & Contest) {
			static_cast<Contest_Stats *>(Contest.User_data.get())->Ping(Contest);
		}
	};

	struct My_contests_type : Contests_type {
		My_contests_type() {
			contest_inotify.reset(new My_contests_reactor);
		}
	};

	::boost::shared_ptr<Synapse_client_type> Synapse_client{::boost::make_shared<Synapse_client_type>()};

	uint_fast64_t Publisher_ack_messages_size_hack{0};
	Status Progress{Initial};

	// Starting routine for the example class (boilerplate code calls it).
	void Run() {
		// We need to open client before interacting with it.
		Synapse_client->On_error = [](){
			throw ::std::runtime_error("Boo on error.");
		};
		auto this_(base_type::shared_from_this());
		Synapse_client->Open(host, port, [this, this_](bool Error) {
			if (!Error) {
				auto & Channel(Synapse_client->Get_channel(1));

				if (Current_stage == Testing_stage::Subscribe) {
					Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
						if (Envelope != nullptr) {
							if (Envelope->Type_name == "Contests") {
								Channel.template Process_incoming_message<Waypoints_type, My_contests_type>(Envelope,
									[this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope){
										if (Envelope != nullptr) {
											synapse::asio::io_service::get().post([this, this_, &Channel, Envelope](){
												assert(Envelope->Message);

												auto & Contests(static_cast<My_contests_type&>(*Envelope->Message));

												if (Progress == Initial && Contests.State_Of_Dom_Node.Has_Seen_Non_Delta()) {
													Progress = Seen_non_delta_before_poison;
													data_processors::synapse::misc::log("Traversal seen non-delta.\n", true);
												} else if (Progress == Seen_non_delta_before_poison && Contests.State_Of_Dom_Node.Is_Delta_Poison_Edge_On()) {
													Progress = Delta_poisoned;
													data_processors::synapse::misc::log("Traversal seen delta-poison.\n", true);
												} else  if (Progress == Delta_poisoned && Contests.State_Of_Dom_Node.Has_Seen_Non_Delta()) {
													Progress = Done;
													data_processors::synapse::misc::log("Traversal Done OK.\n", true);

													for (unsigned i(0); i != 100; ++i) {
														auto & Contest(Contests.Get_contest_element(::std::to_string(i)));
														if (static_cast<typename My_contests_reactor::Contest_Stats&>(*Contest.User_data).Progress != Done)
															throw ::std::runtime_error("Reactors are not in Done state, but should be.");
													}
													data_processors::synapse::misc::log("Reactors progress done OK.\n", true);

													data_processors::synapse::asio::io_service::get().stop();
												} else if (Progress != Done && Contests.State_Of_Dom_Node.Read_Counter > Total_messages * .8)
													throw ::std::runtime_error("Traversal should probably have seen all the delta statuses in the subscription mode.");

												Synapse_client->strand.dispatch([this_, &Channel, Envelope](){
														Channel.Release_message(*Envelope);
												});
											});
										}
									}
								);
							}
						}
					};
					typename Synapse_channel_type::Subscription_options Options;
					Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2015, 1, 1);
					Options.Supply_metadata = true;
					Channel.Subscribe( [this_](bool) { } , "test.cpp.*", Options);
				} else if (Current_stage == Testing_stage::Publish) {

					Channel.On_ack = [this, this_](::std::string const & Topic_name, uint_fast64_t, uint_fast64_t, data_processors::synapse::amqp_0_9_1::foreign::copernica::Table *, bool Error) {
						if (!Error) {
							if (Topic_name == "test.cpp.1") {
								synapse::asio::io_service::get().post([this, this_](){
									if (++Publisher_ack_messages_size_hack == Total_messages) {
										data_processors::synapse::misc::log("Done OK.\n", true);
										data_processors::synapse::asio::io_service::get().stop();
									} 
								});
							}
						}
					};

					typename Synapse_channel_type::Subscription_options Options;
					Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2015, 1, 1);
					Options.Supply_metadata = true;
					Options.Skip_payload = true;

					Channel.Subscribe( [this_](bool) { } , "test.cpp.*", Options);

					Synapse_client->Open_channel(2, [this, this_](bool Error){
						if (!Error) {
							auto & Channel(Synapse_client->Get_channel(2)); 
							Channel.Set_as_publisher("test.cpp.1");

							::boost::shared_ptr<Waypoints_type> Waypoints;
							auto Contests(::boost::make_shared<Contests_type>());

							Publish_message(Channel, Waypoints, Contests, Total_messages);
						}
					});


				} else
					throw ::std::runtime_error("Unknown current_state...");
			} else 
				throw ::std::runtime_error("Boo on Open.");
		});

	}

	void Publish_message(Synapse_channel_type & Channel, ::boost::shared_ptr<Waypoints_type> Waypoints, ::boost::shared_ptr<Contests_type> Contests, unsigned Remaining_messages_size) {
		if (Remaining_messages_size) {
			auto this_(base_type::shared_from_this());
			synapse::asio::io_service::get().post([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](){

				Contests->Set_datasource("Some datasource name. Indubitably. " + ::std::to_string(Remaining_messages_size));
				for (unsigned i(0); i != 100; ++i) {
					auto & Contest(Contests->Set_contest_element(::std::to_string(i)));
					auto & Contest_name(Contest.Set_contestName());
					::std::string Some_long_contest_name("Some long contest name indeed " + ::std::to_string(Remaining_messages_size));
					for (int i(0); i != 10; ++i)
						Some_long_contest_name += Some_long_contest_name;
					Contest_name.Set_value(::std::move(Some_long_contest_name));
					if (i == 3) {
						Contest.Inhibit_delta_mode = true;
						Contest.Propagate_modified_flag_upstream();
					}
				}

				Synapse_client->strand.dispatch([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](){
					Channel.Publish([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](bool Error) {
							if (!Error) {
								Publish_message(Channel, Waypoints, Contests, Remaining_messages_size - 1);
							}
						} 
						, Contests, Waypoints, Remaining_messages_size % 10 ? true : false
					);
				});
			});
		}
	}

};

int static
Run(int argc, char* argv[])
{
	try
	{
		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
			if (!::strcmp(argv[i], "--Stage")) {
				++i;
				if (!::strcmp(argv[i], "Publish")) {
					Current_stage = Testing_stage::Publish;
				} else if (!::strcmp(argv[i], "Corrupt")) {
					if (++i == argc)
						throw ::std::runtime_error("Corrupt argument must supply also the filepath");
					::boost::filesystem::path Data_file_path(argv[i]);
					if (!::boost::filesystem::exists(Data_file_path))
						throw ::std::runtime_error("Corrupt data path (=" + ::std::string(argv[i]) + ") does not exist");

					auto const Data_file_size(::boost::filesystem::file_size(Data_file_path));

					::std::fstream Data_file_stream;
					Data_file_stream.open(Data_file_path.string().c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary);

					Data_file_stream.seekp(Data_file_size / 2);

					::std::string Some_corrupted_content("aoeUAOEUAOEUAOEUAOEu");
					Data_file_stream.write(Some_corrupted_content.c_str(), Some_corrupted_content.size());


					return 0;
				} else if (!::strcmp(argv[i], "Subscribe")) {
					Current_stage = Testing_stage::Subscribe;
				} else
					throw ::std::runtime_error("incorrect --Stage value: (=" + ::std::string(argv[i]) + ')');
			} else
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		if (Current_stage == Testing_stage::Undefined)
			throw ::std::runtime_error("--Stage must be specified.");

		data_processors::synapse::misc::log("Synapse client starting.\n", true);

		auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

		// create based on chosen runtime parameters
		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		// Automatic quitting handling
		// ... signal based (mostly for unix, but also on Windows when ran from proper consoles (not msys or cygwin which intercept handling of CTRL-C and play-around w.r.t. expected behaviour))
		data_processors::synapse::asio::Autoquit_on_signals Signal_quitter;
#ifndef GDB_STDIN_BUG
		// the following quitting handling is for user-based keypress interaction which should work on any platform (windows, linux etc.). 
		data_processors::synapse::asio::Autoquit_on_keypress Keypress_quitter;
#endif

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);

		::boost::make_shared<Tester<data_processors::synapse::asio::tcp_socket>>()->Run();

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
		return data_processors::synapse::asio::Exit_error;
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

