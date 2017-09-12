#if defined(__GNUC__)  || defined(__clang__) 
#include "Topic_mirror_pch.h"
#endif
#include "../../../Version.h"
#include <data_processors/Federated_serialisation/Utils.h>
#include <data_processors/Federated_serialisation/Schemas/waypoints_types.h>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
	namespace data_processors {
		namespace Topic_mirror {
			/**
			* To quit the applicaiton.
			*/
			void quit_on_request(const ::std::string& message) {
				auto const prev(::data_processors::synapse::asio::quit_requested.test_and_set());
				if (!prev) {
					synapse::misc::log("Topic_mirror quits automatically (" + message + ").\n", true);
					synapse::asio::quitter::get().quit([]() {
						synapse::asio::io_service::get().stop();
						synapse::misc::log("...exiting at a service-request.\n", true);
					});
				}
			}

#ifdef __WIN32__
			bool static run_as_service{ false };
			::SERVICE_STATUS static service_status;
			::SERVICE_STATUS_HANDLE static service_handle;
			char service_name[] = "Topic_mirror";
			bool require_stop = false;
			void
				service_control_handler(DWORD control_request)
			{
				switch (control_request) {
				case SERVICE_CONTROL_STOP:
				case SERVICE_CONTROL_SHUTDOWN:
					require_stop = true;
					service_status.dwCurrentState = SERVICE_STOP_PENDING;
					service_status.dwControlsAccepted = 0;
					service_status.dwWaitHint = 5 * 60 * 1000;
					::SetServiceStatus(service_handle, &service_status);
					quit_on_request("Service is asked to shutdown/stop");
				}
			}
#endif

			// defaults...
			::std::string static host("localhost");
			::std::string static port("5672");
			::std::string static ssl_key;
			::std::string static ssl_certificate;
			unsigned static so_rcvbuf(-1);
			unsigned static so_sndbuf(-1);
			unsigned static concurrency_hint(0);
			uint_fast64_t static Subscribe_from(1);
			::std::set<::std::string> SubscriptionS;
			::std::vector<::std::pair<::std::string, ::std::string>> Mirror_to;
			// The subscribe_to option.
			uint_fast64_t static Subscribe_to(-1);
			// Total topics to be subscribed (get from server).
			unsigned static Total_topics_to_be_subscribed{ 0 };
			unsigned static Already_subscribed_topic{ 0 };
			// Total subscriptions provided from command line
			unsigned static Total_subscription{ 0 };
			unsigned static Total_target_servers{ 0 };
			bool static Mirror_start{ false };
			size_t static Closed_client{ 0 };
			unsigned static Time_out{ 20 };
			::std::atomic_bool static Incomming_error{ true };
			::std::string static Instance_name{ "Topic_mirror" };

			bool static not_using_original_seq_timestamp{ false };
			// Synapse at
			::std::string static Synapse_at;
			::std::set<::std::string> All_topics;
			// All new topics will be stored in here, and they will be moved to All_topics once an asynchrounous work is started.
			::std::set<::std::string> Late_topics;
			// This variable is to indicate asynchronous work is in progress or not (the asynchronous work includes get ack from the target server, and create the publish
			// channel etc.). In this app, we need to handle new topic at any given time, if this variable is true, new arrived topic will
			// just sit in the queue, and wait until the current asynchronous work is completed; otherwise the asynchronous work can be started immediately.
			bool static Async_work_in_progress{ false };
			// Total ack received from target server (only in this async task), need to be reset when an async task is started.
			unsigned static Total_ack_received{ 0 };

			::std::string static Log_path;
			namespace basio = ::boost::asio;

			template <typename Socket_type>
			struct Topic_mirror : ::boost::enable_shared_from_this<Topic_mirror<Socket_type>> {

				typedef ::boost::enable_shared_from_this<Topic_mirror<Socket_type>> base_type;

				typedef data_processors::synapse::amqp_0_9_1::Basic_synapse_client<Socket_type> Synapse_client_type;
				typedef data_processors::synapse::amqp_0_9_1::Channel<Synapse_client_type> Synapse_channel_type;
				typedef DataProcessors::Waypoints::waypoints Waypoints_type;

				::boost::shared_ptr<Synapse_client_type> Synapse_client{ ::boost::make_shared<Synapse_client_type>() };
				::boost::shared_ptr<Synapse_client_type> Ack_synapse_client{ ::boost::make_shared<Synapse_client_type>() };

				struct Topic_info {
					Topic_info() : Sequence(0), Timestamp(1)
					{

					}
					Topic_info(::std::string const& name) : Topic_name(name), Sequence(0), Timestamp(1)
					{

					}
					Topic_info(::std::string const& name, uint_fast64_t seq, uint_fast64_t ts) : Topic_name(name), Sequence(seq), Timestamp(ts)
					{
					}
					::std::string Topic_name;
					int		Channel_number;
					uint_fast64_t	Sequence;
					uint_fast64_t   Timestamp;
				};

				struct Topic_subscription 
				{
					Topic_subscription(const ::std::string& topic, uint_fast64_t from, uint_fast64_t to = 0)
						: Topic_name(topic)
						, Subscribe_from(from)
						, Subscribe_to(to)
					{
					}
					::std::string Topic_name;	// Topic name
					uint_fast64_t Subscribe_from; // For this topic, it should be subscribed from this point -- we found this value by scanning stored files on local disk.
					uint_fast64_t Subscribe_to;	// Subscribed to
				};

				// This container is managed by Synapse_client.strand, therefore no lock is required.
				::std::queue<Topic_subscription> Topic_subscriptions;
				// These two variables are managed by Synapse_client.strand, therefore no lock is requried.
				unsigned Subscription_fence{ 0 };
				bool Can_subscribe{ true };

				struct Topic_processor 
				{
					::std::string Topic_name;
					uint_fast64_t Subscribe_begin;
					uint_fast64_t Subscribe_end;
					std::set<::std::string> Ack_updater;
					size_t           counter;
					::boost::shared_ptr<::apache::thrift::transport::TMemoryBuffer> Payload;

					Topic_processor(const ::std::string& Topic_name) : Topic_name(Topic_name), Subscribe_begin(1), Subscribe_end(::data_processors::Topic_mirror::Subscribe_to), counter(0)
					{
					}

					Topic_processor(Topic_processor const &) = delete;
				};

				struct Topic_processor_shm 
				{
					Topic_processor* pTopic_processor;
				};
				::std::map<::std::string, Topic_processor> Topic_processor_map;

				struct Target_server
				{
					Target_server(::std::string const & name, ::std::string const & port) : Server_name(name), Port(port), Is_opened(false)
					{

					}
					::std::string Server_name;
					::std::string Port;
					int Channel{ 2 };
					bool Shutdown{ false };
					bool Is_error{ false };
					bool Is_opened{ false };  //If the connection to the server is already established, used to determine if we need to open a connection.
					int Pending_message{ 0 }; //How wmany messages received but not sending out yet, it is used to decide if we can shutdown this app.
					bool Unsubscribe_in_progress{ false }; //A topic unsubscription is in progress
					::std::set<::std::string> To_be_unsubscribed; //All topics to be unsubscribed are stored here.
					::boost::shared_ptr<Synapse_client_type> Ack_client{ ::boost::make_shared<Synapse_client_type>() };
					::std::map<::std::string, Topic_info> Topics;

					void process(::std::set<::std::string> const & m) 
					{
						for (auto&& it(m.begin()); it != m.end(); ++it) 
						{
							Topics.emplace(*it, *it);
						}
					}
					void SetChannel(::std::string const& name, int channel)
					{
						if (Topics.find(name) == Topics.end())
							throw ::std::runtime_error("Could not find topic:" + name);
						Topics[name].Channel_number = channel;
					}
				};

				std::vector<Target_server> Mirrors;
				/**
				* dtor
				*
				* It is here for test only as I want to track if the applicaiton is quit gracefully.
				* We will remove it later.
				*
				*/
				~Topic_mirror()
				{
					::data_processors::synapse::misc::log("dtor is called.\n", true);
				}

				unsigned Decode_waypoints(uint8_t const* Payload_buffer, unsigned Payload_size, Waypoints_type* Concrete_waypoints, data_processors::synapse::amqp_0_9_1::Message_envelope& Envelope, bool hasEnvelope = true)
				{
					auto temp_buffer = ::boost::shared_ptr<::apache::thrift::transport::TMemoryBuffer>(new ::apache::thrift::transport::TMemoryBuffer());
					temp_buffer->resetBuffer(const_cast<uint8_t*>(Payload_buffer) + 4, Payload_size - 4, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::OBSERVE);
					auto Thrift_protocol = ::boost::make_shared<::apache::thrift::protocol::TCompactProtocol>(temp_buffer);
					int8_t Version;
					Thrift_protocol->readByte(Version);
					int32_t Flags;
					Thrift_protocol->readI32(Flags);

					::std::string Type_name;
					Thrift_protocol->readString(Type_name);

					::std::string Stream_id;
					if (Envelope.Has_stream_id(Flags))
						Thrift_protocol->readString(Stream_id);
					if (hasEnvelope)
					{
						Concrete_waypoints->Read_in_delta_mode = false;
						Concrete_waypoints->read(Thrift_protocol.get());
					}
					return temp_buffer->available_read();
				}

				void Run() {
					if (SubscriptionS.empty())
						throw ::std::runtime_error("Empty subscriptions list");

					Total_subscription = SubscriptionS.size();
					auto this_(base_type::shared_from_this());
					Synapse_client->Open(host, port, [this, this_](bool Error) {
						if (!Error) {
							// To set On_pending_writes_watermark call back function so we will have the notification about keep subscribing or not.
							Synapse_client->On_pending_writes_watermark = [this, this_](::boost::tribool const &val) {
								if (val) {
									Can_subscribe = false;
								}
								else if (::boost::tribool::indeterminate_value == val.value) {
									// Error, flush files
									Can_subscribe = false;
									Put_into_error_state(this_, ::std::runtime_error("On_pending_writes_watermark from Synapse_client."));
								}
								else {
									Can_subscribe = true;
									Synapse_client->strand.post([this, this_]() {
										Async_subscribe(this_);
									});
								}
							};

							// To set On_error callback function.
							Synapse_client->On_error = [this, this_]() {
								Put_into_error_state(this_, ::std::runtime_error("On_error from Synapse_client."));
							};

							Synapse_client->Open_channel(2, [this, this_](bool Error) {
								if (Error) {
									Put_into_error_state(this_, ::std::runtime_error("Open_channel 2 failed (Synapse_client)."));
								}
								else {

									auto & Channel(Synapse_client->Get_channel(1));
									Channel.On_incoming_message = [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) {
										try {
											if (pEnvelope != nullptr) {
												Channel.Stepover_incoming_message(pEnvelope, [this, this_, &Channel](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) {
													try {
														if (pEnvelope == nullptr) {
															Put_into_error_state(this_, ::std::runtime_error("Envolope is null in Stepover_incomming_message."));
															return;
														}
														if (!pEnvelope->User_data) {
															// We have to check if the item alredy existed or not, otherwise a temporary Topic_processor will be
															// constructed and then destructed, but during constructed, it will failed as we have a mutex there.
															auto && it(Topic_processor_map.find(pEnvelope->Topic_name));
															if (it == Topic_processor_map.end()) {
																auto ret = Topic_processor_map.emplace(pEnvelope->Topic_name, pEnvelope->Topic_name);
																it = ret.first;
															}
															pEnvelope->User_data = ::std::unique_ptr<char, void(*)(void *)>(reinterpret_cast<char*>(new Topic_processor_shm{ &it->second }), [](void * X) { delete reinterpret_cast<Topic_processor_shm*>(X); });
														}

														assert(!pEnvelope->Pending_buffers.empty());
														auto && Topic_processor(*reinterpret_cast<Topic_mirror::Topic_processor_shm*>(pEnvelope->User_data.get())->pTopic_processor);
														auto && Envelope(*pEnvelope);
														assert(Envelope.Pending_buffers.front().Message_timestamp);
														assert(Envelope.Pending_buffers.front().Message_sequence_number);
														auto const & Pending_buffer(Envelope.Pending_buffers.front());
														try {
															uint_fast64_t Timestamp(Pending_buffer.Message_timestamp);
															uint_fast64_t Sequence(Pending_buffer.Message_sequence_number);

															if (Timestamp <= Subscribe_to) {
																auto Payload_buffer(Pending_buffer.Get_amqp_frame() + 3);
																assert(!(reinterpret_cast<uintptr_t>(Pending_buffer.Get_amqp_frame() + 1) % 16));
																uint32_t const Payload_size(synapse::misc::be32toh_from_possibly_misaligned_source(Payload_buffer) + 5);

																assert(Payload_buffer[Payload_size - 1] == 0xCE);
																Topic_processor.counter = 0;
																// send to other servers
																auto Thrift_buffer = ::boost::shared_ptr<::apache::thrift::transport::TMemoryBuffer>(new ::apache::thrift::transport::TMemoryBuffer());
																if (!not_using_original_seq_timestamp)
																	Thrift_buffer->resetBuffer(const_cast<uint8_t*>(Payload_buffer), Payload_size, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::OBSERVE);
																else
																{
																	//not using original seq and timestamp, we will have to inject some info into waypoints, so we can know
																	//the original message sequence number and timestamp.
																	::boost::shared_ptr<Waypoints_type> Concrete_waypoints(::boost::make_shared<Waypoints_type>());
																	unsigned remain{ 0 }; //Remained data size (we only decode up to the completion of the waypoints, we don't touch the message part)
																	if (Envelope.Has_waypoints())
																	{
																		remain = Decode_waypoints(Payload_buffer, Payload_size, Concrete_waypoints.get(), Envelope);
																		auto && all_path(Concrete_waypoints->Get_path());
																		for (auto&& it(all_path.begin()); it != all_path.end(); ++it)
																		{
																			if (it->Is_tag_set() && it->Get_tag() == "topic_mirror" && 
																				it->Is_messageId_set() && it->Get_messageId() == Instance_name)
																				throw ::std::runtime_error("Find identical topic mirror tag and Id in the source message."); //loop or something else?
																		}
																	}
																	else
																	{
																		remain = Decode_waypoints(Payload_buffer, Payload_size, Concrete_waypoints.get(), Envelope, false);
																	}
																	auto && waypoint(Concrete_waypoints->Add_path_element(::DataProcessors::Waypoints::waypoint()));
																	waypoint.Set_tag("topic_mirror");
																	waypoint.Set_messageId(::std::string(Instance_name));
																	waypoint.Set_contentId(::std::to_string(Sequence) + "-" + ::std::to_string(Timestamp));
																	auto Thrift_protocol_to_publish_with = ::boost::make_shared<::apache::thrift::protocol::TCompactProtocol>(Thrift_buffer);
																	Topic_processor.Payload = Thrift_buffer; //Hold this buffer, to prevent it from being destroied when mirroring is not completed yet.

																	auto Thrift_buffer_raw_ptr(Thrift_buffer.get());
																	Thrift_buffer_raw_ptr->Advance(4); //Length of the buffer, will be updated later.
																	Thrift_protocol_to_publish_with->writeByte(1); //version, hardcoded as 1.
																	uint_fast32_t Flags(Pending_buffer.Flags);
																	Flags |= 0x1; //have waypoints now
																	Thrift_protocol_to_publish_with->writeI32(Flags);
																	if (Envelope.Type_name.empty())
																		throw ::std::runtime_error("Type name is empty in Envelope");
																	Thrift_protocol_to_publish_with->writeString(Envelope.Type_name);
																	if (!Envelope.Stream_id.empty())
																		Thrift_protocol_to_publish_with->writeString(Envelope.Stream_id);

																	Concrete_waypoints->write(Thrift_protocol_to_publish_with.get(), true);
																	assert(Payload_size > remain);
																	Thrift_buffer_raw_ptr->write(Payload_buffer + Payload_size - remain, remain);
																	char unsigned * Buffer_to_send_begin; uint32_t Buffer_to_send_size;
																	Thrift_buffer_raw_ptr->getBuffer(&Buffer_to_send_begin, &Buffer_to_send_size);
																	assert(Buffer_to_send_size);
																	assert(!(reinterpret_cast<uintptr_t>(Buffer_to_send_begin) % 4));
																	new (Buffer_to_send_begin) uint32_t(htobe32(Buffer_to_send_size - 5));
																}
																for (auto&& it(Mirrors.begin()); it != Mirrors.end(); ++it)
																{
																	auto&& target(*it);
																	++target.Pending_message;
																	target.Ack_client->strand.post([this_, this, Thrift_buffer, &target, &Topic_processor, Sequence, Timestamp, &Channel, &Envelope]() {
																		auto&& topic = target.Topics.find(Topic_processor.Topic_name);
																		if (topic == target.Topics.end())
																		{
																			Put_into_error_state(this_, ::std::runtime_error("Unknow topic while publishing " + Topic_processor.Topic_name + " for server: " + target.Server_name + ":" + target.Port));
																		}
																		else
																		{
																			if (Sequence > topic->second.Sequence && Timestamp >= topic->second.Timestamp)
																			{
																				topic->second.Sequence = Sequence;
																				topic->second.Timestamp = Timestamp;
																				auto & Pub_Channel(target.Ack_client->Get_channel(topic->second.Channel_number));
																				Pub_Channel.On_async_request_completion = ::std::forward<::std::function<void(bool)>>([this, this_, &Topic_processor, &Channel, &Envelope, &target](bool Error) {
																					Synapse_client->strand.post([this, this_, &target]() {
																						--target.Pending_message;
																						if (::data_processors::synapse::asio::Exit_error)Put_into_error_state(this_, ::std::runtime_error("App quits while completiion publishing a message(" + target.Server_name + ":" + target.Port + ")"));
																					});
																					if (!Error) {
																						Release_a_message(this_, Channel, Envelope, Topic_processor);
																					}
																					else
																					{
																						Put_into_error_state(this_, ::std::runtime_error("Publishing message failed to server(" + target.Server_name + ":" + target.Port + ")"));
																					}
																				});
																				if (not_using_original_seq_timestamp)
																					Pub_Channel.Publish_Helper_2(Thrift_buffer.get(), 0, 0, 0, false, true);
																				else
																					Pub_Channel.Publish_Helper_2(Thrift_buffer.get(), Sequence, Timestamp, 0, false, true);
																			}
																			else
																			{
																				Synapse_client->strand.post([this, this_, &target]() { --target.Pending_message; if (::data_processors::synapse::asio::Exit_error)Put_into_error_state(this_, ::std::runtime_error("App quits while completiion publishing a message(" + target.Server_name + ":" + target.Port + ")")); });
																				Release_a_message(this_, Channel, Envelope, Topic_processor);
																			}
																		}
																	});
																}
															}
															else
															{
																Release_a_message(this_, Channel, Envelope, Topic_processor);
															}
														}
														catch (::std::exception const & e) {
															Put_into_error_state(this_, e);
														}
														catch (...) {
															Put_into_error_state(this_, ::std::runtime_error("Unknow exception."));
														}

													}
													catch (::std::exception const& e) {
														Put_into_error_state(this_, e);
													}
													catch (...) {
														Put_into_error_state(this_, ::std::runtime_error("Unknow exception in Stepover_incoming_message callback."));
													}
												});
											}
											else {
												// Error reaction... throw for the time being.
												Incomming_error = true;
												throw ::std::runtime_error("On incoming message enevlope is null.");
											}
										}
										catch (::std::exception const & e) {
											Put_into_error_state(this_, e);
										}
										catch (...) {
											Put_into_error_state(this_, ::std::runtime_error("Unknow exception in On_incoming_message."));
										}
									};
								}
							});
							// Only start processing other tasks when Synapse_client open is succeeded.
							try {
								Start_ack_subscription(this_);
							}
							catch (::std::exception& e) {
								Put_into_error_state(this_, e);
							}
						}
						else {
							// Error reaction... throw for the time being.
							Put_into_error_state(this_, ::std::runtime_error("Could not open synapse_client connection."));
						}
					}, Time_out / 2);
				}
				void Release_a_message(const ::boost::shared_ptr<Topic_mirror>& this_, Synapse_channel_type & Channel, data_processors::synapse::amqp_0_9_1::Message_envelope& Envelope, Topic_processor& Topic_processor)
				{
					Synapse_client->strand.post([this, this_, &Channel, &Envelope, &Topic_processor]() {
						if (++Topic_processor.counter == Mirrors.size())
						{
							Topic_processor.Payload.reset(); // Free this buffer now as sending (event to multiple servers) is completed now.
							Synapse_client->strand.dispatch([this, this_, &Channel, &Envelope]() {
								try {
									//Channel.decrement_async_fence_size();
									Channel.Release_message(Envelope);
								}
								catch (::std::exception const & e) {
									Put_into_error_state(this_, e);
								}
								catch (...) {
									Put_into_error_state(this_, ::std::runtime_error("Unknow exception while Release_message."));
								}
							});
						}
					});
				}
				/**
				* To put this into an error state.
				*
				* @param[this_]: Shared_ptr of Topic_mirror to ensure Topic_mirror instance is valid while this function is called.
				* @param[e]: The actual exception.
				*
				* @return:
				*/
				void Put_into_error_state(const ::boost::shared_ptr<Topic_mirror>& this_, ::std::exception const & e) {
					if (!::data_processors::synapse::asio::Exit_error.exchange(true, ::std::memory_order_relaxed)) {
						::data_processors::synapse::misc::log("Error: " + ::std::string(e.what()) + "\n", true);
						// Call put_into_error_state to remove all call back function objects, which are holding this_.
						Synapse_client->strand.post([this, this_, e]() { Synapse_client->put_into_error_state(e); });
						// Call put_into_error_state to remove all call back function objects, which are holding this_; call io_service::get() is to ensure our application
						// will quit when something is wrong.
						Ack_synapse_client->strand.post([this, this_, e]() { Ack_synapse_client->put_into_error_state(e); });
					}
					// Wait until no more message is received, then we close the publisher side
					if (Incomming_error)
					{
						Synapse_client->strand.post([this, this_, e]() {
							if (Mirrors.size() > 0) {
								for (auto&& it(Mirrors.begin()); it != Mirrors.end(); ++it) {
									auto&& target(*it);
									if (!target.Shutdown && (target.Is_error || target.Pending_message == 0)) {
										target.Shutdown = true;
										target.Ack_client->strand.post([this, this_, e, &target]() {
											target.Ack_client->put_into_error_state(e);
											Ack_synapse_client->strand.post([this, this_]() {
												if (++Closed_client == Mirror_to.size())
													::data_processors::Topic_mirror::quit_on_request("Error in Topic_mirror, will quit ...");
											});
										});
									}
								}
							}
							else {
								// Publishers are not created yet.
								::data_processors::Topic_mirror::quit_on_request("Error in Topic_mirror, will quit ...");
							}
						});
					}
				}
				/**
				* This function is to subscribe with ACK mode only.
				*
				* @param[this_]: Shared_ptr of Topic_mirror, to ensure Topic_mirror is valid when this method is executed.
				* @param[Channel]: An instance of AMQP channel.
				* @param[I]: Iterator to find the topic to be subscribed.
				*
				* @return:
				*/

				void Async_subscribe(::boost::shared_ptr<Topic_mirror> const & this_, Synapse_channel_type & Channel, ::std::set<::std::string>::const_iterator I) {
					if (I != SubscriptionS.end()) {
						typename Synapse_channel_type::Subscription_options Options;
						Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(::boost::gregorian::date(::boost::gregorian::day_clock::universal_day()) + ::boost::gregorian::years(1));
						Options.Supply_metadata = true;
						Options.Preroll_if_begin_timestamp_in_future = true;
						Options.Skipping_period = 157680000000000u; // => Ack semantics
						Options.Skip_payload = true; // => Ack semantics
						Channel.Subscribe(
							[this, this_, &Channel, I](bool Error) mutable {
							// Error reaction... throw for the time being.
							if (Error)
								Put_into_error_state(this_, ::std::runtime_error("ACK subcsription failed"));
							else
								Async_subscribe(this_, Channel, ++I);
						}
							, *I, Options
							);
					}
					else {
						data_processors::synapse::amqp_0_9_1::bam::Table Fields;
						Fields.set("Current_subscription_topics_size", data_processors::synapse::amqp_0_9_1::bam::Long(1));
						Channel.Subscribe_extended_ack(
							[this, this_](bool Error) {
							if (Error)
								Put_into_error_state(this_, ::std::runtime_error("Error while subscribing for extended Report."));
						}, Fields);
					}
				}
				/**
				* This function is to subscribe to full mode, so we can retrieve data.
				*
				* @param[this_]: Shared_ptr of Topic_mirror, to ensure Topic_mirror is valid when this method is executed.
				*
				* @return:
				*/
				void Async_subscribe(::boost::shared_ptr<Topic_mirror> const & this_) {
					if (Can_subscribe && Subscription_fence == 0 && Topic_subscriptions.size() > 0) {
						++Subscription_fence;
						Topic_subscription subscription(Topic_subscriptions.front());
						Topic_subscriptions.pop();

						auto & Channel(Synapse_client->Get_channel(1));
						// TBD, we may need to update this to use Cork subscription to let server sort data for all subscribed topics.
						typename Synapse_channel_type::Subscription_options Options;
						Options.Begin_utc = subscription.Subscribe_from;
						Options.End_utc = subscription.Subscribe_to;
						Options.Supply_metadata = true;
						Options.Preroll_if_begin_timestamp_in_future = true;
						Options.Atomic_subscription_cork = ++Already_subscribed_topic < Total_topics_to_be_subscribed ? true : false;

						::data_processors::synapse::misc::log("Subscribe to topic:" + subscription.Topic_name + ", begin_utc: " + ::std::to_string(Options.Begin_utc) + "\n", true);
						Channel.Subscribe([this, this_](bool Error) mutable {
							assert(Subscription_fence == 1);
							--Subscription_fence;
							if (Error)
								Put_into_error_state(this_, ::std::runtime_error("Full mode(non-ACK) subscription failed.")); //Need retry later.
							else {
								Incomming_error = false;
								if (Topic_subscriptions.size() > 0) {
									Synapse_client->strand.post([this, this_]() {
										Async_subscribe(this_);
									});
								}
							}
						}, subscription.Topic_name, Options);
					}
				}

				void Start_ack_subscription(const ::boost::shared_ptr<Topic_mirror>& this_) 
				{
					Ack_synapse_client->Open(host, port, [this, this_](bool Error) {
						if (!Error) {
							// To set On_error callback function.
							Ack_synapse_client->On_error = [this, this_]() {
								Put_into_error_state(this_, ::std::runtime_error("On_error from Ack_synapse_client"));
							};

							try {
								auto & Channel(Ack_synapse_client->Get_channel(1));
								Channel.On_ack = [this, this_](::std::string const & Topic_name, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, data_processors::synapse::amqp_0_9_1::bam::Table * Amqp_headers, bool error) {
									if (!error) {
										try {
											if (Timestamp > 0 && Sequence_number > 0 && !Topic_name.empty() && Topic_name.compare("/")) {
												Synapse_client->strand.post([this, this_, Topic_name, Timestamp, Sequence_number]() { // postpone for later execution
													auto && it(Topic_processor_map.find(Topic_name));
													if (it == Topic_processor_map.end())
													{
														auto ret = Topic_processor_map.emplace(Topic_name, Topic_name);
														it = ret.first;
													}
													
													::data_processors::synapse::misc::log("Get new topic: " + Topic_name + ", will be processed soon.\n", true);
													Late_topics.emplace(Topic_name);

													if (!Async_work_in_progress && Late_topics.size() > 0) //Asynchronous work is not in progress and there are some new topics
													{
														::data_processors::synapse::misc::log("No async task is running, " + ::std::to_string(Late_topics.size()) + " new topics will be processed now.\n", true);
														All_topics.clear();
														All_topics.swap(Late_topics);
														Start_collecting_target_info();
													}
													else
														::data_processors::synapse::misc::log("Asynchronous work is in progress, " + ::std::to_string(Late_topics.size()) + " new topics will be processed when current task is completed.\n", true);

												}); // end of Synapse_client->strand.post
											}
											else {
												if (Amqp_headers != nullptr && !Topic_name.compare("/")) {
													Total_topics_to_be_subscribed = (uint64_t)(Amqp_headers->get("Current_subscription_topics_size"));
													::data_processors::synapse::misc::log("Got Current_subscription_topics_size: " + ::std::to_string(Total_topics_to_be_subscribed) + ".\n", true);
												}
												else {
													::data_processors::synapse::misc::log("Topic_name: " + Topic_name + ", Timestamp: " + ::std::to_string(Timestamp) + ", Sequence number: " + ::std::to_string(Sequence_number) + ".\n", true);
													throw ::std::runtime_error("In On_ack, neither Amqp_headers nor Topic_name is valid.");
												}
											}
										}
										catch (::std::exception& e) {
											Put_into_error_state(this_, e);
										}
										catch (...) {
											Put_into_error_state(this_, ::std::runtime_error("Unknow exception in On_ack."));
										}
									}
									else {
										Put_into_error_state(this_, ::std::runtime_error("On_ack report error."));
									}
								}; // end of On_ack call back function.
								   // Subscribe all topics to ACK mode.
								Async_subscribe(this_, Channel, SubscriptionS.begin());
							}
							catch (::std::exception& e) {
								Put_into_error_state(this_, e);
							}
							catch (...) {
								Put_into_error_state(this_, ::std::runtime_error("Unknow exception in Ack_synapse_client->Open callback."));
							}
						}
						else {
							Put_into_error_state(this_, ::std::runtime_error("Could not open Ack_synapse_client connection."));
						}
					}, Time_out / 2);
				}

				void Update_seq_and_timestamp(Target_server& Target, const ::boost::shared_ptr<Topic_mirror>& this_, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, ::std::string const& Topic_name)
				{
					auto && it(Target.Topics.find(Topic_name));
					if (it == Target.Topics.end())
					{
						Put_into_error_state(this_, ::std::runtime_error("On_ack received non-subscribed topic:" + Topic_name + " from server: " + Target.Server_name));
					}
					else
					{
						it->second.Sequence = Sequence_number;
						it->second.Timestamp = Timestamp;
					}
					::std::string server(Target.Server_name + ":" + Target.Port);
					Synapse_client->strand.post([this, this_, Topic_name, Timestamp, Sequence_number, server]()
					{
						auto &&it(Topic_processor_map.find(Topic_name));
						if (it != Topic_processor_map.end())
						{
							it->second.Ack_updater.insert(server);
							if ((it->second.Subscribe_begin == 1 || it->second.Subscribe_begin > Timestamp))
							{
								it->second.Subscribe_begin = Timestamp;
							}
						}
						if (!Mirror_start)
						{
							if (++Total_ack_received == All_topics.size() * Mirror_to.size())
							{
								Mirror_start = true;
								timer->cancel();
								Start_subscribe(this_);
							}
						}
					});
				}

				void Start_collecting_target_info() 
				{
					Async_work_in_progress = true;
					Mirror_start = false;
					Total_ack_received = 0;
					auto this_(base_type::shared_from_this());
					if (Mirrors.size() == 0)
					{
						for (auto&& it(Mirror_to.begin()); it != Mirror_to.end(); ++it) 
						{
							Mirrors.emplace_back(it->first, it->second);
						}
					}
					for (auto&& it(Mirrors.begin()); it != Mirrors.end(); ++it)
					{
						auto & Target(*it);
						Target.process(All_topics);
						if (!Target.Is_opened)
						{
							Target.Ack_client->Open(Target.Server_name, Target.Port, [this, this_, &Target](bool Error) {
								Target.Is_opened = true;
								if (!Error)
								{
									Target.Ack_client->On_error = [this, this_, &Target]()
									{
										Synapse_client->strand.post([this, this_, &Target]() { Target.Is_error = true; });
										Put_into_error_state(this_, ::std::runtime_error("On_error from Ack_client of target server(" + Target.Server_name + ":" + Target.Port + ")"));
									};
									try
									{
										auto & Channel(Target.Ack_client->Get_channel(1));
										if (!not_using_original_seq_timestamp)
										{
											Channel.On_ack = [this, this_, &Target](::std::string const & Topic_name, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, data_processors::synapse::amqp_0_9_1::bam::Table * Amqp_headers, bool error)
											{
												if (!error)
												{
													try
													{
														if (Timestamp > 0 && Sequence_number > 0 && !Topic_name.empty() && Topic_name.compare("/"))
														{
															Update_seq_and_timestamp(Target, this_, Timestamp, Sequence_number, Topic_name);
														}
														else
														{
															if (Amqp_headers != nullptr && !Topic_name.compare("/"))
															{
																Total_topics_to_be_subscribed = (uint64_t)(Amqp_headers->get("Current_subscription_topics_size"));

															}
															else {
																::data_processors::synapse::misc::log("Topic_name: " + Topic_name + ", Timestamp: " + ::std::to_string(Timestamp) + ", Sequence number: " + ::std::to_string(Sequence_number) + ".\n", true);
																throw ::std::runtime_error("In On_ack, neither Amqp_headers nor Topic_name is valid.");
															}
														}
													}
													catch (::std::exception& e)
													{
														Put_into_error_state(this_, e);
													}
													catch (...)
													{
														Put_into_error_state(this_, ::std::runtime_error("Unknow exception in On_ack (Target is:" + Target.Server_name + ")."));
													}
												}
												else
												{
													Put_into_error_state(this_, ::std::runtime_error("On_ack report error."));
												}
											};
										}
										else
										{
											// Process incoming message and unsubscribe from a topic
											auto & Channel(Target.Ack_client->Get_channel(1));
											Channel.On_incoming_message = [this, this_, &Channel, &Target](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) 
											{
												try {
													if (pEnvelope != nullptr) {
														Channel.Stepover_incoming_message(pEnvelope, [this, this_, &Channel, &Target](data_processors::synapse::amqp_0_9_1::Message_envelope * pEnvelope) {
															try {
																if (pEnvelope == nullptr) 
																{
																	Put_into_error_state(this_, ::std::runtime_error("Envolope is null in Stepover_incomming_message(Target's ACK)."));
																	return;
																}

																assert(!pEnvelope->Pending_buffers.empty());
																auto && Envelope(*pEnvelope);
																assert(Envelope.Pending_buffers.front().Message_timestamp);
																assert(Envelope.Pending_buffers.front().Message_sequence_number);
																auto const & Pending_buffer(Envelope.Pending_buffers.front());
																auto Payload_buffer(Pending_buffer.Get_amqp_frame() + 3);
																assert(!(reinterpret_cast<uintptr_t>(Pending_buffer.Get_amqp_frame() + 1) % 16));
																uint32_t const Payload_size(synapse::misc::be32toh_from_possibly_misaligned_source(Payload_buffer) + 5);
																assert(Payload_buffer[Payload_size - 1] == 0xCE);																	
																::boost::shared_ptr<Waypoints_type> Concrete_waypoints(::boost::make_shared<Waypoints_type>());

																if (Envelope.Has_waypoints())
																{
																	Decode_waypoints(Payload_buffer, Payload_size, Concrete_waypoints.get(), Envelope);

																	auto && all_path(Concrete_waypoints->Get_path());
																	bool find = false;
																	for (auto&& it(all_path.begin()); it != all_path.end(); ++it)
																	{
																		if (it->Is_tag_set() && it->Get_tag() == "topic_mirror" &&
																			it->Is_messageId_set() && it->Get_messageId() == Instance_name)
																		{
																			find = true;
																			if (it->Is_contentId_set())
																			{
																				auto content(it->Get_contentId());
																				auto pos = content.find_first_of('-');
																				assert(pos != ::std::string::npos);
																				auto seq = ::boost::lexical_cast<uint_fast64_t>(content.substr(0, pos));
																				auto timestamp = ::boost::lexical_cast<uint_fast64_t>(content.substr(pos + 1));
																				Update_seq_and_timestamp(Target, this_, timestamp, seq, Envelope.Topic_name);
																			}
																			else
																				throw ::std::runtime_error("Content_id should be set (" + Target.Server_name + ":" + Target.Port + ")");
																		}
																	}
																	if (!find)
																	{
																		throw ::std::runtime_error("Could not find Waypoints in message from server " + Target.Server_name + ":" + Target.Port);
																	}
																}
																else
																{
																	throw ::std::runtime_error("Envelope doesn't has waypoints, this is not expected when not_using_source_seq_and_timestamp is used");
																}
																Channel.Release_message(Envelope);
																Target.Ack_client->strand.post([this, this_, &Target, &Envelope, &Channel]() mutable {
																	if (Target.Unsubscribe_in_progress == false)
																	{
																		Target.Unsubscribe_in_progress = true;
																		
																		// Unscribe this topic
																		this->Unsubscribe_a_topic(Channel, this_, Target, Envelope.Topic_name);
																	}
																	else
																	{
																		Target.To_be_unsubscribed.insert(Envelope.Topic_name);
																	}
																});
															}
															catch (::std::exception& e)
															{
																Put_into_error_state(this_, e);
															}
															catch (...)
															{
																Put_into_error_state(this_, ::std::runtime_error("Unknow exception in Step_over_incomming_message of Target server(" + Target.Server_name + ":" + Target.Port + ")"));
															}
														});
													}
													else
													{
														Put_into_error_state(this_, ::std::runtime_error("Envelope is nullptr in On_incommming_message(" + Target.Server_name + ":" + Target.Port + ")"));
													}
												}
												catch (::std::exception& e)
												{
													Put_into_error_state(this_, e);
												}
												catch (...)
												{
													Put_into_error_state(this_, ::std::runtime_error("Unknow exception in On_incomming_message callback of Target server(" + Target.Server_name + ":" + Target.Port + ")"));
												}
											};
										}
										Async_subscribe_ack(Channel, this_, All_topics.begin());
									}
									catch (::std::exception& e)
									{
										Put_into_error_state(this_, e);
									}
									catch (...)
									{
										Put_into_error_state(this_, ::std::runtime_error("Unknow exception in Ack_synapse_client->Open callback."));
									}
								}
								else
								{
									Put_into_error_state(this_, ::std::runtime_error("Could not open Ack_synapse_client connection to server: " + Target.Server_name + ":" + Target.Port + "."));
								}
							}, Time_out / 2);
						}
						else
						{
							::data_processors::synapse::misc::log("Connection to " + Target.Server_name + ":" + Target.Port + " is already eltablished, start ack subscription now.\n", true);
							Total_target_servers = 0;
							Target.Ack_client->strand.post([this, this_, &Target]() {
								auto & Channel(Target.Ack_client->Get_channel(1));

								Async_subscribe_ack(Channel, this_, All_topics.begin());
							});
						}
					}
				}

				void Unsubscribe_a_topic(Synapse_channel_type & Channel, ::boost::shared_ptr<Topic_mirror> const & this_, Target_server& Target, ::std::string const& Topic)
				{
					// Unscribe this topic
					Channel.Unsubscribe([this_, this, &Target, Topic, &Channel](bool) {
						::data_processors::synapse::misc::log("Topic " + Topic + " is unsubscribed from " + Target.Server_name + ":" + Target.Port + ".\n", true);
						if (!Target.To_be_unsubscribed.empty())
						{
							auto temp(*Target.To_be_unsubscribed.begin());
							Target.To_be_unsubscribed.erase(Target.To_be_unsubscribed.begin());
							Target.Ack_client->strand.post([&Channel, this, this_, &Target, temp]() mutable {
								Unsubscribe_a_topic(Channel, this_, Target, temp);
							});
						}
						else
						{
							Target.Unsubscribe_in_progress = false;
						}
					}, Topic);
				}
				::std::shared_ptr<::boost::asio::deadline_timer> timer;
				void Async_subscribe_ack(Synapse_channel_type & Channel, ::boost::shared_ptr<Topic_mirror> const & this_, std::set<::std::string>::const_iterator I) 
				{
					if (I != All_topics.end()) 
					{
						typename Synapse_channel_type::Subscription_options Options;
						Options.Begin_utc = data_processors::synapse::misc::Get_micros_since_epoch(::boost::gregorian::date(::boost::gregorian::day_clock::universal_day()) + ::boost::gregorian::years(1));
						Options.Supply_metadata = true;
						Options.Preroll_if_begin_timestamp_in_future = true;
						Options.Skipping_period = 157680000000000u; // => Ack semantics
						Options.Skip_payload = !not_using_original_seq_timestamp;
						Channel.Subscribe(
							[this, this_, &Channel, I](bool Error) mutable 
								{
									// Error reaction... throw for the time being.
									if (Error)
										Put_into_error_state(this_, ::std::runtime_error("ACK subcsription failed"));
									else
										Async_subscribe_ack(Channel, this_, ++I);
								}
							, *I, Options
							);
					}
					else 
					{
						Synapse_client->strand.post([this, this_]() {
							if (++Total_target_servers == Mirror_to.size()) 
							{
								// Start a timer, which is to check the difference between message received and current time.
								timer.reset(new ::boost::asio::deadline_timer(synapse::asio::io_service::get(), ::boost::posix_time::seconds(Time_out)));
								::std::function<void(::boost::system::error_code const &)> on_timeout =
									[this, this_](::boost::system::error_code const & ec) {
									if (!ec) 
									{
										// At this stage, we should already receive all ACKs for all topics.
										Synapse_client->strand.post([this, this_]() {
											if (!Mirror_start)
											{
												Mirror_start = true;
												Start_subscribe(this_);
											}
										});
									}
								};
								timer->async_wait(on_timeout);
							}
						});
					}
				}

				void Open_pub_channel(::boost::shared_ptr<Topic_mirror> const & this_, Target_server& target, ::std::set<::std::string>::const_iterator I) {
					if (I != All_topics.end())
					{
						::std::string topic(*I);
						target.Ack_client->Open_channel(target.Channel, [this, this_, &target, topic, I](bool Error) mutable {
							try
							{
								if (!Error) {
									target.SetChannel(topic, target.Channel);
									auto & Channel(target.Ack_client->Get_channel(target.Channel++));
									Channel.Set_as_publisher(topic);
									Open_pub_channel(this_, target, ++I);
								}
								else
								{
									Put_into_error_state(this_, ::std::runtime_error("Open channel failed (" + target.Server_name + ")"));
								}
							}
							catch (std::exception& e)
							{
								Put_into_error_state(this_, e);
							}
							catch (...)
							{
								Put_into_error_state(this_, std::runtime_error("Unknow exception in Open_channel call back"));
							}
						});
					}
					else
					{
						// All channels are open for this target server
						Synapse_client->strand.post([this, this_]() {
							try
							{
								if (++Total_target_servers == Mirror_to.size())
								{
									// If all channels are open fine for all servers, we can start subscribing all topics and publishing them to targets.
									for (auto&& it(Topic_processor_map.begin()); it != Topic_processor_map.end(); ++it)
									{
										if (All_topics.find(it->first) != All_topics.end())
											Topic_subscriptions.emplace(it->first, std::max(it->second.Ack_updater.size() == Mirrors.size() ? it->second.Subscribe_begin : 1, Subscribe_from), it->second.Subscribe_end);
									}
									All_topics.clear();
									if (Topic_subscriptions.size() > 0)
										Async_subscribe(this_);
									if (Late_topics.size() > 0) // Some new topics are arrived, we need to repeat the same procedure
									{
										::data_processors::synapse::misc::log("Current asynchronous work is completed, and find " + ::std::to_string(Late_topics.size()) + " more new topics, will be processed now.\n", true);
										All_topics.swap(Late_topics);
										Start_collecting_target_info();
									}
									else
									{
										::data_processors::synapse::misc::log("Asynchronous work is completed, and there is no more new topic.\n", true);
										Async_work_in_progress = false;
									}
								}
							}
							catch (std::exception& e)
							{
								Put_into_error_state(this_, e);
							}
							catch (...)
							{
								Put_into_error_state(this_, ::std::runtime_error("Unknow exception while calling Ansync_subscribe"));
							}
						});
					}
				}

				void Start_subscribe(::boost::shared_ptr<Topic_mirror> const & this_) {
					// First, open channel for each topic to be published
					Total_target_servers = 0;
					for (auto&& it(Mirrors.begin()); it != Mirrors.end(); ++it) 
					{
						auto & target(*it);
						target.Ack_client->strand.post([this, this_, &target]() {
							try
							{
								Open_pub_channel(this_, target, All_topics.begin());
							}
							catch (::std::exception& e)
							{
								Put_into_error_state(this_, e);
							}
							catch (...)
							{
								Put_into_error_state(this_, ::std::runtime_error("Unknow exception while calling 'Open_sub_channel'"));
							}
						});

					}
				}
			};

			void static default_parse_arg(int const argc, char* argv[], int & i) {
				if (i == argc - 1)
					throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
				else if (!::strcmp(argv[i], "--log_root")) {
					Log_path = argv[i + 1];
					data_processors::synapse::misc::log.Set_output_root(argv[++i]);
				}
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
				else if (!::strcmp(argv[i], "--subscribe_from"))
					Subscribe_from = data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::from_iso_string(argv[++i]));
				else if (!::strcmp(argv[i], "--subscription"))
					SubscriptionS.emplace(argv[++i]);
				else if (!::strcmp(argv[i], "--time_out"))
					Time_out = 2 * ::boost::lexical_cast<unsigned>(argv[++i]);
				else if (!::strcmp(argv[i], "--subscribe_to"))
					Subscribe_to = data_processors::synapse::misc::Get_micros_since_epoch(::boost::posix_time::from_iso_string(argv[++i]));
				else if (!::strcmp(argv[i], "--instance_name"))
					Instance_name = argv[++i];
				else if (auto Host_Port = data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--mirror_to", i, argv))
					Mirror_to.emplace_back(Host_Port.Host, Host_Port.Port);
				else
					throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');
			}


			void static run_(int argc, char* argv[]) {
				try {
#ifdef __WIN32__
					::std::atomic_thread_fence(::std::memory_order_acquire);
					if (run_as_service == true) {
						// allow sys-admins to override some parameters via the services applet in control panel
						assert(argc);
						for (int i(1); i != argc; ++i)
							default_parse_arg(argc, argv, i);
						service_handle = ::RegisterServiceCtrlHandler(service_name, (LPHANDLER_FUNCTION)service_control_handler);
						if (!service_handle)
							throw ::std::runtime_error("could not register windows service control handler");

						::memset(&service_status, 0, sizeof(service_status));
						service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
						service_status.dwCurrentState = SERVICE_RUNNING;
						service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

						::SetServiceStatus(service_handle, &service_status);
					}
#endif

					synapse::misc::log(">>>>>>> BUILD info: " BUILD_INFO " main source: " __FILE__ ". date: " __DATE__ ". time: " __TIME__ ".\n", true);
					data_processors::synapse::misc::log("Topic mirror starting.\n", true);

					if (!concurrency_hint)
						concurrency_hint = data_processors::synapse::misc::concurrency_hint();

					// create based on chosen runtime parameters
					data_processors::synapse::asio::io_service::init(concurrency_hint);
					data_processors::synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates

																				   // Automatic quitting handling
					data_processors::synapse::asio::Autoquit_on_signals Signal_quitter;
#ifndef GDB_STDIN_BUG
					data_processors::synapse::asio::Autoquit_on_keypress Keypress_quitter;
#endif
					::boost::shared_ptr<Topic_mirror<data_processors::synapse::asio::ssl_tcp_socket>> ssl_server;
					::boost::shared_ptr<Topic_mirror<data_processors::synapse::asio::tcp_socket>> tcp_server;
					data_processors::synapse::misc::log("Connection to " + host + ':' + port + '\n', true);
					if (!ssl_key.empty() || !ssl_certificate.empty()) {
						if (ssl_key.empty() || ssl_certificate.empty())
							throw ::std::runtime_error("not all SSL options have been specified. mandatory:{--ssl_key, --ssl_certificate} optional: --ssl_listen_on");

						data_processors::synapse::misc::log("Listening in SSL on " + host + ':' + port + '\n', true);
						data_processors::synapse::asio::io_service::init_ssl_context();

						auto && ssl(data_processors::synapse::asio::io_service::get_ssl_context());
						ssl.use_certificate_file(ssl_certificate, basio::ssl::context::pem);
						ssl.use_private_key_file(ssl_key, basio::ssl::context::pem);

						::boost::make_shared<Topic_mirror<data_processors::synapse::asio::ssl_tcp_socket>>()->Run();
						ssl_server->Run();
					}
					else {
						tcp_server = ::boost::make_shared<Topic_mirror<data_processors::synapse::asio::tcp_socket>>();
						tcp_server->Run();
					}

					::std::vector<::std::thread> pool(concurrency_hint * 8 - 1);
					for (auto && i : pool) {
						i = ::std::thread([]()->void {
							data_processors::synapse::asio::run();
						});
					}

					data_processors::synapse::asio::run();

					for (auto & i : pool)
						i.join();
				}
				catch (::std::exception const & e) {
#ifdef __WIN32__
					if (run_as_service == true)
						service_status.dwWin32ExitCode = -1;
#endif
					data_processors::synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
					data_processors::synapse::asio::Exit_error = true;
				}
#ifdef __WIN32__
				if (run_as_service == true) {
					if (require_stop) {
						service_status.dwCurrentState = SERVICE_STOPPED;
						service_status.dwControlsAccepted = 0;
						::SetServiceStatus(service_handle, &service_status);
					}
					else {
						if (::data_processors::synapse::asio::Exit_error.load(::std::memory_order_relaxed))
						{
							throw ::std::runtime_error("To force our service restart.");
						}
					}
				}
#endif
				data_processors::synapse::misc::log("Bye bye.\n", true);
			}

			int static run(int argc, char* argv[]) {
				try {
					// CLI...
					for (int i(1); i != argc; ++i) {
#ifdef __WIN32__
						if (!::strcmp(argv[i], "--run_as_service"))
							run_as_service = true;
						else
#endif
						if (!::strcmp(argv[i], "--not_using_source_seq_and_timestamp"))
							not_using_original_seq_timestamp = true;
						else						
							default_parse_arg(argc, argv, i);
					}

#ifdef __WIN32__
					if (run_as_service == true) {
						::SERVICE_TABLE_ENTRY dispatch_table[2];
						::memset(dispatch_table, 0, sizeof(dispatch_table));
						dispatch_table[0].lpServiceName = ::data_processors::Topic_mirror::service_name;
						dispatch_table[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)::data_processors::Topic_mirror::run_;
						::std::atomic_thread_fence(::std::memory_order_release);
						return !::StartServiceCtrlDispatcher(dispatch_table);
					}
#endif
					run_(0, nullptr); // dummy for the sake of windows nonsense...
				}
				catch (::std::exception const & e) {
					data_processors::synapse::misc::log("oops: " + ::std::string(e.what()) + "\n", true);
				}
				return data_processors::synapse::asio::Exit_error;
			}

		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[]) {
#if !defined(NDEBUG) && defined(__WIN32__)
	::SetErrorMode(0);
#endif
	return ::data_processors::Topic_mirror::run(argc, argv);
}

