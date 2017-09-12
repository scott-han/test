#include <iostream>
#include <atomic>
#include <chrono>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/high_resolution_timer.hpp>

#include "../../../misc/misc.h"
#include "Basic_client_1.h"

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

	namespace data_processors {

		namespace Test {
			unsigned static Message_size{ 100 };
			unsigned static Total_messages{ 300000 };
			::std::atomic_uint_fast32_t static Received_messages{ 0 };
			::std::string static Log_path;

			::std::string static Host{ "localhost" };
			::std::string static Port{ "5672" };
			::std::string static Source{ "source" };
			::std::string static Topic;
			::std::map<uint_fast64_t, uint_fast64_t> Latency;
			unsigned static Time_out{ 60 };
			bool static Tcp_no_delay{ false };
			bool static Is_publisher{ true };
			bool static Has_waypoint{ false };
			int_fast64_t Frequency{ 0 };
			int_fast64_t static Duration{ -1 };

			namespace basio = ::boost::asio;
			typedef DataProcessors::Waypoints::waypoints Waypoints_type;
			typedef DataProcessors::Contests4::Contests Contests_type;
			typedef DataProcessors::Contests4::Contest Contest_type;
			void static Log_performance(std::string const& logs)
			{
				::data_processors::synapse::misc::log("PERFORMANCE LOG: " + logs, true);
			}

			static int_fast64_t GetHighResolutionTime() {
				if (Frequency == 0)
				{
					LARGE_INTEGER temp;
					if (QueryPerformanceFrequency(&temp))
					{
						Frequency = temp.QuadPart;
					}
				}
				if (Frequency == 0)
					throw ::std::runtime_error("Could not acquire Frequency");
				LARGE_INTEGER File_time;
				// Microsoft document says this call will never fail since XP.
				::QueryPerformanceCounter(&File_time);
				return File_time.QuadPart;
			}
			auto static  Last_call{ GetHighResolutionTime() };
			template <typename Socket_type>
			struct Test_client : ::boost::enable_shared_from_this<Test_client<Socket_type>>
			{
				typedef ::boost::enable_shared_from_this<Test_client<Socket_type>> base_type;

				typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
				typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

				::boost::shared_ptr<Synapse_client_type> Client{ ::boost::make_shared<Synapse_client_type>() };
				::boost::shared_ptr<Synapse_client_type> Ack_client{ ::boost::make_shared<Synapse_client_type>() };
				::boost::shared_ptr<Contests_type> Contests;
				::boost::shared_ptr<Waypoints_type> Waypoints;
				::DataProcessors::Waypoints::waypoint * Waypoint_ptr;

				::std::string Strearm_Id;

				void Start_ack_client()
				{
					Ack_client->On_error = []() {};
					auto this_(base_type::shared_from_this());
					Ack_client->Open(Host, Port, [this, this_](bool Error) {
						if (!Error) {
							auto & Channel(Ack_client->Get_channel(1));
							Channel.On_ack = [this, this_, &Channel](::std::string const & Topic_name, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, data_processors::synapse::amqp_0_9_1::bam::Table * Amqp_headers, bool error) {
								if (!error) 
								{
									if (Sequence_number >= Total_messages)
									{
										Client->strand.post([this, this_]() { Client->Close(); });
										
										Ack_client->strand.post([this, this_]() { Ack_client->Close(); });
									}
								}
								else 
								{
									throw ::std::runtime_error("On_ack return error.");
								}
							};
							typename Synapse_channel_type::Subscription_options Options;
							Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(::boost::gregorian::date(::boost::gregorian::day_clock::universal_day()) + ::boost::gregorian::years(1));
							Options.Supply_metadata = true;
							Options.Preroll_if_begin_timestamp_in_future = true;
							Options.Skip_payload = true;
							Channel.Subscribe(
								[this_, &Channel](bool Error) mutable 
								{
									// Error reaction... throw for the time being.
									if (Error)
										throw ::std::runtime_error("ACK subcsription failed");

								}
								, Topic, Options);
						}
						else
							throw ::std::runtime_error("Open failed");
					}, Time_out, Tcp_no_delay);
				}

				void Publish_message(Synapse_channel_type & Channel, int Remaining_messages) 
				{
					Last_call = GetHighResolutionTime();
					if (Remaining_messages) 
					{
						if (Has_waypoint)
							Waypoint_ptr->Set_timestamp(static_cast<int64_t>(data_processors::Federated_serialisation::Utils::Encode_timestamp(::boost::posix_time::microsec_clock::universal_time())), true);
						Channel.Publish([this, &Channel, Remaining_messages, this_(base_type::shared_from_this())](bool Error) 
						{
							if (!Error)
							{
								if (Has_waypoint) // Latency test
								{
									Publisher_throttling_timer.expires_from_now(::std::chrono::milliseconds(53));
									Publisher_throttling_timer.async_wait(Client->strand.wrap([this, &Channel, Remaining_messages, this_](::boost::system::error_code const &Error) 
									{
										if (!Error)
											Publish_message(Channel, Remaining_messages - 1);
									}));
								}
								else
								{
									if (Duration < 0)
										Publish_message(Channel, Remaining_messages - 1);
									else 
									{
										do {
											auto end = GetHighResolutionTime();
											int_fast64_t elapsed = (end - Last_call);
											// even elapsed is less than zero, we just need to wait more loops, so we don't need to handle it.
											if (elapsed * 1000000 /Frequency >= Duration)break;
										} while (true);
										Publish_message(Channel, Remaining_messages - 1);
									}
								}
							}
							else
								throw ::std::runtime_error("Publish failed at " + ::std::to_string(Remaining_messages));
						}
						, Contests.get(), Has_waypoint ? Waypoints.get() : NULL, false, Strearm_Id, Total_messages - Remaining_messages + 1, 0, 0);
					}
					else 
					{
						data_processors::synapse::misc::log("Synapse client is closing.\n", true);
//						Start_ack_client();
						auto this_(this->shared_from_this());
						Client->Soft_Close([this_](bool) { data_processors::synapse::asio::io_service::get().stop(); });
					}
				}
				::boost::asio::high_resolution_timer Publisher_throttling_timer{ data_processors::synapse::asio::io_service::get() };
				void Publish()
				{
					Contests = ::boost::make_shared<Contests_type>();
					Contests->Set_datasource(Source + "_datasource");
					auto & Contest(Contests->Set_contest_element("Some_contest_id_key"));
					auto & Contest_name(Contest.Set_contestName());
					Contest_name.Set_value(::std::string(Message_size - 100, 'z'));
					Waypoints = ::boost::make_shared<Waypoints_type>();
					Waypoint_ptr = &Waypoints->Add_path_element(::DataProcessors::Waypoints::waypoint());
					Waypoint_ptr->Set_tag(Source + "_waypoint");

					Client->On_error = []() {};
					auto this_(base_type::shared_from_this());
					Client->Open(Host, Port, [this, this_](bool Error) {
						if (!Error) {
							Client->Open_channel(2, [this, this_](bool Error){
								if (!Error) {
									auto & Channel(Client->Get_channel(2));
									Channel.Set_as_publisher(Topic);
									Publish_message(Channel, Total_messages);
								}
								else
									throw ::std::runtime_error("Open_channel failed");
							});
						}
						else
							throw ::std::runtime_error("Open failed");
					}, Time_out, Tcp_no_delay);
				}

				void Subscribe() {
					auto this_(base_type::shared_from_this());
					data_processors::synapse::misc::log("Starting data subscription.\n", true);
					Client->On_error = []() {data_processors::synapse::misc::log("Error in application.\n", true); };
					Client->Open(Host, Port, [this, this_](bool Error) {
						if (!Error) {
							auto & Channel(Client->Get_channel(1));
							Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope) {
								if (Envelope != nullptr) {
									if (Envelope->Type_name == "Contests") {
//										if (!Envelope->User_data) {
//											Envelope->User_data = ::std::unique_ptr<char, void(*)(void *)>(reinterpret_cast<char*>(new My_user_data{ this_ }), [](void * X) { delete reinterpret_cast<My_user_data*>(X); });
//										}
										Channel.template Process_incoming_message<Waypoints_type, Contests_type>(Envelope,
											[this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * Envelope){
											if (Envelope != nullptr) {
												if (++Received_messages == Total_messages) {
													data_processors::synapse::misc::log("Subscriber closing.\n", true);
													for (auto a : Latency)
													{
														Log_performance(::std::to_string(a.first) + "," + ::std::to_string(a.second) + "\n");
													}
													Client->Close();
												}
												else {
													assert(Envelope->Message);

													if (Envelope->Waypoints) {
														auto Concrete_waypoints(static_cast<Waypoints_type *>(Envelope->Waypoints.get()));
														if (Concrete_waypoints->Is_path_set()) {
															auto && Path(Concrete_waypoints->Get_path());
															for (auto && Waypoint : Path)
																if (Waypoint.Is_tag_set() && Waypoint.Is_timestamp_set()) {
																	Latency.emplace(Envelope->Get_message_sequence_number(), (::boost::posix_time::microsec_clock::universal_time() - data_processors::Federated_serialisation::Utils::Decode_timestamp_to_ptime(Waypoint.Get_timestamp())).total_microseconds());
//																	::data_processors::synapse::misc::log(Waypoint.Get_tag() + ":" + to_simple_string(::data_processors::Federated_serialisation::Utils::Decode_timestamp_to_ptime(Waypoint.Get_timestamp())) + "\n", true);
																	break;
																}
														}
													}
													Channel.Release_message(*Envelope);
												}
//												::data_processors::synapse::misc::log("Received: " + ::std::to_string(Received_messages) + "\n", true);
											}
											else {
												throw ::std::runtime_error("Envelope is nullptr");
											}
										}
										, true);
									}
								}
								else
								{
									throw ::std::runtime_error("Envelope is null ptr");
								}
							};
							typename Synapse_channel_type::Subscription_options Options;
							Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2017, 4, 20);
							Options.Tcp_No_delay = Tcp_no_delay;
							Options.Supply_metadata = true;
							Channel.Subscribe([this_](bool Error) { if (Error) throw ::std::runtime_error("Error in subcsribe callback");	}, Topic, Options);
						}
						else
							throw ::std::runtime_error("Open failed.");
					}, Time_out, Tcp_no_delay);
				}
			};

			void static default_parse_arg(int argc, char* argv[], int& i)
			{
				if (i == argc - 1)
					throw ::std::runtime_error(::std::string(argv[i]) + " should have an explicit value.");
				else if (!strcmp(argv[i], "--log_root"))
				{
					Log_path = argv[i + 1];
					data_processors::synapse::misc::log.Set_output_root(argv[++i]);
				}
				else if (!data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--synapse_at", i, argv, Host, Port))
				if (!::strcmp(argv[i], "--message_size"))
					Message_size = ::std::stoi(argv[++i]);
				else if (!::strcmp(argv[i], "--total_messages"))
					Total_messages = ::std::stoi(argv[++i]);
				else if (!::strcmp(argv[i], "--source"))
					Source = argv[++i];
				else if (!::strcmp(argv[i], "--topic"))
				{
					Topic = argv[++i];
					::std::transform(Topic.begin(), Topic.end(), Topic.begin(), ::tolower);
				}
				else if (!::strcmp(argv[i], "--time_out"))
					Time_out = ::std::stoi(argv[++i]);
				else if (!::strcmp(argv[i], "--duration"))
					Duration = ::std::stoi(argv[++i]);
				else
					throw std::runtime_error("Unknow command option --" + ::std::string(argv[i]));
			}

			int static run(int argc, char* argv[])
			{
				try
				{
					for (int i(1); i != argc; ++i)
					{
						if (!::strcmp(argv[i], "--sub"))
							Is_publisher = false;
						else if (!strcmp(argv[i], "--has_waypoint"))
							Has_waypoint = true;
						else if (!strcmp(argv[i], "--tcp_no_delay"))
							Tcp_no_delay = true;
						else
							default_parse_arg(argc, argv, i);
					}
					if (Message_size <= 100 && Is_publisher)
						throw ::std::runtime_error("Message_size must greater than 101.");
					Log_performance("has_waypoint: " + ::std::to_string(Has_waypoint) + ", tcp_no_delay: " + ::std::to_string(Tcp_no_delay) + ", message_size: " + ::std::to_string(Message_size) +
						", total_messages: " + ::std::to_string(Total_messages) + ", source: " + Source + ", topic: " + Topic + "\n");
					if (!Log_path.empty())
						::CreateDirectory(Log_path.c_str(), NULL);

					data_processors::synapse::misc::log("Synapse client starting.\n", true);

					auto const concurrency_hint(data_processors::synapse::misc::concurrency_hint());

					// create based on chosen runtime parameters
					data_processors::synapse::asio::io_service::init(concurrency_hint);
					data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

					data_processors::synapse::misc::log("Connection to " + Host + ':' + Port + '\n', true);

					if (Is_publisher)
						::boost::make_shared<Test_client<data_processors::synapse::asio::tcp_socket>>()->Publish();
					else
						::boost::make_shared<Test_client<data_processors::synapse::asio::tcp_socket>>()->Subscribe();

					::std::vector<::std::thread> pool(concurrency_hint * 8 - 1);
					for (auto && i : pool) {
						i = ::std::thread([]()->void {
							data_processors::synapse::asio::run();
						});
					}

					data_processors::synapse::asio::run();

					for (auto & i : pool)
						i.join();

					if (!Is_publisher) {
						if (Received_messages != Total_messages)
							throw ::std::runtime_error("did not receieve all messages");
					}

					data_processors::synapse::misc::log("Bye bye.\n", true);

					return 0;
				}
				catch (::std::exception &e)
				{
					::data_processors::synapse::misc::log("Oops -- " + std::string(e.what()) + "\n", true);
					return -1;
				}
				catch (...)
				{
					::data_processors::synapse::misc::log("Oops -- Unknow exception\n", true);
					return -1;
				}
			}
		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif





int main(int argc, char* argv[])
{
	return ::data_processors::Test::run(argc, argv);;
}
