#include <iostream>
#include <atomic>
#include <chrono>
#include <random>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/deadline_timer.hpp>

#include "Basic_client_1.h"
#include "../../../misc/Stop_Watch.h"

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>

#define DATA_PROCESSORS_CPP_CLIENT_BENCHMARKING_1_MINIMAL_THRIFT_PROCESSING

#ifndef DATA_PROCESSORS_CPP_CLIENT_BENCHMARKING_1_MINIMAL_THRIFT_PROCESSING
	#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>
#else
	#include <data_processors/Federated_serialisation/Schemas/XRate_types.h>
#endif


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

unsigned static Total_Messages_Size{0};
::std::atomic_uint_fast32_t static Received_messages_size{0};

namespace data_processors { namespace Test {

// defaults...
::std::string static host("localhost");
::std::string static port("5672");

static constexpr char const  * Key_token = "vid|20161218;MORUYA;M;3";


data_processors::synapse::misc::Stopwatch_Type static Stopwatch;

namespace basio = ::boost::asio;

typedef ::DataProcessors::Waypoints::waypoints Waypoints_type;
#ifndef DATA_PROCESSORS_CPP_CLIENT_BENCHMARKING_1_MINIMAL_THRIFT_PROCESSING
	typedef ::DataProcessors::Contests4::Contests Message_Type;
#else
	typedef ::DataProcessors::XRate::Rate Message_Type;
#endif

template <typename Socket_type> 
struct Tester : ::boost::enable_shared_from_this<Tester<Socket_type>> {

	typedef ::boost::enable_shared_from_this<Tester<Socket_type>> base_type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

	::boost::shared_ptr<Synapse_client_type> Subscriber;
	::boost::shared_ptr<Synapse_client_type> Publisher;

	::std::vector<::std::pair<int_fast64_t, uint_fast32_t>> Subscriber_statistics;

	::std::random_device Random_device;
	::std::default_random_engine Random_engine{Random_device()};
	::std::uniform_int_distribution<uint_fast16_t> Uniform_distribution{0, 7};

	void Subscribe() {
		Subscriber_statistics.reserve(Total_Messages_Size);
		Subscriber = ::boost::make_shared<Synapse_client_type>(0, -1, -1, -1, -1, true);
		data_processors::synapse::misc::log("Starting data subscription.\n", true);
		Subscriber->On_error = [](){};
		Subscriber->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) mutable {
			if (!Error) {

				auto & Channel(Subscriber->Get_channel(1));

				Channel.On_incoming_message = [this, this_(::std::move(this_)), &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
					if (Envelope != nullptr) {
						Channel.template Process_incoming_message<
							#if defined(__GNUC__) || defined (__clang__)
								Waypoints_type, Message_Type
							#else
								typename Waypoints_type, typename Message_Type
							#endif
						>(Envelope,
							[this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
								if (Envelope != nullptr) {
									if (++Received_messages_size == Total_Messages_Size) {
										data_processors::synapse::misc::log("Subscriber closing.\n", true);
										Subscriber->Close();
									} else {
										//synapse::asio::io_service::get().post([this, this_(::std::move(this_)), &Channel, Envelope](){
											assert(Envelope->Message);
											if (Envelope->Waypoints) {
												auto Concrete_waypoints(static_cast<Waypoints_type *>(Envelope->Waypoints.get()));
												if (Concrete_waypoints->Is_path_set()) {
													auto && Path(Concrete_waypoints->Get_path());
													for (auto && Waypoint : Path) 
														if (Waypoint.Is_tag_set() && Waypoint.Is_timestamp_set()) {
															Subscriber_statistics.emplace_back(::std::piecewise_construct,
																::std::forward_as_tuple(Stopwatch.Get_Delta_As_Microseconds_With_Auto_Correction(Waypoint.Get_timestamp(), Stopwatch.Ping())),
																::std::forward_as_tuple(Envelope->Get_payload_size())
															);
															break;
														}
												}
											}
											//Subscriber->strand.dispatch([this, this_(::std::move(this_)), &Channel, Envelope](){
												Channel.Release_message(*Envelope);
											//});
										//});
									}
								}
							}
						, true);
					}
				};
				typename Synapse_channel_type::Subscription_options Options;
				Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2015, 1, 1);
				Options.Tcp_No_delay = true;
				Channel.Subscribe(
					[](bool Error) {
						if (Error) 
							throw ::std::runtime_error("Error in subcsribe callback");
					}
					, "some.topic.indeed.abc.xyz", Options
				);
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 50 , true);
	}

	::boost::shared_ptr<Waypoints_type> Published_waypoints;
	::DataProcessors::Waypoints::waypoint * Waypoint_ptr;
	::boost::shared_ptr<Message_Type> Message;

