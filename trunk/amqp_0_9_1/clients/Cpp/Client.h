#include <queue>
#include <unordered_map>

#include <boost/regex.hpp>
#include <boost/logic/tribool.hpp>

#include <thrift/DpThrift.h>

#include "../../foreign/copernica/endian.h"
#include "../../foreign/copernica/bytebuffer.h"
#include "../../foreign/copernica/protocolexception.h"
#include "../../foreign/copernica/table.h"
#include "../../foreign/copernica/connectionstartokframe.h"
#include "../../foreign/copernica/connectiontuneokframe.h"
#include "../../foreign/copernica/connectionopenframe.h"
#include "../../foreign/copernica/connectioncloseokframe.h"
#include "../../foreign/copernica/channelopenframe.h"
#include "../../foreign/copernica/basicframe.h"
#include "../../foreign/copernica/basicconsumeframe.h"
#include "../../foreign/copernica/basicpublishframe.h"
#include "../../foreign/copernica/basicheaderframe.h"
#include "../../foreign/copernica/receivedframe.h"
#include "../../foreign/copernica/connectiontuneframe.h"
#include "../../foreign/copernica/connectioncloseframe.h"
#include "../../foreign/copernica/queueunbindframe.h"
#include "../../foreign/copernica/channelcloseframe.h"

#include <data_processors/Federated_serialisation/Dom_node.h>

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_CLIENT_CONSTS_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_CLIENT_CONSTS_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 {
unsigned constexpr static asio_buffer_size = 8192 * 8;
unsigned constexpr static max_supported_channels_end = 8192;
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif

#include "../../Connection_base.h"

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_CLIENT_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_CLIENT_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 {

namespace basio = ::boost::asio;

namespace bam = data_processors::synapse::amqp_0_9_1::foreign::copernica; // for the time being, may replace later on with our own

struct Message_envelope {

	::std::unique_ptr<char, void(*)(void *)> User_data{nullptr, [](void*){}};

	struct Pending_buffer {
		::std::unique_ptr<char unsigned, ::std::function<void(char unsigned *)>> Allocated_buffer;
		::std::unique_ptr<::apache::thrift::protocol::TCompactProtocol, ::std::function<void(::apache::thrift::protocol::TCompactProtocol *)>> Thrift_protocol;
		uint_fast64_t Message_sequence_number;
		uint_fast64_t Message_timestamp;
		uint_fast32_t Flags;
		uint_fast32_t Payload_size;

		Pending_buffer() = default;
		Pending_buffer(Pending_buffer &&) = default;

		char unsigned const * Get_amqp_frame() const {
			auto Raw(reinterpret_cast<uintptr_t>(Allocated_buffer.get() + 16) & ~static_cast<uintptr_t>(15u));
			return reinterpret_cast<char unsigned*>(Raw - 1);
		}

	};
	
	// Todo: the whole custom mem alloc
	::std::queue<Pending_buffer> Pending_buffers;

	bool Freshly_created{true};

	::std::string Topic_name;
	::std::string Stream_id;
	::std::string Type_name;
	::boost::shared_ptr<data_processors::Federated_serialisation::Dom_node> Waypoints;
	::boost::shared_ptr<data_processors::Federated_serialisation::Dom_node> Message;
	Pending_buffer const * Front_pending_buffer
	#ifndef NDEBUG
		{nullptr}
	#endif
	;

	uint_fast64_t Get_message_sequence_number() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Front_pending_buffer->Message_sequence_number;
	}

	uint_fast64_t Get_message_timestamp() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Front_pending_buffer->Message_timestamp;
	}

	uint_fast64_t Get_payload_size() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Front_pending_buffer->Payload_size;
	}
	bool static Is_Delta_Poison(uint_fast32_t Flags) {
		return Flags & 0x10;
	}

	bool Has_stream_id() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Has_stream_id(Front_pending_buffer->Flags);
	}

	bool Has_waypoints() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Has_waypoints(Front_pending_buffer->Flags);
	}

	bool Is_delta() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Is_delta(Front_pending_buffer->Flags);
	}

	bool static Has_stream_id(uint_fast32_t Flags) {
		return Flags & 0x8;
	}
	bool static Has_waypoints(uint_fast32_t Flags) {
		return Flags & 0x1;
	}
	bool static Is_delta(uint_fast32_t Flags) {
		return Flags & 0x2;
	}

	bool Is_Delta_Poison() const {
		assert(!Pending_buffers.empty() && Front_pending_buffer != nullptr);
		return Is_Delta_Poison(Front_pending_buffer->Flags);
	}

private:
	template <typename T> friend class Channel;
	bool Front_message_ready{false};
	::std::function<void(Message_envelope *)> Message_processed_callback;

	data_processors::Federated_serialisation::State_Of_Dom_Node_Type Root_State_Of_Dom_Node;
};

template <typename Connection>
class Channel {

	// Mainly publishing fields...

	::std::unique_ptr<char unsigned[]> emulation_buffer;
	char unsigned * emulation_buffer_begin;
	unsigned emulation_buffer_size;
	char unsigned * emulation_buffer_content_header_begin;
	char unsigned * ch_overwrite_buffer_begin;
	uint_fast8_t constexpr static ch_overwrite_buffer_size{3};

	friend void amqp_0_9_1::Set_emulation_buffer<Channel<Connection>, bam::BasicPublishFrame>(Channel<Connection> &, bam::BasicPublishFrame const &, unsigned const, bool const);

	typedef ::std::array<basio::const_buffer, 2> Write_scatter_buffers_sequence_type;
	Write_scatter_buffers_sequence_type Write_scatter_buffers;

	::std::unique_ptr<::apache::thrift::protocol::TCompactProtocol, ::std::function<void(::apache::thrift::protocol::TCompactProtocol *)>> Thrift_protocol_to_publish_with;

	bool First_publish{true}; // Used to save on re-sending of topic name string on subsequent republishings (works because we only allow 1 topic per channel during publishing).

	uint_fast64_t Target_data_consumption_limit{static_cast<uint_fast64_t>(-1)};
	uint8_t Publisher_Sticky_Mode{0};

	// ... end of mainly publishing fields.

	// Todo: make a configurable parameter out of this one...
	unsigned constexpr static Max_pending_buffers_per_stream{25};

	Connection & connection;
	unsigned const id;

	::std::unique_ptr<char unsigned, ::std::function<void(char unsigned *)>> Allocated_buffer;

	// Todo, later on construct with 'sp' custom allocator also!
	::std::unordered_map<::std::string, // Topic_name
		::std::unordered_map<::std::string, // Stream_name
			::std::unordered_map<::std::string, // Type_name
				Message_envelope // polymorphic for the time being
			>
		>
	> Accumulated_messages;

public:

	bool Set_Delta_Poison_On_First_Published_Message{false};
	bool Set_Delta_Poison_On_First_Received_Message{true};
	uint_fast64_t Published_Messages_Size{0}; // Number of payload (thrift) published messages (not the same as number of AMQP published messages because publisher can publish a non-payload message such as 'topic consumption limit' "control" message only.

	::std::function<void(::std::string const & Topic_name, uint_fast64_t Timestamp, uint_fast64_t Sequence_number, bam::Table * Amqp_headers, bool Error)> On_ack;
	::std::function<void(Message_envelope *)> On_incoming_message;
	::std::function<void(bool)> On_async_request_completion;

	// TODO better encapsulation
	char unsigned * data
		#ifndef NDEBUG
			{nullptr}
		#endif
	; // Always points to own buffer (if any)... 'connection.data' is the one that floats around (pointing either to internal connection's asio buffer, or the channel buffer/portion-thereof).

	unsigned max_message_size{0};

	// TODO also deploy Large memory pages (even in the segregated pools allocator!)
	#ifndef NDEBUG
		unsigned Data_size{0};
	#endif

