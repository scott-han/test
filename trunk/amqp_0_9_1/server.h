#include <boost/regex.hpp>

#include "foreign/copernica/endian.h"
#include "foreign/copernica/protocolexception.h"
#include "foreign/copernica/bytebuffer.h"
#include "foreign/copernica/outbuffer.h"
#include "foreign/copernica/table.h"
#include "foreign/copernica/receivedframe.h"
#include "foreign/copernica/connectionstartframe.h"
#include "foreign/copernica/connectionstartokframe.h"
#include "foreign/copernica/connectiontuneframe.h"
#include "foreign/copernica/connectiontuneokframe.h"
#include "foreign/copernica/connectionopenframe.h"
#include "foreign/copernica/connectionopenokframe.h"
#include "foreign/copernica/connectioncloseokframe.h"
#include "foreign/copernica/channelopenframe.h"
#include "foreign/copernica/channelopenokframe.h"
#include "foreign/copernica/channelcloseokframe.h"
#include "foreign/copernica/basicpublishframe.h"
#include "foreign/copernica/basicconsumeframe.h"
#include "foreign/copernica/basicconsumeokframe.h"
#include "foreign/copernica/basiccancelframe.h"
#include "foreign/copernica/basiccancelokframe.h"
#include "foreign/copernica/basicdeliverframe.h"
#include "foreign/copernica/basicheaderframe.h"
#include "foreign/copernica/basicrejectframe.h"
#include "foreign/copernica/queuedeclareframe.h"
#include "foreign/copernica/queuedeclareokframe.h"
#include "foreign/copernica/queuebindframe.h"
#include "foreign/copernica/queuebindokframe.h"
#include "foreign/copernica/queueunbindframe.h"
#include "foreign/copernica/queueunbindokframe.h"
#include "foreign/copernica/exchangedeclareframe.h"
#include "foreign/copernica/exchangedeclareokframe.h"

#include "Connection_base.h"

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_SERVER_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_SERVER_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 {

namespace basio = ::boost::asio;

namespace bam = amqp_0_9_1::foreign::copernica; // for the time being, may replace later on with our own

// temporary stats hack (messages as in amqp data payload, not individual AMQP messages of which there could be much more...)
::std::atomic_uint_fast64_t static total_written_messages{0};
::std::atomic_uint_fast64_t static total_read_messages{0};
::std::atomic_uint_fast64_t static total_read_payload{0};

template <typename Channel>
struct Channel_traits {
	auto constexpr static Approximated_memory_consumption_guard_size{static_cast<uint_fast32_t>(sizeof(Channel)) + 1024}; // 1024 for strings like 'key' etc.
};
template <typename Connection>
class channel {
	typedef ::boost::intrusive_ptr<amqp_0_9_1::topics_type::meta_topic> meta_topic_ptr;
	typedef ::boost::shared_ptr<amqp_0_9_1::topics_type::topic_type> topic_ptr;
	typedef typename amqp_0_9_1::topics_type::topic_type::range_ptr_type range_ptr;

public:
	data_processors::misc::alloc::Scoped_contributor_to_virtual_allocated_memory_size<uint_fast32_t> Scoped_contributor_to_virtual_allocated_memory_size;
private:

	Connection & connection;
	unsigned const id;
public:
	enum class state_type {none, open};
	state_type
	get_state() const
	{
		return state;
	}
private:
	state_type state = state_type::none;
	enum class content_state_type {none, publish, subscribe};
	content_state_type content_state = content_state_type::none;
	bool content_ready = false;

	::std::string exchange;
	::std::string key;
	::std::string Consumer_tag;
	// wildcarded accumulator (note that wildcard is really a regex and now, given the feature of not creating previously non-existent topics only due to subscription to such topics, even a non-wildcarded topic name may be a part of this 'Wildcard' mechanism/container/etc.
	uint_fast16_t constexpr static Wildcard_approximated_memory_consumption_chunk_size{1024};
	struct Wildcarded_subscription_parameters {
		::std::string const & Key;
		uint_fast64_t Begin_timestamp;
		uint_fast64_t End_timestamp;
		uint_fast64_t Skipping_period;
		bool Skip_payload;
		bool Preroll_if_begin_timestamp_in_future;
		Wildcarded_subscription_parameters(::std::string const & Key, uint_fast64_t const Begin_timestamp, uint_fast64_t const End_timestamp, uint_fast64_t const Skipping_period, bool const Skip_payload, bool const Preroll_if_begin_timestamp_in_future) : Key(Key),Begin_timestamp(Begin_timestamp), End_timestamp(End_timestamp), Skipping_period(Skipping_period), Skip_payload(Skip_payload), Preroll_if_begin_timestamp_in_future(Preroll_if_begin_timestamp_in_future)  {
		}
		::std::function<void()> Erase_Index_Membership; // function instead of just the Wildcarded_subscriptions_keys_index_type::iterator because our custom allocator is incomplete type until T is seen (I think we are using sizeof(T) somewhere there)... meaning that at least some compilers dont like this (recursive template instantiation)... otherwise we could have gotten away with curiously recurring template pattern thingy... alas this may not be the case. 
	};
	typedef ::std::list<Wildcarded_subscription_parameters, typename Connection::template basic_alloc_1<Wildcarded_subscription_parameters>> Wildcarded_subscriptions_type;
	Wildcarded_subscriptions_type Wildcarded_subscriptions{typename Wildcarded_subscriptions_type::allocator_type{connection.sp}};
	::std::map<::std::string const, typename Wildcarded_subscriptions_type::iterator, ::std::less<::std::string const>, typename Connection::template basic_alloc_1<::std::pair<::std::string const, typename Wildcarded_subscriptions_type::iterator>>> Wildcarded_subscriptions_keys_index{typename decltype(Wildcarded_subscriptions_keys_index)::allocator_type{connection.sp}};

	uint_fast64_t preset_msg_seq_no;
	uint_fast64_t begin_timestamp;
	bool preroll_if_begin_timestamp_in_future;
	uint_fast64_t end_timestamp;

	uint_fast64_t Skipping_period{0};
	bool Skip_payload{false};

	amqp_0_9_1::Subscription_To_Missing_Topics_Mode_Enum Subscription_To_Missing_Topics_Mode;

	struct Extended_report_type {

		data_processors::misc::alloc::Scoped_contributor_to_virtual_allocated_memory_size<uint_fast64_t> Scoped_contributor_to_virtual_allocated_memory_size;

		// Memory alarm accounting info
		uint_fast32_t constexpr static Topics_list_approximated_memory_consumption_chunk_size{1024 * 1024}; 

		::std::string Consumer_tag;
		bam::Table Options;
		bam::Table Topics;
		bam::Array Array;
		::std::unique_ptr<char unsigned[]> Buffer;
		uint_fast32_t Buffer_size;
		bool topic_factory_listener_membership_initialised{false};
		typename amqp_0_9_1::topics_type::topic_notification_listeners_type::iterator topic_factory_listener_membership;
		~Extended_report_type() {
			if (topic_factory_listener_membership_initialised)
				amqp_0_9_1::topics->deregister_from_notifications(topic_factory_listener_membership);
		}
	};
	::std::unique_ptr<Extended_report_type> Extended_report;

	enum class Streaming_activation_attributes : uint8_t { None, Allow_preroll_if_begin_timestamp_in_future, Streaming_started, Pick_up_exactly_from_specified_starting_point };

	struct topic_stream : ::boost::enable_shared_from_this<topic_stream>  {

		data_processors::misc::alloc::Scoped_contributor_to_virtual_allocated_memory_size<uint_fast32_t> Scoped_contributor_to_virtual_allocated_memory_size{
			#ifdef DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_STREAM_1
				(data_processors::misc::alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) + DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_STREAM_1 > data_processors::misc::alloc::Memory_alarm_threshold ? throw data_processors::Size_alarm_error("Memory_alarm_threshold in topic_stream ctor breached.") : DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_STREAM_1) +
			#endif
			// Todo more thoughtful tuning...
			1024u * 1024u,
			data_processors::misc::alloc::Memory_alarm_breach_reaction_throw(), "Memory_alarm_threshold in topic_stream ctor breached."
		};

		topic_ptr topic;
		range_ptr range;
		range_ptr next_range;
		bool next_range_pending{false};
		uint_fast32_t const message_preroll;

		// These '_next_message_' variables are primarily for caching (to save on calling range->get_next_message). Even though get_next_message is optimized (based on My_back_message) to simply advance the pointer whenever possible, the subsequent interaction with the message such as getting message size/timestamp/etc happens in *sparse* memory-access fashion (i.e. not cache friendly). If there are many messages/topics (e.g. wildcard/multi-topic subscriptions) then it is likely that the needed address is no longer in cache and a longer trip to slower-tier -level memory is in order (and in multi-topic subscription the on_consumer loop will re-iterate over pretty much 'all' relevant topics for 'every' message being sent-out to a subscriber). So caching is to prevent this.
		synapse::database::Message_ptr My_next_message;
		uint_fast64_t My_next_message_timestamp;
		uint_fast64_t My_next_message_sequence_number;

		synapse::database::Message_ptr my_msg;
		synapse::database::Message_const_ptr My_back_message;

		bool scan{false};

		enum class Notification_level : uint8_t { Unwanted, Wanted, Wanted_due_to_message_being_appended };
		Notification_level notification_wanted{Notification_level::Unwanted};
		Notification_level notification_requested{Notification_level::Unwanted};

		char unsigned const * amqp_msg_buffer_begin;
		uint_fast32_t amqp_msg_payload_size;

		::std::unique_ptr<char unsigned[]> emulation_buffer;
		char unsigned * emulation_buffer_begin;
		unsigned emulation_buffer_size;
		char unsigned * emulation_buffer_content_header_begin;

		char unsigned * ch_overwrite_buffer_begin;
		uint_fast8_t constexpr static ch_overwrite_buffer_size{3};

		typedef ::std::array<basio::const_buffer, 2> write_scatter_buffers_sequence_type;
		write_scatter_buffers_sequence_type write_scatter_buffers;

		// TODO -- move it to range class in the semantics of 'used only by publishing thread' so to speak
		uint_fast32_t written_in_range; // used primarily as performance caching mechanism (not to reference too frequently the atomic var touched by 'get_written_size()' in range class... as well as nullptr check for msg that get_written_size() performs)

		uint_fast64_t begin_timestamp;
		uint_fast64_t end_timestamp;

		uint_fast64_t begin_msg_seq_no;

		uint_fast64_t Skipping_period;
		bool Skip_payload;

		Streaming_activation_attributes Activation_attributes;

		topic_stream(topic_stream const &) = delete; 
		void operator = (topic_stream const &) = delete; 

		topic_stream(topic_ptr const & t, range_ptr const & r, uint_fast64_t const begin_timestamp = -1, uint_fast64_t const end_timestamp = -1, uint_fast64_t const begin_msg_seq_no = -1, Streaming_activation_attributes const & Activation_attributes = Streaming_activation_attributes::None, uint_fast64_t const Skipping_period = 0, bool const Skip_payload = false)
		: topic(t), range(r), message_preroll(
			// our fields size prior to user's payload are:
			4 // size field
			+ 4 // number of indicies field
			+ synapse::database::Message_Indicies_Size * 8
			- 3 // not storing octet and ch fields in our database
		), written_in_range(r->get_written_size()), begin_timestamp(begin_timestamp), end_timestamp(end_timestamp), begin_msg_seq_no(begin_msg_seq_no), Skipping_period(Skipping_period), Skip_payload(Skip_payload), Activation_attributes(Activation_attributes) {
		}
		~topic_stream() = default;

		topic_stream(topic_ptr const & t, range_ptr const & r, unsigned ch, ::std::string const & exchange, ::std::string const & key, ::std::string const & Consumer_tag, bool const supply_metadata, uint_fast64_t begin_timestamp, uint_fast64_t end_timestamp, uint_fast64_t begin_msg_seq_no, Streaming_activation_attributes const & Activation_attributes, uint_fast64_t const Skipping_period, bool const Skip_payload)
		: topic_stream(t, r, begin_timestamp, end_timestamp, begin_msg_seq_no, Activation_attributes, Skipping_period, Skip_payload) {
			amqp_0_9_1::Set_emulation_buffer(*this, bam::BasicDeliverFrame(ch, Consumer_tag, 1, false, exchange, key), ch, supply_metadata);
		}

		char unsigned *
		get_amqp_buffer_begin(
		#ifndef NDEBUG
			unsigned & Remaining_buffer_size
		#endif 
		)
		{
			assert(topic && range);
			assert(written_in_range == range->get_written_size());
			assert(range->file.buffer_capacity() > written_in_range + message_preroll);
			#ifndef NDEBUG
				Remaining_buffer_size = range->file.buffer_capacity() - written_in_range + message_preroll;
			#endif
			return range->file.buffer_begin() + written_in_range + message_preroll;
		}

		void
		set_amqp_msg(database::Message_ptr msg, bool supply_metadata) 
		{
			my_msg = msg;
			assert(my_msg == My_next_message);
			assert(topic && range);
			assert(my_msg);
			assert(My_back_message);
			assert(my_msg <= My_back_message);
			assert(reinterpret_cast<uintptr_t>(my_msg.Raw()) >= reinterpret_cast<uintptr_t>(range->file.buffer_begin()));
			assert(reinterpret_cast<uintptr_t>(my_msg.Raw()) - reinterpret_cast<uintptr_t>(range->file.buffer_begin()) + my_msg.Get_size_on_disk() <= range->get_written_size());
			assert(my_msg.Get_size_on_disk() > message_preroll);

			// set amqp_buffer begin coordinates, and the payload size, 
			// end-byte octet should already be there as part of the recorded payload
			// but ch value cannot be overritten here because of cocurrent use of the same memory location of the range's file buffer in multi-socket and/or multi-channel subscription cases.
			assert(!(reinterpret_cast<uintptr_t>(my_msg.Raw()) % 8));
			amqp_msg_buffer_begin = my_msg.Raw() + message_preroll;
			assert(!(reinterpret_cast<uintptr_t>(amqp_msg_buffer_begin + 1) % alignof(uint16_t)));
			assert(!(reinterpret_cast<uintptr_t>(amqp_msg_buffer_begin + 3) % alignof(uint64_t)));

			amqp_msg_payload_size = my_msg.Get_payload_size();
			assert(be32toh(synapse::misc::Get_alias_safe_value<uint32_t>(amqp_msg_buffer_begin + 3)) == amqp_msg_payload_size - 5);
			assert(my_msg.Get_payload()[my_msg.Get_payload_size() - 1] == 0xce); // end octet
			assert(amqp_msg_buffer_begin[amqp_msg_payload_size + 2] == 0xce); // end octet

			if (supply_metadata == true) {
				assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 35) % alignof(uint64_t)));
				assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 43) % alignof(uint64_t)));

				//new (emulation_buffer_content_header_begin + 35) uint64_t(htobe64(my_msg.Get_index(0)));
				//new (emulation_buffer_content_header_begin + 43) uint64_t(htobe64(my_msg.Get_index(1)));
				new (emulation_buffer_content_header_begin + 35) uint64_t(htobe64(My_next_message_sequence_number));
				new (emulation_buffer_content_header_begin + 43) uint64_t(htobe64(My_next_message_timestamp));
			}
		}
	};

	// TODO also provide custom mem alloc for 'string' types in this map
	// topic_streams (actively participating streams)
	::std::map<::std::string const, ::boost::shared_ptr<topic_stream>, ::std::less<::std::string const>, typename Connection::template basic_alloc_1<::std::pair<::std::string const, ::boost::shared_ptr<topic_stream>>>> topic_streams{typename decltype(topic_streams)::allocator_type{connection.sp}};

	// meta_topics awaiting processing (e.g. too early)
	struct Pending_topics_supplementary_subscription_parameters {
		uint_fast64_t const End_timestamp;
		::std::string const Key;
		meta_topic_ptr const Meta_topic;
		uint_fast64_t const Begin_message_sequence_number;
		Streaming_activation_attributes const Activation_attributes;
		uint_fast64_t const Skipping_period;
		bool const Skip_payload;
		Pending_topics_supplementary_subscription_parameters(uint_fast64_t const End_timestamp, ::std::string const & Key, meta_topic_ptr const Meta_topic, uint_fast64_t const Begin_message_sequence_number, Streaming_activation_attributes const Activation_attributes, uint_fast64_t const Skipping_period, bool const Skip_payload) : End_timestamp(End_timestamp), Key(Key), Meta_topic(Meta_topic), Begin_message_sequence_number(Begin_message_sequence_number), Activation_attributes(Activation_attributes), Skipping_period(Skipping_period), Skip_payload(Skip_payload) {
		}
	};
	// TODO make this container into an asynchronous counterpart -- the more topics there are the more 'blocking' the behaviour will become
	// TODO also consider hash-mapping
	::std::multimap<uint_fast64_t const, Pending_topics_supplementary_subscription_parameters, ::std::less<uint_fast64_t const>, typename Connection::template basic_alloc_1<::std::pair<uint_fast64_t const, Pending_topics_supplementary_subscription_parameters>>> pending_meta_topics{typename decltype(pending_meta_topics)::allocator_type{connection.sp}};
	// also a by-name index for pending meta-topics... (to prevent subscribing to 'already-subscribed' topics)
	// value type is a 'link-to-multimap' container shortcut (used for when unsubscribing from topics and also taking out any of the relevant postponed meta-topic elements)
	#ifdef _MSC_VER
		typedef decltype(pending_meta_topics) _msvc_hack_1;
		::std::map<::std::reference_wrapper<::std::string const>, typename _msvc_hack_1::iterator, ::std::less<::std::string const>, typename Connection::template basic_alloc_1<::std::pair<::std::reference_wrapper<::std::string const>, typename _msvc_hack_1::iterator>>> pending_meta_topic_names{typename decltype(pending_meta_topic_names)::allocator_type{connection.sp}};
	#else
		::std::map<::std::reference_wrapper<::std::string const>, typename decltype(pending_meta_topics)::iterator, ::std::less<::std::string const>, typename Connection::template basic_alloc_1<::std::pair<::std::reference_wrapper<::std::string const>, typename decltype(pending_meta_topics)::iterator>>> pending_meta_topic_names{typename decltype(pending_meta_topic_names)::allocator_type{connection.sp}};
	#endif

	// during atomic subscription to retire 'futuristic' topics loaded as active during preceeding async-enum of topics 
	::std::multimap<uint_fast64_t const, ::std::string const, ::std::less<uint_fast64_t const>, typename Connection::template basic_alloc_1<::std::pair<uint_fast64_t const, ::std::string const>>> postponable_atomically_subscribed_topic_streams{typename decltype(postponable_atomically_subscribed_topic_streams)::allocator_type{connection.sp}}; // not relying on the 'streaming' to take out the futuristic topics because such may not be started at all (e.g. if 1st subscription is a wildcarded one)

	void clear_topic_streams(bool In_Destructor) {
		if (topic_streams.empty() == false) {
			if (content_state == content_state_type::publish) {
				auto & ts(*topic_streams.begin()->second);
				if (ts.topic)
					amqp_0_9_1::topics->Resign_Publisher(::std::move(ts.topic), In_Destructor ? nullptr : connection.shared_from_this());
			}
			topic_streams.clear();
		}
		pending_meta_topic_names.clear();
		pending_meta_topics.clear();
		content_state = content_state_type::none;
	}

