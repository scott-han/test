#include "../asio/server.h"
#include "../misc/alloc.h"

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_CONNECTION_BASE_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_CONNECTION_BASE_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 {

namespace basio = ::boost::asio;
namespace bam = amqp_0_9_1::foreign::copernica; // for the time being, may replace later on with our own

enum Subscription_To_Missing_Topics_Mode_Enum : uint_fast8_t { None, Error_If_Missing, Create_If_Missing, Expect_If_Missing, Ignore_If_Missing };

// Helper to set emulation buffer for basic deliver/publish messaging...
template <typename Has_emulation_buffer, typename Preceeding_content_header_frame>
void static Set_emulation_buffer(Has_emulation_buffer & Target, Preceeding_content_header_frame const & Preceeding_frame, unsigned const Channel_id, bool const supply_metadata) {
	unsigned const Preceeding_frame_size(Preceeding_frame.totalSize());
	unsigned const raw_offset(Preceeding_frame_size + 11);
	unsigned const alignment_padding((raw_offset + 7 & ~7u) - raw_offset);

	auto && emulation_buffer(Target.emulation_buffer);
	auto && emulation_buffer_begin(Target.emulation_buffer_begin);
	auto && emulation_buffer_size(Target.emulation_buffer_size);
	auto && emulation_buffer_content_header_begin(Target.emulation_buffer_content_header_begin);
	auto && ch_overwrite_buffer_begin(Target.ch_overwrite_buffer_begin);

	uint16_t const Channel_id_be(htobe16(Channel_id));
	if (supply_metadata == false) {
		bam::BasicHeaderFrame content_header_frm(Channel_id, bam::Envelope(nullptr, 0));
		unsigned const content_header_frm_size(content_header_frm.totalSize());
		emulation_buffer_size = Preceeding_frame_size + content_header_frm_size;
		emulation_buffer.reset(new char unsigned[alignment_padding + emulation_buffer_size + Target.ch_overwrite_buffer_size]);
		emulation_buffer_begin = &emulation_buffer[alignment_padding];
		assert(!(reinterpret_cast<uintptr_t>(&emulation_buffer[0]) % alignof(uint64_t)));
		emulation_buffer_content_header_begin = emulation_buffer_begin + Preceeding_frame_size;
		// ... and populate
		Preceeding_frame.to_wire(bam::OutBuffer(emulation_buffer_begin, Preceeding_frame_size));
		content_header_frm.to_wire(bam::OutBuffer(emulation_buffer_begin + Preceeding_frame_size, content_header_frm_size));
	} else {
		unsigned constexpr content_header_frm_size(52);
		emulation_buffer_size = Preceeding_frame_size + content_header_frm_size;
		emulation_buffer.reset(new char unsigned[alignment_padding + emulation_buffer_size + Target.ch_overwrite_buffer_size]);
		emulation_buffer_begin = &emulation_buffer[alignment_padding];
		assert(!(reinterpret_cast<uintptr_t>(&emulation_buffer[0]) % alignof(uint64_t)));

		Preceeding_frame.to_wire(bam::OutBuffer(emulation_buffer_begin, Preceeding_frame_size));

		emulation_buffer_content_header_begin = emulation_buffer_begin + Preceeding_frame_size;
		// content bodies size (set on writing of each of the frame)
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 11) % alignof(uint64_t)));
		// metadata seq no of the message
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 35) % alignof(uint64_t)));
		// metadata timestamp
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 43) % alignof(uint64_t)));

		emulation_buffer_content_header_begin[0] = 2;

		// channel
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 1) % alignof(uint16_t)));
		*(new (emulation_buffer_content_header_begin + 1) uint16_t) = Channel_id_be;

		// generic payload size
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 3) % alignof(uint32_t)));
		*(new (emulation_buffer_content_header_begin + 3) uint32_t) = htobe32(content_header_frm_size - 8);

		// basic class id
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 7) % alignof(uint16_t)));
		*(new (emulation_buffer_content_header_begin + 7) uint16_t) = htobe16(60);

		// weight, reserved
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 9) % alignof(uint16_t)));
		*(new (emulation_buffer_content_header_begin + 9) uint16_t) = 0;

		// properties flag
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 19) % alignof(uint16_t)));
		*(new (emulation_buffer_content_header_begin + 19) uint16_t) = htobe16(1u << 15 | 1u << 13 | 1u << 6);

		emulation_buffer_content_header_begin[21] = 1;
		emulation_buffer_content_header_begin[22] = 'X'; // dummy for alignment purposes...

		// table size...
		assert(!(reinterpret_cast<uintptr_t>(emulation_buffer_content_header_begin + 23) % alignof(uint32_t)));
		*(new (emulation_buffer_content_header_begin + 23) uint32_t) = htobe32(16);

		emulation_buffer_content_header_begin[27] = 6;
		::memset(emulation_buffer_content_header_begin + 28, 'X', 6);
		emulation_buffer_content_header_begin[34] = 'l';

		emulation_buffer_content_header_begin[51] = 0xce;
	}
	ch_overwrite_buffer_begin = emulation_buffer_begin + emulation_buffer_size;
	*ch_overwrite_buffer_begin = 3; // only doing this for the body content frames
	::memcpy(ch_overwrite_buffer_begin + 1, &Channel_id_be, 2); // overwrite channel (cant do it on real/shared by multiple sockets concurrently range's file buffer)
}