	void Publish() {

		Message = ::boost::make_shared<Message_Type>();
		#ifndef DATA_PROCESSORS_CPP_CLIENT_BENCHMARKING_1_MINIMAL_THRIFT_PROCESSING
			Message->Set_datasource("Some datasource name. Indubitably. With randomness: " + ::std::to_string(Uniform_distribution(Random_engine)));
			for (unsigned i(0); i != 8; ++i) {
				auto & Contest(Message->Set_contest_element(Key_token + ::std::to_string(i)));
				auto & Contest_name(Contest.Set_contestName());
				Contest_name.Set_value("Some contest name indeed " + ::std::to_string(Uniform_distribution(Random_engine)));
				for (int i(0); i != 8; ++i) {
					auto & Market(Contest.Set_markets_element(Key_token + ::std::to_string(i)));
					for (int i(0); i != 8; ++i) {
						auto & Outer_offer(Market.Set_offers_element(Key_token + ::std::to_string(i)));
						for (int i(0); i != 8; ++i) {
							auto & Offer(Outer_offer.Set_offers_element(Key_token + ::std::to_string(i)));
							Offer.Set_price(Uniform_distribution(Random_engine));
						}
					}
				}
			}
		#else
			Message->Set_currency(::std::string(Key_token));
		#endif

		Publisher = ::boost::make_shared<Synapse_client_type>(0, -1, -1, -1, -1, true);

		Published_waypoints = ::boost::make_shared<Waypoints_type>();

		Waypoint_ptr = &Published_waypoints->Add_path_element(::DataProcessors::Waypoints::waypoint());
		Waypoint_ptr->Set_tag("Benchmark_1.publish");

		Publisher->On_error = [](){};
		Publisher->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) mutable {
			if (!Error) {
				Publisher->Open_channel(2, [this, this_(::std::move(this_))](bool Error){
					if (!Error) {
						auto & Channel(Publisher->Get_channel(2)); 
						Channel.Set_as_publisher("some.topic.indeed.abc.xyz");
						Publish_message(Channel, Total_Messages_Size);
					} else 
						throw ::std::runtime_error("Error in callback on Open_channel");
				});
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 50, true);
	}