public:
	// TODO better encapsulation
	char unsigned * data; // always points to own buffer (if any)... 'connection.data' is the one that floats around (pointing either to internal connection's asio buffer, or the range/file buffer)
	#ifndef NDEBUG
		unsigned Data_size{0};
	#endif
	unsigned max_message_size = synapse::database::MaxMessageSize;

	channel(channel const &) = delete;
	void operator = (channel const &) = delete;

	channel(Connection & connection, unsigned const id, data_processors::misc::alloc::Memory_alarm_breach_reaction_no_throw) noexcept
	: Scoped_contributor_to_virtual_allocated_memory_size(
		amqp_0_9_1::Channel_traits<channel>::Approximated_memory_consumption_guard_size, 
		data_processors::misc::alloc::Memory_alarm_breach_reaction_no_throw()
	),
	connection(connection), id(id) {
		assert(amqp_0_9_1::total_written_messages.is_lock_free());
		assert(amqp_0_9_1::total_read_messages.is_lock_free());
		assert(amqp_0_9_1::total_read_payload.is_lock_free());
	}
	channel(Connection & connection, unsigned const id, data_processors::misc::alloc::Memory_alarm_breach_reaction_throw)
	: Scoped_contributor_to_virtual_allocated_memory_size(
		#ifdef DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_CHANNEL_1
			(data_processors::misc::alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) + DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_CHANNEL_1 > data_processors::misc::alloc::Memory_alarm_threshold ? throw data_processors::Size_alarm_error("Memory_alarm_threshold in channel ctor breached.") : DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_CHANNEL_1) +
		#endif
		amqp_0_9_1::Channel_traits<channel>::Approximated_memory_consumption_guard_size, 
		data_processors::misc::alloc::Memory_alarm_breach_reaction_throw(), "Memory_alarm_threshold in channel ctor breached."
	),
	connection(connection), id(id) {
		assert(amqp_0_9_1::total_written_messages.is_lock_free());
		assert(amqp_0_9_1::total_read_messages.is_lock_free());
		assert(amqp_0_9_1::total_read_payload.is_lock_free());
	}

	~channel()
	{
		deresiter_from_topic_factory_listener_membership();
		#ifndef NDEBUG
			for (auto && ts : topic_streams) {
				assert(ts.second.unique());
			}
		#endif
		clear_topic_streams(true);
	}

	unsigned get_id() const { return id; }

	uint_fast64_t async_fence_size = 0;

	template <typename Frame>
	void
	write_frame(Frame && frm)
	{
		write_frame<&channel::Decrement_async_fence_size_and_process_awaiting_frame_if_any>(::std::forward<Frame>(frm));
	}

	template <void (channel::*Callback) (), typename Frame>
	void
	write_frame(Frame && frm)
	{
		connection.template write_frame<Callback>(this, ::std::forward<Frame>(frm));
	}

	bool frame_is_awaiting{false};
	void
	frame_awaits()
	{
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

	// TODO -- when performance-tuning, consider reducing to another var and taking it out...
	bool awaiting_frame_is_being_processed{false};
	void
	process_awaiting_frame()
	{
		assert(!connection.async_error);
		awaiting_frame_is_being_processed = true;
		assert(!async_fence_size);
		assert(frame_is_awaiting == true);
		assert(connection.strand.running_in_this_thread());
		assert(ts_to_process == nullptr);

		assert(connection.read_awaiting_frame_initiated == false);
		assert(connection.on_msg_ready_initiated == false);

		uint_fast32_t const payload_size(synapse::misc::be32toh_from_possibly_misaligned_source(connection.data + 3));

		// if we have an active topic and range into which the connection's socket (peer) may write data...
		if (content_ready == true && content_state == content_state_type::publish) {
			auto & ts(*topic_streams.begin()->second);
			assert(ts.topic && ts.range);
			// ... will use range's file buffer for zero-copy mechanisms
			assert(ts.written_in_range == ts.range->get_written_size());
			if (*connection.data != 3) {
				frame_is_awaiting = awaiting_frame_is_being_processed = false;
				connection.read_awaiting_frame(id, payload_size);
			} else {
				uint_fast32_t const estimated_size_on_disk(database::estimate_size_on_disk(payload_size + 5)); // theoretically one is able to get exact size on disk due to known number of indicies (in some cases)...	TODO refactor the call to this method to leverage this info!!!
				if (estimated_size_on_disk > max_message_size)
					throw ::std::runtime_error("maximim message size exceeds reasonable limits");
				if (ts.written_in_range + estimated_size_on_disk < ts.range->file.buffer_capacity()) {
					data = ts.get_amqp_buffer_begin(
					#ifndef NDEBUG
						Data_size
					#endif
					);
					assert(Data_size >= payload_size);
					frame_is_awaiting = awaiting_frame_is_being_processed = false;
					connection.read_awaiting_frame(id, payload_size);
				} else { // will need to do something
					// if current range is not empty
					assert(ts.written_in_range == ts.range->get_written_size());
					if (ts.written_in_range) {
						assert(ts.range->get_back_msg());
						auto c_(connection.shared_from_this());
						++async_fence_size;
						assert(async_fence_size == 1);
						#ifndef NDEBUG
							::boost::weak_ptr<topic_stream> Weak_ts_ptr(ts.shared_from_this());
						#endif
						ts.topic->async_get_next_range(ts.range, connection.strand.wrap([this, c_, &ts, estimated_size_on_disk, payload_size
							#ifndef NDEBUG
								, Weak_ts_ptr
							#endif
						](range_ptr const & range) noexcept {
							#ifndef NDEBUG
								assert(Weak_ts_ptr.lock());
							#endif
							try {
								if (range && !connection.async_error) {
									assert(connection.strand.running_in_this_thread());
									decrement_async_fence_size();
									assert(range->is_open_ended() == true);
									assert(!range->get_written_size());
									assert(!range->get_back_msg());
									assert(ts.range->is_open_ended() == false);
									ts.written_in_range = 0;
									assert(ts.range != range);
									ts.range = range;
									ts.my_msg.Reset();
									ts.My_next_message.Reset();
									ts.My_back_message.Reset();
									range->ensure_reserved_capacity_no_less_than(estimated_size_on_disk);
									// Note it is either thread fence or write-up range->Async_ensure_reserved_capacity_no_less_than, with callback to continue from. Also the implementation of such an Async method would need to call the registered notifications. It is more robust but less efficient as it will cause extra round-trip and re-addition of 'next message' notifications from various subcsribers. The reason the thread-fence works is because others (topic and resource_checking_async_loop as well as subcsribers) use atomic (back_msg offset) of range variable when getting total size or next message... (and the fact that the modified range is empty, i.e. no messages are present at this point).
									::std::atomic_thread_fence(::std::memory_order_release);
									// begin reading data...
									data = ts.get_amqp_buffer_begin(
									#ifndef NDEBUG
										Data_size
									#endif
									);
									assert(Data_size >= payload_size);
									frame_is_awaiting = awaiting_frame_is_being_processed = false;
									connection.read_awaiting_frame(id, payload_size);
								} else
									throw ::std::runtime_error("asyng_get_next_range from topic in from amqp channel");
							} catch (::std::exception const & e) { connection.put_into_error_state(e); }
						}));
						return;
					} else { // empty range, just re-allocate it's buffers!
						assert(!ts.range->get_written_size());
						assert(!ts.written_in_range);
						assert(!ts.range->get_back_msg());
						assert(ts.range->is_open_ended() == true);
						ts.range->ensure_reserved_capacity_no_less_than(estimated_size_on_disk);
						// Note it is either thread fence or write-up range->Async_ensure_reserved_capacity_no_less_than, with callback to continue from. Also the implementation of such an Async method would need to call the registered notifications. It is more robust but less efficient as it will cause extra round-trip and re-addition of 'next message' notifications from various subcsribers. The reason the thread-fence works is because others (topic and resource_checking_async_loop as well as subcsribers) use atomic (back_msg offset) of range variable when getting total size or next message... (and the fact that the modified range is empty, i.e. no messages are present at this point).
						::std::atomic_thread_fence(::std::memory_order_release);
						data = ts.get_amqp_buffer_begin(
						#ifndef NDEBUG
							Data_size
						#endif
						);
						assert(Data_size >= payload_size);
						frame_is_awaiting = awaiting_frame_is_being_processed = false;
						connection.read_awaiting_frame(id, payload_size);
					}
				}
			}
		} else { // no range's buffer exsits -- use default buffer
			if (payload_size >= connection.max_message_size)
				throw ::std::runtime_error("maximim message size exceeds reasonable limits");
			frame_is_awaiting = awaiting_frame_is_being_processed = false;
			connection.read_awaiting_frame(id, payload_size);
		}
	}

	void
	on_frame(uint_fast32_t const payload_size)
	{
		assert(connection.strand.running_in_this_thread());
		if (*connection.data == 3) { // priority to content body above all else
			assert(!(reinterpret_cast<uintptr_t>(data + 1) % alignof(uint16_t)));
			if (content_ready == true && content_state == content_state_type::publish) {
				if (data[payload_size + 7] == 0xce) {

					auto & ts(*topic_streams.begin()->second);
					assert(ts.topic && ts.range);

					// +16 as in +8 for amqp overhead and +8 for trailing crc/hash in message on wire... TODO encapsulate better in the actual message class/struct (+8 and not +5 for amqp overhead because message_preroll already deducts 3, so to make up for it in total size, using full +8)...
					synapse::database::Message_ptr New_message(data - ts.message_preroll, payload_size + 16 + ts.message_preroll);

					typename ::std::remove_reference<decltype(*ts.range.get())>::type::scoped_atomic_bool msg_app_racing_cond(ts.range.get());

					// stamp message sequence number (server-wide)
					#if DP_ENDIANNESS != LITTLE
						#error current code only supports LITTLE_ENDIAN archs. -- extending to BIG_ENDIAN is trivial but there wasnt enough time at this stage...
					#endif
					// stamp our index for the message (timestamp)
					auto const Timestamp(!begin_timestamp ? synapse::misc::cached_time::get() : begin_timestamp);
					New_message.Set_index(1, Timestamp);
					assert(New_message.Get_index(1) == Timestamp);
					auto const prev_timestamp(ts.topic->Meta_Topic_Shim.mt->Latest_Timestamp.load(::std::memory_order_relaxed));
					if (Timestamp >= prev_timestamp) { 
						auto const Index_Gap(Timestamp - prev_timestamp);
						if (ts.topic->indicies[0]->Max_indexable_gap < Index_Gap && prev_timestamp) {
							assert(ts.topic);
							assert(ts.topic->Meta_Topic_Shim.mt);
							synapse::misc::log("WARNING (=" + connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + ") published message yields indexable gap greater than default maximum allowed. Incoming(=" + ::std::to_string(Index_Gap) + ") Default allowed(=" + ::std::to_string(ts.topic->indicies[0]->Max_indexable_gap) + ")\n");
							auto const One_Shot_Allowance(ts.topic->Meta_Topic_Shim.mt->Maximum_Indexable_Gaps[0].load(::std::memory_order_relaxed));
							if (One_Shot_Allowance > Index_Gap) {
								synapse::misc::log("WARNING (=" + connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + ") published message of overly large indexable gap is allowed in a one-shot fashion. Incoming(=" + ::std::to_string(Index_Gap) + ") One-shot allowance(=" + ::std::to_string(One_Shot_Allowance) + ")\n", true);
								ts.topic->Meta_Topic_Shim.mt->Maximum_Indexable_Gaps[0].store(0, ::std::memory_order_relaxed);
							} else
								throw ::std::runtime_error("amqp channel: on frame with published content -- timestamp is too far into the future with respect to the allowed maximum indexable gap, can't proceed");
						}
					} else
						throw ::std::runtime_error("amqp channel: on frame with published content -- timestamp yields out of order semantics, can't proceed");

					auto const Sequence_Number(!preset_msg_seq_no ? database::Maximum_sequence_number.fetch_add(1u, ::std::memory_order_acq_rel) : preset_msg_seq_no);
					New_message.Set_index(0, Sequence_Number);
					assert(New_message.Get_index(0) == Sequence_Number);
					auto const Topic_maximum_sequence_number(ts.topic->Meta_Topic_Shim.mt->Maximum_Sequence_Number.load(::std::memory_order_relaxed));
					if (Sequence_Number <= Topic_maximum_sequence_number)
						throw ::std::runtime_error("amqp channel: on frame with published content -- sequence_number yields out of order semantics, can't proceed");


					ts.topic->Meta_Topic_Shim.mt->Latest_Timestamp.store(Timestamp, ::std::memory_order_relaxed);

					ts.topic->Meta_Topic_Shim.mt->Maximum_Sequence_Number.store(Sequence_Number, ::std::memory_order_relaxed);

					uint_fast32_t const bytes_written(New_message.Get_size_on_disk());

					assert(ts.range);

					// TODO pedantically speaking this could be done later if async hash is being calculated... (minor future todo really)
					ts.written_in_range += bytes_written;

					amqp_0_9_1::total_read_messages.fetch_add(1u, ::std::memory_order_relaxed);
					amqp_0_9_1::total_read_payload.fetch_add(payload_size, ::std::memory_order_relaxed);

					if (bytes_written < 1024 * 128) {
						New_message.Set_hash(New_message.Calculate_hash());
						New_message.Clear_verified_hash_status();
						ts.range->message_appended(New_message, msg_app_racing_cond); // todo -- analysis -- atomic access
						assert(ts.written_in_range == ts.range->get_written_size());
					} else {
						auto c_(connection.shared_from_this());
						++async_fence_size;
						New_message.Async_calculate_hash(connection.strand.wrap([this, c_, New_message, msg_app_racing_cond](uint32_t calculated_hash) mutable {
							try {
								if (!connection.async_error) {
									assert(connection.strand.running_in_this_thread());
									decrement_async_fence_size();
									New_message.Set_hash(calculated_hash);
									New_message.Clear_verified_hash_status();

									assert(New_message.Get_payload()[New_message.Get_payload_size() - 1] == 0xce); // end octet

									topic_streams.begin()->second->range->message_appended(New_message, msg_app_racing_cond); // todo -- analysis -- atomic access

									assert(New_message.Get_payload()[New_message.Get_payload_size() - 1] == 0xce); // end octet

									assert(topic_streams.begin()->second->written_in_range == topic_streams.begin()->second->range->get_written_size());
									if (!async_fence_size && frame_is_awaiting == true && awaiting_frame_is_being_processed == false) {
										connection.strand.post([this, c_](){
											assert(connection.strand.running_in_this_thread());
											try {
												if (!connection.async_error && !async_fence_size && frame_is_awaiting == true && awaiting_frame_is_being_processed == false)
													process_awaiting_frame();
												else
													throw ::std::runtime_error("amqp channel: posting to process_awaiting_frame but the connection is already in error");
											} catch (::std::exception const & e) { connection.put_into_error_state(e); }
										});
									}
								} else 
									throw ::std::runtime_error("Async_calculate_hash in producer came back but the connection is already in error");
							} catch (::std::exception const & e) { connection.put_into_error_state(e); }
						}));
					}
				} else
					throw bam::ProtocolException("body frame is corrupt");
			} else
				throw bam::ProtocolException("body frame is not expected");
		} else if (*connection.data < 3) {
			// somewhat ugly (but coding-time saving) hack -- will be overwritten by 'on_basic_content_header' method to 'true' if everything is ok...
			content_ready = false;

			auto const data_size(payload_size + 8);
			if (data_size < 12)
				throw bam::ProtocolException("frame is too small");
			uint16_t const class_id(synapse::misc::be16toh_from_possibly_misaligned_source(connection.data + 7));
			// fast-path decoding for basic_publish and basic_content_header frames (AMQP tends to send those on every payload)
			if (class_id == 60) {
				switch (*connection.data) {
					case 1 :
						// method_id
						if (synapse::misc::be16toh_from_possibly_misaligned_source(connection.data + 9) == 40) {
							on_basic_publish(connection.data, data_size);
							return;
						} else
							break;
					case 2:
						on_basic_content_header(connection.data, data_size);
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
							throw bam::ProtocolException("channel id for connection frames ought to be zero");
						switch (frm.methodID) {
							case 11 : on_connection_start_ok(frm); break;
							case 31 : on_connection_tune_ok(frm); break;
							case 40 : on_connection_open(frm); break;
							case 50 : on_connection_close(); break;
						}
						break;
					case 20 : // channel
						if (!get_id()) // todo -- more elegant handlig (e.g. send out of order frame reply back to peer)
							throw bam::ProtocolException("channel id for channel-management frames ought not be zero");
						switch (frm.methodID) {
							case 10 : on_channel_open(); break;
							case 40 : on_channel_close(); break;
						}
						break;
					case 40 : // exchange
						if (!get_id()) // todo -- more elegant handlig (e.g. send out of order frame reply back to peer)
							throw bam::ProtocolException("channel id for exchange-management frames ought not be zero");
						switch (frm.methodID) {
							case 10 : on_exchange_declare(frm); break;
						}
						break;
					case 50: // queue
						if (!get_id()) // todo -- more elegant handlig (e.g. send out of order frame reply back to peer)
							throw bam::ProtocolException("channel id for queue-management frames ought not be zero");
						switch (frm.methodID) {
							case 10 : on_queue_declare(frm); break;
							case 20 : on_queue_bind(frm); break;
							case 50 : On_queue_unbind(frm); break;
						}
						break;
					case 60 : // basic
						if (!get_id()) // todo -- more elegant handlig (e.g. send out of order frame reply back to peer)
							throw bam::ProtocolException("channel id for basic-class frames ought not be zero");
						switch (frm.methodID) {
							case 20 : on_basic_consume(frm); break;
							case 30 : on_basic_cancel(frm); break;
							case 90 : On_basic_reject(frm); break;
						}
						break;
				}
				return;
			}
			// need to call on_consumer_notify if mode is subscribe... (previous attempt could have bailed early due to 'pending frame' scenario)
			if (content_state == content_state_type::subscribe)
				on_consumer_notify();
		}
	}

	void
	on_connection_start_ok(bam::ReceivedFrame & frm)
	{
		// TODO -- must handle more proper protocol-related relpies (e.g. MUST clauses in the specs)
		if (connection.state >= Connection::state_type::started)
			throw bam::ProtocolException("out of order startok frame");
		bam::ConnectionStartOKFrame start_ok_frame(frm);

		bool sasl_verified(false);
		if (start_ok_frame.response().empty() == false) {
			unsigned remaining(start_ok_frame.response().size());
			char const * sasl(start_ok_frame.response().data());
			if (start_ok_frame.mechanism() == "PLAIN") {
				if (remaining > 2) {
					if (!*sasl) {
						++sasl; // position on username
						--remaining;
						unsigned len(::strnlen(sasl, remaining));
						if (len && len < --remaining) { // -- because of null preceeding password
							::std::string username(sasl++, len); // ++ because of null trailing username
							remaining -= len;
							sasl += len; // position on password
							len = ::strnlen(sasl, remaining);
							if (len == remaining) {
								::std::string password(sasl, len);
								// todo: here verify username and password
								connection.data_processors::synapse::asio::Any_Connection_Type::Set_User_Name_And_Password(username, password);
								synapse::misc::log("received PLAIN login (=" + connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + ")\n", true);
								sasl_verified = true;
							}
						}
					} 
				}
				if (sasl_verified == false)
					throw bam::ProtocolException("PLAIN sasl should have valid username and password");
			} else if (start_ok_frame.mechanism() == "AMQPLAIN") { // TODO -- DEPRECATE THIS -- a quick hack for the time being to enable quick testing with PHP-AMQPLIB which is determined to use old AMQP v0.8 PLAIN login mechanism
				if (remaining > 25) {
					if (*sasl++ == 5) {
						if (!::strncmp("LOGIN", sasl, 5)) {
							if (*(sasl += 5) == 'S') { // a quick hack... only long strings are supported atm... (the whole AMQP 0.8 should be deprecated anyway)
								unsigned username_size;
								::memcpy(&username_size, ++sasl, 4);
								username_size = be32toh(username_size);
								remaining -= 11; // processed up to the username begin
								if (username_size < remaining) {
									::std::string const username(sasl += 4, username_size);
									sasl += username_size;
									remaining -= username_size;
									if (remaining > 14) {
										if (*sasl++ == 8) {
											if (!::strncmp("PASSWORD", sasl, 8)) {
												if (*(sasl += 8) == 'S') { // a quick hack... only long strings are supported atm... (the whole AMQP 0.8 should be deprecated anyway)
													unsigned password_size;
													::memcpy(&password_size, ++sasl, 4);
													password_size = be32toh(password_size);
													remaining -= 14; // processed up to the password begin
													if (password_size == remaining) {
														::std::string const password(sasl += 4, password_size);
														// todo: here verify username and password
														sasl_verified = true;
														connection.data_processors::synapse::asio::Any_Connection_Type::Set_User_Name_And_Password(username, password);
														synapse::misc::log("received AMQPLAIN (naughty, naughty) login (=" + connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + ")\n", true);
													}
												}
											}
										}
									}
								}
							}
						} 
					}
				}
				if (sasl_verified == false)
					throw bam::ProtocolException("AMQPLAIN sasl should have valid username and password");
			}
		}

		if (sasl_verified == false)
			throw bam::ProtocolException("only supporting valid PLAIN and AMQPLAIN sasl at the moment instead got(=" + start_ok_frame.mechanism() + ')');

		if (start_ok_frame.locale() != "en_US")
			throw bam::ProtocolException("only supporting en_US locale at the moment");

		// client supports what I want... now start tuning the connection...
		connection.state = Connection::state_type::started;
		++async_fence_size;
		connection.dispatch([this](){
			write_frame(bam::ConnectionTuneFrame(max_supported_channels_end - 1, Maximum_amqp_frame_size, connection.heartbeat_frame_rate));
		}, true, 1024);
	}

	uint_fast32_t constexpr static Maximum_amqp_frame_size{synapse::database::estimate_max_payload_size()
		//  Theoretically, if just reading amqp 0.9.1 docs, need to subtract -5 for extra storage of payload size and end byte variables that we store on disk also (i.e. if client calculates that max-frame size means amqp payload only)
		// However some info (rabbitmq etal) appears to suggest that max-frame size implies total frame size (inclusive of header variables)... meaning that clients should honour this in their calculation and reduce the size of the payload... but this may not be something that client libs will do properly. So we want to account for this at the server side in the 'defensive' approach type of a manner (meaning -8 (type, channel, payload size, ending octet)
		// Note, keeping in mind that the above value should be in multiple of 8 due to alignment needs we have... so if such is changed later on, then we would need to jump to 16 or more :)
		- 8
	};

	void
	on_connection_tune_ok(bam::ReceivedFrame & frm)
	{
		synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " on tune ok\n");
		if (connection.state < Connection::state_type::started || connection.state >= Connection::state_type::tuned)
			throw bam::ProtocolException("out of order tuneok frame");
		bam::ConnectionTuneOKFrame tune_ok_frame(frm);
		if (tune_ok_frame.channels() >= max_supported_channels_end)
			throw ::std::runtime_error("max number of channels requested by peer " + ::std::to_string(tune_ok_frame.channels()) + " is gerater than maximum allowed " + ::std::to_string(max_supported_channels_end - 1));
		else if (tune_ok_frame.frameMax() != Maximum_amqp_frame_size)
			throw bam::ProtocolException("yet unimplemented tune configuration: peer must match our max frame size (we do not currently support message fragmentation)");

		connection.state = Connection::state_type::tuned;

		connection.heartbeat_timer.cancel();
		connection.heartbeat_frame_rate = tune_ok_frame.heartbeat();
		synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " heartbeat(=" + ::std::to_string(connection.heartbeat_frame_rate) + ")\n");
		if (!connection.heartbeat_frame_rate)
			throw ::std::runtime_error("Zero heartbeats are no longer supported (security and stability requirement).");
		else if (connection.heartbeat_frame_rate > connection.Maximum_heartbeat_frame_rate) // Todo: should be max enough(?)
			throw ::std::runtime_error("Heartbeats are too long (> connection.Maximum_heartbeat_frame_rate) for security and stability requirement.");
		connection.dispatch_heartbeat_timer();
	}

	void
	on_connection_open(bam::ReceivedFrame & frm)
	{
		if (connection.state < Connection::state_type::tuned || connection.state >= Connection::state_type::open)
			throw bam::ProtocolException("out of order connection-open frame");
		bam::ConnectionOpenFrame open_frame(frm);
		// for the time being, vhost is hard coded...
		if (open_frame.vhost() != "/")
			throw bam::ProtocolException("yet unimplemented vhost path");

		// we are ok with opening the connection
		connection.state = Connection::state_type::open;
		++async_fence_size;
		connection.dispatch([this](){
			write_frame(bam::ConnectionOpenOKFrame());
			connection.snd_heartbeat_flag = true; // a little hack for non-conforming peers (AMQP standard says that any octet data sent to peer is a valid replacement of heartbeat semantics): in case receiving peer dismisses octet data recevied prior to this frame as valid heartbeat-replacement(s)
		}, true, 1024);
	}

	void
	on_connection_close()
	{
		if (connection.state < Connection::state_type::tuned || state != state_type::none)
			throw bam::ProtocolException("out of order connection-close frame");
		connection.state = Connection::state_type::protocol_established;
		++async_fence_size;
		connection.dispatch([this](){
			write_frame(bam::ConnectionCloseOKFrame());
		}, true, 1024);
	}

	void
	on_channel_open()
	{
		synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " Opening channel (id=" + ::std::to_string(id) + ")\n", true);
		if (connection.state < Connection::state_type::open || state >= state_type::open)
			throw bam::ProtocolException("out of order channel-open frame");
		state = state_type::open;
		++async_fence_size;
		connection.dispatch([this](){
			write_frame(bam::ChannelOpenOKFrame(get_id()));
			connection.snd_heartbeat_flag = true; // a little hack for non-conforming peers (AMQP standard says that any octet data sent to peer is a valid replacement of heartbeat semantics): in case receiving peer dismisses octet data recevied prior to this frame as valid heartbeat-replacement(s)
		}, true, 1024);
	}

	void
	on_channel_close()
	{
		synapse::misc::log(::std::string(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " Closing channel (id=") + ::std::to_string(id) + ")\n", true);
		if (state != state_type::open)
			throw bam::ProtocolException("out of order channel-close frame");
		assert(connection.strand.running_in_this_thread());
		clear_topic_streams(false);
		state = state_type::none;
		++async_fence_size;
		connection.dispatch([this](){
			write_frame(bam::ChannelCloseOKFrame(get_id()));
		}, true, 1024);
	}

	void
	on_queue_declare(bam::ReceivedFrame & frm)
	{
		if (connection.state < Connection::state_type::open || state < state_type::open)
			throw bam::ProtocolException("out of order queue-declare frame");
		bam::QueueDeclareFrame queue_declare_frm(frm);
		auto const Requested_queue(queue_declare_frm.name());
		if (Requested_queue.empty() == false && Requested_queue.compare("/")) {
			key = Requested_queue;
			synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " on_queue_declare. set key and queue (=" + Requested_queue + ")\n", true);
		}
		// dummy reply
		++async_fence_size;
		connection.dispatch([this, Requested_queue](){
			write_frame(bam::QueueDeclareOKFrame(get_id(), Requested_queue, 0, 0));
		}, true, 1024);
	}

	void
	on_exchange_declare(bam::ReceivedFrame & frm)
	{
		if (connection.state < Connection::state_type::open || state < state_type::open)
			throw bam::ProtocolException("out of order exchange-declare frame");
		bam::ExchangeDeclareFrame exchange_declare_frm(frm);
		if (exchange_declare_frm.name().empty() == false)
			exchange = exchange_declare_frm.name();
		synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " exchange declared(=" + exchange + ")\n", true);
		// dummy reply
		++async_fence_size;
		connection.dispatch([this](){
			write_frame(bam::ExchangeDeclareOKFrame(get_id()));
		}, true, 1024);
	}

	uint_fast32_t last_basic_publish_frame_size{0};
	void
	on_basic_publish(char unsigned * const data, uint_fast32_t const data_size)
	{
		if (connection.state < Connection::state_type::open || state < state_type::open)
			throw bam::ProtocolException("out of order basic publish frame");
		else if (data_size < 14u || data[data_size - 1] != 0xce)
			throw bam::ProtocolException("basic_publish_frame is corrupt: cant read size of exchange string, or end-octet is incorrect");
#if 0
		skipping exchange info for publishing for the time being...
		bam::BasicPublishFrame pub_frm(frm);
		if (pub_frm.exchange().empty() == false)
			exchange = pub_frm.exchange();
		if (exchange.empty() == false && exchange != "amq.topic" && exchange != "amq.direct"
			&& exchange != "router" // TODO -- more thinking about this, a concession to the quick-hack for the php-amqplib
		)
			throw bam::ProtocolException("default/nameless or 'amq.topic' or 'amq.direct' exchanges only are supported at the moment (non compliant for the time being)");
#endif
		uint_fast32_t routing_key_size_offest(14u + data[13]);
		if (data_size < routing_key_size_offest + 3)
			throw bam::ProtocolException("basic_publish_frame is too small, cant contain valid routing key");
		uint_fast8_t const routing_key_size(data[routing_key_size_offest]);
		if (routing_key_size) {
			if (++routing_key_size_offest + routing_key_size >= data_size)
				throw bam::ProtocolException("basic_publish_frame is too small, routing key size and 2 bits implies overrunning the buffer");
			char * const routing_key_begin(reinterpret_cast<char *>(data + routing_key_size_offest));
			if (key.compare(0, ::std::string::npos, routing_key_begin, routing_key_size)) { // keys are different or new...
				if (key.empty() == false && content_state == content_state_type::publish)
					throw bam::ProtocolException("currently publishing to different keys on the same channel is unsupported");
				else {
					key.assign(routing_key_begin, routing_key_size);
					if (key.find("*") != ::std::string::npos || key.find("#") != ::std::string::npos)
						throw bam::ProtocolException("publisher may not use wildcards in the routing key");
					synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " publisher routing key set(=" + key + ")\n", true);
				}
			}
		}
		if (content_state != content_state_type::publish) {
			if (key.empty() == true)
				throw bam::ProtocolException("need to set routing key (topic) before publishing data");
			connection.Publisher_Sticky_Mode.store(data[data_size - 2], ::std::memory_order_relaxed);
			assert(connection.strand.running_in_this_thread());
			clear_topic_streams(false);
			// load topic_stream...
			::boost::weak_ptr<Connection> weak_c_(connection.shared_from_this());
			++async_fence_size;
			// TODO -- refactor topic.h::get_topic... since publisher does not need to see meta_topic stuff... (although may need to be careful as a number of pending requests may well be comprised from a mixture of subscribers and publisher)
			amqp_0_9_1::topics->async_get_topic_for_publisher(key, connection.strand.wrap([this, weak_c_](meta_topic_ptr const & mt, topic_ptr const & topic) mutable {
				auto && c_(weak_c_.lock());
				if (c_) {
					try {
						if (!connection.async_error && mt) {
							assert(topic);
							// request range...
							topic->async_get_first_range_for_connected_publisher(connection.strand.wrap([this, c_, topic](range_ptr const & range){
								try {
									if (!connection.async_error && range) {
										// TODO -- deal with the fact that connection/channel may change their state by the time this async call get to run...!!!
										assert(connection.strand.running_in_this_thread());
										assert(topic_streams.empty() == true);
										topic_streams.emplace(key, connection.sp.template make_shared<topic_stream>(topic, range));
										// This, alternative, is not yet possible -- later on topic_stream is passed around as weak_ptr thereby still retaining scope of control-block when connection object is long gone... a todo for future considerations.
										//topic_streams.emplace(key, ::boost::allocate_shared<topic_stream>(data_processors::misc::alloc::Basic_alloc_2<topic_stream>(connection.sp), topic, range));
										// begin reading data...
										content_state = content_state_type::publish;
										Decrement_async_fence_size_and_process_awaiting_frame_if_any();
									} else
										throw ::std::runtime_error("async_get_first_range_for_connected_publisher from topic in from amqp channel");
								} catch (::std::exception const & e) { connection.put_into_error_state(e); }
							}),
							[this, weak_c_(::std::move(weak_c_))](bool Throttle) mutable {
								auto && c_(weak_c_.lock());
								if (c_) 
									if (Throttle) {
										#ifdef TEST_SYNAPSE_INDEX_MAX_KEY_UPDATE_REQUESTS_THROTTLING_WATERMARK_1
											synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " publisher throttling ON due to index notification\n");
										#endif
										// Todo more elegant with repsect to when called on already connections strand (from index -- on pre-max_key_update call!)
										c_->strand.dispatch([this, c_]() {
											++async_fence_size;
										});
									} else {
										#ifdef TEST_SYNAPSE_INDEX_MAX_KEY_UPDATE_REQUESTS_THROTTLING_WATERMARK_1
											synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " publisher throttling OFF due to index notification\n");
										#endif
										c_->strand.post([this, c_]() {
											try {
												if (!connection.async_error)
													Decrement_async_fence_size_and_process_awaiting_frame_if_any();
											} catch (::std::exception const & e) { connection.put_into_error_state(e); }
										});
									}
							});
						} else
							throw ::std::runtime_error("asyng_get_topic_for_publisher from amqp channel");
					} catch (::std::exception const & e) { connection.put_into_error_state(e); }
				}
			}), weak_c_);
			// todo, a bit more elegance please -- got to do with asinc_get_topic either not dispatching immediately in its implementation (and then preventing possibly double-posting cases in the implementation of topic.h), or re-checking the async_error field here...
			if (connection.async_error)
				throw ::std::runtime_error("asyng_get_topic_for_publisher, direct dispatch, from amqp channel");
		}
		last_basic_publish_frame_size = data_size;
	}

	void
	on_queue_bind(bam::ReceivedFrame & frm)
	{
		if (connection.state < Connection::state_type::open || state < state_type::open)
			throw bam::ProtocolException("out of order queue bind frame");
		bam::QueueBindFrame bind_frm(frm);
		exchange = bind_frm.exchange();
		auto const Requested_queue(bind_frm.name());

		if (exchange.empty() == false && exchange != "amq.topic" && exchange != "amq.direct"
			&& exchange != "router" // TODO -- more thinking about this, a concession to the quick-hack for the php-amqplib
		)
			throw bam::ProtocolException("on_queue_bind default/nameless or 'amq.topic' or 'amq.direct' exchanges only are supported at the moment (non compliant for the time being)");
		else if (bind_frm.routingKey().empty()) 
			throw bam::ProtocolException("on_queue_bind empty routing_key supplied");
		else if (Requested_queue.empty() == false && Requested_queue.compare("/"))
			throw ::std::runtime_error("on_queue_bind can only have either empty queue or '/'. Requested queue(=" + Requested_queue + ')');

		++async_fence_size;

		auto const & Subscription_properties(bind_frm.arguments().get_fields());
		auto const & Transient_key(bind_frm.routingKey());
		assert(!Transient_key.empty());
		if (Subscription_properties.find("Sparsify_only") == Subscription_properties.end()) {
			key = Transient_key;
			if (content_state == content_state_type::subscribe) {
				Process_subscription(Subscription_properties, [this](){
					return bam::QueueBindOKFrame(get_id());
				});
			} else {
				synapse::misc::log(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " on_queue_bind, routing key from consumer set(=" + key + ")\n", true);
				connection.dispatch([this](){
					write_frame(bam::QueueBindOKFrame(get_id()));
				}, true, 1024);
			}
		} else {
			::std::string Log_text(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " on_queue_bind with Sparsify_only");
			Process_subscription_sparsification_properties(Subscription_properties, Log_text);
			synapse::misc::log(::std::move(Log_text += '\n'), true);
			connection.dispatch([this](){
				write_frame<&channel::On_dont_stream_subscription_processed>(bam::QueueBindOKFrame(get_id()));
			}, true, 1024);
		}
	}

	void
	On_queue_unbind(bam::ReceivedFrame & frm)
	{
		assert(connection.strand.running_in_this_thread());
		if (content_state == content_state_type::subscribe) {
			bam::QueueUnbindFrame unbind_frm(frm);
			auto const & key(unbind_frm.routingKey());
			#ifdef LAZY_LOAD_LOG_1	
				synapse::misc::log("LAZY_LOAD_LOG ON_BASIC_UNSUBSCRIBE " + key + '\n', true);
			#endif

			Erase_topic_streams_by_routing_key(key);

			// todo -- more validation and various replies if was/wasnt erased
			assert(ts_to_process == nullptr);
			++async_fence_size;
			connection.dispatch([this](){
				write_frame<&channel::On_cancelled_subscription>(bam::QueueUnbindOKFrame(get_id()));
			}, true, 1024);
		}
	}

	template <typename CallbackType, typename On_Registered_Listener_For_New_Topics_Callback = ::std::nullptr_t, typename On_New_Topic_Callback = ::std::nullptr_t>
	void
	async_topic_stream_loading_helper(::std::string const & key, decltype(begin_timestamp) begin_timestamp, CallbackType && callback, meta_topic_ptr const & mt, decltype(begin_timestamp) mt_begin_timestamp, decltype(end_timestamp) mt_end_timestamp, uint_fast64_t mt_begin_msg_seq_no, Streaming_activation_attributes const & How_to_activate, uint_fast64_t Skipping_period, bool Skip_payload, On_Registered_Listener_For_New_Topics_Callback && On_Registered_Listener_For_New_Topics = nullptr,  On_New_Topic_Callback && On_New_Topic = nullptr)
	{
		assert(!key.empty());
		assert(connection.strand.running_in_this_thread());

		auto subscribe_from(::std::max(mt_begin_timestamp, begin_timestamp));

		if (mt_begin_timestamp // proactive protection against too far in the past (cannot retire topics unless mt_begin_timestamp is validly known because 'subscribe_from' may be waaay too early, with actual data starting later on...)
			&& max_sent_out_timestamp > subscribe_from && max_sent_out_timestamp - subscribe_from > delayed_delivery_tolerance
		) {
			#ifdef LAZY_LOAD_LOG_1	
				synapse::misc::log("LAZY_LOAD_LOG RETIRING " + key + ", right away in stream loading helper, max_sent_out " + ::std::to_string(max_sent_out_timestamp) + ", adjusted subscribe_from " + ::std::to_string(subscribe_from) + '\n', true);
			#endif
			connection.strand.post([this, callback(::std::forward<CallbackType>(callback))]() noexcept { // callback is expected to keep the scope (c_).
				if (!connection.async_error)
					try {
						callback(topic_streams.end());
					} catch (::std::exception const & e) { connection.put_into_error_state(e); }
			});
		} else {
			// early double-loading protection
			if (topic_streams.find(key) == topic_streams.end() && pending_meta_topic_names.find(key) == pending_meta_topic_names.end()) {

				// NOTE if there are no topics currently in topic_streams then we must get at least one (even if it is too far into future)... because if it ends up being the only topic subscribed-to (e.g. even in case of 'wildcard' matching) and we will filter it out due to 'too far into the future' semantics, then there won't be anything in the subscribing loop and the channel won't get anywhere at all...
				// TODO all these 0 literals are hard-coded indicies (need to parameterize later on)
				amqp_0_9_1::topics->async_get_topic_for_subscriber(0, topic_streams.empty() == true ? -1 : mt_begin_timestamp ? mt_begin_timestamp : lazy_load_timestamp + too_futuristic_watermark, key, 
					connection.strand.wrap([this, key, callback, subscribe_from, mt_end_timestamp, mt_begin_msg_seq_no, How_to_activate, Skipping_period, Skip_payload](meta_topic_ptr const & mt, topic_ptr const & topic) mutable {
						assert(connection.strand.running_in_this_thread());
						try {
							if (!connection.async_error && mt) {
								auto const mt_begin_timestamp(mt->indicies_begin[0].load(::std::memory_order_relaxed));
								auto const subcribe_until(mt_end_timestamp ? mt_end_timestamp : end_timestamp);
								// actual double-loading protection
								if (topic_streams.find(key) == topic_streams.end() && pending_meta_topic_names.find(key) == pending_meta_topic_names.end()) {
									if (topic) {
										assert(mt);
										if (How_to_activate == Streaming_activation_attributes::Allow_preroll_if_begin_timestamp_in_future) {
											auto const topic_latest_timestamp(topic->Meta_Topic_Shim.mt->Latest_Timestamp.load(::std::memory_order_relaxed));
											if (topic_latest_timestamp < subscribe_from) {
												subscribe_from = topic_latest_timestamp;
												assert(subscribe_from >= mt_begin_timestamp);
												mt_begin_msg_seq_no = topic->Meta_Topic_Shim.mt->Maximum_Sequence_Number.load(::std::memory_order_relaxed);
											}
										}
										// request range...
										topic->async_get_range(0, subscribe_from, connection.strand.wrap([this, key, callback(::std::move(callback)), mt, topic, subscribe_from, subcribe_until, mt_begin_timestamp, mt_begin_msg_seq_no, How_to_activate, Skipping_period, Skip_payload](range_ptr const & range) mutable {
											try {
												if (!connection.async_error && range) {

													uint_fast64_t mt_timestamp;
													if (How_to_activate != Streaming_activation_attributes::Pick_up_exactly_from_specified_starting_point && How_to_activate != Streaming_activation_attributes::Streaming_started && subscribe_from < mt_begin_timestamp)
														mt_timestamp = mt_begin_timestamp;
													else
														mt_timestamp = subscribe_from;

													// proactive protection against too far in the past
													if (mt_begin_timestamp // proactive protection against too far in the past (cannot retire topics unless mt_begin_timestamp is validly known because 'subscribe_from' may be waaay too early, with actual data starting later on...)
														&& max_sent_out_timestamp > mt_timestamp && max_sent_out_timestamp - mt_timestamp > delayed_delivery_tolerance
													) {
														#ifdef LAZY_LOAD_LOG_1	
															synapse::misc::log("LAZY_LOAD_LOG RETIRING " + key + ", stream loading helper upon getting valid topic from topic->async_get_range, max_sent_out " + ::std::to_string(max_sent_out_timestamp) + ", adjusted from timestamp " + ::std::to_string(mt_timestamp) + ", index begin timestamp " + ::std::to_string(mt_begin_timestamp) + '\n', true);
														#endif
														callback(topic_streams.end());
													} else {
														auto callback_rv(topic_streams.emplace(key, connection.sp.template make_shared<topic_stream>(topic, range, get_id(), exchange, key, Consumer_tag, supply_metadata, mt_timestamp, subcribe_until, mt_begin_msg_seq_no, How_to_activate, Skipping_period, Skip_payload)).first);
														// This, alternative, is not yet possible -- later on topic_stream is passed around as weak_ptr thereby still retaining scope of control-block when connection object is long gone... a todo for future considerations.
														//auto callback_rv(topic_streams.emplace(key, ::boost::allocate_shared<topic_stream>(data_processors::misc::alloc::Basic_alloc_2<topic_stream>(connection.sp), topic, range, get_id(), exchange, key, Consumer_tag, supply_metadata, mt_timestamp, subcribe_until, mt_begin_msg_seq_no)).first);
														#ifdef LAZY_LOAD_LOG_1	
															synapse::misc::log("LAZY_LOAD_LOG LOADED ACTIVE " + key + ", topic start " + ::std::to_string(mt_begin_timestamp) + '\n', true);
														#endif
														if (mt_timestamp < lazy_load_timestamp || !lazy_load_timestamp)
															lazy_load_timestamp = mt_timestamp;
														callback(callback_rv);
													}
												} else
													throw ::std::runtime_error("async_get_range from topic in from amqp channel");
											} catch (::std::exception const & e) { connection.put_into_error_state(e); }
										}));
									} else {
										assert(subscribe_from);
										auto Added_pending_meta_topic(pending_meta_topics.emplace(::std::piecewise_construct,
											::std::forward_as_tuple(!mt_begin_msg_seq_no ? ::std::max(mt_begin_timestamp, subscribe_from) : subscribe_from), 
											::std::forward_as_tuple(subcribe_until, key, mt, mt_begin_msg_seq_no, How_to_activate, Skipping_period, Skip_payload))
										);
										pending_meta_topic_names.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Added_pending_meta_topic->second.Key), ::std::forward_as_tuple(Added_pending_meta_topic));
										#ifdef LAZY_LOAD_LOG_1	
											synapse::misc::log("LAZY_LOAD_LOG LOADED POSTPONED " + key + '\n', true);
										#endif
										callback(topic_streams.end());
									}
								} else
									callback(topic_streams.end());
							} else
								throw ::std::runtime_error("async_get_topic_for_subscriber from amqp channel");
						} catch (::std::exception const & e) { connection.put_into_error_state(e); }
					}), mt, ::std::forward<On_Registered_Listener_For_New_Topics_Callback>(On_Registered_Listener_For_New_Topics), ::std::forward<On_New_Topic_Callback>(On_New_Topic)
				);
				// todo, a bit more elegance please -- got to do with asinc_get_topic either not dispatching immediately in its implementation (and then preventing possibly double-posting cases in the implementation of topic.h), or re-checking the async_error field here...
				if (connection.async_error)
					throw ::std::runtime_error("asyng_get_topic_for_subscriber, direct dispatch, from amqp channel");
			} else
				callback(topic_streams.end());
		}
	}

	template <typename T> 
	void Process_subscription_sparsification_properties(T const & props, ::std::string & Log_text) {
		assert(!key.empty());
		auto const Consumption_limit_property(props.find("Target_data_consumption_limit"));
		uint_fast64_t Consumption_limit(0);
		if (Consumption_limit_property != props.end()) {
			Consumption_limit = static_cast<uint64_t>(*Consumption_limit_property->second);
			Log_text += " Target_data_consumption_limit(=" + ::std::to_string(Consumption_limit) + ')';
		}
		auto const Timestamp_property(props.find("Sparsify_upto_timestamp"));
		uint_fast64_t Timestamp(0);
		if (Timestamp_property != props.end()) {
			Timestamp = static_cast<uint64_t>(*Timestamp_property->second);
			Log_text += " Sparsify_upto_timestamp(=" + ::std::to_string(Timestamp) + ')';
		}
		if (Consumption_limit || Timestamp) {
			if (key.find("*") != ::std::string::npos || key.find("#") != ::std::string::npos) 
				throw bam::ProtocolException("Subcsriber cannot specify wildcarded topic name with sparsification parameters (currently not supported). Log_text thus far: " + Log_text);
			if (Subscription_To_Missing_Topics_Mode != Create_If_Missing) {
				Subscription_To_Missing_Topics_Mode = Create_If_Missing;
				Log_text += " Subscription_To_Missing_Topics_Mode(=CHANGED to 'Create_If_Missing' because truncation parameters were specified)";
			}
			amqp_0_9_1::topics->Async_set_topic_sparsification_parameters_from_subscriber(key, Consumption_limit, Timestamp, [c_(connection.shared_from_this())](bool Error) {
					if (Error)
						c_->strand.post([c_](){
							if (!c_->async_error)
								c_->put_into_error_state(::std::runtime_error("peer error from Async_set_topic_sparsification_limits_from_subscriber"));
						});
			}); 
		}
		auto const Maximum_Timestamp_Gap_Property(props.find("Maximum_Timestamp_Gap"));
		if (Maximum_Timestamp_Gap_Property != props.end()) {
			uint_fast64_t const Maximum_Timestamp_Gap(static_cast<uint64_t>(*Maximum_Timestamp_Gap_Property->second));
			Log_text += " Maximum_Timestamp_Gap(=" + ::std::to_string(Maximum_Timestamp_Gap) + ')';
			if (key.find("*") != ::std::string::npos || key.find("#") != ::std::string::npos) 
				throw bam::ProtocolException("Subcsriber cannot specify wildcarded topic name with max-gap parameters (currently not supported). Log_text thus far: " + Log_text);
			amqp_0_9_1::topics->Async_Set_Topic_Maximum_Timestamp_Gap(key, Maximum_Timestamp_Gap, [c_(connection.shared_from_this())](bool Error) {
				if (Error)
					c_->strand.post([c_](){
						if (!c_->async_error)
							c_->put_into_error_state(::std::runtime_error("peer error from Async_Set_Topic_Maximum_Timestamp_Gap"));
					});
			}); 
		}
		auto const Purge_Property(props.find("Purge"));
		if (Purge_Property != props.end()) {
			Log_text += " marking topic for deletion (=" + key + ')';
			amqp_0_9_1::topics->Delete_Topic(key); 
		}
	}

	bool Atomic_subscription_cork{false};
	bool Previous_atomic_subscription_cork{false};
	uint_fast32_t Async_subscriber_streaming_fence_size{0};

	bool supply_metadata;
	uint_fast64_t max_sent_out_timestamp{0};
	uint_fast64_t delayed_delivery_tolerance{0};
	uint_fast64_t constexpr static too_futuristic_watermark{static_cast<uint_fast64_t>(1000000u) * 60u * 60u * 24u}; // lazy load topics which are too much into future (in micros)
	uint_fast64_t lazy_load_timestamp{0};
	uint_fast32_t async_lazy_load_topic_streams_size{0}; // how many topics are being asynchronously loaded
	bool lazy_load_fence{false}; // used to prevent furthur sending of existing topic_streams messages until waited-for async-loading meta_topics are loaded as active topic_streams (e.g. in cases when awaited for topic has starting timestamp earlier than currently used reference)
	void
	on_basic_consume(bam::ReceivedFrame & frm)
	{
		bam::BasicConsumeFrame consume_frm(frm);
		if (connection.state < Connection::state_type::open || state < state_type::open)
			throw bam::ProtocolException("out of order basic consume frame");

		auto && Requested_consumer_tag(consume_frm.consumerTag());
		if (content_state == content_state_type::subscribe && Consumer_tag.empty() == false && Consumer_tag != Requested_consumer_tag)
				throw bam::ProtocolException("Currently not supporting different consumers on same channels incoming consumer tag(=" + Requested_consumer_tag + ") != already existing consumer tag(=" + Consumer_tag + ')');

		Consumer_tag = Requested_consumer_tag;

		++async_fence_size;

		auto const & Subscription_properties(consume_frm.filter().get_fields());
		auto const & Transient_key(consume_frm.queueName());
		if (!Transient_key.empty())
			key = Transient_key;
		if (key.empty() == true)
			throw bam::ProtocolException("routing key cannot be empty by the time on_basic_consume is actioned");
		if (Subscription_properties.find("Sparsify_only") == Subscription_properties.end()) {
			Process_subscription(Subscription_properties, [this](){
				return bam::BasicConsumeOKFrame(get_id(), Consumer_tag);
			});
		} else {
			::std::string Log_text(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " on_basic_consume with Sparsify_only");
			Process_subscription_sparsification_properties(Subscription_properties, Log_text);
			synapse::misc::log(::std::move(Log_text += '\n'), true);
			connection.dispatch([this](){
				write_frame<&channel::On_dont_stream_subscription_processed>(bam::BasicConsumeOKFrame(get_id(), Consumer_tag));
			}, true, 1024);
		}

	}

	// Hack, mainly for subscription message being used to control sparsification of the topic.
	void On_dont_stream_subscription_processed() {
		assert(connection.strand.running_in_this_thread());
		++Async_subscriber_streaming_fence_size;
		Uncork_on_end_of_processed_subcsription();
		Decrement_async_fence_size_and_ping_consumer_notify();
	}

	template <typename Subsription_properties_type, typename Message_reply_type>
	void Process_subscription(Subsription_properties_type const & props, Message_reply_type && Message_reply) {
		assert(!key.empty());
		if (key.compare("/")) {
			{
				auto const & found(props.find("begin_timestamp"));
				if (found != props.end()) {
					begin_timestamp = static_cast<decltype(begin_timestamp)>(static_cast<uint64_t>(*found->second));
					if (!begin_timestamp)
						throw bam::ProtocolException("Begin timestamp is supplied from subscribing client as absolute zero (currently unsupported).");
					else if (begin_timestamp > 4611686018427387904)
						throw bam::ProtocolException("Begin timestamp is supplied greater than allowed maximum in current implementation(4611686018427387904).");
				} else
					begin_timestamp = synapse::misc::cached_time::get();
			}
			if (begin_timestamp + too_futuristic_watermark < begin_timestamp)
				throw bam::ProtocolException("begin timestamp and too_futuristic_watermark values are overflowing");

			{
				auto const & found(props.find("preroll_if_begin_timestamp_in_future"));
				if (found != props.end()) 
					preroll_if_begin_timestamp_in_future = true;
				else
					preroll_if_begin_timestamp_in_future = false;
			}

			{
				auto const & found(props.find("end_timestamp"));
				if (found != props.end()) {
					end_timestamp = static_cast<decltype(end_timestamp)>(static_cast<uint64_t>(*found->second));
					if (!end_timestamp)
						throw bam::ProtocolException("End timestamp is supplied from subscribing client as absolute zero (currently unsupported).");
				} else
					end_timestamp = -1;
			}

			{
				auto const & found(props.find("delayed_delivery_tolerance"));
				if (found != props.end()) 
					delayed_delivery_tolerance = static_cast<decltype(delayed_delivery_tolerance)>(static_cast<uint64_t>(*found->second));
				else
					delayed_delivery_tolerance = -1;
			}

			{
				if (props.find("supply_metadata") != props.end()) 
					supply_metadata = true;
				else
					supply_metadata = false;
			}

			{
				auto const & found(props.find("Skipping_period"));
				if (found != props.end()) 
					Skipping_period = static_cast<decltype(Skipping_period)>(static_cast<uint64_t>(*found->second));
				else
					Skipping_period = 0;
			}

			{
				auto const & found(props.find("Skip_payload"));
				if (found != props.end()) 
					Skip_payload = true;
				else
					Skip_payload = false;
			}

			{
				if (props.find("Atomic_subscription_cork") != props.end()) {
					Atomic_subscription_cork = true;
					if (!Previous_atomic_subscription_cork) // Note: not assigning Previous_atomic_subscription_cork here because it is done at the end of the subscribe call handling (in Uncork_on_end_of_processed_subcsription). There is no danger of new/multiple 'atomic corks' coming-in before the previous is handled because 'async_fence_size' keeps all incoming IO (messages) at bay until current processing is done.
						++Async_subscriber_streaming_fence_size;
				} else
					Atomic_subscription_cork = false;
			}

			::std::string Log_text(connection.data_processors::synapse::asio::Any_Connection_Type::Get_Description() + " Accepted subscription event. key(=" + key + ", will consider begin_timestamp(=" + ::std::to_string(begin_timestamp) + ") preroll_if_begin_timestamp_in_future(=" + ::std::to_string(preroll_if_begin_timestamp_in_future) + ") end_timestamp(=" + ::std::to_string(end_timestamp) + ") delayed_delivery_tolerance(=" + ::std::to_string(delayed_delivery_tolerance) + ") supply_metadata(=" + ::std::to_string(supply_metadata) + ") Skipping_period(=" + ::std::to_string(Skipping_period) + ") Skip_payload(=" + ::std::to_string(Skip_payload) + ") Atomic_subscription_cork(=" + ::std::to_string(Atomic_subscription_cork) + ") Tcp_no_delay(=");

			{
				auto const & Missing_Topic_Mode(props.find("Missing_Topic_Mode"));
				if (Missing_Topic_Mode != props.end()) {
					Log_text += " Subscription_To_Missing_Topics_Mode (=";
					auto Type_Id(Missing_Topic_Mode->second->typeID());
					Subscription_To_Missing_Topics_Mode_Enum Mode;
					switch (Type_Id) {
					case 'b':
						Mode = static_cast<Subscription_To_Missing_Topics_Mode_Enum>(static_cast<int8_t>(*Missing_Topic_Mode->second));
					break;
					case 'B':
						Mode = static_cast<Subscription_To_Missing_Topics_Mode_Enum>(static_cast<uint8_t>(*Missing_Topic_Mode->second));
					break;
					case 'I':
						Mode = static_cast<Subscription_To_Missing_Topics_Mode_Enum>(static_cast<int32_t>(*Missing_Topic_Mode->second));
					break;
					default:
						throw ::std::runtime_error("Unsupported Missing_Topic_Mode type encoding(=" + ::std::to_string(Type_Id) + ')');
					}
					switch (Mode) {
					case Error_If_Missing : 
						Subscription_To_Missing_Topics_Mode = Error_If_Missing;
						Log_text += "Error_If_Missing)";
					break;
					case Create_If_Missing : 
						Subscription_To_Missing_Topics_Mode = Create_If_Missing;
						Log_text += "Create_If_Missing)";
					break;
					case Expect_If_Missing : 
						Subscription_To_Missing_Topics_Mode = Expect_If_Missing;
						Log_text += "Expect_If_Missing)";
					break;
					case Ignore_If_Missing : 
						Subscription_To_Missing_Topics_Mode = Ignore_If_Missing;
						Log_text += "Ignore_If_Missing)";
					break;
					default :
						throw ::std::runtime_error("Unsupported Missing_Topic_Mode with unerlying numeric value(=" + ::std::to_string(Mode) + ')');
					}
				} else
					Subscription_To_Missing_Topics_Mode = Expect_If_Missing;
			}


			// NOTE/TODO -- a bit of a hack... -- ideally it ought to be done on connection setup... yet some client amqp libs do not allow setting client/peer properties when establishing connections... moreover the client may want to change the latency/bandwidth tradeoff on the next subscription... without closing connection/channenls, etc.
			// the negative part of it here is that it will affect all existing channels/subscriptions on this connection (so it will clobber previously-set values on different subscriptions on other channels)...
			{
				if (props.find("tcp_no_delay") != props.end()) {
					connection.get_socket().set_option(basio::ip::tcp::no_delay(true));
					Log_text += "true)";
				} else {
					connection.get_socket().set_option(basio::ip::tcp::no_delay(false));
					Log_text += "false)";
				}
			}

			Process_subscription_sparsification_properties(props, Log_text);

			synapse::misc::log(::std::move(Log_text += '\n'), true);

			// TODO check user's rights for the topic in question if 'Create_If_Missing' mode is active.

			connection.dispatch([this, Message_reply(::std::move(Message_reply))](){
				write_frame<&channel::On_activate_subscription>(Message_reply());
			}, true, 1024);
		} else { // Special extended info report subscription

			// TODO, retest this! Essentially a hack for the sake of those clients that cannot use basic_consume everytime (but switch to on_queue_bind) and want to mix full-data with the extended ack reporting subscriptions.
			content_state = content_state_type::subscribe;

			// Create composed message...
			Extended_report.reset(new Extended_report_type());
			Extended_report->Consumer_tag = Consumer_tag;

			if (props.find("Current_subscription_topics_size") != props.end())
				Extended_report->Options.set("Current_subscription_topics_size", bam::ULongLong(topic_streams.size() + pending_meta_topics.size()));

			if (props.find("Topics") != props.end()) {
				auto c_(connection.shared_from_this());
				amqp_0_9_1::topics->register_for_notifications(connection.strand.wrap([this, c_, Message_reply(::std::move(Message_reply))](amqp_0_9_1::topics_type::topic_notification_listeners_type::iterator const & i) mutable {
					if (!connection.async_error) try {
						Extended_report->topic_factory_listener_membership_initialised = true;
						Extended_report->topic_factory_listener_membership = i;
						amqp_0_9_1::topics->async_enumerate_meta_topics(meta_topic_ptr(), connection.strand.wrap([this, c_, Message_reply(::std::move(Message_reply))](meta_topic_ptr const & Meta_Topic, bool error) mutable {
							On_extended_report_enumerated_topic(c_, Meta_Topic, error, ::std::move(Message_reply));
						}));
					} catch (::std::runtime_error const & e) { connection.put_into_error_state(e); }
				}), [](meta_topic_ptr const &){});
			} else 
				connection.dispatch([this, Message_reply(::std::move(Message_reply))](){
					write_frame<&channel::Send_extended_info_report_subscription>(Message_reply());
				}, true, 1024);
		}
	}

	/**
		Sort of a special method. Must be called from the right context.
	*/
	void Decrement_async_fence_size_and_ping_consumer_notify() {
		decrement_async_fence_size();
		// Not checking here for async_fence_size because the context of calling code should imply thath the "most likely" outcome being just that,
		// whilst other cases (which rely on unconditional calling of the consumer_notify regardless of the async_fence_size value) are then also satisfied.
		on_consumer_notify(); // in case other subscriptions (e.g. streaming kind) are also present...
	}

	template <typename Message_reply_type>
	void On_extended_report_enumerated_topic(::boost::shared_ptr<Connection> const & c_, meta_topic_ptr const & Meta_Topic, bool error, Message_reply_type && Message_reply) noexcept {
		try {
			if (!error && !connection.async_error) {
				if (Meta_Topic) {
					// todo TEST for this like in other tests for memory alarm
					if (!(Extended_report->Topics.get_fields().size() % 512))
						Extended_report->Scoped_contributor_to_virtual_allocated_memory_size.Increment_contribution_size(Extended_report_type::Topics_list_approximated_memory_consumption_chunk_size, "Extended report memory alarm breached during addition to the accumulated topics list.");

					auto && Array(Extended_report->Array);
					Array.set(0, bam::ULongLong(Meta_Topic->Maximum_Sequence_Number));
					Array.set(1, bam::ULongLong(Meta_Topic->Latest_Timestamp));
					Array.set(2, bam::ULongLong(Meta_Topic->Approximate_Topic_Size_On_Disk));

					Extended_report->Topics.set(Meta_Topic->Meta_Topics_Membership->first, Array);

					amqp_0_9_1::topics->async_enumerate_meta_topics(Meta_Topic, connection.strand.wrap([this, c_, Message_reply(::std::move(Message_reply))](meta_topic_ptr const & Meta_Topic, bool error) mutable noexcept {
						On_extended_report_enumerated_topic(c_, Meta_Topic, error, ::std::move(Message_reply));
					}));
				} else {
					Extended_report->Options.set("Topics", Extended_report->Topics);
					bam::Array Array;
					Array.set(0, bam::ULongLong(data_processors::misc::alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed)));
					Array.set(1, bam::ULongLong(synapse::database::Database_size.load(::std::memory_order_relaxed)));
					Array.set(2, bam::ULongLong(data_processors::misc::alloc::Memory_alarm_threshold));
					Array.set(3, bam::ULongLong(database::Database_alarm_threshold));
					Array.set(4, bam::ULongLong(amqp_0_9_1::total_written_messages.load(::std::memory_order_relaxed)));
					Array.set(5, bam::ULongLong(amqp_0_9_1::total_read_messages.load(::std::memory_order_relaxed)));
					Array.set(6, bam::ULongLong(synapse::asio::Written_Size.load(::std::memory_order_relaxed)));
					Array.set(7, bam::ULongLong(amqp_0_9_1::total_read_payload.load(::std::memory_order_relaxed)));
					Array.set(8, bam::ULongLong(amqp_0_9_1::Start_Of_Run_Timestamp));
					Array.set(9, bam::ULongLong(synapse::asio::Created_Connections_Size.load(::std::memory_order_relaxed)));
					Array.set(10, bam::ULongLong(synapse::asio::Destroyed_Connections_Size.load(::std::memory_order_relaxed)));
					Extended_report->Options.set("Stats", Array);
					connection.dispatch([this, Message_reply(::std::move(Message_reply))]() {
						write_frame<&channel::Send_extended_info_report_subscription>(Message_reply());
					}, true, 1024);
				}
			} else
				throw ::std::runtime_error("On_extended_report_enumerated_topic");
		} catch (::std::exception const & e) { connection.put_into_error_state(e); }
	}

	void Send_extended_info_report_subscription() {

		bam::BasicDeliverFrame Basic_deliver(get_id(), Extended_report->Consumer_tag, 1, false, exchange, "/");
		uint_fast32_t const Basic_deliver_size(Basic_deliver.totalSize());

		bam::Envelope Options_envelope(nullptr, 0);
		Options_envelope.setHeaders(Extended_report->Options);
		Options_envelope.setPriority(0); // allows to disambiguate between frame-size 52 w. timestamp and 6-char text field which is used for non-ack timestamp/sequ-num injected fast-path delivery of messages (most of the time).
		bam::BasicHeaderFrame Content_header(get_id(), Options_envelope);
		uint_fast32_t const Content_header_size(Content_header.totalSize());

		Extended_report->Buffer.reset(new char unsigned[Extended_report->Buffer_size = Basic_deliver_size + Content_header_size]);

		Basic_deliver.to_wire(bam::OutBuffer(Extended_report->Buffer.get(), Basic_deliver_size));
		Content_header.to_wire(bam::OutBuffer(Extended_report->Buffer.get() + Basic_deliver_size, Content_header_size));

		connection.dispatch([this](){
			connection.template Write_External_Buffer<&channel::On_sent_extended_info_report_subscription>(this, Extended_report->Buffer.get(), Extended_report->Buffer_size);
		});
	}

	void On_sent_extended_info_report_subscription() {
		Extended_report.reset(); // we don't want to keep it dangling (todo -- current implementation likes smaller mem usage in expectation that real-life use-case will be discouraging from frequent application of this feature.
		assert(async_fence_size);
		Decrement_async_fence_size_and_ping_consumer_notify(); // in case other subscriptions (e.g. streaming kind) are also present...
	}

	void
	On_basic_reject(bam::ReceivedFrame & frm)
	{
		bam::BasicRejectFrame Qos(frm);
		auto c_(connection.shared_from_this());
		++async_fence_size;
		// Overloading semantics... using for the purpose of throttling subcsription streaming
		if (Qos.requeue()) {
			if (!++Async_subscriber_streaming_fence_size)
				throw ::std::runtime_error("incorrect sequence of basic reject frame (client most likely issued too many unmatched Subscribe_batch_begin() calls)");
			connection.strand.post([this, c_](){
				if (!connection.async_error)
					try {
						Decrement_async_fence_size_and_process_awaiting_frame_if_any();
					} catch (::std::exception const & e) { connection.put_into_error_state(e); }
			});
		} else {
			connection.strand.post([this, c_](){
				if (!connection.async_error)
					try {
						if (!Async_subscriber_streaming_fence_size--)
							throw ::std::runtime_error("incorrect sequence of basic reject frame (client most likely issued too many unmatched Subscribe_batch_end() calls)");
						Decrement_async_fence_size_and_ping_consumer_notify();
					} catch (::std::exception const & e) { connection.put_into_error_state(e); }
			});
		}
	}

	template <typename Wildcarded_Subscriptions_Iterator>
	void Try_Erase_Explicit_Topic_Name_From_Wildcard_Subscriptions_Via_Subscription_Iterator(Wildcarded_Subscriptions_Iterator const & Iterator) {
	}

	void Erase_From_Wildcard_Subscriptions(::std::string const & Key) {
		assert(connection.strand.running_in_this_thread());
		assert(Wildcarded_subscriptions.size() == Wildcarded_subscriptions_keys_index.size());
		auto const Existing_subscription(static_cast<decltype(Wildcarded_subscriptions_keys_index) const &>(Wildcarded_subscriptions_keys_index).find(Key));
		if (Existing_subscription != Wildcarded_subscriptions_keys_index.cend()) {
			assert(Existing_subscription->second != Wildcarded_subscriptions.cend());
			Wildcarded_subscriptions.erase(Existing_subscription->second);
			Wildcarded_subscriptions_keys_index.erase(Existing_subscription);
			Scoped_contributor_to_virtual_allocated_memory_size.Decrement_contribution_size(Wildcard_approximated_memory_consumption_chunk_size);
		}
		assert(Wildcarded_subscriptions.size() == Wildcarded_subscriptions_keys_index.size());
	}

	void
	Erase_topic_streams_by_routing_key(::std::string key)
	{
		assert(connection.strand.running_in_this_thread());
		// simple case, no wildcard
		if (key.find("*") == ::std::string::npos && key.find("#") == ::std::string::npos) {
			topic_streams.erase(key);
			auto Found_pending_meta_topic_name(pending_meta_topic_names.find(key));
			if (Found_pending_meta_topic_name != pending_meta_topic_names.end()) {
				assert(pending_meta_topics.find(Found_pending_meta_topic_name->second->first) != pending_meta_topics.end());
				pending_meta_topics.erase(Found_pending_meta_topic_name->second);
				pending_meta_topic_names.erase(Found_pending_meta_topic_name);
			}
			Erase_From_Wildcard_Subscriptions(key); // because initial subscriptions to non-existent topics may go to the wildcards list (and subscribe/unsubscribe pair of calls can happen well before any topic is actually created by the publisher).
		} else { // ... wildcarded unsubscription
			auto Rabbified_key(key);
			Rabbify_wildcarded_key(Rabbified_key);
			for (auto && i(topic_streams.begin()); i != topic_streams.end();) {
				auto && tmp(i++);
				if (::boost::regex_match(tmp->first, ::boost::regex(Rabbified_key, ::boost::regex::extended)))
					topic_streams.erase(tmp);
			}
			for (auto && i(pending_meta_topic_names.begin()); i != pending_meta_topic_names.end();) {
				auto && tmp(i++);
				if (::boost::regex_match(tmp->first.get(), ::boost::regex(Rabbified_key, ::boost::regex::extended))) {
					assert(pending_meta_topics.find(tmp->second->first) != pending_meta_topics.end());
					pending_meta_topics.erase(tmp->second);
					pending_meta_topic_names.erase(tmp);
				}
			}
			Erase_From_Wildcard_Subscriptions(Rabbified_key);
		}
	}

	void
	on_basic_cancel(bam::ReceivedFrame & frm)
	{
		assert(connection.strand.running_in_this_thread());
		bam::BasicCancelFrame cancel_frm(frm);
		auto const & tag_is_key(cancel_frm.consumerTag());
		#ifdef LAZY_LOAD_LOG_1	
			synapse::misc::log("LAZY_LOAD_LOG ON_BASIC_UNSUBSCRIBE " + tag_is_key + '\n', true);
		#endif

		Erase_topic_streams_by_routing_key(tag_is_key);

		// todo -- more validation and various replies if was/wasnt erased
		assert(ts_to_process == nullptr);
		++async_fence_size;
		connection.dispatch([this, tag_is_key](){
			write_frame<&channel::On_cancelled_subscription>(bam::BasicCancelOKFrame(get_id(), tag_is_key));
		}, true, 1024);
	}

	void
	On_cancelled_subscription()
	{
		assert(pending_meta_topics.empty() == true || pending_meta_topic_names.empty() == false);
		Decrement_async_fence_size_and_ping_consumer_notify();
	}

	void
	decrement_async_fence_size()
	{
#ifndef NDEBUG
		auto const prev(async_fence_size);
#endif
		--async_fence_size;
		assert(prev > async_fence_size);
	}

	void Uncork_on_end_of_processed_subcsription() {
		--Async_subscriber_streaming_fence_size;
		if (Atomic_subscription_cork != Previous_atomic_subscription_cork) {
			if (Previous_atomic_subscription_cork)
				--Async_subscriber_streaming_fence_size;
			Previous_atomic_subscription_cork = Atomic_subscription_cork;
		}
	}

	void
	on_enumerated_topic(::boost::shared_ptr<Connection> const & c_, meta_topic_ptr const & Meta_Topic, bool error) noexcept
	{
		try {
			if (!error && !connection.async_error) {
				if (Meta_Topic) {
					// if the topic name matches the key (in reverse -- latest addition wins)
					auto Matched_iterator(Wildcarded_subscriptions.crbegin());
					for (; Matched_iterator != Wildcarded_subscriptions.crend() && !(::boost::regex_match(Meta_Topic->Meta_Topics_Membership->first, ::boost::regex(Matched_iterator->Key, ::boost::regex::extended))); ++Matched_iterator);
					if (Matched_iterator != Wildcarded_subscriptions.crend()) {
						// Capturing Meta_Topic because want to retain in against 'marking for deletion' at this stage (i.e. part of enumeration already, consumers are not dropped).
						async_topic_stream_loading_helper(Meta_Topic->Meta_Topics_Membership->first, Matched_iterator->Begin_timestamp, [this, c_, Meta_Topic](typename decltype(topic_streams)::iterator const & ts_i){
							// postpone any currently-active topic_streams in this subscription if they turn out to be too far into the future...
							if (ts_i != topic_streams.end()) {
								auto const too_futuristic(lazy_load_timestamp + too_futuristic_watermark);
								for (auto && i(postponable_atomically_subscribed_topic_streams.begin()); i != postponable_atomically_subscribed_topic_streams.end() && i->first > too_futuristic;) {
									auto const & found(topic_streams.find(i->second));
									if (found == topic_streams.end())
										i = postponable_atomically_subscribed_topic_streams.erase(i);
									else if (found->second.get() != ts_to_process) {
										auto const & ts(*found->second);
										assert(ts.topic);
										assert(ts.topic->Meta_Topic_Shim.mt);
										auto Added_pending_meta_topic(pending_meta_topics.emplace(::std::piecewise_construct,
											::std::forward_as_tuple(ts.begin_timestamp), 
											::std::forward_as_tuple(ts.end_timestamp, found->first, ts.topic->Meta_Topic_Shim.mt, ts.begin_msg_seq_no, ts.Activation_attributes, ts.Skipping_period, ts.Skip_payload))
										);
										pending_meta_topic_names.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Added_pending_meta_topic->second.Key), ::std::forward_as_tuple(Added_pending_meta_topic));
										#ifdef LAZY_LOAD_LOG_1	
											synapse::misc::log("LAZY_LOAD_LOG POSTPONING " + found->first + " during atomic multi-topic load, too futuristic watermark " + ::std::to_string(too_futuristic) + ", begin timestamp " + ::std::to_string(ts.begin_timestamp) + '\n', true);
										#endif
										topic_streams.erase(found);
										assert(topic_streams.empty() == false);
										i = postponable_atomically_subscribed_topic_streams.erase(i);
									} else
										++i;
								}

								assert(ts_i != topic_streams.end());
								postponable_atomically_subscribed_topic_streams.emplace(ts_i->second->begin_timestamp, ts_i->first);
							}

							assert(connection.strand.running_in_this_thread());
							amqp_0_9_1::topics->async_enumerate_meta_topics(Meta_Topic, connection.strand.wrap([this, c_](meta_topic_ptr const & Next_Meta_Topic, bool error) noexcept {
								on_enumerated_topic(c_, Next_Meta_Topic, error);
							}));
						}, nullptr, 0, Matched_iterator->End_timestamp, 0, Matched_iterator->Preroll_if_begin_timestamp_in_future ? Streaming_activation_attributes::Allow_preroll_if_begin_timestamp_in_future : Streaming_activation_attributes::None, Matched_iterator->Skipping_period, Matched_iterator->Skip_payload);
					} else
						amqp_0_9_1::topics->async_enumerate_meta_topics(Meta_Topic, connection.strand.wrap([this, c_](meta_topic_ptr const & Next_Meta_Topic, bool error) noexcept {
							on_enumerated_topic(c_, Next_Meta_Topic, error);
						}));
				} else {
					postponable_atomically_subscribed_topic_streams.clear();
					Uncork_on_end_of_processed_subcsription();
					Decrement_async_fence_size_and_ping_consumer_notify();
					// TODO make it a client-determined feature (whether to close connection if nothing matching a wildcard is present)
					// currently will wait if some topics will get published later on
					//else 
					// throw ::std::runtime_error("no topics were found for supplied wildcard " + key);
				}
			} else
				throw ::std::runtime_error("asyng_get_topic from amqp channel");
		} catch (::std::exception const & e) { connection.put_into_error_state(e); }
	}

	typename amqp_0_9_1::topics_type::topic_notification_listeners_type::iterator topic_factory_listener_membership;
	bool topic_factory_listener_membership_initialised{false};
	void
	deresiter_from_topic_factory_listener_membership()
	{
		if (topic_factory_listener_membership_initialised) {
			amqp_0_9_1::topics->deregister_from_notifications(topic_factory_listener_membership);
			topic_factory_listener_membership_initialised = false;
		}
	}

	void
	Rabbify_wildcarded_key(::std::string & key) 
	{
		// make RabbitMQ style key regex-friendly (will need it for later)
		// TODO -- test this!!!
		// TODO -- in future would be nice to drop it altogether and simply utilise actual regex for our topic matching...
		::boost::replace_all(key, ".", "\\.");
		::boost::replace_all(key, "#", "(.+)?"); // Allows subscribers to just the "#" get all topics
		::boost::replace_all(key, "*", "\\w+");
		::boost::replace_all(key, "\\.(.+)?", "(\\..+)?"); // RabbitMQ way of sayng zero or more words in xyz.# notation (xyz.# should get: xyz, xyz.a, xyz.b.c, etc.)
	}

	void Add_To_Wildcard_Subscriptions() {
		auto const & Emplaced(Wildcarded_subscriptions_keys_index.emplace(::std::piecewise_construct,
			::std::forward_as_tuple(key),
			#ifndef NDEBUG
				::std::forward_as_tuple(Wildcarded_subscriptions.end())
			#else
				::std::forward_as_tuple()
			#endif
		));

		if (Emplaced.second) { // newly insterted
			Scoped_contributor_to_virtual_allocated_memory_size.Increment_contribution_size(Wildcard_approximated_memory_consumption_chunk_size, "Wildcard subscription memory alarm breached during addition to the accumulated topics list.");
			Wildcarded_subscriptions.emplace_back(Emplaced.first->first, begin_timestamp, end_timestamp, Skipping_period, Skip_payload, preroll_if_begin_timestamp_in_future);
			auto Subscription_Iterator(::std::prev(Wildcarded_subscriptions.end()));
			Emplaced.first->second = Subscription_Iterator;
			Subscription_Iterator->Erase_Index_Membership = [this, Index_Membership(Emplaced.first)] {
				Wildcarded_subscriptions_keys_index.erase(Index_Membership);
			};
		} else { // already exists
			Wildcarded_subscriptions.splice(Wildcarded_subscriptions.end(), Wildcarded_subscriptions, Emplaced.first->second);
			Emplaced.first->second = ::std::prev(Wildcarded_subscriptions.end());
			auto && Paremeters_to_update(Wildcarded_subscriptions.back());
			assert(!Paremeters_to_update.Key.empty());
			Paremeters_to_update.Begin_timestamp = begin_timestamp;
			Paremeters_to_update.End_timestamp = end_timestamp;
			Paremeters_to_update.Skipping_period = Skipping_period;
			Paremeters_to_update.Skip_payload = Skip_payload;
			Paremeters_to_update.Preroll_if_begin_timestamp_in_future = preroll_if_begin_timestamp_in_future;
		}
		assert(Wildcarded_subscriptions.size() == Wildcarded_subscriptions_keys_index.size());
	}

	void On_New_Topic_Notification(::boost::weak_ptr<Connection> const & weak_c_, meta_topic_ptr const & Meta_Topic) {
		if (auto && c_{weak_c_.lock()}) 
			if (!connection.async_error)
				connection.strand.post([this, c_, Meta_Topic](){ // this callback must have a strand.post() in it! (due to sync loop iteration in the calling topic_factory codebase)
					if (!connection.async_error && content_state == content_state_type::subscribe) try {
						// if the topic name matches the key
						for (auto Iterator(Wildcarded_subscriptions.crbegin()); Iterator != Wildcarded_subscriptions.crend(); ++Iterator) {
							if (::boost::regex_match(Meta_Topic->Meta_Topics_Membership->first, ::boost::regex(Iterator->Key, ::boost::regex::extended))) {
								++async_fence_size;
								// Capturing Meta_Topic because want to retain in against 'marking for deletion' at this stage (i.e. part of enumeration already, consumers are not dropped).
								async_topic_stream_loading_helper(Meta_Topic->Meta_Topics_Membership->first, Iterator->Begin_timestamp, [this, c_, Meta_Topic](typename decltype(topic_streams)::iterator const &){
									assert(connection.strand.running_in_this_thread());
									Decrement_async_fence_size_and_ping_consumer_notify();
								}, nullptr, 0, Iterator->End_timestamp, 0, Iterator->Preroll_if_begin_timestamp_in_future ? Streaming_activation_attributes::Allow_preroll_if_begin_timestamp_in_future : Streaming_activation_attributes::None, Iterator->Skipping_period, Iterator->Skip_payload);
								if (Iterator->Key.find("\\w+") == ::std::string::npos && Iterator->Key.find(".+)?") == ::std::string::npos) {
									Iterator->Erase_Index_Membership();
									Wildcarded_subscriptions.erase(::std::next(Iterator).base());
									Scoped_contributor_to_virtual_allocated_memory_size.Decrement_contribution_size(Wildcard_approximated_memory_consumption_chunk_size);
									assert(Wildcarded_subscriptions.size() == Wildcarded_subscriptions_keys_index.size());
								}
								break;
							}
						}
					} catch (::std::runtime_error const & e) { connection.put_into_error_state(e); }
				});
	}

	// Allows template-resolved call without casting lambda/etc. to std::function in some cases
	template <typename On_Registered_Listener_For_New_Topics_Callback = ::std::nullptr_t, typename On_New_Topic_Callback = ::std::nullptr_t>
	void Wrap_Call_To_Async_Topic_Stream_Loading_Helper(::boost::shared_ptr<Connection> const & c_, On_Registered_Listener_For_New_Topics_Callback && On_Registered_Listener_For_New_Topics = nullptr, On_New_Topic_Callback && On_New_Topic = nullptr) {
		// If second or third callbacks are called, it is guaranteed that 1st one will not be.
		// moreover the guarantee is that second callback will be called back before 3rd one.
		// If 2nd callback is null then topic is created if missing.
		// If non-null the 2nd callback should decide whether to error out (Error_If_Missing subscription mode) or not (Expect_If_Missing -- will be automatically streamed when the topic in question gets created).
		async_topic_stream_loading_helper(key, begin_timestamp, [this, c_](typename decltype(topic_streams)::iterator const &){
				assert(connection.strand.running_in_this_thread());
				Uncork_on_end_of_processed_subcsription();
				Decrement_async_fence_size_and_ping_consumer_notify();
			}, nullptr, 0, 0, 0, preroll_if_begin_timestamp_in_future ? Streaming_activation_attributes::Allow_preroll_if_begin_timestamp_in_future : Streaming_activation_attributes::None, Skipping_period, Skip_payload,
			::std::forward<On_Registered_Listener_For_New_Topics_Callback>(On_Registered_Listener_For_New_Topics), 
			::std::forward<On_New_Topic_Callback>(On_New_Topic)
		);
	}

	void
	On_activate_subscription()
	{
		++Async_subscriber_streaming_fence_size;
		assert(!key.empty());
		// TODO -- more thought, at the moment duplicate subscriptions are ignored silently
		if (topic_streams.find(key) == topic_streams.end() && pending_meta_topic_names.find(key) == pending_meta_topic_names.end()) {
			content_state = content_state_type::subscribe;
			auto c_(connection.shared_from_this());
			// if not wildcard then get directly... 
			if (key.find("*") == ::std::string::npos && key.find("#") == ::std::string::npos) {
				if (Subscription_To_Missing_Topics_Mode == Create_If_Missing)
					Wrap_Call_To_Async_Topic_Stream_Loading_Helper(c_);
				else
					Wrap_Call_To_Async_Topic_Stream_Loading_Helper(c_, 
						connection.strand.wrap([this, c_](amqp_0_9_1::topics_type::topic_notification_listeners_type::iterator const & i){
							deresiter_from_topic_factory_listener_membership();
							topic_factory_listener_membership_initialised = true;
							topic_factory_listener_membership = i;
							if (!connection.async_error) try {
								assert(Subscription_To_Missing_Topics_Mode != Create_If_Missing);
								if (Subscription_To_Missing_Topics_Mode == Expect_If_Missing)
									Add_To_Wildcard_Subscriptions();
								else if (Subscription_To_Missing_Topics_Mode != Ignore_If_Missing)
									throw ::std::runtime_error("Subscribed-to topic name (=" + key + ") is not present on the server.");
								Uncork_on_end_of_processed_subcsription();
								Decrement_async_fence_size_and_ping_consumer_notify();
							} catch (::std::runtime_error const & e) { connection.put_into_error_state(e); }
						}), 
						[this, weak_c_(::boost::weak_ptr<Connection>(c_))](meta_topic_ptr const & Meta_Topic){ 
							On_New_Topic_Notification(weak_c_, Meta_Topic);
						}
					);
			} else { // ... otherwise enumerate and match one by one.
				if (::std::any_of(key.begin(), key.end(), [](auto c){return ::std::isupper(c);}))
					throw ::std::runtime_error("Upper case characters used in wildcard subscription... unlikely to match anything.");

				Rabbify_wildcarded_key(key);

				Add_To_Wildcard_Subscriptions();

				deresiter_from_topic_factory_listener_membership();
				amqp_0_9_1::topics->register_for_notifications(
					connection.strand.wrap([this, c_](amqp_0_9_1::topics_type::topic_notification_listeners_type::iterator const & i){
						topic_factory_listener_membership_initialised = true;
						topic_factory_listener_membership = i;
						if (!connection.async_error) try {
							amqp_0_9_1::topics->async_enumerate_meta_topics(meta_topic_ptr(), connection.strand.wrap([this, c_](meta_topic_ptr const & Meta_Topic, bool error){
								on_enumerated_topic(c_, Meta_Topic, error);
							}));
						} catch (::std::runtime_error const & e) { connection.put_into_error_state(e); }
					}), 
					[this, weak_c_(::boost::weak_ptr<Connection>(c_))](meta_topic_ptr const & Meta_Topic){ 
						On_New_Topic_Notification(weak_c_, Meta_Topic);
					}
				);
			}
		} else { // still need to actuate...
			Uncork_on_end_of_processed_subcsription();
			Decrement_async_fence_size_and_ping_consumer_notify();
		}
	}

	// TODO make in more encapsulated (part of topic_stream struct/class)
	void
	on_new_range(topic_stream &ts, range_ptr const & range) noexcept
	{
		assert(connection.strand.running_in_this_thread());
		assert(ts.next_range_pending == true);
		ts.next_range_pending = false;
		assert(!ts.next_range);
		assert(range);
		if (ts.range) {
			assert(!ts.next_range);
			assert(ts.range != range);
			ts.next_range = range;
		} else {
			assert(ts_to_process == nullptr);
			assert(!ts.my_msg);
			assert(!ts.My_next_message);
			assert(!ts.My_back_message);
			ts.range = range;
		}
	}

	void
	on_sent_to_consumer() noexcept
	{
		assert(!connection.async_error);
		assert(ts_to_process != nullptr);
		ts_to_process = nullptr;
		total_written_messages.fetch_add(1u, ::std::memory_order_relaxed);
		Decrement_async_fence_size_and_ping_consumer_notify();	
	}

	topic_stream * ts_to_process{nullptr};
	void
	on_consumer_notify()
	{
		assert(connection.strand.running_in_this_thread());
		if (ts_to_process != nullptr) // something is being written out/processed... or lazy_loading is in the 'wait till its done' mode (so to speak)...
			return;
		else if (frame_is_awaiting == true) { // check if 'to read' frame is awaiting...
			if (!async_fence_size && awaiting_frame_is_being_processed == false)
				process_awaiting_frame();
			return;
		} else if (Async_subscriber_streaming_fence_size  // subscription corking feature for atomic nature of subscribing
			|| content_state != content_state_type::subscribe // protection of late/'out of context' callbacks...
		)
			return;

    // TODO -- watch out here, currently allowing the 'on_consumer_notify' to proceed even if 'async_fence_size' is > 0, for possibly windind-out some messages on some topic streams whilst other topic-streams may be async_getting their next ranges... if some anomalies are detected and design issues arise, then consider also bailing out here if 'async_fence_size' is non zero...

		uint_fast64_t min_seq_no(-1);
		uint_fast64_t min_timestamp(-1);
		database::Message_ptr min_msg;
		bool postback_again(false);
		bool notification_wanted(false);
		bool not_ready_yet(false); // not yet ready to be taken onto the next loop (i.e. some upcoming messages will need to be waited for in the topic streams)
		for (unsigned double_scan(0); double_scan != 2; ++double_scan) {
			bool rescan(false);
			for (auto && ts_i(topic_streams.begin()); ts_i != topic_streams.end();) { // for every subscribed-to absolutely-named topic stream...
				auto & ts(*ts_i->second);
				if (!double_scan || ts.scan == true) {
					assert(ts.topic);
					assert(ts.range);
					ts.scan = false;

					bool const First_message_in_range(!ts.my_msg && !ts.My_next_message);

					if (ts.My_next_message == ts.my_msg && (ts.My_next_message = ts.range->get_next_message(ts.my_msg, ts.My_back_message))) {
						ts.My_next_message_sequence_number = ts.My_next_message.Get_index(0);
						ts.My_next_message_timestamp = ts.My_next_message.Get_index(1);
					}

					if (ts.My_next_message != ts.my_msg) { // ... load topic's message
						assert(ts.My_next_message);
						ts.notification_wanted = topic_stream::Notification_level::Unwanted;

						// TODO, later when ts.My_next_message_sequence_number semantics are better defined (e.g. server-unique, stream-unique, indexed or not, etc.) may deprecate the 'ts.begin_timestamp' and just use 'ts.begin_msg_seq_no' -- can set it to 0 for open-ended ranges in topics which have no data published-to just yet...
						assert(ts.My_next_message_sequence_number);
						assert(ts.begin_msg_seq_no != static_cast<decltype(ts.begin_msg_seq_no)>(-1));
						assert(ts.begin_timestamp != static_cast<decltype(ts.begin_timestamp)>(-1));

						if (ts.My_next_message_timestamp < ts.begin_timestamp || (ts.My_next_message_timestamp == ts.begin_timestamp && ts.My_next_message_sequence_number < ts.begin_msg_seq_no)) { // wind-out NOTE -- valid ts.My_next_message_sequence_number should start from 1
							ts.my_msg = ts.My_next_message;
							not_ready_yet = postback_again = true;
							if (ts.Activation_attributes == Streaming_activation_attributes::Pick_up_exactly_from_specified_starting_point)
								ts.Activation_attributes = Streaming_activation_attributes::Streaming_started;
						} else if (ts.My_next_message_timestamp > ts.end_timestamp || max_sent_out_timestamp > ts.My_next_message_timestamp && max_sent_out_timestamp - ts.My_next_message_timestamp > delayed_delivery_tolerance) { // retire
							#ifdef LAZY_LOAD_LOG_1	
								synapse::misc::log("LAZY_LOAD_LOG RETIRING " + ts_i->first + ", upon seeing next msg in msg loop, max_sent_out " + ::std::to_string(max_sent_out_timestamp) + '\n', true);
							#endif
							ts_i = topic_streams.erase(ts_i);
							continue;
						} else if (ts.My_next_message_timestamp > lazy_load_timestamp + too_futuristic_watermark && lazy_load_timestamp 
							&& !First_message_in_range // Note, excluding 1st message in range because of possibility of 'continuous chasing of the sparsified start' in some rare racing conditions (which ideally should eventuate at: 1st message ever should not be postponed in such cases... this is achieved by 1st message in range being left alone, if need be it will legitimately be postponed on second iteration, etc... and the very 1st message in topic ever should have begin seq no as 0)
						) { // test if message is too far into the future (will temporarily retire this topic_stream to save on resources)
							#ifdef LAZY_LOAD_LOG_1	
								synapse::misc::log("LAZY_LOAD_LOG POSTPONING " + ts_i->first + ", msg time : " + ::std::to_string(ts.My_next_message_timestamp) + ", futuristic " + ::std::to_string(max_sent_out_timestamp + too_futuristic_watermark) + '\n', true);
							#endif
							// move from (acitve) topic_streamn to pending_meta_topics...
							// taking out below assertion -- 'indicies_begin' can be set concurrently (not on the same strand).
							//assert(ts.My_next_message_timestamp >= ts.topic->Meta_Topic_Shim.mt->indicies_begin[0].load(::std::memory_order_relaxed));
							assert(ts.topic);
							assert(ts.topic->Meta_Topic_Shim.mt);
							auto Added_pending_meta_topic(pending_meta_topics.emplace(::std::piecewise_construct,
								::std::forward_as_tuple(ts.My_next_message_timestamp), 
								::std::forward_as_tuple(ts.end_timestamp, ts_i->first, ts.topic->Meta_Topic_Shim.mt, ts.My_next_message_sequence_number, ts.Activation_attributes, ts.Skipping_period, ts.Skip_payload))
							);
							pending_meta_topic_names.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Added_pending_meta_topic->second.Key), ::std::forward_as_tuple(Added_pending_meta_topic));
							ts_i = topic_streams.erase(ts_i);
							continue;
						} else if (ts.My_next_message_timestamp <= min_timestamp && lazy_load_fence == false) { // see if this message is a candidate for processing/sending out (should be among the earliest of all the topic streams) 
							if (ts.My_next_message_timestamp < min_timestamp) {
								min_timestamp = ts.My_next_message_timestamp;
								min_seq_no = -1;
							}
							if (ts.My_next_message_sequence_number < min_seq_no) { // after grouping messages by timestamp, we want to honor the sequence number in currently-considered set of topic streams...
								min_seq_no = ts.My_next_message_sequence_number;
								ts_to_process = &ts;
								min_msg = ts.My_next_message;
							}
						}

						// Opportunistic preparation of next/upcoming range object (essentially start loading second one immediately on obtaining 1st message from the first one)... this is a simplified approach which essentially treats 2 ranges as one semantic one, with 1st half being started-on streaming whilst second half is disk-loading io :)
						if (First_message_in_range && !ts.next_range && !ts.range->is_open_ended() && !ts.next_range_pending) {
							ts.next_range_pending = true;
							++async_fence_size;
							auto c_(connection.shared_from_this());
							// NOTE -- same comments as for similar section of code on choice between weak_ptr vs not erasing topic_streams when async_fence_size > 0
							::boost::weak_ptr<topic_stream> Weak_ts_ptr(ts.shared_from_this());
							ts.topic->async_get_next_range(ts.range, connection.strand.wrap([this, c_, Weak_ts_ptr](range_ptr const & range) mutable noexcept {
								try {
									if (range && !connection.async_error) {
										if (auto Ts_ptr{Weak_ts_ptr.lock()})
											on_new_range(*Ts_ptr, range);
										Decrement_async_fence_size_and_ping_consumer_notify();
									} else 
										throw ::std::runtime_error("async_get_next_range in ts in error");
								} catch (::std::runtime_error const & e) { connection.put_into_error_state(e); }
							}));
							assert(ts_to_process == nullptr || min_msg);
						}
					} else if (!double_scan) {
						if (max_sent_out_timestamp > ts.begin_timestamp && max_sent_out_timestamp - ts.begin_timestamp > delayed_delivery_tolerance
							//&& ts.topic->Meta_Topic_Shim.mt->Latest_Timestamp.load(::std::memory_order_relaxed)
							&& ts.my_msg
						) { // retire
							#ifdef LAZY_LOAD_LOG_1	
								synapse::misc::log("LAZY_LOAD_LOG RETIRING " + ts_i->first + ", without seeing next msg in msg loop, max_sent_out " + ::std::to_string(max_sent_out_timestamp) + '\n', true);
							#endif
							ts_i = topic_streams.erase(ts_i);
							continue;
						}
						// next msg boundary may not have advanced just yet (due to non-atomic icrementation of max messages seq. number vs. back_msg_buffer_byte_offest -- latter may not be gotten-around-to by potentially paused thread)... but it's ok... because this can only happen on the open range (closed ones are read from disk, and if not from disk -- i.e. recently closed, still in RAM -- then the following call to 'get_back_msg' will not equate to 'ts.My_next_message' and the message will not get skipped... i think...
						if (ts.range->is_open_ended() == false) { // if closed range -- must resolve to getting next message before proceeding with sending any of the currently-rememberd 'earliest' one...
							// MUST call get_back_msg again (should NOT use if/when get_next_message returns the back_msg as well)
							// because 'is_open_ended' can be modified after the call to get_next_message and this call to get_back_msg will ensure that no messages are skippe in possibly recently closed range.
							// ALTETRNATIVELY, when refactoring 'get_next_message', to return back_msg as well -- will need to think if simply getting the 'is open ended' status (caching it) before getting the back_msg buffer offset will be sufficient)...
							auto const back_message(ts.range->Get_back_message_or_throw_if_invalidated_by_sparsification());
							if (ts.My_next_message == back_message && ts.My_next_message) { // at the end of the closed range...
								// see if cached range is available... (as a conequence of prefetch/read-ahead during previous range)
								if (ts.next_range) {
									assert(ts.next_range.get() != ts.range.get());
									ts.range = ::std::move(ts.next_range);
									ts.next_range.reset();
									ts.my_msg.Reset();
									ts.My_next_message.Reset();
									ts.My_back_message.Reset(); 
									assert(!ts.next_range);
									postback_again = true;
								} else  {// need to get a new range...
									if (ts.next_range_pending == false) {
										ts.next_range_pending = true;
										++async_fence_size;
										assert(ts.range);
										auto c_(connection.shared_from_this());
										// NOTE -- more than 1 way to handle this topic_stream issue of staying in scope:
										// using weak_ptr here, or not retiring/postponing streams via topic-stream.erase when async_fence_size > 0
										// the problem with the latter is that given the overlapping/async nature of various topic streams obtaining their 'next range' (which in itself is beneficial to performance etc.) the opportunity for all to be not doing this (i.e. async_fence_size == 0) is rather small (esp. if many streams are used)... so currently using the weak_ptr approach...
										::boost::weak_ptr<topic_stream> Weak_ts_ptr(ts.shared_from_this());
										ts.topic->async_get_next_range(ts.range, connection.strand.wrap([this, c_, Weak_ts_ptr](range_ptr const & range) noexcept {
											try {
												if (range && !connection.async_error) {
													if (auto Ts_ptr{Weak_ts_ptr.lock()}) {
														assert(ts_to_process == nullptr);
														Ts_ptr->my_msg.Reset(); 
														Ts_ptr->My_next_message.Reset();
														Ts_ptr->My_back_message.Reset(); 
														Ts_ptr->range.reset();
														on_new_range(*Ts_ptr, range);
													}
													Decrement_async_fence_size_and_ping_consumer_notify();
												} else 
													throw ::std::runtime_error("async_get_next_range in ts in error");
											} catch (::std::runtime_error const & e) { connection.put_into_error_state(e); }
										}));
										assert(ts_to_process == nullptr || min_msg);
									}
								}
							} else {
								//if (back_message != nullptr) TODO: && next_message is not 'crc32 async calculating'... (haven't done API for it yet...)
								//	postback_again = true;
								//else
								notification_wanted = true; 
								ts.notification_wanted = topic_stream::Notification_level::Wanted;
							}
							not_ready_yet = true;
						} else { // with open-ended ranges
							// generally-speaking, future/open-ended range has data that hasn't happened yet, so it is in future impicitly (and will not be < currently saved min_timestamp)...
							notification_wanted = true;
							if (ts.range->is_message_being_appended() == true) { // ... unless atomic updates of max msg may have already been done but back msg not yet updated...
								not_ready_yet = true; // do not proceed (and potentially skip) this 'in progress' back_msg... (as it may be of earlier sequence number than current 'min_timestamp')
								ts.notification_wanted = topic_stream::Notification_level::Wanted_due_to_message_being_appended;
							} else { // racing condition (message_appended in range.h can happen completely before or after the above 'if' check...
								rescan = true; 
								ts.scan = true;
								ts.notification_wanted = topic_stream::Notification_level::Wanted;
							}
						}
					}
				}
				++ts_i;
			} // end of scan loop
			if (ts_to_process == nullptr) // no rescanning if nothing was found thus far to send out (at the very least should pickup via 'nontification_wanted' thingy)
				break;
			else if (not_ready_yet == true) { // no rescanning if any of the current stream isn't ready to be scanned (but need be)
				ts_to_process = nullptr;
				break;
			} else if (rescan == false) // no rescanning if no streams had indicated the need to do so (performance type of a thing in case of many topics/streams)
				break;
			// ... otherwise there are some streams that may have just been 'updated out of order' w.r.t. max_message_seq_no and need to be 'picked-up'
			// TODO -- now that absolute, server-wide, message sequencing may not be so relevant we may be able to delete the double-scan loop and relevant 'atomic_updating_flag' used to flag the racing conditing between max message increment and its CRC calculation etal
			/*
				 the whole 'double_scan' thingy goes something like this:
				 1st scan finds the 'earliest' message to send. but because of the delay between updating the max_message_size in program-wide context and, possibly-async, crc32 calculation of the message, there may be other messages with earlier sequence numbers that should not be skipped (i.e. they should be sent out before 'currently-found' earliest message...
				 the above case has the following restrictions in the given design:
				 *) starting of the 'is_message_being_appended' is guranteed to be set before max_msg_seq_no is being incremented
				 *) therefore the only possible 2 cases for the 'late' appearance of the earlier message *after* the current-earliest has been found are:
				 *) the flag for message_being_appended is still true
				 *) if the flag has been already done the second scan will pick it up
				 *) it should not be needed to do 3rd scan etc. because if other 'still not-presented yet already crc32 calculated' messages will become available later on, the 'current-earliest' from the 1st scan should have the least message_seq_no
			 */
		} // end of double scan loop

		// TODO more elegance!
		assert(ts_to_process == nullptr || min_msg);
		
		if (pending_meta_topics.empty() == false) { //~~~ load from pending (futuristic) topics if any
			assert(pending_meta_topic_names.empty() == false);
			bool one_shot_into_the_future(ts_to_process == nullptr && not_ready_yet == false && !async_lazy_load_topic_streams_size); // jump from currently ended topic_stream to pending one which is much much much into the future 
			if (one_shot_into_the_future == true) 
				lazy_load_timestamp = pending_meta_topics.cbegin()->first;
			while (one_shot_into_the_future == true || ts_to_process != nullptr && pending_meta_topics.cbegin()->first < min_timestamp + too_futuristic_watermark) {
				++async_fence_size;
				++async_lazy_load_topic_streams_size;
				assert(async_lazy_load_topic_streams_size);
				auto && i(pending_meta_topics.begin());
				auto const mt_begin_timestamp(i->first);
				if (ts_to_process != nullptr && mt_begin_timestamp < min_timestamp) {
					assert(min_timestamp != static_cast<decltype(min_timestamp)>(-1));
					ts_to_process = nullptr;
					lazy_load_fence = true;
				}
				auto c_(connection.shared_from_this());
				#ifdef LAZY_LOAD_LOG_1	
					synapse::misc::log("LAZY_LOAD_LOG ACTIVATING POSTPONED " + i->second.Key + ", pending queue size " + ::std::to_string(pending_meta_topics.size()) + ", starting timestamp " + ::std::to_string(mt_begin_timestamp) + '\n', true);
				#endif
				pending_meta_topic_names.erase(i->second.Key);
				connection.strand.post([this, c_, mt_begin_timestamp, Supplementary(::std::move(i->second))]() noexcept {
					try {
						if (!connection.async_error)
							async_topic_stream_loading_helper(Supplementary.Key, mt_begin_timestamp, [this, c_](typename decltype(topic_streams)::iterator const &){
								assert(connection.strand.running_in_this_thread());
								decrement_async_fence_size();
								if (!--async_lazy_load_topic_streams_size) {
									lazy_load_fence = false;
									on_consumer_notify(); 
								}
							}, Supplementary.Meta_topic, mt_begin_timestamp, Supplementary.End_timestamp, Supplementary.Begin_message_sequence_number, Supplementary.Activation_attributes, Supplementary.Skipping_period, Supplementary.Skip_payload);
					} catch (::std::exception const & e) { connection.put_into_error_state(e); }
				});
				if (pending_meta_topics.erase(pending_meta_topics.cbegin()) == pending_meta_topics.end()) {
					assert(pending_meta_topic_names.empty() == true);
					break;
				}
				one_shot_into_the_future = false;
			}
		}//```

		// by this stage we have something to process/send out to consumers
		if (ts_to_process != nullptr) { // ... unless, of course, all ranges are being publshed-to and no messages are ready as of yet on any of the ranges...
			assert(connection.strand.running_in_this_thread());
			ts_to_process->set_amqp_msg(min_msg, supply_metadata);

			assert(min_msg.Get_index(0) == min_seq_no);

			assert(min_msg.Get_index(1) == min_timestamp);
			assert(min_timestamp != static_cast<decltype(min_timestamp)>(-1));
			if (min_timestamp > max_sent_out_timestamp) 
				max_sent_out_timestamp = min_timestamp;

			assert(ts_to_process->begin_timestamp <= min_timestamp);
			assert(ts_to_process->begin_msg_seq_no <= min_seq_no);

			// TODO This may be deprecated if lazy-loading (and specifically: activation of postponed topics) is also deprecated.

			if (ts_to_process->Activation_attributes == Streaming_activation_attributes::Pick_up_exactly_from_specified_starting_point) {
				if (min_timestamp > ts_to_process->begin_timestamp || min_timestamp == ts_to_process->begin_timestamp && min_seq_no > ts_to_process->begin_msg_seq_no)
					throw ::std::runtime_error("Forward range is sensed (e.g. to sparsification)");
				ts_to_process->Activation_attributes = Streaming_activation_attributes::Streaming_started;
			} else  if (ts_to_process->Activation_attributes != Streaming_activation_attributes::Streaming_started)
				ts_to_process->Activation_attributes = Streaming_activation_attributes::Streaming_started;

			if (!ts_to_process->Skipping_period) {
				ts_to_process->begin_timestamp = min_timestamp;
				ts_to_process->begin_msg_seq_no = min_seq_no + 1;
			} else {
				ts_to_process->begin_timestamp = min_timestamp + ts_to_process->Skipping_period;
				ts_to_process->begin_msg_seq_no = 1;
			}

			lazy_load_timestamp = min_timestamp;
			
			// queue sending of the amqp message out to the world 
			++async_fence_size;
			assert(ts_to_process != nullptr);
			// clobber emulation buffer's size of the content frame...
			auto tmp(ts_to_process->emulation_buffer_content_header_begin + 11);
			assert(!(reinterpret_cast<uintptr_t>(tmp) % alignof(uint64_t)));
			assert(!(reinterpret_cast<uintptr_t>(ts_to_process->amqp_msg_buffer_begin + 3) % alignof(uint32_t)));
			assert(synapse::misc::Get_alias_safe_value<uint32_t>(ts_to_process->amqp_msg_buffer_begin + 3) == htobe32(ts_to_process->amqp_msg_payload_size - 5));

			if (!ts_to_process->Skip_payload) {
				new (tmp) uint64_t(htobe64(ts_to_process->amqp_msg_payload_size - 5));
				// instead using scattered-write to send 2 frames in one async call
				//connection.template write<&channel::on_legacy_sent_to_consumer>(this, ts_to_process->emulation_buffer_begin, ts_to_process->emulation_buffer_size + topic_stream::ch_overwrite_buffer_size);
				auto const Pre_payload_buffer_size(ts_to_process->emulation_buffer_size + topic_stream::ch_overwrite_buffer_size);
				ts_to_process->write_scatter_buffers[0] = basio::const_buffer(ts_to_process->emulation_buffer_begin, Pre_payload_buffer_size);
				auto const Payload_buffer_size(ts_to_process->amqp_msg_payload_size);
				ts_to_process->write_scatter_buffers[1] = basio::const_buffer(ts_to_process->amqp_msg_buffer_begin + topic_stream::ch_overwrite_buffer_size, Payload_buffer_size);
				auto const Total_Write_Size(Pre_payload_buffer_size + Payload_buffer_size);
				connection.dispatch([this, Total_Write_Size](){
					assert(ts_to_process != nullptr);
					connection.template Write_External_Buffers<&channel::on_sent_to_consumer>(this, ts_to_process->write_scatter_buffers, Total_Write_Size);
					connection.snd_heartbeat_flag = false;
				}, false, Total_Write_Size);
			} else {
				new (tmp) uint64_t(0);
				connection.dispatch([this](){
					assert(ts_to_process != nullptr);
					connection.template Write_External_Buffer<&channel::on_sent_to_consumer>(this, ts_to_process->emulation_buffer_begin, ts_to_process->emulation_buffer_size);
					connection.snd_heartbeat_flag = false;
				}, false, ts_to_process->emulation_buffer_size);
			}
		} else if (postback_again == true) {
			auto c_(connection.shared_from_this());
			// note -- not doing async_fence here -- on_consumer_notify is designed not to care... 
			connection.strand.post([this, c_]() noexcept {
				try {
					if (!connection.async_error)
						on_consumer_notify();
				} catch (::std::exception const & e) { connection.put_into_error_state(e); }
			});
		} else if (notification_wanted == true) {
			// NOTE -- due to (below) usage of weak_ptr on topic_stream and because "connection" itself may go out of scope,  need to also have weak pointer to connection (to transit for register_for_notification call)... 
			::boost::weak_ptr<Connection> Weak_c_(connection.shared_from_this());
			for (auto && ts_i(topic_streams.begin()); ts_i != topic_streams.end(); ++ts_i) {
				auto & ts(*ts_i->second);
				if (ts.notification_wanted > ts.notification_requested) {
					ts.notification_requested = ts.notification_wanted;
					// NOTE -- it is either weak_ptr or trigger range object to "process pending notifications" to every registered client... (making sure that it will be done for every time a new range is assigned over the old/existing one -- so that outgoing range is not holding any references to me)
					// OR, of coures, can leave 'as is' with shared_ptr with the only consequence being that the resources associated with connection_1 object will still hang around until no clients are present for range and the topic gets deallocated during the next async resource monitoring loop in topic.h ... for the time being will use weak_ptr for the sake of being kinder to available RAM resources... BUT then must also remember to clear "is being published to" by the given topic!
					// Currently using weak_ptr approach...
					ts.range->register_for_notification(ts.my_msg, 
						[this, Weak_ts_(::boost::weak_ptr<topic_stream>(ts.shared_from_this())), Weak_c_]() mutable {
							auto c_(Weak_c_.lock());
							if (c_)
								// Posting because connection.strand.dispatch (or connection.strand.wrap around above lambda) may cause deep stack recursion in on_consumer_notify (2 strands from say topic and tcp stream may share the same thread-implementation in Boost.Asio)
								connection.strand.post([this, Weak_ts_(::std::move(Weak_ts_)), c_(::std::move(c_))]() {
										auto ts_ptr(Weak_ts_.lock());
										if (ts_ptr) {
											if (!connection.async_error) {
												try {
													ts_ptr->notification_requested = topic_stream::Notification_level::Unwanted;
													on_consumer_notify();
												} catch (::std::exception const & e) { connection.put_into_error_state(e); }
											}
										}
								});
						}, ts.notification_wanted == topic_stream::Notification_level::Wanted_due_to_message_being_appended
					);
				}
			}
		}
	}

	uint_fast32_t last_basic_content_header_frame_size{0};
	void
	on_basic_content_header(char unsigned const * const data, uint_fast32_t const data_size)
	{
		if (topic_streams.empty() == true)
			throw bam::ProtocolException("no allocated topic");
		else if (connection.state < Connection::state_type::open || state < state_type::open)
			throw bam::ProtocolException("basic content header must happen after the establishment of connection and channel");
		auto & ts(*topic_streams.begin()->second);
		if (content_state != content_state_type::publish || !ts.topic || !ts.range)
			throw bam::ProtocolException("basic content header shall be preceeded by setting up of a key/topic");

		if (synapse::misc::be64toh_from_possibly_misaligned_source(data + 11) > max_message_size - 8)
			throw bam::ProtocolException("fragmentation is not currently supported, message size is too large...");
		else if (data_size == 22) { // no properties are set
			// cant do a default timestamping here (well ideally shoudn't) because of the:
			//
			// racing condition w.r.t. msg seq number incrementation being non atomic w.r.t. when back_msg is updated; and
			//
			// it may be necessary to order playback of messages as MSB based on timestamp and then on sequence numbers (e.g. in cases when distributed sync. between synapse servers takes place).
			begin_timestamp = preset_msg_seq_no = 0;
		} else if (data_size == 52) {
			auto const tmp_flag(synapse::misc::be16toh_from_possibly_misaligned_source(data + 19));
			if (tmp_flag != (1u << 15 | 1u << 13 | 1u << 6) || data[27] != 6)
				throw bam::ProtocolException("Currently only supporting specific content header frame properties... if the incoming frame has any properties set (and it appears to do so) then it must have at least 'timestamp' and ('XXXXXX' or 'SEQNUM') in headers set.");
			//assert(!(reinterpret_cast<uintptr_t>(data + 19) % 2));
			//assert(!(reinterpret_cast<uintptr_t>(data + 35) % 8));
			//assert(!(reinterpret_cast<uintptr_t>(data + 43) % 8));
			preset_msg_seq_no = synapse::misc::be64toh_from_possibly_misaligned_source(data + 35);
			begin_timestamp = synapse::misc::be64toh_from_possibly_misaligned_source(data + 43);
		} else {
			bam::ByteBuffer const Byte_buffer(data, data_size);
			bam::ReceivedFrame frm(Byte_buffer, data_size);
			bam::BasicHeaderFrame Content_header_frame(frm);
			auto && Content_metadata(Content_header_frame.Get_metadata());

			if (Content_metadata.hasTimestamp())
				begin_timestamp = Content_metadata.timestamp();
			else
				throw bam::ProtocolException("Currently only supporting specific content header frame properties... if the incoming frame has any properties set (and it appears to do so) then it must have 'timestamp'.");

			if (!Content_metadata.hasHeaders())
				throw bam::ProtocolException("Currently only supporting specific content header frame properties... if the incoming frame has any properties set (and it appears to do so) then it must have 'headers' table.");

			auto && Headers(Content_metadata.headers().get_fields());
			auto found(Headers.find("SEQNUM"));
			if (found != Headers.end()) 
				preset_msg_seq_no = static_cast<decltype(preset_msg_seq_no)>(static_cast<uint64_t>(*found->second));
			else { 
				found = Headers.find("XXXXXX"); // deprecated, will be deleted soon.. as the next few lines...
				if (found != Headers.end()) 
					preset_msg_seq_no = static_cast<decltype(preset_msg_seq_no)>(static_cast<uint64_t>(*found->second));
				else // ... end deprecated
					throw bam::ProtocolException("Currently only supporting specific content header frame properties... if the incoming frame has any properties set (and it appears to do so) then it must have 'XXXXXX' or 'SEQNUM' property in the headers table.");
			}

			bool Extra_parameters_present(false);

			found = Headers.find("Target_data_consumption_limit");
			if (found != Headers.end()) {
				assert(!topic_streams.empty());
				auto & ts(*topic_streams.begin()->second);
				assert(ts.topic);
				ts.topic->Set_target_data_consumption_limit(static_cast<uint64_t>(*found->second), ts.range);
				Extra_parameters_present = true;
			} 

			found = Headers.find("Sparsify_upto_timestamp");
			if (found != Headers.end()) {
				assert(!topic_streams.empty());
				auto & ts(*topic_streams.begin()->second);
				assert(ts.topic);
				auto const Timestamp(static_cast<uint64_t>(*found->second));
				if (Timestamp) {
					ts.topic->Set_sparsify_upto_timestamp(Timestamp, ts.range);
					Extra_parameters_present = true;
				}
			} 

			found = Headers.find("Publisher_Sticky_Mode");
			if (found != Headers.end()) {
				// Could have been without first cast to int32_t, but some clients (e.g. Python pika lib) do not support the encoding and we dont at the moment fork that lib, so allowing 'int' as a generic compromise.
				auto const Sticky(static_cast<typename ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type>(static_cast<int32_t>(*found->second)));
				connection.Publisher_Sticky_Mode.store(Sticky, ::std::memory_order_relaxed);
				Extra_parameters_present = true;
			} 
			
			if (!Extra_parameters_present)
				throw bam::ProtocolException("Currently only supporting specific content header frame properties... the incoming frame appers to have more properties than just 'timestamp' and ('XXXXXX' or 'SEQNUM') then the 'Target_data_consumption_limit' property is also expected.");
		}
		content_ready = true;
		last_basic_content_header_frame_size = data_size;
	}