template <typename DerivingType, typename ChannelType, typename SocketType, unsigned MaxBufferSize, unsigned Pools_size = 8, unsigned Starting_chunk_size = 16>
class Connection_base : public synapse::asio::connection_1<SocketType, MaxBufferSize, 5, DerivingType, ChannelType> {
protected:

	typedef synapse::asio::connection_1<SocketType, MaxBufferSize, 5, DerivingType, ChannelType> base_type;

	uint_fast32_t constexpr static max_message_size = MaxBufferSize;

	data_processors::misc::alloc::basic_segregated_pools_1<Pools_size, Starting_chunk_size> sp;

	template <typename T>
	using basic_alloc_1 = data_processors::misc::alloc::basic_alloc_1<T, Pools_size, Starting_chunk_size>;

	template <typename T>
	using Basic_alloc_2 = data_processors::misc::alloc::Basic_alloc_2<T, Pools_size, Starting_chunk_size>;

	::std::vector<::boost::shared_ptr<ChannelType>, basic_alloc_1<::boost::shared_ptr<ChannelType>>> channels{typename decltype(channels)::allocator_type{sp}};

	using base_type::dispatch;
	using base_type::Write_Internal_Buffer;
	using base_type::Write_External_Buffer;
	using base_type::data; // this one floats around (not just w.r.t. underlying asio's buffer, but also possibly pointing to buffer of range's file (i.e. channel/topic).
	#ifndef NDEBUG
		using base_type::Data_size;
	#endif
	using base_type::bytes;
	using base_type::write_data;
	using base_type::strand;
	uint_fast32_t payload_size;

	// TODO -- replace with similar to 'write_callback' interface with 'callee' -- thereby, in 'on_msg_size_read' can call directly the channe'ls method 'on frame'... albeit a relatively minor 'todo'
	uint32_t pending_channel_on_read;

	// used in prefetching buffering when reading incoming messages (in case of small(ish) payloads causing proportionally high price of system calls per second thereby making syscalls more of a bottleneck)
	uint_fast32_t read_some_consumed_size{0};
	uint_fast32_t read_some_accumulated_size{0};

	float constexpr static large_message_size_threshold{1024}; // arbitrary amount to denote when cost of CPU-memcpy is above(ish) cost of a syscall to setup async io 
	// these are used in controlling various 'prefetch' amounts when reading messages (esp. when large(ish) messages are being processed -- as AMQP sends 3 frames per payload, and it would be nice to have probable deployment where at least 1st two frames are prefetched in one syscall)
	float lpf_message_size{.0};
	float lpf_basic_publish_frame_size{.0};
	float lpf_basic_content_header_frame_size{.0};

