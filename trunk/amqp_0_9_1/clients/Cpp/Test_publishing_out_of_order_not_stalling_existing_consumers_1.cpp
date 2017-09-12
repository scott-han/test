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
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

unsigned static constexpr Total_messages_size{3500000};
::std::atomic_uint_fast32_t static Received_messages_size{0};
unsigned static Erroneously_published_messages_size{0};

namespace data_processors { namespace Test {

// defaults...
::std::string static host("localhost");
::std::string static port("5672");


namespace basio = ::boost::asio;

typedef DataProcessors::Waypoints::waypoints Waypoints_type;
typedef DataProcessors::Contests4::Contests Contests_type;
typedef DataProcessors::Contests4::Contest Contest_type;

auto static Build_contests() {
	auto Contests(::boost::make_shared<Contests_type>());
	Contests->Set_datasource("Some datasource name. Indubitably. With randomness: " + ::std::to_string(::rand()));
	for (unsigned i(0); i != 5; ++i) {
		auto & Contest(Contests->Set_contest_element(::std::to_string(i)));
		auto & Contest_name(Contest.Set_contestName());
		::std::string Some_long_contest_name("Some long contest name indeed " + ::std::to_string(::rand()));
		for (int i(0); i != 5; ++i)
			Some_long_contest_name += Some_long_contest_name;
		Contest_name.Set_value(::std::move(Some_long_contest_name));
	}
	return Contests;
}


template <typename Socket_type> 
struct Tester : ::boost::enable_shared_from_this<Tester<Socket_type>> {

	typedef ::boost::enable_shared_from_this<Tester<Socket_type>> base_type;

	typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
	typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

	::boost::shared_ptr<Synapse_client_type> Subscriber{::boost::make_shared<Synapse_client_type>()};
	::boost::shared_ptr<Synapse_client_type> Publisher{::boost::make_shared<Synapse_client_type>()};

	struct Erroneous_publisher_type : data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type, Erroneous_publisher_type> {
		typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type, Erroneous_publisher_type> base_type;
		// Temporary compromises for various compilers
		void on_msg_size_read() {
			base_type::on_msg_size_read();
		}
		void on_read_some_awaiting_frame() {
			base_type::on_read_some_awaiting_frame();
		}
		void on_msg_ready() {
			base_type::on_msg_ready();
		}
		void on_post_from_msg_ready() noexcept {
			base_type::on_post_from_msg_ready();
		}
		// ... end of temprorary compromises (todo: see if can take it out completly later on).
		::boost::shared_ptr<Tester> Containing_tester;
		Erroneous_publisher_type(::boost::shared_ptr<Tester> && Containing_tester) : Containing_tester(Containing_tester) {
			data_processors::synapse::misc::log("Erroneous publisher ctor\n", true);
		}
		~Erroneous_publisher_type() {
			data_processors::synapse::misc::log("Erroneous publisher dtor\n", true);
			Containing_tester->Run_erroneous_pass();
		}
	};

	::boost::asio::deadline_timer Erroneous_publisher_timer{data_processors::synapse::asio::io_service::get()};
	bool Erroneous_publisher_timer_pending{false};

	void Run_erroneous_pass(bool First_time = false) {
		Erroneous_publisher_timer.expires_from_now(::boost::posix_time::seconds(10));
		Erroneous_publisher_timer.async_wait([this, this_(base_type::shared_from_this()), Before_timeout_received_messages_size(Received_messages_size.load()), First_time](::boost::system::error_code const &Error){
			if (!Error) {
				if (Received_messages_size != Total_messages_size) {
					if (Before_timeout_received_messages_size == Received_messages_size)
						throw ::std::runtime_error("Subscriber should have gotten more messages by now");
					Invoke_erroneous_publisher(First_time);
				}
			}
		});
	}

	void Invoke_erroneous_publisher(bool First_time = false) {
		auto Erroneous_publisher(::boost::make_shared<Erroneous_publisher_type>(base_type::shared_from_this()));
		Erroneous_publisher->On_error = [](){};
		Erroneous_publisher->Open(host, port, [Erroneous_publisher, First_time](bool Error) {
			if (!Error) {
				Erroneous_publisher->Open_channel(2, [Erroneous_publisher, First_time](bool Error){
					if (!Error) {
						auto & Channel(Erroneous_publisher->Get_channel(2)); 
						Channel.Set_as_publisher("a.a");
						++Erroneously_published_messages_size;
						Channel.Publish([&Channel, First_time](bool Error) {
							if (!Error && First_time)
								Channel.Publish([](bool) {}, Build_contests(), ::boost::shared_ptr<Waypoints_type>(), false, ::std::string(), 1, 5);
						}, Build_contests(), ::boost::shared_ptr<Waypoints_type>(), false, ::std::string(), 1, 5);
					} else 
						throw ::std::runtime_error("Error in callback on Open_channel");
				});
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 10);
	}

	void Subscribe() {
		data_processors::synapse::misc::log("Starting data subscription.\n", true);
		Subscriber->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) {
			if (!Error) {
				Run_erroneous_pass(true);

				auto & Channel(Subscriber->Get_channel(1));

				Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
					if (Envelope != nullptr) {
						if (Envelope->Type_name == "Contests") {
							Channel.template Process_incoming_message<Waypoints_type, Contests_type>(Envelope,
								[this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope){
									if (Envelope != nullptr) {
										if (++Received_messages_size == Total_messages_size)
											Subscriber->Close();
										else {
											Channel.Release_message(*Envelope);
											if (!(Received_messages_size % 10000))
												data_processors::synapse::misc::log("Ping. received " + ::std::to_string(Received_messages_size) + " messages.\n", true);
										}
									}
								}
							);
						}
					}
				};
				typename Synapse_channel_type::Subscription_options Options;
				Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2015, 1, 1);
				Channel.Subscribe(
					[this_](bool Error) {
						if (Error) 
							throw ::std::runtime_error("Error in subcsribe callback");
					}
					, "a.*", Options
				);
			} else 
				throw ::std::runtime_error("Boo on Open.");
		});
	}