#if 0
	void
	process_out_of_flow_message(bam::ReceivedFrame & frm)
	{
		if (frm.type() == 1) {
			if (frm.classID == 10 && !frm.channel() && frm.methodID == 50) { // client wants to close connection
				if (state == state_type::connection_open) 
					dispatch([this](){
						bam::ConnectionCloseOKFrame frm;
						write_frame<&connection_1::Decrement_async_fence_size_and_process_awaiting_frame_if_any>(frm);
					});
				// else send an error code -- expecting elegant closure (?)
			} else if (frm.classID == 20 && frm.channel() && frm.methodID == 50) { // wants to close the channel
			}
		}
		// todo -- instead of throwing send an error code
		throw bam::ProtocolException("expecting tune-ok frame");
	}
#endif
	void 
	Decrement_async_fence_size_and_process_awaiting_frame_if_any() 
	{ 
		assert(async_fence_size);
		decrement_async_fence_size();
		if (!async_fence_size && frame_is_awaiting == true && awaiting_frame_is_being_processed == false)
			process_awaiting_frame();
	}
};

template <typename SocketType, unsigned MaxBufferSize>
class connection_1 final : public amqp_0_9_1::Connection_base<connection_1<SocketType, MaxBufferSize>, amqp_0_9_1::channel<connection_1<SocketType, MaxBufferSize>>, SocketType, MaxBufferSize> {
	friend class amqp_0_9_1::channel<connection_1>;

