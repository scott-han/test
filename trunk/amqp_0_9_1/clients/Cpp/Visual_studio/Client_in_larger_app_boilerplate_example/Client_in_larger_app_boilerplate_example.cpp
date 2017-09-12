// Client_in_larger_app_boilerplate_example.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <iostream>
#include <atomic>

#include <boost/algorithm/string.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/deadline_timer.hpp>

// Until the 'distribution repo' is updated, will use localised Synapse_Client version...
// #include <data_processors/synapse/amqp_0_9_1/clients/Cpp/Basic_client_1.h>
#include "../../Basic_client_1.h"

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace Some_example {

namespace basio = ::boost::asio;

// This API builds on Asio library (Asynchronous IO), so overall concepts (not intermixing multiple writes, callbacks always coming back, multithreaded guarantees/etc) may be of interest to the end coder:
// http://think-async.com/
// The Asio is also part of the Boost libraries suite, so there is plethora of docs there too :)
// Also, to make this simpler: 
// All callbacks from Synapse_client will come-back on the Synapse_client strand.
// For strand doco/examples, one can read something like this: http://think-async.com/asio/asio-1.5.2/doc/asio/overview/core/strands.html
// All async methods to be invoked on the Synapse_client by user code also ought to be on strand of Synapse_client. This could be implicit because it is done from within the callback coming from Synapse_client, or explicit (where user code us running some extra threads which want to interact with Synapse_client). 
// The syntax for explicit wrapping would be something like this (if using lambdas):
// Synapse_client->strand.dispatch([your lambda captures]() mutable { your lamda code } );
// Naturally any other method (e.g. explicit method/handler, etc.) ought to work also.

// Generic application example class. Allows to publish multiple topics and subscribe to such.
// Note: error states indicated by callbacks are not acted upon here to keep the codebase reasonably simple to follow.
template <typename Socket_type> 
struct My_app_example : ::boost::enable_shared_from_this<My_app_example<Socket_type>> {

	typedef ::boost::enable_shared_from_this<My_app_example<Socket_type>> base_type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

	typedef DataProcessors::Waypoints::waypoints Waypoints_type;
	typedef DataProcessors::Contests4::Contests Contests_type;

	struct My_top_level_user_data_when_publishing : data_processors::Federated_serialisation::User_data_type {
		// do whatever you like here :)
	};

	struct My_contest_user_data : data_processors::Federated_serialisation::User_data_type {
		// do whatever you like here :)
	};
 
	typedef DataProcessors::Contests4::Contest Contest_type;
	struct My_contests_reactor : data_processors::Federated_serialisation::Field_inotify<::std::string, Contest_type> {
		void On_new(::std::string const & k, Contest_type & v) {
			v.User_data.reset(new My_contest_user_data);
		}
		void On_add(::std::string const & k, Contest_type & v) {
		}
		void On_modify(::std::string const & k, Contest_type & v) {
		}
		void On_delete(::std::string const & k, Contest_type & v) {
		}
	};

	struct My_contests_type : Contests_type {
		My_contests_type() {
			contest_inotify.reset(new My_contests_reactor);
		}
		template <class Protocol>
		uint32_t read(Protocol * iprot) {
			return Contests_type::read(iprot);
		}
		// May omit if not wanting to have it...
		template <class Envelope_type>
		void Set_envelope(Envelope_type * ) {
		}
	};

	::boost::shared_ptr<Synapse_client_type> Synapse_client{::boost::make_shared<Synapse_client_type>()};

	::boost::shared_ptr<Synapse_client_type> Ack_synapse_client_example{::boost::make_shared<Synapse_client_type>()};

	::std::atomic_uint_fast64_t Received_messages_size{0};

	// Starting routine for the example class (boilerplate code calls it).
	void Run() {
		// We need to open client before interacting with it.
		Synapse_client->On_error = [](){
			throw ::std::runtime_error("Boo on error.");
		};
		auto this_(base_type::shared_from_this());
		Synapse_client->Open(host, port, [this, this_](bool Error) {
			if (!Error) {
				// We perform any explicit subscription/publication over a 'channel' obtained from Synapse_client.
				auto & Channel(Synapse_client->Get_channel(1)); // Channels 0 and 1 are opened for us by default, no need to do anything... (will make it more elegant later on, etc.) Channel-zero is really reserved (for overall connection usage), so most-frequent usecase is to use channel 1 for subscriptions.
				// Set up callback for incoming message arrival and top-level object type determination/parsing.
				Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
					if (Envelope != nullptr) {
						// Your topic factory (deciding which namespace<->schema to deploy).
						if (Envelope->Type_name == "Contests") {
							// Actually parse Thrift stuff as determined type message.
							Channel.Process_incoming_message<Waypoints_type, My_contests_type>(Envelope,
								[this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope){
									if (Envelope != nullptr) {
										synapse::asio::io_service::get().post([this, this_, &Channel, Envelope](){
											::std::string Print_string;
											assert(Envelope->Message);
											if (Envelope->Waypoints) {
												auto Concrete_waypoints(static_cast<Waypoints_type *>(Envelope->Waypoints.get()));
												if (Concrete_waypoints->Is_path_set()) {
													auto && Path(Concrete_waypoints->Get_path());
													for (auto && Waypoint : Path) {
														if (Waypoint.Is_tag_set())
															Print_string += "WAYPOINT TAG: " + Waypoint.Get_tag() + '\n';
													}
												}
											}
											Print_string += "Message received. Topic: " + Envelope->Topic_name + '\n';
											auto Message(static_cast<Contests_type *>(Envelope->Message.get()));
											Print_string += "Non-delta seen: " + ::std::to_string(Message->State_Of_Dom_Node.Has_Seen_Non_Delta()) + '\n';
											if (Message->Is_datasource_set())
												Print_string += "Contests received. Datasource: " + Message->Get_datasource() + '\n';
											if (Message->Is_contest_set()) {
												auto && Contests(Message->Get_contest());
												Print_string += "Contests in message: " + ::std::to_string(Contests.size()) + '\n';
												for (auto && Contest_kv : Contests) {
													auto && Contest(Contest_kv.second);
													if (Contest.Is_contestName_set()) {
														auto && Contest_name(Contest.Get_contestName());
														if (Contest_name.Is_value_set())
															Print_string += "Contest name: " + Contest_name.Get_value() + '\n';
													}
													if (Contest.Is_startDate_set()) {
														auto && Start_date(Contest.Get_startDate());
														// Decoding to Barry string 
														Print_string += "start date(dp): " + ::data_processors::Federated_serialisation::Utils::Decode_date(Start_date.Get_value()) + '\n';
													}
													if (Contest.Is_startTime_set()) {
														auto && Start_time(Contest.Get_startTime());
														// Decoding to Barry string 
														Print_string += "start time(dp): " + ::data_processors::Federated_serialisation::Utils::Decode_time(Start_time.Get_value()) + '\n';
													}
													if (Contest.Is_scheduledStart_set()) {
														auto && Scheduled_start(Contest.Get_scheduledStart());
														// Decoding to ISO string
														Print_string += "scheduled start(iso): " + ::boost::posix_time::to_iso_string(::data_processors::Federated_serialisation::Utils::Decode_timestamp_to_ptime(Scheduled_start.Get_value())) + '\n';
														// Decoding to Barry string 
														Print_string += "scheduled start(dp): " + ::data_processors::Federated_serialisation::Utils::Decode_timestamp(Scheduled_start.Get_value()) + '\n';
													}
												}
											}
											::std::cout << Print_string << ::std::endl;
											// As per API requirements, Synapse_client interaction is only on its own strand. 
											Synapse_client->strand.dispatch([this, this_, &Channel, Envelope](){
													if (++Received_messages_size == 100)
														Channel.Unsubscribe([this, this_](bool Error) {
															if (Error)
																throw ::std::runtime_error("Boo on Unsubscribe.");
														}, Envelope->Topic_name);
													Channel.Release_message(*Envelope);
											});
										});
									}
								}
							);
						}
					}
				};
				// We can subscribe to multiple topics on the same channel (wildcard).
				typename Synapse_channel_type::Subscription_options Options;
				Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2015, 1, 1);
				Options.Supply_metadata = true;
				Channel.Subscribe(
					[this, this_](bool Error) {
					}
					, "test.cpp.*", Options 
				);
				// We can publish (one topic per channel, but multiple channels are permitted) to multiple topics.
				Synapse_client->Open_channel(2, [this, this_](bool Error){
					if (!Error) {
						auto & Channel(Synapse_client->Get_channel(2)); // Todo: make it more elegant later on, etc.
						// We only allow publishing on topic-per-channel basis. Moreover, generally speaking, AMQP channel is really for uni-directional flow of data (not for consuming/publishing on the same channel).
						Channel.Set_as_publisher("test.cpp.1"); // Todo: may offer less verbose, yet slower, version for all this bootstrapping.

						// These are our DOM messages which we will be streaming on the topic (incrementally/delta or as a whole).
						// Note: may also use clients 'allocate_shared' for faster (yet requiring more responsibility) memory allocation engine... (but in this case will need to ensure that such a code is running on connection.strand if it is connections 'sp' is being used)
						::boost::shared_ptr<Waypoints_type> Waypoints(::boost::make_shared<Waypoints_type>());
						auto && Waypoint(Waypoints->Add_path_element(DataProcessors::Waypoints::waypoint()));
						Waypoint.Set_tag("TAAAAAG");
						auto Contests(::boost::make_shared<Contests_type>());
						Contests->User_data.reset(new My_top_level_user_data_when_publishing);

						Publish_message(Channel, Waypoints, Contests, 1000000);
					}
				});
				Synapse_client->Open_channel(3, [this, this_](bool Error){
					if (!Error) {
						auto & Channel(Synapse_client->Get_channel(3));
						Channel.Set_as_publisher("test.cpp.2");
						::boost::shared_ptr<Waypoints_type> Waypoints(::boost::make_shared<Waypoints_type>());
						DataProcessors::Waypoints::waypoint Waypoint;
						Waypoint.Set_tag("TAAAAAG");
						Waypoints->Add_path_element(::std::move(Waypoint));
						auto Contests(::boost::make_shared<Contests_type>());
						Contests->User_data.reset(new My_top_level_user_data_when_publishing);

						Publish_message(Channel, Waypoints, Contests, 1000000);
					}
				});
				// Todo: in future may consider decoupling Synapse_client into Synapse_subcsriber and Synapse_publisher ... as per Python counterparts... don't quite know yet... but in any case one is free to use two instances of the Synapse_client, one as publisher and another as subscriber -- if TCP-socket head-of-line blocking becomes an issue for the end-coder.
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 50, true);

		// An ack-related example
		Ack_synapse_client_example->On_error = []() {
		};
		Ack_synapse_client_example->Open(host, port, [this, this_](bool Error) {
			if (!Error) {
				auto & Channel(Ack_synapse_client_example->Get_channel(1)); 
				Channel.On_ack = [this, this_, &Channel](::std::string const & Topic_name, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, data_processors::synapse::amqp_0_9_1::bam::Table * Amqp_headers, bool Error) {
					::std::cerr << Topic_name + " got ACK. time(=" + ::std::to_string(Timestamp) + ") seq(=" + ::std::to_string(Sequence_number) + ") error(=" << ::std::to_string(Error) + ") headers(=";
					if (Amqp_headers) 
						Amqp_headers->output(::std::cerr);
					else
						::std::cerr << "nullptr";
					::std::cerr << ')' << ::std::endl; 
				};
				typename Synapse_channel_type::Subscription_options Options; 
				Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2015, 1, 1);
				Options.Supply_metadata = true;
				Options.Preroll_if_begin_timestamp_in_future = true;
				Options.Skipping_period = 1000000u; // => Ack semantics
				Options.Skip_payload = true; // => Ack semantics
				Channel.Subscribe(
					[this, this_, &Channel](bool Error) {
						if (!Error) {
							data_processors::synapse::amqp_0_9_1::bam::Table Fields;
							Fields.set("Current_subscription_topics_size", data_processors::synapse::amqp_0_9_1::bam::Long(1));
							Fields.set("Topics", data_processors::synapse::amqp_0_9_1::bam::Long(1));
							Channel.Subscribe_extended_ack(
								[this, this_](bool Error) {
									if (Error)
										throw ::std::runtime_error("Boo on Subscribe for extended Report.");
								}, ::std::move(Fields));
						} else throw ::std::runtime_error("Boo on Subscribe for fast ACK.");
					}
					, "test.cpp.*", Options
				);
			} else 
				throw ::std::runtime_error("Boo on Open.");
		});
	}

	void Publish_message(Synapse_channel_type & Channel, ::boost::shared_ptr<Waypoints_type> Waypoints, ::boost::shared_ptr<Contests_type> Contests, unsigned Remaining_messages_size) {
		if (Remaining_messages_size) {
			// The following 2 lines essentially enable (if wanted) parallel population of a given topic with repsect to other topics -- thereby allowing one to publish multiple topics mostly 'concurrently'(ish)...
			auto this_(base_type::shared_from_this());
			synapse::asio::io_service::get().post([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](){

				Contests->Set_datasource("Some datasource name. Indubitably. With randomness: " + ::std::to_string(::rand()));
				for (unsigned i(0); i != 100; ++i) {
					auto const Contest_key(::std::to_string(i));
					auto & Contest(Contests->Set_contest_element(::std::string(Contest_key)));
					auto & Contest_name(Contest.Set_contestName());
					Contest_name.Set_value("Race " + Contest_key + " With randomness: " + ::std::to_string(::rand()));

					// Direct encoding of date example.
					::DataProcessors::Shared::Date Start_date; 
					Start_date.Set_value(::data_processors::Federated_serialisation::Utils::Encode_date(2001, 2, 3));
					Contest.Set_startDate(::std::move(Start_date));

					// Direct encoding of time example.
					::DataProcessors::Shared::Time Start_time; 
					Start_time.Set_value(::data_processors::Federated_serialisation::Utils::Encode_time(5, 7, 8, 123));
					Contest.Set_startTime(::std::move(Start_time));

					// From-string encoding of timestamp example.
					::DataProcessors::Shared::Timestamp Scheduled_start; 
					Scheduled_start.Set_value(::data_processors::Federated_serialisation::Utils::Encode_timestamp_from_iso_string("20010203T050708.123000"));
					Contest.Set_scheduledStart(::std::move(Scheduled_start));

					for (unsigned Markets_iterator(0); Markets_iterator != 10; ++Markets_iterator) {
						auto const Markets_iterator_as_string(::std::to_string(Markets_iterator));
						auto & Market(Contest.Set_markets_element(::std::string(Markets_iterator_as_string)));
						Market.Set_name("Some example name " + Markets_iterator_as_string);
						Market.Set_type(::DataProcessors::Shared::BetTypeEnum::BetTypeEnum_WIN);
						for (unsigned Prices_iterator(0); Prices_iterator != 10; ++Prices_iterator)
							Market.Set_prices_element(::std::to_string(Prices_iterator), static_cast<double>(Prices_iterator));
					}

					for (unsigned Participants_iterator(0); Participants_iterator != 8; ++Participants_iterator) {
						auto const Participants_iterator_as_string(::std::to_string(Participants_iterator));
						auto & Participant(Contest.Set_participants_element(::std::string(Participants_iterator_as_string)));
						Participant.Set_name("Some example name " + Participants_iterator_as_string);
						Participant.Set_number(::std::string(Participants_iterator_as_string));
					}
				}

				// As per API requirements, Synapse_client interaction is only on its own strand. 
				Synapse_client->strand.dispatch([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](){
					if (::std::rand() % 7) {
						Channel.Publish([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](bool Error) {
								if (!Error) {
									Publish_message(Channel, Waypoints, Contests, Remaining_messages_size - 1);
								}
							} 
							, Contests, Waypoints, Remaining_messages_size % 10 ? true : false
						);
					} else { // just a quick hack of an example to publish non-message for sparsification control
						Channel.Publish<Contests_type *, Waypoints_type *>([this, this_, &Channel, Waypoints, Contests, Remaining_messages_size](bool Error) {
								if (!Error) {
									Publish_message(Channel, Waypoints, Contests, Remaining_messages_size);
								}
							} 
							, nullptr, nullptr, Remaining_messages_size % 10 ? true : false, ::std::string(), 0, 0, 1 + (!(::std::rand() % 3))
						);
					}
				});
			});
		}
	}

};

