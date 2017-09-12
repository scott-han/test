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

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/Waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/Contests4_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

namespace data_processors { namespace Test {

::std::string static host("localhost");
::std::string static port("5672");

unsigned static constexpr Topics_Size{3};

unsigned static constexpr Data_Values_Size{20};

::std::atomic_uint Done_Topics_Size{0};
::std::atomic_uint_fast64_t Cumulative_Total_Received_Bytes_Size{0};

namespace basio = ::boost::asio;

typedef ::DataProcessors::Waypoints::waypoints Waypoints_type;
typedef ::DataProcessors::Contests4::Contests Contests_type;

template <typename Socket_type> 
struct Cumulative_Subscriber : ::boost::enable_shared_from_this<Cumulative_Subscriber<Socket_type>> {
	typedef ::boost::enable_shared_from_this<Cumulative_Subscriber<Socket_type>> base_type;
	::std::string Topic_Name;
	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;
	::boost::shared_ptr<Synapse_client_type> Subscriber;
	Cumulative_Subscriber(::std::string const & Topic_Name) : Topic_Name(Topic_Name) {
	}
	~Cumulative_Subscriber() {
		synapse::asio::io_service::Stop();
	};
	void Run() {
		Subscriber = ::boost::make_shared<Synapse_client_type>();
		data_processors::synapse::misc::log("Starting cumulative data subscription.\n", true);
		Subscriber->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) mutable {
			if (!Error) {

				auto & Channel(Subscriber->Get_channel(1));

				Channel.On_incoming_message = [this, &Channel, this_(::std::move(this_))](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
					if (Envelope != nullptr) {
						if (Envelope->Type_name == "Contests") {
							Channel.template Process_incoming_message<Waypoints_type, Contests_type>(Envelope,
								[this, &Channel, this_
								#if defined(_MSC_VER) && !defined(__clang__)
									(this_)
								#endif
								](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) mutable {
									if (Envelope != nullptr) {
										auto Contests(static_cast<Contests_type *>(Envelope->Message.get()));
										if (Contests->Get_datasource().size() > Cumulative_Total_Received_Bytes_Size)
											throw ::std::runtime_error("Cumulative subscriber: incoming message is greater than anticipated.");
										if (Cumulative_Total_Received_Bytes_Size -= Contests->Get_datasource().size()) {
											synapse::asio::io_service::get().post([this, &Channel, Envelope, this_(::std::move(this_))]() mutable {
												assert(Envelope->Message);
												Subscriber->strand.dispatch([&Channel, Envelope, this_(::std::move(this_))]() mutable {
													Channel.Release_message(*Envelope);
												});
											});
										} else { 
											data_processors::synapse::misc::log("Cumulative_Subscriber closing.\n", true);
											Subscriber->Close();
										}
									} else 
										throw ::std::runtime_error("Cumulative_Subscriber null envelope.");
								}
							);
						} else
							throw ::std::runtime_error("Cumulative_Subscriber unexpected top type.");
					}
				};
				typename Synapse_channel_type::Subscription_options Options;
				Options.Begin_utc = 1;
				Channel.Subscribe(
					[](bool Error) {
						if (Error) 
							throw ::std::runtime_error("Error in subcsribe callback");
					}
					, Topic_Name, Options
				);
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 50);
	}
};

template <typename Socket_type> 
struct Tester : ::boost::enable_shared_from_this<Tester<Socket_type>> {

	typedef ::boost::enable_shared_from_this<Tester<Socket_type>> base_type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

	struct Processing_Controller {
		unsigned static constexpr Total_Passes_Size{3};
		unsigned static constexpr Datasource_Value_Bandwidth_Size{1024 * 1024 * 135};

		int Scan_Index{0};
		unsigned Accumulated_Bandwidth_Size{0};
		int Scan_Direction{1};
		int Passes_Size{0};
		uint_fast64_t Total_Size_Processed{0};

		bool Ping(unsigned const & Value_Size) {
			Total_Size_Processed += Value_Size;
			if ((Accumulated_Bandwidth_Size += Value_Size) > Datasource_Value_Bandwidth_Size) {
				Accumulated_Bandwidth_Size = 0;
				if ((Scan_Index += Scan_Direction) == Data_Values_Size || Scan_Index < 0)
					if (++Passes_Size != Total_Passes_Size) {
						Scan_Direction = -Scan_Direction;
						Scan_Index += Scan_Direction;
					} else
						return false;
			}
			return true;
		}
	};

	::std::string Topic_Name;
	Tester(::std::string const & Topic_Name) : Topic_Name(Topic_Name) {
	}

	~Tester() {
		data_processors::synapse::misc::log(Topic_Name + " Published overall bytes: " + ::std::to_string(Publishing_Controller.Total_Size_Processed) + "\n", true);
		data_processors::synapse::misc::log(Topic_Name + " Subscribed overall bytes: " + ::std::to_string(Subscribing_Controller.Total_Size_Processed) + "\n", true);
		if (Publishing_Controller.Total_Size_Processed != Subscribing_Controller.Total_Size_Processed || !Publishing_Controller.Total_Size_Processed)
			throw ::std::runtime_error(Topic_Name + " Something isnt right -- total pub vs sub should match and be non-zero.");
	}
	