	typedef amqp_0_9_1::Connection_base<connection_1<SocketType, MaxBufferSize>, amqp_0_9_1::channel<connection_1<SocketType, MaxBufferSize>>, SocketType, MaxBufferSize> base_type;

	friend base_type;

	data_processors::misc::alloc::Scoped_contributor_to_virtual_allocated_memory_size<uint_fast32_t> Scoped_contributor_to_virtual_allocated_memory_size;

	using base_type::data;
	using base_type::channels;
	using base_type::dispatch;
#ifndef NDEBUG
	using base_type::Data_size;
#endif
	using base_type::write_data;
	using base_type::heartbeat_timer;

public:
	// Policy-compliance, should be compilable into nothing (static polymorphism from the base class)
	void On_pop_pending_writechain() { }
	void On_push_pending_writechain() { }
	// Compiler should be able to optimize 'this' out for static method calls
	unsigned static get_base_type_pending_writechain_capacity(unsigned const x) {
		// *2 because of async-triggered write from read (extended report should not need it because it keeps the daisy-chained 'in-reply' sequence of to be written messages (i.e. it is within the 'async-triggerd write by a read message' allocation/specs),  
		// +1 is for async-triggered write from heartbeat_timer, note used to be extra +1 for the end_timestamp_timer (when connection would automatically close... somewhere at around r334 in svn), but such functionality has been taken out
		return x * 2 + 1; 
	}
	//.

private:
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