	void
	on_msg_size_read() 
	{
		assert(base_type::async_error == false);
		assert(data >= base_type::get_default_data() && data - base_type::get_default_data() <= MaxBufferSize);
		assert(data - base_type::get_default_data() == read_some_consumed_size);
		read_some_accumulated_size += bytes;
		assert(read_some_accumulated_size > read_some_consumed_size);
		unsigned const ready_to_read(read_some_accumulated_size - read_some_consumed_size);
		if (ready_to_read > 6)
			on_msg_size_complete();
		else
			on_msg_size_incomplete(ready_to_read);
	}

	#ifndef NDEBUG
		uint_fast32_t payload_size_initiated;
	#endif
	void
	on_msg_size_complete() 
	{
		assert(strand.running_in_this_thread());
		assert(base_type::async_error == false);
		assert(data - base_type::get_default_data() == read_some_consumed_size);
		#ifndef NDEBUG
			payload_size_initiated = synapse::misc::be32toh_from_possibly_misaligned_source(data + 3);
		#endif
		auto const Channel_id(synapse::misc::be16toh_from_possibly_misaligned_source(data + 1));
		Ensure_existing_channel(Channel_id);
		channels[Channel_id]->frame_awaits();
	}

	void Ensure_existing_channel(uint_fast16_t const Channel_id) {
		if (Channel_id < max_supported_channels_end) {
			if (Channel_id >= channels.size()) {
				channels.resize(Channel_id + 1);
				base_type::set_pending_writechain_capacity(static_cast<DerivingType*>(this)->get_base_type_pending_writechain_capacity(channels.size()));
			} 
			if (!channels[Channel_id])
				channels[Channel_id] = ::boost::allocate_shared<ChannelType>(basic_alloc_1<ChannelType>(sp), static_cast<DerivingType&>(*this), Channel_id, data_processors::misc::alloc::Memory_alarm_breach_reaction_throw()); // ok because channels are not referenced by weak_ptr anywhere...
		} else
			throw ::std::runtime_error("supplied channel " + ::std::to_string(Channel_id) + " is gerater than maximum allowed " + ::std::to_string(max_supported_channels_end - 1));
	}

	void
	on_msg_size_incomplete(unsigned const ready_to_read) 
	{
		assert(strand.running_in_this_thread());
		assert(base_type::async_error == false);
		assert(data - base_type::get_default_data() == read_some_consumed_size);
		assert(ready_to_read < 7);
		unsigned target_read(7 - ready_to_read);
		if (lpf_message_size > large_message_size_threshold) {
			if (ready_to_read) {
				switch (*data) {
					case 1 : 
					target_read += lpf_basic_publish_frame_size + lpf_basic_content_header_frame_size;
					break;
					case 2 : 
					target_read += lpf_basic_content_header_frame_size;
					break;
				}
			} else
				target_read += lpf_basic_publish_frame_size + lpf_basic_content_header_frame_size;
		}
		if (target_read + ready_to_read > MaxBufferSize)
			throw ::std::runtime_error("on_msg_size_incomplete: non-body frames are simply too large for our liking");

		static_assert(MaxBufferSize > large_message_size_threshold, "MaxBufferSize ought to be bigger indeed");
		assert(read_some_accumulated_size <= MaxBufferSize);
		if (MaxBufferSize - read_some_accumulated_size < target_read) {
			assert(data - base_type::get_default_data() == read_some_consumed_size);
			assert(data > base_type::get_default_data());
			assert(data - base_type::get_default_data() > ready_to_read);
			if (ready_to_read) {
				assert(read_some_accumulated_size > ready_to_read);
				assert(MaxBufferSize >= ready_to_read);
				::memcpy(base_type::get_default_data(), data, ready_to_read);
			}
			read_some_accumulated_size = ready_to_read;
			read_some_consumed_size = 0;
			data = base_type::get_default_data();
			#ifndef NDEBUG
				Data_size = MaxBufferSize;
			#endif
		}
		assert(read_some_accumulated_size + target_read <= MaxBufferSize);
		assert(read_some_accumulated_size < MaxBufferSize);
		if (lpf_message_size > large_message_size_threshold) {
			assert(Data_size >= target_read + ready_to_read);
			base_type::template read_some<&DerivingType::on_msg_size_read>(target_read, ready_to_read);
		} else {
			assert(Data_size >= MaxBufferSize - read_some_accumulated_size + ready_to_read);
			base_type::template read_some<&DerivingType::on_msg_size_read>(MaxBufferSize - read_some_accumulated_size, ready_to_read);
		}
	}