	::std::string Transient_key; // reused by publisher also.
	uint_fast64_t Transient_message_sequence_number;
	uint_fast64_t Transient_message_timestamp;

	uint_fast64_t async_fence_size{0};

	Channel(Channel const &) = delete;
	void operator = (Channel const &) = delete;
	
	template <typename Memory_alarm_policy>
	Channel(Connection & connection, unsigned const id, Memory_alarm_policy const &) // memory alarm policy is not really used in client at the moment (here just to comply with commond code-base from the server)... todo later on -- either will add this feature to client or refactor Connection_base (e.g. make channel ctor default version not to throuw without any extra arg, and make Ensure_existing_channel in Connection_base do static-polymorphism upcall to deriving class to allocate actual channel.. this way server can do throw, etc. and client won't have to do anything)...
	: connection(connection), id(id) {
	}

	~Channel() {
	}

	template <typename String>
	void Set_as_publisher(String && Topic_name) {
		assert(!emulation_buffer); // Cannot change target topic association after one has been established (at the moment).
		assert(!On_incoming_message); // Cannot subscribe and publish on the same channel: AMQP is mostly for unidirectional flow on any given channel.
		Transient_key = ::std::forward<String>(Topic_name);
		amqp_0_9_1::Set_emulation_buffer(*this, bam::BasicPublishFrame(id, "", Transient_key), id, true);
	}

	unsigned get_id() const { return id; }

	template <void (Channel::*Callback) (), typename Frame>
	void write_frame(Frame && frm) {
		connection.template write_frame<Callback>(this, ::std::forward<Frame>(frm));
	}

	bool frame_is_awaiting{false};
	void frame_awaits() {
		assert(connection.strand.running_in_this_thread());
		if (!async_fence_size) {
			#ifndef NDEBUG
				frame_is_awaiting = true;
			#endif
			assert(awaiting_frame_is_being_processed == false);
			process_awaiting_frame();
		} else
			frame_is_awaiting = true;
	}

	bool awaiting_frame_is_being_processed{false};

	void process_awaiting_frame() {
		assert(connection.strand.running_in_this_thread());
		assert(!connection.async_error);
		assert(!async_fence_size);
		assert(frame_is_awaiting == true);
		assert(connection.read_awaiting_frame_initiated == false);
		assert(connection.on_msg_ready_initiated == false);

		awaiting_frame_is_being_processed = true;

		uint_fast32_t const Payload_size(synapse::misc::be32toh_from_possibly_misaligned_source(connection.data + 3));

		if (*connection.data != 3) {
			frame_is_awaiting = awaiting_frame_is_being_processed = false;
			connection.read_awaiting_frame(id, Payload_size);
		} else { // If data contains payload...
			assert(id);
			if (Payload_size > connection.Max_message_with_payload_size)
				throw ::std::runtime_error("maximim message size exceeds reasonable limits");

			assert(!Allocated_buffer);
			assert(data == nullptr);
			auto const Target_buffer_size(Payload_size + 8 + 15);
			#ifndef NDEBUG
				Data_size = Target_buffer_size - 15;
			#endif
			Allocated_buffer = connection.sp.Make_unique_buffer(Target_buffer_size);
			// Runtime alignment of 'data + 1' to 16 bytes
			auto Raw(reinterpret_cast<uintptr_t>(Allocated_buffer.get() + 16) & ~static_cast<uintptr_t>(15u));
			data = reinterpret_cast<char unsigned*>(Raw - 1);
			assert(!(reinterpret_cast<uintptr_t>(data + 1) % 16));

			frame_is_awaiting = awaiting_frame_is_being_processed = false;
			connection.read_awaiting_frame(id, Payload_size);
		}
	}

	/**
		Note: Process_Directly_In_Strand parameter is mainly for latency-sensitive apps (which reduces 'given message to be processed' time but also lowers the throughput due to lower parallelization of processing).
	 */
	template <typename Waypoints_type, typename Message_type, typename Callback_type>
	void Process_incoming_message(Message_envelope * Envelope_ptr, Callback_type && Callback, bool const Process_Directly_In_Strand = false) {
		assert(connection.strand.running_in_this_thread());
		//assert(async_fence_size);

		auto && Envelope(*Envelope_ptr);
		assert(!Envelope.Pending_buffers.empty());
		assert(Envelope.Pending_buffers.front().Thrift_protocol);
		assert(Envelope.Pending_buffers.front().Allocated_buffer);

		Envelope.Front_pending_buffer = &Envelope.Pending_buffers.front();

		Envelope.Message_processed_callback = ::std::forward<Callback_type>(Callback);

		if (Envelope.Has_waypoints() && !Envelope.Waypoints) {
			Envelope.Waypoints = connection.sp.template make_shared<Waypoints_type>();
		}

		if (!Envelope.Message) {
			Envelope.Message = connection.sp.template make_shared<Message_type>();
			static_cast<Message_type *>(Envelope.Message.get())->Set_envelope(Envelope_ptr);
		}

		//decrement_async_fence_size();

		::apache::thrift::protocol::TCompactProtocol & Thrift_protocol(*Envelope.Pending_buffers.front().Thrift_protocol.get());

		if (!Process_Directly_In_Strand) {
			// Process message body, concurrently (depending on policy... currently concurrently w.r.t. other channels)...
			auto c_(connection.shared_from_this());
			synapse::asio::io_service::get().post([this, c_, &Envelope, &Thrift_protocol]() {
			// Todo: either make some special atomic-variable for the error (to be seen from non-strand of the connection), or make sure that 'on error' in other code bits does not alter/clear the Accumulated_messages->Envelope->Pending_buffers
				try {
					assert(Envelope.Message);

					Envelope.Root_State_Of_Dom_Node.Increment_Read_Counter(Envelope.Is_Delta_Poison());
					if (Envelope.Has_waypoints()) {
						auto Concrete_waypoints(static_cast<Waypoints_type *>(Envelope.Waypoints.get()));
						Concrete_waypoints->Read_in_delta_mode = false;
						Concrete_waypoints->State_Of_Dom_Node.Root = &Envelope.Root_State_Of_Dom_Node;
						Concrete_waypoints->read(&Thrift_protocol);
					}
					auto Concrete_message(static_cast<Message_type *>(Envelope.Message.get()));
					Concrete_message->Read_in_delta_mode = Envelope.Is_delta();
					Concrete_message->State_Of_Dom_Node.Root = &Envelope.Root_State_Of_Dom_Node;
					Concrete_message->read(&Thrift_protocol);

					// ... done processing the message concurrently... back into strand.
					c_->strand.dispatch([this, c_, &Envelope](){
						if (!connection.async_error) {
							assert(!Envelope.Front_message_ready);
							Envelope.Front_message_ready = true;
							Ping_ordered_envelope_decoders();
						} 
					});
				} catch (::std::exception const & e) { 
					connection.strand.dispatch([this, c_, e]() {
						connection.put_into_error_state(e); 
					});
				}
			});
		} else { // NOTE!!! Important -- this (i.e. direct invocation of Ping_ordered_envevlope_decoders) only works because the Connection_base code for process_awaiting_frame will do a 'stack-breaking' strand.post() for subsequent invocation of the rest of the callbacks associated with reading of the NEXT message... otherwise stackoverlow may occur!
			assert(Envelope.Message);
			Envelope.Root_State_Of_Dom_Node.Increment_Read_Counter(Envelope.Is_Delta_Poison());
			if (Envelope.Has_waypoints()) {
				auto Concrete_waypoints(static_cast<Waypoints_type *>(Envelope.Waypoints.get()));
				Concrete_waypoints->Read_in_delta_mode = false;
				Concrete_waypoints->State_Of_Dom_Node.Root = &Envelope.Root_State_Of_Dom_Node;
				Concrete_waypoints->read(&Thrift_protocol);
			}
			auto Concrete_message(static_cast<Message_type *>(Envelope.Message.get()));
			Concrete_message->Read_in_delta_mode = Envelope.Is_delta();
			Concrete_message->State_Of_Dom_Node.Root = &Envelope.Root_State_Of_Dom_Node;
			Concrete_message->read(&Thrift_protocol);
			assert(!Envelope.Front_message_ready);
			Envelope.Front_message_ready = true;
			Ping_ordered_envelope_decoders();
		}
	}