	void
	on_protocol_header()
	{
		char const static proto[] = "AMQP\x00\x00\x09\x01";
		// if client speaks my protocol...
		if (!::memcmp(proto, data, 8)) {
			// onto the next step: send start frame and wait for reply...

			dispatch([this]()->void {
				// todo -- just cache it among all of the connections/sockets
				bam::Table properties;
				properties["product"] = "synapse";
				properties["version"] = MY_VERSION;
				properties["platform"] = "unknown";
				properties["copyright"] = "Copyright Data Processors (www.dataprocessors.com.au)";
				properties["information"] = Version_string_with_newline;
				bam::ConnectionStartFrame frm(0, 9, properties, "PLAIN", "en_US");

				++channels[0]->async_fence_size; // TODO better encapsulation
				this->template write_frame<&channel<connection_1>::Decrement_async_fence_size_and_process_awaiting_frame_if_any>(channels.data()->get(), frm);
				assert(Data_size >= 7);
				this->template read<&connection_1::on_msg_size_read>(7);
			}, true, 1024);

		} else { // standard-compliant: reply with my protocol and close socket
			dispatch([this]()->void {
				++channels[0]->async_fence_size; // TODO better encapsulation
				auto const Spaces(write_data.Free());
				assert(Spaces.A_Size >= 8);
				::memcpy(Spaces.A_Begin, proto, 8);
				this->template Write_Internal_Buffer<&channel<connection_1>::Decrement_async_fence_size_and_process_awaiting_frame_if_any>(channels.data()->get(), 8); // hack (exception really -- using ch.0 to do the writing)
				heartbeat_timer.cancel();
			}, true, 8);
		}
	}