	#if 0
	overall message alignment strategy.
	making sure that certain offsets are aligned for zero-copying:
	content_header (+11 bytes offset from amqp-native start)

	database_msg--------------------|payload_size(+4ofst,4algn)--|content_hdr_body_size(+11ofst,8algn)--\
							 amqp_msg--3 bytes--|

	\---|basic_properties(+19ofst,8algn,2size)---|content_type(+21ofst,2size)--|table_size(+23ofst,4algn,4size)--\
	\---|table_hdr(+27ofst,8size,8align=1byte(6),XXXXXX,'l')---|tadle_hdr_msg_seq_no_value(+35ofst,8algn,8size)---\
	\---|timestamp(+43ofst,8size)---|end_byte(+51ofst)---| total size 52

	#endif

	void
	on_read_some_awaiting_frame()
	{
		assert(base_type::async_error == false);
		assert(strand.running_in_this_thread());
		assert(read_awaiting_frame_initiated == true);
		assert(data >= base_type::get_default_data());
		assert(data - base_type::get_default_data() == read_some_consumed_size);
		read_some_accumulated_size += bytes;
		assert(read_some_accumulated_size <= MaxBufferSize);
		read_awaiting_frame();
	}

	#ifndef NDEBUG
		bool read_awaiting_frame_initiated{false};
	#endif
	void
	read_awaiting_frame(unsigned const ch, unsigned const payload_size)
	{
		#ifndef NDEBUG
			assert(read_awaiting_frame_initiated == false);
			read_awaiting_frame_initiated = true;
			assert(payload_size_initiated == payload_size);
		#endif
		assert(data - base_type::get_default_data() == read_some_consumed_size);
		assert (ch < channels.size());
		assert(base_type::async_error == false);
		pending_channel_on_read = ch;
		this->payload_size = payload_size;
		read_awaiting_frame();
	}