	void Publish() {

		Publisher->On_error = [](){};
		data_processors::synapse::misc::log("Initial seeding with data started.\n", true);
		Publisher->Open(host, port, [this, this_(base_type::shared_from_this())](bool Error) {
			if (!Error) {
				Publisher->Open_channel(2, [this, this_](bool Error){
					if (!Error) {
						auto & Channel(Publisher->Get_channel(2)); 
						Channel.Set_as_publisher("a.b");
						Publish_message(Channel, Build_contests(), Total_messages_size);
					} else 
						throw ::std::runtime_error("Error in callback on Open_channel");
				});
			} else 
				throw ::std::runtime_error("Boo on Open.");
		}, 10);
	}

	void Publish_message(Synapse_channel_type & Channel, ::boost::shared_ptr<Contests_type> Contests, unsigned Remaining_messages_size) {
		if (Remaining_messages_size) {
			data_processors::synapse::asio::io_service::get().post([this, this_(this->shared_from_this()), &Channel, Contests, Remaining_messages_size](){
				Publisher->strand.dispatch([this, this_, &Channel, Contests, Remaining_messages_size](){
					Channel.Publish([this, this_, &Channel, Contests, Remaining_messages_size](bool Error) {
							if (!Error)
								Publish_message(Channel, Contests, Remaining_messages_size - 1);
							else
								throw ::std::runtime_error("Boo in publish.");
						} 
						, Contests, ::boost::shared_ptr<Waypoints_type>(), false, ::std::string(), Total_messages_size - Remaining_messages_size + 1
					);
				});
			});
		} else {
			auto & Channel(Publisher->Get_channel(1));
			Channel.On_incoming_message = [this, this_(this->shared_from_this()), &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
				if (Envelope != nullptr) {
					if (Envelope->Type_name == "Contests") {
						if (Envelope->Pending_buffers.front().Message_sequence_number != Total_messages_size) {
							Channel.Stepover_incoming_message(Envelope,
								[this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope){
									if (Envelope != nullptr)
										Channel.Release_message(*Envelope);
								}
							);
						} else {
							data_processors::synapse::misc::log("Initial seeding with data verified.\n", true);
							Publisher->Close();
						}
					}
				}
			};
			typename Synapse_channel_type::Subscription_options Options;
			Options.Preroll_if_begin_timestamp_in_future = true;
			Options.Supply_metadata = true;
			Channel.Subscribe(
				[this_(this->shared_from_this())](bool Error) {
					if (Error) 
						throw ::std::runtime_error("Error in subcsribe callback");
				}
				, "a.b", Options
			);
		}
	}
};

int static Run(int argc, char* argv[]) {
	enum Action {None, Subscribe, Publish};
	Action Run_as{None};
	try {
		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, host, port))
			if (!::strcmp(argv[i++], "--Action")) {
				if (!::strcmp(argv[i], "Subscribe"))
					Run_as = Subscribe;
				else if (!::strcmp(argv[i], "Publish"))
					Run_as = Publish;
				else
					throw ::std::runtime_error("unknown --Action value(=" + ::std::string(argv[i]) + ')');
			} else
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		if (Run_as == None)
			throw ::std::runtime_error("--Action option must be specified");

		data_processors::synapse::misc::log("Synapse client starting.\n", true);

		auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

		// create based on chosen runtime parameters
		data_processors::synapse::asio::io_service::init(concurrency_hint);
		data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

		data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);

		if (Run_as == Subscribe)
			::boost::make_shared<Tester<data_processors::synapse::asio::tcp_socket>>()->Subscribe();
		else
			::boost::make_shared<Tester<data_processors::synapse::asio::tcp_socket>>()->Publish();

		::std::vector<::std::thread> pool(concurrency_hint * 8 - 1);
		for (auto && i : pool) {
			i = ::std::thread([]()->void{
				data_processors::synapse::asio::run();
			});
		}

		data_processors::synapse::asio::run();

		for (auto & i: pool)
			i.join();

		if (Run_as == Subscribe) {
			if (Received_messages_size != Total_messages_size)
				throw ::std::runtime_error("did not receieve all messages");

			if (Erroneously_published_messages_size < 10)
				throw ::std::runtime_error("did not publish enough erroneous messages(=" + ::std::to_string(Erroneously_published_messages_size) + "), adjust testing parameters");
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