	uint_fast32_t const Socket_receive_buffer_size;
	uint_fast32_t const Socket_send_buffer_size;
public:
	connection_1(uint_fast32_t const Socket_receive_buffer_size = -1, uint_fast32_t const Socket_send_buffer_size = -1)
	: base_type(get_base_type_pending_writechain_capacity(base_type::init_supported_channels)), Socket_receive_buffer_size(Socket_receive_buffer_size), Socket_send_buffer_size(Socket_send_buffer_size) {
		// Note this is a special case -- we do not throw here if memory is over the alarm size. Got to do with async-nature of a accepting chain-reaction (on a server socket). This should not be a problem though because such is really "single stranded" per server... (daisy-chained)...
		// ... and the check is done in On_accept


		assert(Publisher_Sticky_Mode.is_lock_free());
	}

	// Doing it here because SSL handshake can cause delays before on_connect is called, and during this time more accepted sockets can come through thereby adding to the memory consumption...
	// TOOD -- if SSL type then may do a secondary chekc in on_connect if SSL-handshake is expected to increase some buffering/overall memory usage that is not already taken into account by the initial approximatio_guard (although ideally it should be taken into accound by the initial approximation guard).
	void On_accept() {
		assert(this->strand.running_in_this_thread());

		// Todo more thoughtful tuning...
		// Todo/Note this does not really work: (a) windows auto-window tuning may extend things like socket buffers anyways... but such may take RAM size into account automatically... will need to investigate further.'; and (b) Default send buffer sike obtained via mechanisms below yields 16MB for each socket... that's rather huge and windows underneath should surely be adjusting this dynamically anyways.
		// ... so we just use 'default minimum overestimate' ...
		/*
		// Augment memory consumption for the socket buffers...
		basio::socket_base::receive_buffer_size Receive_buffer_size;
		base_type::get_socket().get_option(Receive_buffer_size);
		basio::socket_base::send_buffer_size Send_buffer_size;
		base_type::get_socket().get_option(Send_buffer_size);
		*/

		Scoped_contributor_to_virtual_allocated_memory_size.Increment_contribution_size(
			2u * ( // some OSes can double in-kernel space for socket buffers
				(Socket_receive_buffer_size == static_cast<decltype(Socket_receive_buffer_size)>(-1) ? 32768 : Socket_receive_buffer_size)
				+ (Socket_send_buffer_size == static_cast<decltype(Socket_send_buffer_size)>(-1) ? 32768 : Socket_send_buffer_size)
			) 
			+ static_cast<uint_fast32_t>(sizeof(connection_1)) + 512 * 1024   
			+ amqp_0_9_1::max_supported_channels_end * static_cast<uint_fast32_t>(sizeof(::boost::shared_ptr<amqp_0_9_1::channel<connection_1>>)) // because user can open channel max, with earlier not being open -- Connection_base will allocate vector of upto size of biggest channel-id used (but without allocating actual intermediate channels)	
			// channels (i.e. init_supported_channels will be implicitly allocated in Connection_base ctor)
			, "Synapse server On_accept refusing more connecitons -- memory alarm breached."
		);
	}