	void
	read_awaiting_frame()
	{
		assert(strand.running_in_this_thread());
		assert(base_type::async_error == false);
		assert(base_type::is_read_in_progress() == false);
		assert(data - base_type::get_default_data() == read_some_consumed_size);
		assert(read_some_accumulated_size >= read_some_consumed_size);
		auto const ready_to_read(read_some_accumulated_size - read_some_consumed_size);
		assert(payload_size_initiated == payload_size);
		auto const need_to_read(payload_size + 8);
		assert(ready_to_read > 6);
		assert(channels[pending_channel_on_read]->data != data);
		// if we have the whole frame prefetched in our (asio.connection) buffer...
		if (ready_to_read >= need_to_read) { 
			//assert(data[payload_size + 7] == 0xce);
			if (*data == 3) {
				assert(channels[pending_channel_on_read]->data != nullptr);
				assert(channels[pending_channel_on_read]->Data_size >= need_to_read);
				::memcpy(channels[pending_channel_on_read]->data, data, need_to_read);
			}
			if ((read_some_consumed_size += need_to_read) == read_some_accumulated_size) // an opportunistic move of 'data' to prefteching-buffer beginning (if possible to do so w/o any migration) this way next read will be nicely aligned.
				read_some_consumed_size = read_some_accumulated_size = 0;
			on_msg_ready();
		} else { // not enough in the prefetching buffer
			if (*data != 3) { // not a body frame, try migration within connection's buffer because of prefetching (read_some) benefits... (non-body frame ought to fit into the connection's buffer by design-decision)
				unsigned target_read(need_to_read - ready_to_read);
				if (lpf_message_size > large_message_size_threshold) {
					switch (*data) {
						case 1 : 
							target_read += lpf_basic_content_header_frame_size + 7;
							break;
						case 2 : 
							target_read += 7;
							break;
					}
				}
				if (target_read + ready_to_read > MaxBufferSize)
					throw ::std::runtime_error("read_awaiting_frame: non-body frames are simply too large for our liking");
				if (MaxBufferSize - read_some_accumulated_size < target_read) {
					assert(data > base_type::get_default_data());
					assert(MaxBufferSize >= ready_to_read);
					if (ready_to_read > read_some_consumed_size)
						::memmove(base_type::get_default_data(), data, ready_to_read);
					else {
						assert(data - base_type::get_default_data() >= ready_to_read);
						::memcpy(base_type::get_default_data(), data, ready_to_read);
					}
					read_some_accumulated_size = ready_to_read;
					read_some_consumed_size = 0;
					data = base_type::get_default_data();
					#ifndef NDEBUG
						Data_size = MaxBufferSize;
					#endif
				}
				assert(read_some_accumulated_size + target_read <= MaxBufferSize);
				assert(read_some_accumulated_size < MaxBufferSize);
				if (lpf_message_size > large_message_size_threshold) {
					assert(Data_size >= target_read + ready_to_read);
					base_type::template read_some<&DerivingType::on_read_some_awaiting_frame>(target_read, ready_to_read);
				} else {
					assert(Data_size >= MaxBufferSize - read_some_accumulated_size + ready_to_read);
					base_type::template read_some<&DerivingType::on_read_some_awaiting_frame>(MaxBufferSize - read_some_accumulated_size, ready_to_read);
				}
			} else { // body-frame... will need to do the thing...
				// migrate partial message (from cached readahead)
				assert(ready_to_read);
				assert(channels[pending_channel_on_read]->Data_size >= ready_to_read);
				::memcpy(channels[pending_channel_on_read]->data, data, ready_to_read);
				read_some_accumulated_size = read_some_consumed_size = 0;
				// read the message to the external buffer
				data = channels[pending_channel_on_read]->data;
				#ifndef NDEBUG
					Data_size = channels[pending_channel_on_read]->Data_size;
				#endif
				assert(need_to_read <= Data_size);
				base_type::template read<&DerivingType::on_msg_ready>(need_to_read - ready_to_read, ready_to_read);
			}
		}
	}

public:
	synapse::asio::basic_alloc_type<128> post_read_allocator;
protected:
	void * post_read_handler_alloc(unsigned size) { return post_read_allocator.alloc(size); }
	void post_read_handler_free(void * ptr) { return post_read_allocator.dealloc(ptr); }
	typedef void * (Connection_base::*alloc_callback_type) (unsigned); 
	typedef void (Connection_base::*free_callback_type) (void *); 
	typedef void (DerivingType::*on_post_callback_type) (); 
	template<on_post_callback_type Callback>
	using post_bind_type = synapse::asio::handler_type_1<DerivingType, on_post_callback_type, alloc_callback_type, free_callback_type, Callback, &Connection_base::post_read_handler_alloc, &Connection_base::post_read_handler_free>;

	void
	on_post_from_msg_ready() noexcept
	{
		assert(strand.running_in_this_thread());
		try {
			if (!this->async_error) {
				assert(base_type::is_read_in_progress() == false);
				#ifndef NDEBUG
					assert(read_awaiting_frame_initiated == true);
					assert(on_msg_ready_initiated == true);
					read_awaiting_frame_initiated = false;
					on_msg_ready_initiated = false;
					assert(payload_size_initiated == payload_size);
				#endif
				assert(read_some_consumed_size < MaxBufferSize);
				data = this->get_default_data() + read_some_consumed_size;
				#ifndef NDEBUG
					Data_size = MaxBufferSize - read_some_consumed_size;
				#endif
				on_msg_size_complete();
			} else
				throw ::std::runtime_error("strand.post after on_msg_ready");
		} catch (::std::exception const & e) { static_cast<DerivingType*>(this)->put_into_error_state(e); }
	}