	template <typename Callback_type>
	void Stepover_incoming_message(Message_envelope * Envelope_ptr, Callback_type && Callback) {
		assert(connection.strand.running_in_this_thread());
		//assert(async_fence_size);

		auto && Envelope(*Envelope_ptr);

		Envelope.Message_processed_callback = ::std::forward<Callback_type>(Callback);

		//decrement_async_fence_size();

		assert(!Envelope.Pending_buffers.empty());
		assert(Envelope.Pending_buffers.front().Thrift_protocol);
		assert(Envelope.Pending_buffers.front().Allocated_buffer);

		Envelope.Front_pending_buffer = &Envelope.Pending_buffers.front();

		auto c_(connection.shared_from_this());
		c_->strand.post([this, c_, &Envelope](){
			if (!connection.async_error) {
				assert(!Envelope.Front_message_ready);
				Envelope.Front_message_ready = true;
				Ping_ordered_envelope_decoders();
			} 
		});
	}
private:
	void Ping_ordered_envelope_decoders() {
		if(!On_wire_ordered_envelope_decoders.empty())
			On_wire_ordered_envelope_decoders.front()();
		Process_awaiting_frame_if_any();
	}
public:

	/**
		Note: currently will call next 'On_incoming_message' directly (i.e. not async-post style).
		This gives caller the freedom to decide whether to invoke (occasionally) busy-polling processing, or post for later (next in queue of jobs) processing (i.e. via explicit strand.post from the caller).
	*/
	void Release_message(Message_envelope & Envelope) {
		assert(connection.strand.running_in_this_thread());
		if (!connection.async_error) {
			Envelope.Front_message_ready = false;
			if (Envelope.Pending_buffers.size() == Max_pending_buffers_per_stream)
				decrement_async_fence_size();
			assert(!Envelope.Pending_buffers.empty());
			Envelope.Pending_buffers.pop();
			#ifndef NDEBUG
				Envelope.Front_pending_buffer = nullptr;
			#endif
			if (!Envelope.Pending_buffers.empty())
				On_incoming_message(&Envelope);
			Ping_ordered_envelope_decoders();
		}
	}

	void
	on_frame(uint_fast32_t const payload_size)
	{
		assert(connection.strand.running_in_this_thread());
		assert(!async_fence_size);
		if (*connection.data == 3) { // priority to content body above all else
			assert(!(reinterpret_cast<uintptr_t>(data + 1) % alignof(uint16_t)));
			if (data[payload_size + 7] == 0xce) {

				// TODO -- dontdo this ideally... because would like to process other messages concurrently... (payloads for different topics) -- will need to decide whether to suffer 'memcpy' vs parallel decoding... vs ability to expand the memory buffer...
				//++async_fence_size;


				// Using shared_ptr only because Thrift mechansms are too embedded with using it and it would take too long at present to dig all those up and refactor into non shared_ptr specialisations in our case...
				auto Thrift_buffer(::boost::allocate_shared<::apache::thrift::transport::TMemoryBuffer>(typename Connection::template Basic_alloc_2<::apache::thrift::transport::TMemoryBuffer>(connection.sp)));

				Thrift_buffer->resetBuffer(data + 7, payload_size, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::OBSERVE);

				Message_envelope::Pending_buffer Pending;
				Pending.Thrift_protocol = connection.sp.template Make_unique<::apache::thrift::protocol::TCompactProtocol>(Thrift_buffer); 
				Pending.Allocated_buffer = ::std::move(Allocated_buffer);
				assert(!Allocated_buffer);
				data = nullptr;

				// Todo: Thrift really ought to start supporting unsigned types, otherwise only would work with things like 2-complement representation on both source and target machines.

				auto && Thrift_protocol(Pending.Thrift_protocol);

				int8_t Version;
				Thrift_protocol->readByte(Version);
				if (static_cast<uint8_t>(Version) != 1u)
					throw bam::ProtocolException("Unsupported version number in our message envelope.");

				int32_t Flags;
				Thrift_protocol->readI32(Flags);

				::std::string Type_name;
				Thrift_protocol->readString(Type_name);

				::std::string Stream_id;
				if (Message_envelope::Has_stream_id(Flags))
					Thrift_protocol->readString(Stream_id);

				Pending.Message_sequence_number = Transient_message_sequence_number;
				Pending.Message_timestamp = Transient_message_timestamp;
				Pending.Flags = Flags;
				Pending.Payload_size = payload_size;

				auto & Envelope(Accumulated_messages[Transient_key][Stream_id][Type_name]);

				On_wire_ordered_envelope_decoders.emplace_back([this, &Envelope]() {
					auto Decoder([this, &Envelope](){
						if (Envelope.Front_message_ready || connection.async_error) {
							On_wire_ordered_envelope_decoders.pop_front(); // now can nuke the top-level lambda
							assert(Envelope.Message_processed_callback || connection.async_error);
							auto tmp(Envelope.Message_processed_callback);
							Envelope.Message_processed_callback = nullptr;
							if (tmp)
								tmp(!connection.async_error ? &Envelope : nullptr);
							Envelope.Front_message_ready = false;
						}
					});
					Decoder();
				});

				if (Envelope.Freshly_created) {
					Envelope.Topic_name = Transient_key;
					Envelope.Stream_id = Stream_id;
					Envelope.Type_name = Type_name;
					if (Set_Delta_Poison_On_First_Received_Message)
						Pending.Flags |= 0x10; // first message in client's streaming view is always delta-reset -poisoned. Note, however, that non-delta/key status for any (sub)-node is orthogonal and is implicitly delta-insensitive (poison or otherwise).
				}
				Envelope.Pending_buffers.emplace(::std::move(Pending));
				auto const Pending_buffers_size(Envelope.Pending_buffers.size());
				assert(Max_pending_buffers_per_stream > 1);
				if (Pending_buffers_size == Max_pending_buffers_per_stream)
					++async_fence_size;
				else if (Pending_buffers_size == 1)
					On_incoming_message(&Envelope);
				Envelope.Freshly_created = false;
			} else
				throw bam::ProtocolException("body frame is corrupt");

			auto c_(connection.shared_from_this());
			c_->strand.post([this, c_](){
				if (!connection.async_error)
					Process_awaiting_frame_if_any();
			});

		} else if (*connection.data < 3) {
			auto const data_size(payload_size + 8);
			if (data_size < 12)
				throw bam::ProtocolException("frame is too small");
			uint16_t const class_id(synapse::misc::be16toh_from_possibly_misaligned_source(connection.data + 7));
			if (class_id == 60) {
				switch (*connection.data) {
					case 1 :
						// method_id
						if (synapse::misc::be16toh_from_possibly_misaligned_source(connection.data + 9) == 60) {
							on_basic_deliver(connection.data, data_size);
							return;
						} else
							break;
					case 2:
						On_basic_content_header(connection.data, data_size);
						return;
				}
			}
			bam::ByteBuffer const Byte_buffer(connection.data, data_size);
			bam::ReceivedFrame frm(Byte_buffer, data_size);
			switch (frm.type()) {
			case 1 : // methods
				frm.load_class_id();
				frm.load_method_id();
				switch (frm.classID) {
					case 10 : // connection
						if (get_id()) // todo -- more elegant handlig (e.g. send out of order frame reply back to peer)
							throw bam::ProtocolException("Channel id for connection frames ought to be zero");
						switch (frm.methodID) {
							case 30 : On_connection_tune(frm); break;
							case 51 : connection.On_Connection_Close(); break;
						}
						break;
				}
				return;
			}
		}
	}