	::std::string Strearm_Id;
	void Publish_message(Synapse_channel_type & Channel, unsigned Remaining_messages_size) {
		if (Remaining_messages_size) {
			Waypoint_ptr->Set_timestamp(static_cast<int64_t>(Stopwatch.Ping()), true);
			#ifndef DATA_PROCESSORS_CPP_CLIENT_BENCHMARKING_1_MINIMAL_THRIFT_PROCESSING
				auto & Contest(Message->Get_contest_element(Key_token + ::std::to_string(Uniform_distribution(Random_engine))));
				for (int i(0); i != 1; ++i) {
					auto & Market(Contest.Get_markets_element(Key_token + ::std::to_string(Uniform_distribution(Random_engine))));
					for (int i(0); i != 1; ++i) {
						auto & Outer_offer(Market.Get_offers_element(Key_token + ::std::to_string(Uniform_distribution(Random_engine))));
						for (int i(0); i != 2; ++i) {
							auto & Offer(Outer_offer.Get_offers_element(Key_token + ::std::to_string(Uniform_distribution(Random_engine))));
							Offer.Set_price(Uniform_distribution(Random_engine), true);
						}
					}
				}
			#endif
			Channel.Publish([this, &Channel, Remaining_messages_size, this_(base_type::shared_from_this())](bool Error) mutable {
					if (!Error) {
						Publisher_throttling_timer.expires_from_now(::boost::posix_time::milliseconds(50));
						Publisher_throttling_timer.async_wait(Publisher->strand.wrap([this, &Channel, Remaining_messages_size, this_(::std::move(this_))](::boost::system::error_code const &Error){
							if (!Error)
								Publish_message(Channel, Remaining_messages_size - 1);
						}));
					} else
						throw ::std::runtime_error("Boo in publish.");
				} 
				, Message.get(), Published_waypoints.get(), 
				#ifndef DATA_PROCESSORS_CPP_CLIENT_BENCHMARKING_1_MINIMAL_THRIFT_PROCESSING
					true, 
				#else
					false,
				#endif
				Strearm_Id, Total_Messages_Size - Remaining_messages_size + 1, 0, 0, true
			);
		} else {
			data_processors::synapse::misc::log("Publisher closing.\n", true);
			Publisher->Close();
		}
	}
	::boost::asio::deadline_timer Publisher_throttling_timer{data_processors::synapse::asio::io_service::get()};
};

int static Run(int argc, char* argv[]) {
	enum Action {None, Subscribe, Publish};
	Action Run_as{None};
	unsigned IO_Service_Concurrency(0);
	unsigned IO_Service_Concurrency_Reserve_Multiplier(8);
	uint_fast64_t Processor_Affinity(0);
	bool Busy_Poll(false);
	try {
		// CLI...
		for (int i(1); i != argc; ++i)
			if  (!::strcmp(argv[i], "--Busy_Poll"))
				Busy_Poll = true;
			else if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
			if (!::strcmp(argv[i], "--Action")) {
				++i;
				if (!::strcmp(argv[i], "Subscribe"))
					Run_as = Subscribe;
				else if (!::strcmp(argv[i], "Publish"))
					Run_as = Publish;
				else
					throw ::std::runtime_error("unknown --Action value(=" + ::std::string(argv[i]) + ')');
			} else if (!::strcmp(argv[i], "--processor_affinity"))
				Processor_Affinity = ::boost::lexical_cast<uint_fast64_t>(argv[++i]);
			else if (!::strcmp(argv[i], "--IO_Service_Concurrency"))
				#ifndef SYNAPSE_SINGLE_THREADED
						IO_Service_Concurrency = ::boost::lexical_cast<unsigned>(argv[++i]);
				#else
						throw ::std::runtime_error("--IO_Service_Concurrency is not supported in this single-threaded build");
				#endif
			else if (!::strcmp(argv[i], "--IO_Service_Concurrency_Reserve_Multiplier")) {
				#ifndef SYNAPSE_SINGLE_THREADED
					IO_Service_Concurrency_Reserve_Multiplier = ::boost::lexical_cast<unsigned>(argv[++i]);
					if (!IO_Service_Concurrency_Reserve_Multiplier)
						throw ::std::runtime_error("--IO_Service_Concurrency_Reserve_Multiplier cannot be less than 1");
				#else
					throw ::std::runtime_error("--IO_Service_Concurrency_Reserve_Multiplier is not supported in this single-threaded build");
				#endif
			} else if (!::strcmp(argv[i], "--Total_Messages_Size"))
				Total_Messages_Size = ::std::stoll(argv[++i]);
			else
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		if (!Total_Messages_Size)
			throw ::std::runtime_error("--Total_Messages_Size option must be specified and should be > 0");

		if (!IO_Service_Concurrency)
			IO_Service_Concurrency = synapse::misc::concurrency_hint();

		if (Run_as == None)
			throw ::std::runtime_error("--Action option must be specified");

		data_processors::synapse::misc::log("Synapse client starting.\n", true);

		if (Processor_Affinity)
			data_processors::synapse::misc::Set_Thread_Processor_Affinity(Processor_Affinity);

		struct multi_priveleges_type {
			DWORD               PrivilegeCount;
			LUID_AND_ATTRIBUTES Privileges[1];
		};
		multi_priveleges_type privs;
		privs.PrivilegeCount = 1;
		privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		HANDLE tok;
		if (
				!::LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &privs.Privileges[0].Luid) 
				|| !::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok) 
				|| !::AdjustTokenPrivileges(tok, FALSE, (PTOKEN_PRIVILEGES)&privs, 0, NULL, NULL) 
				|| ::GetLastError() != ERROR_SUCCESS
				|| !::CloseHandle(tok)
			 )
			if (data_processors::synapse::database::nice_performance_degradation < -39)
				throw ::std::runtime_error("getting SE_LOCK_MEMORY_NAME");
			else if (data_processors::synapse::database::nice_performance_degradation < 1)
				synapse::misc::log("WARNING: PERFORMANCE DEGRADATION!!!	could not activate SE_LOCK_MEMORY_NAME\n", true);

		// create based on chosen runtime parameters
		data_processors::synapse::asio::io_service::init(IO_Service_Concurrency);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);

		auto Tester(::boost::make_shared<Tester<data_processors::synapse::asio::tcp_socket>>());

		if (Run_as == Subscribe)
			Tester->Subscribe();
		else
			Tester->Publish();

		if (!::SetPriorityClass(::GetCurrentProcess(), REALTIME_PRIORITY_CLASS) || !::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
			throw ::std::runtime_error("SetPriorityClass(=REALTIME_PRIORITY_CLASS) || SetThreadPriority(=THREAD_PRIORITY_TIME_CRITICAL)");

		#ifndef SYNAPSE_SINGLE_THREADED
		::std::vector<::std::thread> pool;
		auto const Pool_Size(IO_Service_Concurrency * IO_Service_Concurrency_Reserve_Multiplier - 1);
		pool.reserve(Pool_Size);
		for (unsigned i(0); i != Pool_Size; ++i) {
			pool.emplace_back([Processor_Affinity, Busy_Poll]()->void{
				if (!::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
					throw ::std::runtime_error("SetThreadPriority(=THREAD_PRIORITY_TIME_CRITICAL)");
				if (Processor_Affinity)
					data_processors::synapse::misc::Set_Thread_Processor_Affinity(Processor_Affinity);
				data_processors::synapse::asio::run(0, Busy_Poll);
			});
		}
		#endif

		data_processors::synapse::asio::run(0, Busy_Poll);

		#ifndef SYNAPSE_SINGLE_THREADED
		for (auto & i: pool)
			i.join();
		#endif

		if (Run_as == Subscribe) {
			if (Received_messages_size != Total_Messages_Size)
				throw ::std::runtime_error("did not receieve all messages");
			for (auto const & Point : Tester->Subscriber_statistics)
				::std::cout << Point.first << ',' << Point.second << ::std::endl;
		}

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