	::std::atomic<typename ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type> Publisher_Sticky_Mode{0};
	virtual ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type Get_Publisher_Sticky_Mode_Atomic() override {
		return Publisher_Sticky_Mode.load(::std::memory_order_relaxed);
	}

	void Async_Destroy() override {
		this->strand.post([this_(this->shared_from_this())]() mutable {
			this_->put_into_error_state(::std::runtime_error("Async_Destroy invoked"));
		});
	}

	~connection_1() {
		synapse::misc::log("Peer connection dtor : " + this->data_processors::synapse::asio::Any_Connection_Type::Get_Description() + '\n', true);
	}

	void
	on_connect()
	{
		assert(this->strand.running_in_this_thread());
		// todo -- better encapsulation... (also consider taking the alloc stuff outside from 'ctor' which may happen on a different thread, and moving here whilst wrapping everything here in strand.dispatch() thingy), or because the whole thing up to here is implicitly 'stranded' then just issue c++11 memory fence here...
		base_type::on_connect();
		synapse::misc::log("AMQP connection from peer: " + this->data_processors::synapse::asio::Any_Connection_Type::Get_Description() + '\n', true);
		assert(Data_size >= 8);
		base_type::template read<&connection_1::on_protocol_header>(8);

		// We want to monitor cases if connection becomes stale (peer stops communicating continuation of connection-setup to us).
		this->dispatch_heartbeat_timer();
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