	::std::string User_name{"guest"};
	::std::string User_password{"guest"};

	void On_protocol_header_sent() {
		assert(!id);
		connection.dispatch([this](){
			write_frame<&Channel::Process_awaiting_frame_if_any>(bam::ConnectionStartOKFrame(bam::Table(), "PLAIN", '\0' + User_name + '\0' + User_password, "en_US"));
		}, true, 1024);
	}

	void On_connection_tune(bam::ReceivedFrame & frm) {
		assert(!id);
		bam::ConnectionTuneFrame Tune_frame(frm);
		auto const Candidate_max_message_with_payload_size(Tune_frame.frameMax());
		if (connection.Max_message_with_payload_size < Candidate_max_message_with_payload_size)
			throw bam::ProtocolException("Server max message size bigger than what client can accept and we dont support fragmentation.");
		connection.Max_message_with_payload_size = Candidate_max_message_with_payload_size;
		connection.heartbeat_frame_rate = ::std::min<uint_fast32_t>(Tune_frame.heartbeat(), connection.heartbeat_frame_rate);
		connection.heartbeat_timer.cancel();
		if (connection.heartbeat_frame_rate)
			connection.dispatch_heartbeat_timer();
		connection.dispatch([this](){
			write_frame<&Channel::On_connection_tune_sent>(bam::ConnectionTuneOKFrame(max_supported_channels_end - 1, connection.Max_message_with_payload_size, connection.heartbeat_frame_rate));
		}, true, 1024);
	}

	void On_connection_tune_sent() {
		assert(!id);
		connection.dispatch([this](){
			write_frame<&Channel::On_connection_open_sent>(bam::ConnectionOpenFrame("/"));
		}, true, 1024);
	}

	void On_connection_open_sent() {
		assert(!id);
		connection.On_connection_open_sent();
	}

	void On_channel_open_sent_Notify_connection() {
		connection.On_channel_open_sent_Notify_connection(id);
	}

	template<typename Callback_type>
	void Open(Callback_type && Callback) {
		assert(!On_async_request_completion);
		On_async_request_completion = ::std::forward<Callback_type>(Callback);
		connection.dispatch([this]()->void {
			write_frame<&Channel::On_channel_open_sent>(bam::ChannelOpenFrame(id));
		}, true, 1024);
	}

	void On_channel_open_sent() {
		Invoke_async_completion_callback();
	}

	void Invoke_async_completion_callback() {
		assert(connection.strand.running_in_this_thread());
		assert(On_async_request_completion);
		auto To_call(::std::move(On_async_request_completion));
		assert(!On_async_request_completion);
		To_call(false);
	}

	void On_basic_consume_sent() {
		Invoke_async_completion_callback();
	}

	struct Subscription_options {
		uint_fast64_t Begin_utc{0};
		uint_fast64_t End_utc{0};
		uint_fast64_t Delayed_delivery_tolerance{static_cast<uint_fast64_t>(-1)}; // Todo -- refresh memory as to why this static_cast is needed???
		bool Supply_metadata{false};
		bool Tcp_No_delay{false};
		bool Preroll_if_begin_timestamp_in_future{false};
		uint_fast64_t Skipping_period{0};
		bool Skip_payload{false};
		bool Atomic_subscription_cork{false};
		amqp_0_9_1::Subscription_To_Missing_Topics_Mode_Enum Subscription_To_Missing_Topics_Mode{None};
		uint_fast64_t _DONT_USE_JUST_YET_Target_data_consumption_limit{0}; 
		uint_fast64_t _DONT_USE_JUST_YET_Sparsify_upto_timestamp{0}; 
	};

	template<typename Callback_type, typename String>
	void Subscribe(Callback_type && Callback, String && Topic_name, Subscription_options const & Options = {}) {

		assert(connection.strand.running_in_this_thread());
		assert(!emulation_buffer && (On_incoming_message || Options.Skip_payload)); // Cannot subscribe and publish on the same channel: AMQP is mostly for unidirectional flow on any given channel.

		assert(!On_async_request_completion);
		assert(!::std::string(Topic_name).empty());

		On_async_request_completion = ::std::forward<Callback_type>(Callback);

		bam::Table filter;
		if (Options.Begin_utc)
			filter.set("begin_timestamp", bam::ULongLong(Options.Begin_utc));
		if (Options.End_utc)
			filter.set("end_timestamp", bam::ULongLong(Options.End_utc));
		if (Options.Delayed_delivery_tolerance != static_cast<decltype(Options.Delayed_delivery_tolerance)>(-1))
			filter.set("delayed_delivery_tolerance", bam::ULongLong(Options.Delayed_delivery_tolerance));
		if (Options.Supply_metadata == true)
			filter.set("supply_metadata", bam::UOctet(1));
		if (Options.Tcp_No_delay == true)
			filter.set("tcp_no_delay", bam::UOctet(1));
		if (Options.Preroll_if_begin_timestamp_in_future == true)
			filter.set("preroll_if_begin_timestamp_in_future", bam::UOctet(1));
		if (Options.Skipping_period)
			filter.set("Skipping_period", bam::ULongLong(Options.Skipping_period));
		if (Options.Skip_payload)
			filter.set("Skip_payload", bam::UOctet(1));
		if (Options.Atomic_subscription_cork)
			filter.set("Atomic_subscription_cork", bam::UOctet(1));
		if (Options.Subscription_To_Missing_Topics_Mode != None)
			filter.set("Missing_Topic_Mode", bam::UOctet(Options.Subscription_To_Missing_Topics_Mode));
		if (Options._DONT_USE_JUST_YET_Target_data_consumption_limit)
			filter.set("Target_data_consumption_limit", bam::ULongLong(Options._DONT_USE_JUST_YET_Target_data_consumption_limit));
		if (Options._DONT_USE_JUST_YET_Sparsify_upto_timestamp)
			filter.set("Sparsify_upto_timestamp", bam::ULongLong(Options._DONT_USE_JUST_YET_Sparsify_upto_timestamp));

		connection.dispatch([this, Topic_name(::std::forward<String>(Topic_name)), filter(::std::move(filter))](){
			write_frame<&Channel::On_basic_consume_sent>(
				bam::BasicConsumeFrame(id, Topic_name, "", 
					false, false, false, false, // unused or unsupported anyways at the moment...
					filter
				)
			);
		}, true, 2048);
	}

	template<typename Callback_type, typename String>
	void _DONT_USE_JUST_YET_Set_topic_sparsification(Callback_type && Callback, String && Topic_name, uint_fast64_t const Target_data_consumption_limit = 0, uint_fast64_t const Sparsify_upto_timestamp = 0, uint_fast64_t const Maximum_Timestamp_Gap = 0, bool const Delete_Topic = false) {
		assert(connection.strand.running_in_this_thread());

		assert(!On_async_request_completion);
		assert(!::std::string(Topic_name).empty());

		On_async_request_completion = ::std::forward<Callback_type>(Callback);

		bam::Table filter;
		filter.set("Sparsify_only", bam::Long(1));
		if (Target_data_consumption_limit)
			filter.set("Target_data_consumption_limit", bam::ULongLong(Target_data_consumption_limit));
		if (Sparsify_upto_timestamp)
			filter.set("Sparsify_upto_timestamp", bam::ULongLong(Sparsify_upto_timestamp));
		if (Maximum_Timestamp_Gap)
			filter.set("Maximum_Timestamp_Gap", bam::ULongLong(Maximum_Timestamp_Gap));
		if (Delete_Topic)
			filter.set("Purge", bam::UOctet(1));

		connection.dispatch([this, Topic_name(::std::forward<String>(Topic_name)), filter(::std::move(filter))](){
			write_frame<&Channel::On_basic_consume_sent>(
				bam::BasicConsumeFrame(id, Topic_name, "", 
					false, false, false, false, // unused or unsupported anyways at the moment...
					filter
				)
			);
		}, true, 1024);
	}