	Processing_Controller Publishing_Controller;
	Processing_Controller Subscribing_Controller;

	::std::vector<::boost::shared_ptr<Contests_type>> Datasource_Values;

	::boost::shared_ptr<Synapse_client_type> Subscriber;
	::boost::shared_ptr<Synapse_client_type> Publisher;

	void Run() {
		Datasource_Values.reserve(Data_Values_Size);
		::std::string Value("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
		for (unsigned i(0); i != Data_Values_Size; ++i) {
			Datasource_Values.emplace_back(::boost::make_shared<Contests_type>());
			Datasource_Values.back()->Set_datasource(::std::string(Value += Value));
		}
		Subscribe();
		Publish();
	}

	void Subscribe() {
		Subscriber = ::boost::make_shared<Synapse_client_type>();
		data_processors::synapse::misc::log("Starting data subscription.\n", true);
		Subscriber->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) mutable {
			if (!Error) {

				auto & Channel(Subscriber->Get_channel(1));

				Channel.On_incoming_message = [this, &Channel, this_(::std::move(this_))](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
					if (Envelope != nullptr) {
						if (Envelope->Type_name == "Contests") {
							Channel.template Process_incoming_message<Waypoints_type, Contests_type>(Envelope,
								[this, &Channel, this_
								#if defined(_MSC_VER) && !defined(__clang__)
									(this_)
								#endif
								](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) mutable {
									if (Envelope != nullptr) {
										auto Contests(static_cast<Contests_type *>(Envelope->Message.get()));
										if (Subscribing_Controller.Ping(Contests->Get_datasource().size())) {
											synapse::asio::io_service::get().post([this, &Channel, Envelope, this_(::std::move(this_))]() mutable {
												assert(Envelope->Message);
												Subscriber->strand.dispatch([&Channel, Envelope, this_(::std::move(this_))]() mutable {
													Channel.Release_message(*Envelope);
												});
											});
										} else { 
											data_processors::synapse::misc::log("Subscriber closing.\n", true);
											Subscriber->Close();
											Cumulative_Total_Received_Bytes_Size += Subscribing_Controller.Total_Size_Processed;
											if (++Done_Topics_Size == Topics_Size)
												(::boost::make_shared<Cumulative_Subscriber<data_processors::synapse::asio::tcp_socket>>("topic.*"))->Run();
										}
									} else 
										throw ::std::runtime_error(Topic_Name + " null envelope.");
								}
							);
						} else
							throw ::std::runtime_error(Topic_Name + " unexpected top type.");
					} 
				};
				typename Synapse_channel_type::Subscription_options Options;
				Options.Begin_utc = 1;
				Channel.Subscribe(
					[](bool Error) {
						if (Error) 
							throw ::std::runtime_error("Error in subcsribe callback");
					}
					, Topic_Name, Options
				);
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 50);
	}

	void Publish() {
		Publisher = ::boost::make_shared<Synapse_client_type>();
		Publisher->On_error = [](){
			throw ::std::runtime_error("Publisher is not expected to get an error...");
		};
		Publisher->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) mutable {
			if (!Error) {
				Publisher->Open_channel(2, [this, this_(::std::move(this_))](bool Error) mutable {
					if (!Error) {
						auto & Channel(Publisher->Get_channel(2)); 
						Channel.Set_as_publisher(Topic_Name);
						Publish_message(Channel, this_);
					} else 
						throw ::std::runtime_error("Error in callback on Open_channel");
				});
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 50);
	}

	::std::string Strearm_Id;
	void Publish_message(Synapse_channel_type & Channel, ::boost::shared_ptr<Tester> & this_) {
		auto Contests(Datasource_Values[Publishing_Controller.Scan_Index]);
		Channel.Publish([this, &Channel, Contests, this_(::std::move(this_))](bool Error) mutable {
				if (!Error) {
					if (Publishing_Controller.Ping(Contests->Get_datasource().size()))
						Publish_message(Channel, this_);
					else {
						data_processors::synapse::misc::log("Publisher closing.\n", true);
						Publisher->Soft_Close([this, this_(::std::move(this_))](bool){Publisher->Close();});
					}
				} else
					throw ::std::runtime_error("Boo in publish.");
			}, Contests.get(), static_cast<Waypoints_type *>(nullptr)
		);
	}
};

int static Run(int argc, char* argv[]) {
	try {
		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		data_processors::synapse::misc::log("Synapse client starting.\n", true);

		auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		data_processors::synapse::asio::Autoquit_on_signals Signal_quitter;

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);

		for (unsigned i(0); i != Topics_Size; ++i)
			(::boost::make_shared<Tester<data_processors::synapse::asio::tcp_socket>>("topic." + ::std::to_string(i)))->Run();

		::std::vector<::std::thread> pool(concurrency_hint * 8 - 1);
		for (auto && i : pool) {
			i = ::std::thread([]()->void{
				data_processors::synapse::asio::run();
			});
		}

		data_processors::synapse::asio::run();

		for (auto & i: pool)
			i.join();

		if (Cumulative_Total_Received_Bytes_Size)
			throw ::std::runtime_error("Cumulative_Total_Received_Bytes_Size should be zero");

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