// Now on to the boilerplate codebase (mainly)...

// defaults...
::std::string static host("localhost");
::std::string static port("5672");
::std::string static ssl_key;
::std::string static ssl_certificate;
unsigned static so_rcvbuf(-1);
unsigned static so_sndbuf(-1);
unsigned static concurrency_hint(0);

void static
default_parse_arg(int const argc, char* argv[], int & i)
{
	if (i == argc - 1)
		throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
	else if (!::strcmp(argv[i], "--log_root"))
		data_processors::synapse::misc::log.Set_output_root(argv[++i]);
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
	else
		throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');
}



int static
run(int argc, char* argv[])
{
	try
	{
		// CLI...
		for (int i(1); i != argc; ++i)
			default_parse_arg(argc, argv, i);

		data_processors::synapse::misc::log("Synapse client starting.\n", true);

		if (!concurrency_hint)
			concurrency_hint = data_processors::synapse::misc::concurrency_hint();

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

		if (!ssl_key.empty() || !ssl_certificate.empty()) {
			if (ssl_key.empty() || ssl_certificate.empty())
				throw ::std::runtime_error("not all SSL options have been specified. mandatory:{--ssl_key, --ssl_certificate} optional: --ssl_listen_on");

			data_processors::synapse::misc::log("Listening in SSL on " + host + ':' + port + '\n', true);
			data_processors::synapse::asio::io_service::init_ssl_context();

			auto && ssl(data_processors::synapse::asio::io_service::get_ssl_context());
			ssl.use_certificate_file(ssl_certificate, basio::ssl::context::pem);
			ssl.use_private_key_file(ssl_key, basio::ssl::context::pem);
	
			::boost::make_shared<My_app_example<data_processors::synapse::asio::ssl_tcp_socket>>()->Run();
		} else
			::boost::make_shared<My_app_example<data_processors::synapse::asio::tcp_socket>>()->Run();

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
	return ::data_processors::Some_example::run(argc, argv);
}