	#ifndef NDEBUG
		bool on_msg_ready_initiated{false};
	#endif
	// template <void (DerivingType::*Callback)()> // vis studio compiler crashes if using base_type::frame_callback_type
	void
	on_msg_ready() 
	{
		assert(strand.running_in_this_thread());
		assert(!base_type::async_error);
		assert(channels.size() > pending_channel_on_read);
		//assert(data[payload_size + 7] == 0xce);
		rcv_missed_heartbeats_counter = 0;
		channels[pending_channel_on_read]->on_frame(payload_size);
		assert(base_type::is_read_in_progress() == false);

		#ifndef NDEBUG
			assert(read_awaiting_frame_initiated == true);
			assert(on_msg_ready_initiated == false);
			on_msg_ready_initiated = true;
			assert(payload_size_initiated == payload_size);
		#endif

		if (*data == 3) {
			lpf_message_size = .8f * lpf_message_size + .2f * payload_size;
			lpf_basic_publish_frame_size = .8f * lpf_basic_publish_frame_size + .2f * channels[pending_channel_on_read]->last_basic_publish_frame_size;
			lpf_basic_content_header_frame_size = .8f * lpf_basic_content_header_frame_size + .2f * channels[pending_channel_on_read]->last_basic_content_header_frame_size;
		}

		#if 0
			TODO -- either not use this "channel freeing code portion" or relocate to another event (e.g. channel.On_close_ok_frame_written callback checking if 'no pending frame is awaiting' and then calling the connection.On_channel_done which is where the freeing of channels will be done -- otherwise there is a racing condition -- channel may be closed BUT async_write may still be busy w.r.t. 'on close ok' frame being written out
			Generally speaking though, it may be well within server deployment scenario that it would be ok to retain 'channel skeleton' since on_amqp_close event will close the actual topic_streams and their ranges/topics/etc. anyway...
			// if channel is closed, "free" it...
			if (pending_channel_on_read && channels[pending_channel_on_read]->get_state() == ChannelType::state_type::none) {
				channels[pending_channel_on_read].reset();
				assert(channels.size());
				if (pending_channel_on_read == channels.size() - 1) {
					unsigned i(channels.size());
					while (!channels[--i]); 
					channels.resize(i + 1);
					base_type::set_pending_writechain_capacity(static_cast<DerivingType*>(this)->get_base_type_pending_writechain_capacity(channels.size()));
				}
			}
		#endif

		unsigned const ready_to_read(read_some_accumulated_size - read_some_consumed_size);
		if (ready_to_read > 6)
			strand.post(post_bind_type<&DerivingType::on_post_from_msg_ready>(base_type::shared_from_this()));
		else {
			assert(base_type::is_read_in_progress() == false);
			#ifndef NDEBUG
				assert(read_awaiting_frame_initiated == true);
				assert(on_msg_ready_initiated == true);
				read_awaiting_frame_initiated = false;
				on_msg_ready_initiated = false;
				assert(payload_size_initiated == payload_size);
			#endif
			data = this->get_default_data() + read_some_consumed_size;
			#ifndef NDEBUG
				Data_size = MaxBufferSize - read_some_consumed_size;
			#endif
			on_msg_size_incomplete(ready_to_read);
		}

	}

	template <void (ChannelType::*Callback)(), typename Frame> // vis studio compiler crashes if using base_type::write_frame_callback_type
	void
	write_frame(ChannelType * ch, Frame && frm)
	{
		auto const Spaces(write_data.Free());
		bam::OutBuffer out_buffer(Spaces.A_Begin, Spaces.A_Size);
		frm.to_wire(out_buffer);
		this->template Write_Internal_Buffer<Callback>(ch, out_buffer.size());
		snd_heartbeat_flag = false;
	}

	basio::deadline_timer heartbeat_timer{strand.get_io_service()};
	uint64_t heartbeat_frame{0};
	uint_fast32_t constexpr static Maximum_heartbeat_frame_rate{600};
	uint_fast32_t heartbeat_frame_rate{Maximum_heartbeat_frame_rate}; // 0 to disable heartbeats
	uint_fast8_t rcv_missed_heartbeats_counter{0};
	bool snd_heartbeat_flag{true};
	void
	on_heartbeat_timer_ping() 
	{
		assert(heartbeat_frame_rate);
		assert(strand.running_in_this_thread());
		// check if peer is still there...
		if (++rcv_missed_heartbeats_counter > 2)
			throw ::std::runtime_error("stale peer detected " + ::std::to_string((uintptr_t)this));
		// reciprocate by sending our heartbeat to peer
		if (snd_heartbeat_flag == true && state >= state_type::open && !base_type::is_write_in_progress_or_imminent()) {
			dispatch([this]()->void {
				++channels[0]->async_fence_size; // TODO better encapsulation
				this->template Write_External_Buffer<&ChannelType::Decrement_async_fence_size_and_process_awaiting_frame_if_any>(channels.data()->get(), reinterpret_cast<char unsigned *>(&heartbeat_frame), 8); // hack (exception really -- using ch.0 to do the writing)
			}, false, 8);
		}
		snd_heartbeat_flag = true;
		// schedule the next beat of the heart
		dispatch_heartbeat_timer();
	}

	void
	dispatch_heartbeat_timer() 
	{
		assert(heartbeat_frame_rate);
		heartbeat_timer.expires_from_now(::boost::posix_time::seconds(heartbeat_frame_rate));
		auto c_(base_type::shared_from_this());
		heartbeat_timer.async_wait([this, c_](::boost::system::error_code const & e) {
			if (!e)
				strand.post([this, c_]() noexcept {
					if (!this->async_error)
						try {
							on_heartbeat_timer_ping();
						} catch (::std::exception const & e) { static_cast<DerivingType*>(this)->put_into_error_state(e); }
				});
		});
	}

	enum class state_type {none, protocol_established, started, tuned, open};
	state_type state = state_type::none;

public:
	void
	put_into_error_state(::std::exception const & e) 
	{
		assert(strand.running_in_this_thread());
		heartbeat_timer.cancel();
		base_type::put_into_error_state(e);
		// NOTE -- can't simply nuke the channels (e.g. channels.clear()) -- they are contained by value and in their own (e.g. topic_stream) async methods pass around the connection.shared_from_this() pointer as the life-scope management mechanism... and so channels' life-duration must match that of connection (otherwise channel goes out of scope and takes out other things it owns, then async method returns and interacts with the deleted data)
		// channels.clear();
		// if resource-hogging issue becomes relevant, then TODO -- will need to consider fine-grained release of topic_stream's data: calling on range.reset() etc. (will need to make sure still that calling from whithin the correct strand).
		// ... moverover, must take into account that async/overlapped sparsification may yield 'next range' being gotten to have invalid data *unless* one holds on to the current range's scope until the new/next range is obtained...
		// Overall though, the said resource-hogging should not be particularly apparent because 'base_type::put_into_error_state' essentially closes the socket anyway and imminent overall-closure is to be expected... i think...
	}
protected:
	unsigned constexpr static init_supported_channels{2};

public:
	Connection_base(unsigned max_pending_write_resquests)
	: base_type(max_pending_write_resquests)
	{ 
		static_assert(amqp_0_9_1::max_supported_channels_end, "must support at least 1 channel");
		static_assert(amqp_0_9_1::max_supported_channels_end > init_supported_channels, "max supported channels ought to be larger...");
		channels.reserve(init_supported_channels);
		for (unsigned i(0); i != init_supported_channels; ++i) 
			channels.emplace_back(::boost::allocate_shared<ChannelType>(basic_alloc_1<ChannelType>(sp), static_cast<DerivingType&>(*this), i, data_processors::misc::alloc::Memory_alarm_breach_reaction_no_throw())); // ok because channels are not referenced by weak_ptr anywhere...
		channels[0]->max_message_size = max_message_size;

		char unsigned * const heartbeat_frame_bytes_ptr(reinterpret_cast<char unsigned *>(&heartbeat_frame));
		heartbeat_frame_bytes_ptr[0] = 0x8; // documentation is a bit erroneous--in some places it states it as 8, in others as 4--RabbitMQ sends 8 though...
		heartbeat_frame_bytes_ptr[7] = 0xce;
	}
	~Connection_base()
	{
		#ifndef NDEBUG
			for (auto && c : channels) {
				assert(!c || c.unique());
			}
		#endif
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