	template<typename Callback_type, typename String>
	void Unsubscribe(Callback_type && Callback, String && Topic_name) {
		assert(connection.strand.running_in_this_thread());
		assert(!emulation_buffer && On_incoming_message); // Cannot subscribe and publish on the same channel: AMQP is mostly for unidirectional flow on any given channel.

		assert(!On_async_request_completion);
		assert(!::std::string(Topic_name).empty());

		On_async_request_completion = ::std::forward<Callback_type>(Callback);

		connection.dispatch([this, Topic_name(::std::forward<String>(Topic_name))](){
			write_frame<&Channel::Invoke_async_completion_callback>(
				bam::QueueUnbindFrame(id, ::std::string(), ::std::string(), Topic_name)
			);
		}, true, 1024);
	}

	template<typename Callback_type, typename Extended_ack_fields_type>
	void Subscribe_extended_ack(Callback_type && Callback, Extended_ack_fields_type && Extended_ack_fields) {
		assert(connection.strand.running_in_this_thread());
		assert(!emulation_buffer); // Cannot subscribe and publish on the same channel: AMQP is mostly for unidirectional flow on any given channel.

		assert(!On_async_request_completion);
		On_async_request_completion = ::std::forward<Callback_type>(Callback);

		connection.dispatch([this, Extended_ack_fields(::std::forward<Extended_ack_fields_type>(Extended_ack_fields))](){
			write_frame<&Channel::On_basic_consume_sent>(
				bam::BasicConsumeFrame(id, "/", "", false, false, false, false, Extended_ack_fields)
			);
		}, true, 1024);
	}

private:

	template<typename Message_Pointer, typename Waypoints_Pointer>
	void Publish_Helper_1(::apache::thrift::transport::TMemoryBuffer * Thrift_buffer_raw_ptr, Message_Pointer & Message, Waypoints_Pointer & Waypoints, bool const Send_as_delta, ::std::string const & Stream_id, bool const Delta_Poison) {
		if (Message) {
			// Reserve extra 4 bytes (will clobber later on)
			Thrift_buffer_raw_ptr->Advance(4);

			Thrift_protocol_to_publish_with->writeByte(1);

			uint_fast32_t Flags(0);

			if (Waypoints != nullptr)
				Flags |= 0x1;
			if (Send_as_delta)
				Flags |= 0x2;
			if (!Stream_id.empty())
				Flags |= 0x8;
			if (!Published_Messages_Size++ && Set_Delta_Poison_On_First_Published_Message || Delta_Poison)
				Flags |= 0x10;

			Thrift_protocol_to_publish_with->writeI32(Flags);

			assert(!Message->Get_struct_type_name().empty());
			Thrift_protocol_to_publish_with->writeString(Message->Get_struct_type_name());

			if (!Stream_id.empty())
				Thrift_protocol_to_publish_with->writeString(Stream_id);

			if (Waypoints != nullptr)
				Waypoints->write(Thrift_protocol_to_publish_with.get(), true);

			Message->write(Thrift_protocol_to_publish_with.get(), !Send_as_delta);

			uint8_t const End_byte(0xce);
			Thrift_protocol_to_publish_with->writeByte(*reinterpret_cast<int8_t const *>(&End_byte));

			// Thirft_protoco_to_publish_with->getTransport()->flush();


			// Assign reserved space for amqp body frame payload-size variable.
			char unsigned * Buffer_to_send_begin; uint32_t Buffer_to_send_size;
			Thrift_buffer_raw_ptr->getBuffer(&Buffer_to_send_begin, &Buffer_to_send_size);
			assert(Buffer_to_send_size);
			assert(!(reinterpret_cast<uintptr_t>(Buffer_to_send_begin) % 4));
			new (Buffer_to_send_begin) uint32_t(htobe32(Buffer_to_send_size - 5));

		}
	}

public:
	void Publish_Helper_2(::apache::thrift::transport::TMemoryBuffer * Thrift_buffer_raw_ptr, uint_fast64_t const Sequence_number, uint_fast64_t const Timestamp, uint_fast64_t const Target_data_consumption_limit, bool const Replace_Previous_Publisher, bool const Allow_Replacing_Self) {
		char unsigned * Buffer_to_send_begin; uint32_t Buffer_to_send_size;
		Thrift_buffer_raw_ptr->getBuffer(&Buffer_to_send_begin, &Buffer_to_send_size);

		auto const Publisher_Sticky_Mode(
			(Replace_Previous_Publisher ? static_cast<typename ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type>(data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask::Replace_Another) : 0)
			|
			(Allow_Replacing_Self ? static_cast<typename ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type>(data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask::Replace_Self) : 0)
		);

		auto const Cached_Parameters_Are_Ok(
			(!Target_data_consumption_limit || this->Target_data_consumption_limit == Target_data_consumption_limit)
			&& (this->Publisher_Sticky_Mode == Publisher_Sticky_Mode)
		);

		// emulation buffer's size of the content frame.
		if (Buffer_to_send_size && Cached_Parameters_Are_Ok) {
			// Clobber emulation buffer's size of the content frame.
			assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 11) % alignof(uint64_t)));
			new (emulation_buffer_content_header_begin + 11) uint64_t(htobe64(Buffer_to_send_size - 5));

			// Clobber timestamp and sequence number.
			assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 35) % alignof(uint64_t)));
			assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 43) % alignof(uint64_t)));
			new (emulation_buffer_content_header_begin + 35) uint64_t(htobe64(Sequence_number));
			new (emulation_buffer_content_header_begin + 43) uint64_t(htobe64(Timestamp));
		} else if (!Cached_Parameters_Are_Ok) { // Slow but rare usecase.

			// Todo -- refactor (may be) based on commonality with Connection_base (Set_emulation buffer method)
			bam::BasicPublishFrame Preceeding_frame(id, ::std::string(), First_publish ? Transient_key : ::std::string(), Replace_Previous_Publisher, Allow_Replacing_Self);
			unsigned const Preceeding_frame_size(Preceeding_frame.totalSize());
			unsigned const raw_offset(Preceeding_frame_size + 11);
			unsigned const alignment_padding((raw_offset + 7 & ~7u) - raw_offset);

			bam::Envelope Properties(nullptr, 0);
			Properties.setTimestamp(Timestamp);
			bam::Table Headers;
			Headers.set("XXXXXX", bam::ULongLong(Sequence_number));

			if (Target_data_consumption_limit && this->Target_data_consumption_limit != Target_data_consumption_limit) {
				this->Target_data_consumption_limit = Target_data_consumption_limit;
				Headers.set("Target_data_consumption_limit", bam::ULongLong());
			}

			if (this->Publisher_Sticky_Mode != Publisher_Sticky_Mode) {
				// Could have been UOctet, but other clients (e.g. Python pika lib) do not support the encoding and we dont at the moment fork that lib, so allowing 'int' as a generic compromise.
				Headers.set("Publisher_Sticky_Mode", bam::Long(Publisher_Sticky_Mode));
				this->Publisher_Sticky_Mode = Publisher_Sticky_Mode;
			}

			Properties.setHeaders(Headers);

			bam::BasicHeaderFrame content_header_frm(id, Properties);

			unsigned const content_header_frm_size(content_header_frm.totalSize());
			emulation_buffer_size = Preceeding_frame_size + content_header_frm_size;
			emulation_buffer.reset(new char unsigned[alignment_padding + emulation_buffer_size + (Buffer_to_send_size ? ch_overwrite_buffer_size : 0)]);
			emulation_buffer_begin = &emulation_buffer[alignment_padding];
			assert(!(reinterpret_cast<uintptr_t>(&emulation_buffer[0]) % alignof(uint64_t)));
			emulation_buffer_content_header_begin = emulation_buffer_begin + Preceeding_frame_size;

			Preceeding_frame.to_wire(bam::OutBuffer(emulation_buffer_begin, Preceeding_frame_size));
			content_header_frm.to_wire(bam::OutBuffer(emulation_buffer_begin + Preceeding_frame_size, content_header_frm_size));

			if (Buffer_to_send_size) {

				// Clobber emulation buffer's size of the content frame.
				assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 11) % alignof(uint64_t)));
				new (emulation_buffer_content_header_begin + 11) uint64_t(htobe64(Buffer_to_send_size - 5));

				ch_overwrite_buffer_begin = emulation_buffer_begin + emulation_buffer_size;
				*ch_overwrite_buffer_begin = 3; // only doing this for the body content frames
				uint16_t const Channel_id_be(htobe16(id));
				::memcpy(ch_overwrite_buffer_begin + 1, &Channel_id_be, 2); // overwrite channel (cant do it on real/shared by multiple sockets concurrently range's file buffer)

			} else {
				// Clobber emulation buffer's size of the content frame.
				assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 11) % alignof(uint64_t)));
				new (emulation_buffer_content_header_begin + 11) uint64_t(0);
			}

			First_publish = true; // induce re-setting of the emulation buffer to normal mode after this write.
		} else {
			auto c_(connection.shared_from_this());
			c_->strand.post([this, c_](){
				On_sent_publish_frame();
			});
			return;
		}
		if (Buffer_to_send_size) {

			auto const Pre_payload_buffer_size(emulation_buffer_size + ch_overwrite_buffer_size);
			Write_scatter_buffers[0] = basio::const_buffer(emulation_buffer_begin, Pre_payload_buffer_size);

			// Everything after the channel of the body frame (payload size variable, actual data size worth of bytes, end octet).
			auto const Payload_buffer_size(Buffer_to_send_size);
			Write_scatter_buffers[1] = basio::const_buffer(Buffer_to_send_begin, Payload_buffer_size);

			auto const Total_bytes_size_to_send(Pre_payload_buffer_size + Payload_buffer_size);

			// Todo: possibly refactor some based on commonality of code with the server counterpart.
			connection.dispatch([this, Total_bytes_size_to_send](){
				connection.template Write_External_Buffers<&Channel::On_sent_publish_frame>(this, Write_scatter_buffers, Total_bytes_size_to_send);
				connection.snd_heartbeat_flag = false;
			}, false, Total_bytes_size_to_send);
		} else {
			// Todo: possibly refactor some based on commonality of code with the server counterpart.
			connection.dispatch([this](){
				connection.template Write_External_Buffer<&Channel::On_sent_publish_frame>(this, emulation_buffer_begin, emulation_buffer_size);
				connection.snd_heartbeat_flag = false;
			}, false, emulation_buffer_size);
		}
	}

	// Todo: this is not very efficient, but for the initial sake of the end-user who might not be fully aware of intricate nature of Asio in the initial stages. In future will enforce greater requirements about the user-guaranteed scope of the messages/wayponts/delta_stream_ids without the use of smart pointers...
	template<typename Message_Pointer, typename Waypoints_Pointer, typename Callback>
	void Publish(Callback && On_Publish, Message_Pointer && Message, Waypoints_Pointer && Waypoints, bool const Send_as_delta = false, ::std::string const & Stream_id = ::std::string(), uint_fast64_t const Sequence_number = 0, uint_fast64_t const Timestamp = 0, uint_fast64_t const Target_data_consumption_limit = 0, bool const Process_Directly_In_Strand = false, bool const Delta_Poison = false, bool const Replace_Previous_Publisher = false, bool const Allow_Replacing_Self = false) {
		assert(connection.strand.running_in_this_thread());
		assert(emulation_buffer && !On_incoming_message); // Cannot subscribe and publish on the same channel: AMQP is mostly for unidirectional flow on any given channel.

		assert(!On_async_request_completion);

		if (!Message && !Target_data_consumption_limit)
			throw ::std::runtime_error("Cannot publish with both: null message and zero Target_data_consumption_limit (makes no sense).");

		On_async_request_completion = ::std::forward<Callback>(On_Publish);

		assert(!Thrift_protocol_to_publish_with);
		auto Thrift_buffer(::boost::allocate_shared<::apache::thrift::transport::TMemoryBuffer>(typename Connection::template Basic_alloc_2<::apache::thrift::transport::TMemoryBuffer>(connection.sp)));
		Thrift_protocol_to_publish_with = connection.sp.template Make_unique<::apache::thrift::protocol::TCompactProtocol>(Thrift_buffer); 

		auto Thrift_buffer_raw_ptr(Thrift_buffer.get());

		if (!Process_Directly_In_Strand) {
			// Process concurrently with respect to other channels.
			auto c_(connection.shared_from_this());
			synapse::asio::io_service::get().post([this, c_, Message(::std::forward<Message_Pointer>(Message)), Waypoints(::std::forward<Waypoints_Pointer>(Waypoints)), Send_as_delta, Delta_Poison, Stream_id, Sequence_number, Timestamp, Target_data_consumption_limit, Thrift_buffer_raw_ptr, Replace_Previous_Publisher, Allow_Replacing_Self](){
				Publish_Helper_1(Thrift_buffer_raw_ptr, Message, Waypoints, Send_as_delta, Stream_id, Delta_Poison);
				// ... done processing the message concurrently... back into strand.
				connection.strand.dispatch([this, c_, Thrift_buffer_raw_ptr, Sequence_number, Timestamp, Target_data_consumption_limit, Replace_Previous_Publisher, Allow_Replacing_Self](){
					if (!connection.async_error) {
						Publish_Helper_2(Thrift_buffer_raw_ptr, Sequence_number, Timestamp, Target_data_consumption_limit, Replace_Previous_Publisher, Allow_Replacing_Self);
					}
				});
			});
		} else {
			Publish_Helper_1(Thrift_buffer_raw_ptr, Message, Waypoints, Send_as_delta, Stream_id, Delta_Poison);
			Publish_Helper_2(Thrift_buffer_raw_ptr, Sequence_number, Timestamp, Target_data_consumption_limit, Replace_Previous_Publisher, Allow_Replacing_Self);
		}
	}

	void On_sent_publish_frame() {
		// Minor hack to not resend topic name on the same channel on subsequent Publish calls
		if (First_publish) {
			First_publish = false;
			amqp_0_9_1::Set_emulation_buffer(*this, bam::BasicPublishFrame(id, "", ""), id, true);
		}
		#ifndef NDEBUG
			Thrift_protocol_to_publish_with.reset();
		#endif
		Invoke_async_completion_callback();
	}

	// Not really appropriately named for the client context, but used by Connection_base from the server days/codebase. Todo: in future refactor all to a more neutral name.
	uint_fast32_t last_basic_publish_frame_size{0};
	void on_basic_deliver(char unsigned const * const Data, uint_fast32_t const Data_size) {

		uint_fast32_t Routing_key_data_begin_offset;
		// Skip past consumer tag, delivery tags and exchange name and redelivered flag
		if (Data[Data_size - 1] != 0xce  // valid octet
			|| Data_size < 12u // consumer tag size
			|| Data_size <= (Routing_key_data_begin_offset = 21u + Data[11]) // delivery tag and redelivered
			|| Data_size <= (Routing_key_data_begin_offset += Data[Routing_key_data_begin_offset] + 1) // exchange
		)
			throw bam::ProtocolException("basic_deliver_frame is corrupt or too small.");

		uint_fast8_t const Routing_key_size(Data[Routing_key_data_begin_offset]);
		if (Routing_key_size) {
			if (++Routing_key_data_begin_offset + Routing_key_size > Data_size)
				throw bam::ProtocolException("basic_deliver_frame is too small, routing key size implies overrunning the buffer");
			Transient_key.assign(reinterpret_cast<char const *>(Data + Routing_key_data_begin_offset), Routing_key_size);
		} else if (Transient_key.empty())
			throw bam::ProtocolException("basic_deliver_frame should contain routing key at least for the 1st message.");

		last_basic_publish_frame_size = Data_size;
	}

	void decrement_async_fence_size() {
#ifndef NDEBUG
		auto const prev(async_fence_size);
#endif
		--async_fence_size;
		assert(prev > async_fence_size);
	}

	uint_fast32_t last_basic_content_header_frame_size{0};

	void On_basic_content_header(char unsigned const * const data, uint_fast32_t const data_size) {
		auto const Body_payload_size(synapse::misc::be64toh_from_possibly_misaligned_source(data + 11));
		if (Transient_key.empty())
			throw bam::ProtocolException("basic content header shall be preceeded by setting up of a key/topic");
		else if (Body_payload_size > connection.Max_message_with_payload_size - 8)
			throw bam::ProtocolException("fragmentation is not currently supported, message size is too large...");
		else if (data_size == 52 && data[27] == 6) {
			auto const tmp_flag(synapse::misc::be16toh_from_possibly_misaligned_source(data + 19));
			if (tmp_flag == (1u << 15 | 1u << 13 | 1u << 6)) {
				Transient_message_sequence_number = synapse::misc::be64toh_from_possibly_misaligned_source(data + 35);
				Transient_message_timestamp = synapse::misc::be64toh_from_possibly_misaligned_source(data + 43);
			} else // distinction between extended-Ack (but with same frame-length etc.) and the other kinds of messages is that 'priority' property bit is set for extended Ack (repurposing otherwise unused part of the protocol really).
				Transient_message_sequence_number = Transient_message_timestamp = 0;
		} else // when data size is 22 (no props), etc.
			Transient_message_sequence_number = Transient_message_timestamp = 0;

		last_basic_content_header_frame_size = data_size;

		if (!Body_payload_size) { // for "Acks"...
			decltype(Transient_key) Key(Transient_key);
			decltype(Transient_message_timestamp) Timestamp(Transient_message_timestamp);
			decltype(Transient_message_sequence_number) Sequence(Transient_message_sequence_number);

			::std::shared_ptr<bam::Table> Amqp_headers;
			if (Key.size() == 1 && Key.front() == '/') {
				bam::ByteBuffer const Byte_buffer(data, data_size);
				bam::ReceivedFrame ReceivedFrame(Byte_buffer, data_size);
				bam::BasicHeaderFrame Content_header_frame(ReceivedFrame);
				auto & Meta(Content_header_frame.Get_metadata());
				if (Meta.hasHeaders())
					Amqp_headers.reset(new bam::Table(Meta.headers()));
			}

			On_wire_ordered_envelope_decoders.emplace_back([this, Key, Timestamp, Sequence, Amqp_headers](){
				auto Decoder([this, Key, Timestamp, Sequence, Amqp_headers](){
					On_wire_ordered_envelope_decoders.pop_front(); // ok to nuke top-level lambda here.
					if (!connection.async_error) {
						if (On_ack) 
							On_ack(Key, Timestamp, Sequence, Amqp_headers.get(), false);
						auto c_(connection.shared_from_this());
						c_->strand.post([this, c_](){
							if (!connection.async_error)
								Ping_ordered_envelope_decoders();
						});
					} else if (On_ack) 
						On_ack("", 0, 0, nullptr, true);
				});
				Decoder();
			});
			Ping_ordered_envelope_decoders();
		}

	}

	void Decrement_async_fence_size_and_process_awaiting_frame_if_any() { 
		assert(async_fence_size);
		decrement_async_fence_size();
		Process_awaiting_frame_if_any();
	}

	void Process_awaiting_frame_if_any() { 
		if (!async_fence_size && frame_is_awaiting == true && awaiting_frame_is_being_processed == false)
			process_awaiting_frame();
	}

	void Increment_locked_for_reading() {
		++async_fence_size;
	}

	void Decrement_locked_for_reading() {
		decrement_async_fence_size();
		if (!connection.async_error)
			Process_awaiting_frame_if_any();
	}

	::std::list<::std::function<void()>, typename Connection::template basic_alloc_1<::std::function<void()>>> On_wire_ordered_envelope_decoders{typename decltype(On_wire_ordered_envelope_decoders)::allocator_type{connection.sp}};

	void Put_into_error_state(::std::exception const &) {
		assert(connection.strand.running_in_this_thread());
		if (On_async_request_completion) {
			auto To_call(::std::move(On_async_request_completion));
			if (!connection.Ignore_Error)
				To_call(true);
		}
		if (On_incoming_message) {
			auto To_call(::std::move(On_incoming_message));
			if (!connection.Ignore_Error)
				To_call(nullptr);
		}
		if (On_ack) {
			auto To_call(::std::move(On_ack));
			if (!connection.Ignore_Error)
				To_call("", 0, 0, nullptr, true);
		}
		assert(connection.async_error);
		while (!On_wire_ordered_envelope_decoders.empty())
			On_wire_ordered_envelope_decoders.front()();
	}
};

