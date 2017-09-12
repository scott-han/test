#include <iostream>
#include <atomic>
#include <numeric>

#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>

#include "Basic_client_1.h"

#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>
#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif

	namespace data_processors {

		namespace Utility {
			/**
			* To quit the applicaiton.
			*/
			void quit_on_request(const ::std::string& message) {
				auto const prev(::data_processors::synapse::asio::quit_requested.test_and_set());
				if (!prev) {
					synapse::misc::log("Bench_utility quits automatically (" + message + ").\n", true);
					synapse::asio::quitter::get().quit([]() {
						synapse::asio::io_service::get().stop();
						synapse::misc::log("...exiting at a service-request.\n", true);
					});
				}
			}
			unsigned static Message_size{ 100 };
			unsigned static Total_messages{ 300000 };
			::std::atomic_uint_fast32_t static Received_messages{ 0 };
			::std::string static Log_path;

			::std::string static Host{ "localhost" };
			::std::string static Port{ "5672" };
			::std::string static Source{ "source" };
			::std::string static Topic;
			::std::string static Username{ "guest" };
			::std::string static Password{ "guest" };
			unsigned static Time_out{ 5 };
			bool static Tcp_no_delay{ false };
			bool static Test_publisher{ false };
			bool static Test_subscriber{ false };
			bool static Test_latency{ false };
			::std::atomic_bool static Test_is_completed{ false };
			uint_fast64_t static Subscribe_from{ 1 };
			int static Warmup_by_message{ 0 };
			int static Warmup_by_time{ 0 };
			bool static Test_started{ false };
			uint_fast64_t static Processor_Affinity{ 0 };
			unsigned static IO_Service_Concurrency(0);
			unsigned static IO_Service_Concurrency_Reserve_Multiplier(8);

			int_fast64_t Frequency{ 0 };
			std::vector<int_fast64_t> Latency;

			namespace basio = ::boost::asio;
			typedef DataProcessors::Waypoints::waypoints Waypoints_type;
			typedef DataProcessors::Contests4::Contests Contests_type;
			typedef DataProcessors::Contests4::Contest Contest_type;

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
			struct Bench_utility : ::boost::enable_shared_from_this<Bench_utility<Socket_type>>
			{
				typedef ::boost::enable_shared_from_this<Bench_utility<Socket_type>> base_type;

				typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
				typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;

				::boost::shared_ptr<Synapse_client_type> Client{ ::boost::make_shared<Synapse_client_type>() };
				::boost::shared_ptr<Synapse_client_type> Ack_client{ ::boost::make_shared<Synapse_client_type>() };
				::boost::shared_ptr<Contests_type> Contests;
				::boost::shared_ptr<Waypoints_type> Waypoints;
				::DataProcessors::Waypoints::waypoint * Waypoint_ptr;

				::std::string Strearm_Id;
				::boost::posix_time::ptime start_time = ::boost::posix_time::microsec_clock::universal_time();

				uint_fast64_t Sequence{ 0 };
				int_fast64_t Test_start_time{ 0 };
				int_fast64_t Test_end_time{ 0 };
				uint_fast64_t Accumulated_size{ 0 };

				bool Test_should_start(int_fast32_t messages)
				{
					if (!Test_started && (Warmup_by_message > 0 || Warmup_by_time > 0))
					{
						auto end_time = GetHighResolutionTime();
						int_fast64_t diff(0);
						if (end_time > Test_start_time)
							diff = end_time - Test_start_time;
						double seconds = diff * 1.0 / Frequency;
						if (messages >= Warmup_by_message && seconds >= Warmup_by_time)
						{
							Received_messages = 0;
							Test_start_time = GetHighResolutionTime();
							Accumulated_size = 0;
							return true;
						}
						else
						{
							return false;
						}
					}
					else
					{
						return true;
					}
				}

				void Publish_raw_message(Synapse_channel_type& Channel, int Remaining_messages, ::boost::shared_ptr<Bench_utility<data_processors::synapse::asio::tcp_socket>> this_)
				{
					if (Remaining_messages)
					{
						Channel.On_async_request_completion = ::std::forward<::std::function<void(bool)>>([this, this_, &Channel, Remaining_messages](bool Error) mutable {
							if (!Error)
							{
								if (!Test_started)
								{
									if (Test_should_start(++Received_messages))
									{
										Test_started = true;
									}
									Remaining_messages = Total_messages;
								}
								Publish_raw_message(Channel, Remaining_messages - 1, this_);
							}
							else
								Put_into_error_state(this_, ::std::runtime_error("Publish failed at " + ::std::to_string(Remaining_messages)));
						});
						
						auto Thrift_buffer(::boost::shared_ptr<::apache::thrift::transport::TMemoryBuffer>(new ::apache::thrift::transport::TMemoryBuffer()));
						Thrift_buffer->resetBuffer(Pub_buff.get(), Message_size + 5, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::OBSERVE);
						Channel.Publish_Helper_2(Thrift_buffer.get(), 0, 0, 0, false, false);
					}
					else
					{
						Test_end_time = GetHighResolutionTime();
						Test_is_completed = true;
						data_processors::synapse::misc::log("Test is completed, closing the client.\n", true);
						//						Start_ack_client();

						Client->Soft_Close([this, this_](bool) { 
							auto ms = (Test_end_time - Test_start_time) * 1000000 / Frequency;
							::data_processors::synapse::misc::log("It took " + ::std::to_string(ms) + " micro-seconds to publish " + ::std::to_string(Total_messages) + " messages.\n", true);
							double seconds = ms * 1.0 / 1000000;
							::data_processors::synapse::misc::log("Message publishing rate is: " + ::std::to_string((int)(Total_messages / seconds)) + " messages per second.\n", true);
							::data_processors::synapse::misc::log("Bandwidth usage is: " + ::std::to_string((int)(Total_messages * (Message_size + 5) / seconds)) + " bytes per second.\n", true);
							::data_processors::Utility::quit_on_request("Test is completed");
						});
					}
				}

				void Publish_latency_message(Synapse_channel_type& Channel, ::boost::shared_ptr<Bench_utility<data_processors::synapse::asio::tcp_socket>> this_)
				{
					Channel.On_async_request_completion = ::std::forward<::std::function<void(bool)>>([this, this_, &Channel] (bool Error) {
						if (Error)
							Put_into_error_state(this_, ::std::runtime_error("Publish failed at " + ::std::to_string(Latency.size())));
						else
						{
						}
					});

					auto Thrift_buffer(::boost::shared_ptr<::apache::thrift::transport::TMemoryBuffer>(new ::apache::thrift::transport::TMemoryBuffer()));
					Thrift_buffer->resetBuffer(Pub_buff.get(), Message_size + 5, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::OBSERVE);
					if (Test_started)
						Latency.emplace_back(GetHighResolutionTime());
					Channel.Publish_Helper_2(Thrift_buffer.get(), 0, 0, 0, false, false);
				}

				int_fast64_t To_micro_seconds(int_fast64_t diff)
				{
					return diff * 1000000 / Frequency;
				}

				void init_buffer()
				{
					uint8_t* temp = new uint8_t[Message_size + 5]{ 0 };
					temp[4] = 1; //version
					temp[Message_size + 4] = 0xCE; //end of message
					new (temp + 5) uint32_t(0); //Flags
					new (temp) uint32_t(htobe32(Message_size)); //Message size
					Pub_buff.reset(temp);
				}

				void Latency_test()
				{
					init_buffer();
					Latency.clear();
					Received_messages = 0;
					auto this_(base_type::shared_from_this());
					data_processors::synapse::misc::log("Starting data subscription.\n", true);
					Client->On_error = [this, this_]() {Put_into_error_state(this_, ::std::runtime_error("On_error is called")); };
					Client->Open(Host, Port, [this, this_](bool Error)
					{
						if (!Error)
						{
							auto & Channel(Client->Get_channel(1));
							Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope)
							{
								if (pEnvelope != nullptr)
								{
									Channel.Stepover_incoming_message(pEnvelope, [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope)
									{
										if (pEnvelope != nullptr)
										{
											if (Test_started)
											{
												auto begin(Latency.back());
												auto now(GetHighResolutionTime());
												if (now >= begin)
													Latency.back() = now - begin;
												else
													Latency.back() = 0;
											}
											Channel.Release_message(*pEnvelope);

											if (Test_started && ++Received_messages == Total_messages + 1)
											{
												::data_processors::synapse::misc::log("Test is completed, closing the client.\n", true);
												Test_is_completed = true;
												Client->Soft_Close([this_, this](bool) mutable {
													assert(Latency.size() == Total_messages + 1);
													Latency.erase(Latency.begin()); // The first message may be not published by this app.
													std::sort(Latency.begin(), Latency.end());
													assert(Latency.size() > 1);
													auto sum = std::accumulate(Latency.begin(), Latency.end(), 0LL);
													auto ave = sum / (Latency.size());
													::data_processors::synapse::misc::log("Average latency is " + std::to_string(ave * 1000000LL / Frequency) + " micro-seconds.\n", true);
													std::ostringstream ss;
													ss << std::setw(16) << "Percentile" << std::setw(8) << "50" << std::setw(8) << "90" << std::setw(8) << "95" << std::setw(8) << "99" << std::setw(8) << "99.5" << std::setw(8) << "99.9" << std::endl;
													::data_processors::synapse::misc::log(ss.str(), true);
													ss.str(::std::string());
													auto size(Latency.size());
													ss << std::setw(16) << "Latency value" << std::setw(8) << To_micro_seconds(Latency.at((size_t)(size * 0.5))) << std::setw(8) <<
														To_micro_seconds(Latency.at((size_t)(size * 0.9))) << std::setw(8) <<
														To_micro_seconds(Latency.at((size_t)(size * 0.95))) << std::setw(8) <<
														To_micro_seconds(Latency.at((size_t)(size * 0.99))) << std::setw(8) <<
														To_micro_seconds(Latency.at((size_t)(size * 0.995))) << std::setw(8) <<
														To_micro_seconds(Latency.at((size_t)(size * 0.999))) << std::endl;
													::data_processors::synapse::misc::log(ss.str(), true);
													::data_processors::Utility::quit_on_request("Test is completed");
												});
											}
											else
											{
												if (!Test_started)
												{
													if (Test_should_start(++Received_messages))
													{
														Received_messages = 0;
														Test_started = true;
														Latency.clear();
													}
												}
												auto & Channel(Client->Get_channel(2));
												Publish_latency_message(Channel, this_);
											}
										}
										else
										{
											Put_into_error_state(this_, ::std::runtime_error("pEnvelope is null in Stepover_incoming_message callback"));
										}
									});
								}
								else
								{
									Put_into_error_state(this_, ::std::runtime_error("Envelope is null ptr"));
								}
							};

							Client->Open_channel(2, [this, this_, &Channel](bool Error) {
								if (!Error) {
									auto & Pub_Channel(Client->Get_channel(2));
									Pub_Channel.Set_as_publisher(Topic);
									Publish_latency_message(Pub_Channel, this_);
									typename Synapse_channel_type::Subscription_options Options;
									Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(2030, 1, 1);
									Options.Tcp_No_delay = Tcp_no_delay;
									Options.Supply_metadata = true;
									Options.Preroll_if_begin_timestamp_in_future = true;
									Test_start_time = GetHighResolutionTime();
									Received_messages = 0;
									Accumulated_size = 0;
									Channel.Subscribe([this_, this](bool Error) { if (Error) Put_into_error_state(this_, ::std::runtime_error("Error in subcsribe callback"));	}, Topic, Options);
								}
								else
									Put_into_error_state(this_, ::std::runtime_error("Open_channel failed"));
							});


						}
						else
							Put_into_error_state(this_, ::std::runtime_error("Open failed."));

					}, Time_out, Tcp_no_delay);
				}

				void Put_into_error_state(const ::boost::shared_ptr<Bench_utility>& this_, ::std::exception const & e) {
					if (!Test_is_completed && !::data_processors::synapse::asio::Exit_error.exchange(true, ::std::memory_order_relaxed)) {
						::data_processors::synapse::misc::log("Error: " + ::std::string(e.what()) + "\n", true);
						// Call put_into_error_state to remove all call back function objects, which are holding this_.
						Client->strand.post([this, this_, e]() {
							Client->put_into_error_state(e); 				
							::data_processors::Utility::quit_on_request("Error in Topic_mirror, will quit ...");
						});
					}
				}
				std::shared_ptr<uint8_t> Pub_buff;
				void Publisher_test()
				{
					init_buffer();
					auto this_(base_type::shared_from_this());
					Client->On_error = [this, this_]() {Put_into_error_state(this_, ::std::runtime_error("On_error is called")); };

					Client->Open(Host, Port, [this, this_](bool Error) {
						if (!Error) {
							Client->Open_channel(2, [this, this_](bool Error) {
								if (!Error) {
									auto & Channel(Client->Get_channel(2));
									Channel.Set_as_publisher(Topic);
									Test_start_time = GetHighResolutionTime();
									Publish_raw_message(Channel, Total_messages, this_);
								}
								else
									Put_into_error_state(this_, ::std::runtime_error("Open_channel failed"));
							});
						}
						else
							Put_into_error_state(this_, ::std::runtime_error("Open failed"));
					}, Time_out, Tcp_no_delay);
				}

				void Test_both()
				{
					throw std::runtime_error("Not implemented yet.");
				}

				void Subscriber_test()
				{
					auto this_(base_type::shared_from_this());
					data_processors::synapse::misc::log("Starting data subscription.\n", true);
					Client->On_error = [this, this_]() {Put_into_error_state(this_, ::std::runtime_error("On_error is called")); };
					Client->Open(Host, Port, [this, this_](bool Error) 
					{
						if (!Error) 
						{
							auto & Channel(Client->Get_channel(1));
							Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) 
							{
								if (pEnvelope != nullptr) 
								{
									Channel.Stepover_incoming_message(pEnvelope, [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) 
									{
										if (pEnvelope != nullptr)
										{
											Accumulated_size += pEnvelope->Get_payload_size();
											Channel.Release_message(*pEnvelope);
											if (!Test_started)
											{
												if (Test_should_start(++Received_messages))
													Test_started = true;
												else
													return;
											}
											if (++Received_messages == Total_messages)
											{
												Test_end_time = GetHighResolutionTime();
												::data_processors::synapse::misc::log("Test is completed, closing client.\n", true);
												Test_is_completed = true;
												Client->Close();
												auto diff = (Test_end_time - Test_start_time) * 1000000 / Frequency;
												::data_processors::synapse::misc::log("It took " + ::std::to_string(diff) + " micro-seconds to receive " + ::std::to_string(Total_messages) + " messages.\n", true);
												double seconds = diff * 1.0 / 1000000;
												::data_processors::synapse::misc::log("Message receiving rate is " + ::std::to_string((int)(Total_messages / seconds)) + " messages per second.\n", true);
												::data_processors::synapse::misc::log("Bandwidth usage is " + ::std::to_string((int)(Accumulated_size / seconds)) + " bytes per second.\n", true);
												::data_processors::Utility::quit_on_request("Test is completed");
											}
										}
										else
										{
											Put_into_error_state(this_, ::std::runtime_error("pEnvelope is null in Stepover_incoming_message callback"));
										}
									});
								}
								else
								{
									Put_into_error_state(this_, ::std::runtime_error("Envelope is null ptr"));
								}
							};
							typename Synapse_channel_type::Subscription_options Options;
							Options.Begin_utc = Subscribe_from;
							Options.Tcp_No_delay = Tcp_no_delay;
							Options.Supply_metadata = true;
							Test_start_time = GetHighResolutionTime();
							Received_messages = 0;
							Accumulated_size = 0;
							Channel.Subscribe([this_](bool Error) { if (Error) throw ::std::runtime_error("Error in subcsribe callback");	}, Topic, Options);
						}
						else
							Put_into_error_state(this_, ::std::runtime_error("Open failed."));
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
				else if (!::strcmp(argv[i], "--topic"))
				{
					Topic = argv[++i];
					::std::transform(Topic.begin(), Topic.end(), Topic.begin(), ::tolower);
				}
				else if (!::strcmp(argv[i], "--subscribe_from"))
					Subscribe_from = data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::from_iso_string(argv[++i]));
				else if (!::strcmp(argv[i], "--username"))
					Username = argv[++i];
				else if (!::strcmp(argv[i], "--password"))
					Password = argv[++i];
				else if (!::strcmp(argv[i], "--warmup_by_message"))
					Warmup_by_message = ::std::stoi(argv[++i]);
				else if (!::strcmp(argv[i], "--warmup_by_time"))
					Warmup_by_time = ::std::stoi(argv[++i]);
				else if (!::strcmp(argv[i], "--processor_affinity"))
					Processor_Affinity = ::boost::lexical_cast<uint_fast64_t>(argv[++i]);
				else if (!::strcmp(argv[i], "--IO_Service_Concurrency"))
					IO_Service_Concurrency = ::boost::lexical_cast<unsigned>(argv[++i]);
				else if (!::strcmp(argv[i], "--IO_Service_Concurrency_Reserve_Multiplier")) {
					IO_Service_Concurrency_Reserve_Multiplier = ::boost::lexical_cast<unsigned>(argv[++i]);
					if (!IO_Service_Concurrency_Reserve_Multiplier)
						throw ::std::runtime_error("--IO_Service_Concurrency_Reserve_Multiplier cannot be less than 1");
				} else if (!::strcmp(argv[i], "--test_type"))
				{
					++i;
					if (!::strcmp(argv[i], "sub"))
						Test_subscriber = true;
					else if (!::strcmp(argv[i], "pub"))
						Test_publisher = true;
					else if (!::strcmp(argv[i], "latency"))
						Test_latency = true;
					else if (!::strcmp(argv[i], "both"))
					{
						Test_publisher = true;
						Test_subscriber = true;
					}
					else
						throw ::std::runtime_error("Unknow test_type " + ::std::string(argv[i]));
				}
				else
					throw std::runtime_error("Unknow command option --" + ::std::string(argv[i]));
			}

			int static run(int argc, char* argv[])
			{
				try
				{
					for (int i(1); i != argc; ++i)
					{

						if (!strcmp(argv[i], "--tcp_no_delay"))
							Tcp_no_delay = true;
						else
							default_parse_arg(argc, argv, i);
					}
					if (Test_latency)
						Tcp_no_delay = true;

					if (Warmup_by_message < 0)
						throw ::std::runtime_error("Invalid Warmup_by_message");
					if (Warmup_by_time < 0)
						throw ::std::runtime_error("Invalid Warmup_by_time");
					if (!Log_path.empty())
						::CreateDirectory(Log_path.c_str(), NULL);

					if (Message_size < 5)
						throw ::std::runtime_error("Message size cannot be less than 5 (Version + Flags)");

					data_processors::synapse::misc::log("Bench utility starting.\n", true);
#ifdef __WIN32__
					if (Processor_Affinity)
						data_processors::synapse::misc::Set_Thread_Processor_Affinity(Processor_Affinity);
#endif
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

					if (!IO_Service_Concurrency)
						IO_Service_Concurrency = data_processors::synapse::misc::concurrency_hint();

					// create based on chosen runtime parameters
					data_processors::synapse::asio::io_service::init(IO_Service_Concurrency);
					data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates
					data_processors::synapse::asio::Autoquit_on_signals Signal_quitter;
#ifndef GDB_STDIN_BUG
					data_processors::synapse::asio::Autoquit_on_keypress Keypress_quitter;
#endif
					data_processors::synapse::misc::log("Connection to " + Host + ':' + Port + '\n', true);

					if (Test_latency)
						::boost::make_shared<Bench_utility<data_processors::synapse::asio::tcp_socket>>()->Latency_test();
					else if (Test_publisher && Test_subscriber)
						::boost::make_shared<Bench_utility<data_processors::synapse::asio::tcp_socket>>()->Test_both();
					else if (Test_publisher)
						::boost::make_shared<Bench_utility<data_processors::synapse::asio::tcp_socket>>()->Publisher_test();
					else if (Test_subscriber)
						::boost::make_shared<Bench_utility<data_processors::synapse::asio::tcp_socket>>()->Subscriber_test();
					else
						throw ::std::runtime_error("Unsupported test option");

					::std::vector<::std::thread> pool(IO_Service_Concurrency * IO_Service_Concurrency_Reserve_Multiplier);
					for (auto && i : pool) {
						i = ::std::thread([]()->void {
#ifdef __WIN32__
							if (Processor_Affinity)
								data_processors::synapse::misc::Set_Thread_Processor_Affinity(Processor_Affinity);
#endif
							data_processors::synapse::asio::run();
						});
					}

					data_processors::synapse::asio::run();

					for (auto & i : pool)
						i.join();

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
	return ::data_processors::Utility::run(argc, argv);;
}