template <typename SocketType, unsigned MaxBufferSize, typename TopType = void>
class Client : public amqp_0_9_1::Connection_base<
	typename ::std::conditional<::std::is_void<TopType>::value, Client<SocketType, MaxBufferSize>, TopType>::type
	, amqp_0_9_1::Channel<
		typename ::std::conditional<::std::is_void<TopType>::value, Client<SocketType, MaxBufferSize>, TopType>::type
	>, SocketType, MaxBufferSize, 24, 16
> {

	typedef typename ::std::conditional<::std::is_void<TopType>::value, Client, TopType>::type Top_type;

	friend class amqp_0_9_1::Channel<Top_type>;

	typedef class amqp_0_9_1::Channel<Top_type> Channel_type;

	typedef amqp_0_9_1::Connection_base<Top_type, amqp_0_9_1::Channel<Top_type>, SocketType, MaxBufferSize, 24, 16> base_type;

	unsigned Max_message_with_payload_size{decltype(base_type::sp)::Max_chunk_size};

	friend base_type;

	using base_type::data;
	using base_type::channels;
	using base_type::dispatch;
#ifndef NDEBUG
	using base_type::Data_size;
#endif
	using base_type::write_data;
	using base_type::heartbeat_timer;

	unsigned Write_high_watermark_pressure;
	unsigned Write_low_watermark_pressure;

	// These two methods are mostly for policy-compliance of the server code (since currentnly Client.h shares quite a bit of code with server), consider here as an irrelevant semantics (for the time being at least).
	virtual void Async_Destroy() override {
	}
	virtual ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type Get_Publisher_Sticky_Mode_Atomic() override {
		return 0;
	}

public:
	using base_type::strand;
	::std::function<void(::boost::tribool const &)> On_pending_writes_watermark;
	::std::function<void(bool)> On_async_request_completion;
	::std::function<void()> On_error;

private:

	unsigned Pending_writes_caching_capacity{0};
	bool Ignore_Error{false};

public:
	// As per policy compliance.
	unsigned get_base_type_pending_writechain_capacity(unsigned const Channels_size) const {
		return get_base_type_pending_writechain_capacity_impl(Channels_size, Pending_writes_caching_capacity);
	}
	// Static version used in ctor of this class during initialization of the base_type (members of this class are not yet initialized).
	unsigned static get_base_type_pending_writechain_capacity_impl(unsigned const Channels_size, unsigned const Pending_writes_caching_capacity) {
		return Pending_writes_caching_capacity + 1 + Channels_size; // +1 is for async-triggered write from heartbeat_timer.
	}

	// As per policy compliance.
	void On_pop_pending_writechain() { 
		// Edge triggered implementation
		if (base_type::pending_writechain.size() == Write_low_watermark_pressure && On_pending_writes_watermark)
			On_pending_writes_watermark(false);
	}

	// As per policy compliance.
	void On_push_pending_writechain() { 
		// Edge triggered implementation
		if (base_type::pending_writechain.size() == (Write_high_watermark_pressure + 1) && On_pending_writes_watermark)
			On_pending_writes_watermark(true);
	}

protected:
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

public:
	void on_connect() {
		// todo -- better encapsulation... (also consider taking the alloc stuff outside from 'ctor' which may happen on a different thread, and moving here whilst wrapping everything here in strand.dispatch() thingy), or because the whole thing up to here is implicitly 'stranded' then just issue c++11 memory fence here...
		base_type::on_connect();
		assert(Data_size >= 8);
		char const static Protocol_header[] = "AMQP\x00\x00\x09\x01";
		dispatch([this]()->void {
			auto const Spaces(write_data.Free());
			assert(Spaces.A_Size >= 8);
			::memcpy(Spaces.A_Begin, Protocol_header, 8);
			this->template Write_Internal_Buffer<&Channel_type::On_protocol_header_sent>(channels.data()->get(), 8); // hack (exception really -- using ch.0 to do the writing)
		}, true, 8);
		assert(Data_size >= 7);
		this->template read<&Top_type::on_msg_size_read>(7);
	}

	void On_connection_open_sent() {
		base_type::state = base_type::state_type::open;
		static_assert(base_type::init_supported_channels > 1, "Must have at least two initially supported channel.");
		for (unsigned i(1); i != base_type::init_supported_channels; ++i) {
			dispatch([this, i]()->void {
				channels[i]->template write_frame<&Channel_type::On_channel_open_sent_Notify_connection>(bam::ChannelOpenFrame(i));
			}, true, 1024);
		}
	}

	void Invoke_async_completion_callback() {
		assert(strand.running_in_this_thread());
		assert(On_async_request_completion);
		auto To_call(::std::move(On_async_request_completion));
		assert(!On_async_request_completion);
		To_call(false);
	}

	void On_channel_open_sent_Notify_connection(unsigned const Channel_id) {
		if (Channel_id == base_type::init_supported_channels - 1)
			Invoke_async_completion_callback();
	}

	Client(unsigned const Pending_writes_caching_capacity = 0, unsigned const Write_high_watermark_pressure = -1, unsigned const Write_low_watermark_pressure = -1, unsigned const Soc_rcv_bfr_sze = -1, unsigned const Soc_snd_bfr_sze = -1, bool Tcp_loopback_fast_path = false)
	: base_type(get_base_type_pending_writechain_capacity_impl(base_type::init_supported_channels, Pending_writes_caching_capacity)), Write_high_watermark_pressure(Write_high_watermark_pressure), Write_low_watermark_pressure(Write_low_watermark_pressure), Pending_writes_caching_capacity(Pending_writes_caching_capacity) { 
		base_type::get_socket().open();
		if (Tcp_loopback_fast_path)
			base_type::get_socket().Enable_tcp_loopback_fast_path();
		if (Soc_rcv_bfr_sze != static_cast<decltype(Soc_rcv_bfr_sze)>(-1))
			base_type::get_socket().set_option(::boost::asio::socket_base::receive_buffer_size(Soc_rcv_bfr_sze)); // 8192
		if (Soc_snd_bfr_sze != static_cast<decltype(Soc_snd_bfr_sze)>(-1))
			base_type::get_socket().set_option(::boost::asio::socket_base::send_buffer_size(Soc_snd_bfr_sze)); // 8192
	}

	template<typename Callback_type>
	void Open(::std::string const & host, ::std::string const & port, Callback_type && Callback, uint_fast32_t const & Heartbeat_rate = 0, bool Tcp_No_delay = false) {
		assert(!On_async_request_completion);
		if (Heartbeat_rate && Heartbeat_rate < base_type::heartbeat_frame_rate)
			base_type::heartbeat_frame_rate = Heartbeat_rate;
		On_async_request_completion = ::std::forward<Callback_type>(Callback);
		base_type::get_socket().set_option(basio::ip::tcp::no_delay(Tcp_No_delay));
		base_type::async_connect(
			base_type::shared_from_this() // Todo: refactor the base_type as in our case this shared_ptr is not needed (already in Callback arg).
			, host, port
		);
	}

	void Close() {
		assert(strand.running_in_this_thread());
		Ignore_Error = true;
		base_type::get_socket().close();
	}

	template<typename Callback_type>
	void Soft_Close(Callback_type && Callback) {
		assert(strand.running_in_this_thread());
		assert(!On_async_request_completion);
		On_async_request_completion = ::std::forward<Callback_type>(Callback);

		for (auto && Channel : channels)
			if (Channel->get_id())
				dispatch([Channel]()->void {
					Channel->template write_frame<&Channel_type::Process_awaiting_frame_if_any>(bam::ChannelCloseFrame(Channel->get_id()));
				}, true, 1024);
		dispatch([this](){
			channels.front()->template write_frame<&Channel_type::Process_awaiting_frame_if_any>(bam::ConnectionCloseFrame(0, ""));
		}, true, 1024);
	}

	void On_Connection_Close() {
		assert(strand.running_in_this_thread());
		Invoke_async_completion_callback();
	}

	template<typename Callback_type>
	void Open_channel(unsigned const Channel_id, Callback_type && Callback) {
		assert(strand.running_in_this_thread());
		assert(!On_async_request_completion);
		base_type::Ensure_existing_channel(Channel_id);
		channels[Channel_id]->Open(::std::forward<Callback_type>(Callback));
	}

	Channel_type & Get_channel(unsigned const Channel_id) {
		assert(strand.running_in_this_thread());
		if (Channel_id >= channels.size() || !channels[Channel_id])
			throw ::std::runtime_error("Channel is not openened or does not exist.");
		return *channels[Channel_id].get();
	}


	void put_into_error_state(::std::exception const & e) {
		assert(strand.running_in_this_thread());

		if (!Ignore_Error)
			base_type::put_into_error_state(e);
		else { // Hack. If ignoring the error we replicate all of our base classes implementation of 'put_into_error_state', but without any logging...
			base_type::heartbeat_timer.cancel();
			base_type::async_error = true;
			base_type::pending_writechain.clear();
			base_type::get_socket().close();
		}

		if (On_async_request_completion) {
			auto To_call(::std::move(On_async_request_completion));
			if (!Ignore_Error)
				To_call(true); 
		}
		if (On_pending_writes_watermark) {
			auto To_call(::std::move(On_pending_writes_watermark));
			if (!Ignore_Error)
				To_call(::boost::indeterminate); 
		}
		for (auto && Channel : channels) 
			if (Channel)
				Channel->Put_into_error_state(e); 
		if (On_error) {
			auto To_call(::std::move(On_error));
			if (!Ignore_Error)
				To_call(); 
		}
	}

};

template <typename Socket_type, typename Top_type = void> 
using Basic_synapse_client = Client<Socket_type, asio_buffer_size, Top_type>;

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
