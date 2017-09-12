#include <memory> 
#include <set>
#include <queue>

#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>

#include "../misc/alloc.h"
#include "async_file.h"
#include "message.h"

#ifndef DATA_PROCESSORS_SYNAPSE_DATABASE_RANGE_H
#define DATA_PROCESSORS_SYNAPSE_DATABASE_RANGE_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

::std::atomic_uint_fast64_t static Maximum_sequence_number{1};

namespace basio = ::boost::asio;

/** 
Q: should the file read the data in one chunk (more efficient IO I guess); or one message at a time (more responsive to clients who won't have to wait till the whole range-span to get loaded from disk)?
A: load the whole range in one go. Only the 1st chunk load will experience latency *and* it will not be for the realtime data anyways as such is in RAM implicitly. The subsequent chunks will not experience latency as the async request for the upcoming/following-the-current range will be done in advance (before the current range is finished with and new one is neede).
 */
template <unsigned TargetBufferSize, unsigned PageSize, typename Topic>
struct range : 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
synapse::misc::Test_new_delete<range<TargetBufferSize, PageSize, Topic>>,
#endif
private ::boost::noncopyable 
{
	struct scoped_atomic_bool { 
		// TODO later on just use C++14 moveable semantics, just need to make sure that strand.wrap etal support it under the hood.
		range mutable * Range;
		scoped_atomic_bool(range * Range) 
		: Range(Range) { 
			Range->message_being_appended.store(true);
		}
		scoped_atomic_bool(scoped_atomic_bool const & rhs)
		: Range(rhs.Range) { 
			rhs.Range = nullptr;
		}
		void 
		reset()
		{
			reset_impl();
			Range = nullptr;
		}
		~scoped_atomic_bool()
		{
			// TODO better logic/elegance
			if (Range != nullptr) {
				reset_impl();
				Range->Message_canceled();
			}
		}
	private:
		void
		reset_impl()
		{
			assert(Range != nullptr);
			Range->message_being_appended.store(false, ::std::memory_order_release); // racing condition when max_msg_seq_no is incremented for a given message but other message/thread get their back_msg updated and may cause skipping of this message (wher server tries to preserve the sequential order access)...
		}
	};

	static_assert(synapse::misc::is_multiple_of_po2(TargetBufferSize, PageSize), "TargetBuffersize ought to be power of 2");

	uint_fast64_t constexpr static page_size_mask = synapse::misc::po2_mask<uint_fast64_t>(PageSize);

	uint_fast32_t constexpr static pending_notification_mask = static_cast<uint_fast32_t>(1u) << 31;
	uint_fast32_t constexpr static is_null_value_mask = static_cast<uint_fast32_t>(1u) << 30;
	uint_fast32_t constexpr static Invalidated_by_sparsification_mask = static_cast<uint_fast32_t>(1u) << 29;
	uint_fast32_t constexpr static value_mask = ~(pending_notification_mask | is_null_value_mask | Invalidated_by_sparsification_mask);
	static_assert(value_mask > database::MaxMessageSize, "Cannot support such a large database::MaxMessageSize in " __FILE__ ", line:" DATA_PROCESSORS_STRINGIFY(__LINE__));

	::std::atomic<uint_fast32_t> ref_counter;

	uint_fast32_t
	use_count() const noexcept
	{
		return ref_counter.load(::std::memory_order_relaxed);
	}

	bool
	unique() const noexcept
	{
		return use_count() == 1;
	}

	friend void
	intrusive_ptr_add_ref(range * r)
	{
		if (::std::atomic_fetch_add_explicit(&r->ref_counter, 1u, ::std::memory_order_relaxed) == ::std::numeric_limits<uint_fast32_t>::max())
			throw ::std::runtime_error("too many shared references of range object -- past 32bit max capabilities, need to recompile");
	}

	friend void
	intrusive_ptr_release(range * r)
	{
		assert(r);
		assert(r->t);
		auto const Previous_reference_counter(::std::atomic_fetch_sub_explicit(&r->ref_counter, 1u, ::std::memory_order_release));
		switch (Previous_reference_counter) {
			case 2 :
				r->t->notify_range_became_possibly_available(); // send topic notification about possible free range (so that the resource-checking async loop will pick it up sooner rather than later
			break;
			case 1 :
				::std::atomic_thread_fence(::std::memory_order_acquire); // force others to be done on memory access before actual deletion
				delete r;
		}
	}

	::boost::intrusive_ptr<range>
	intrusive_from_this()
	{
		return ::boost::intrusive_ptr<range>(this);
	}

	::boost::shared_ptr<Topic> t; 

	// starting location of the range's valid view into the file's data
	uint_fast64_t begin_byte_offset = 0; // TODO see if can use file.buffer_offset instead (as it is already there amongst other data members of this class)

	/** 
		\about ending range's data. semantically speaking it is set to -1 to intdicate currently published-to range, and for other ranges to indicate valid data contained in a range (units are absolute byte offset from the very start of the file, not range's buffer)
		*/
	uint_fast64_t end_byte_offset = 0; 

	/*
		index_keys are in 1st message
		Q. but what happpens when the new range has not yet received any of the messages from publisher... yet it has to be in the indicies of the topic???
		A. the range must be constructable without any indicies... (and not added right away to the indicies)
		for the subscribers if no range is present, they will just have to wait 
		for the publishers -- the range is created anyway and when 1st message arrives the range is added to the indicies
	*/
	database::Message_ptr msg; 

	// TODO  -- A MUST FOR PERFORMANCE -- make 'get_back_msg' also private -- to reduce number of times the atomic variable is accessed, instead either may 'get_next_message' automatically return back msg value as well; or generally speaking expose getting back_msg_begin_buffer_byte_offset atomically to the clients which will then be passed as a non-atomic arg to things like get_back_msg, get_next_message, etc.


	// denotes last valid message in the range's data (buffer), updated atomically by the publisher, observed lockless by the subscribers
	// most significant bit is for 'want pending notifications' flag
	::std::atomic<uint_fast32_t> back_msg_begin_buffer_byte_offset;

	::std::atomic_bool message_being_appended{false};

	///\TODO for the ranges which are definitively 'non-open-ended', this method may return non-atomically used variables (such can be cached upon the range's creation/loading/setting)... This would accelerate the use of the consumers of historical data (not a big deal in itself, but it may then also free-up some performance resources for the sake of the, possibily-resident, realtime (open-ended) ranges and their consumers.
	///\threadsafe(ish)
	database::Message_const_ptr
	get_back_msg() const noexcept
	{
		return Get_back_msg_helper(back_msg_begin_buffer_byte_offset);
	}

	database::Message_const_ptr 
	Get_back_message_or_throw_if_invalidated_by_sparsification() const {
		uint_fast32_t const tmp(back_msg_begin_buffer_byte_offset);
		if (tmp & Invalidated_by_sparsification_mask)
			throw ::std::runtime_error("Get_back_message_or_throw_if_invalidated_by_sparsification: invalidated by sparsification");
		else 
			return Get_back_msg_helper(back_msg_begin_buffer_byte_offset);
	}

private:
	database::Message_const_ptr 
	Get_back_msg_helper(uint_fast32_t const & tmp) const noexcept {
		if (tmp & is_null_value_mask)
			return nullptr;
		else
			return get_existing_back_msg(tmp);
	}

	database::Message_const_ptr
	get_existing_back_msg(uint_fast32_t const & tmp) const noexcept
	{
		assert(!(tmp & is_null_value_mask));
		return file.buffer_begin() + (tmp & value_mask);
	}
public:
	::std::queue< ::std::function<void()>, ::std::deque< ::std::function<void()>, data_processors::misc::alloc::basic_alloc_1< ::std::function<void()>>>> pending_notifications{typename decltype(pending_notifications)::container_type::allocator_type{t->sp}};

	// actual data file, TODO consider using unmanaged async file (the size itself ought to be dealt with by the topic class?)
	synapse::database::async_file<PageSize, database::Data_meta_ptr> file; 

	// TODO consider having partial dirtiness in the range object (currently ranges are simply arranged that if wanted data is on file, then range won't consider it's unused buffer for the 'to be published' data, instead expecting a new range/buffer be generated (where only the new/published data wil be birected) -- thusly if it gets data from file, then it is only file-related data, read-only, if not then it's either dirty or not at all... currently this makes for a simpler design and coding (timewise) but will nede to re-condiser later on...

public: // for the time being...
	typedef typename Topic::ranges_type::iterator ranges_membership_type;
	ranges_membership_type ranges_membership;
	bool
	get_error() const
	{
		return async_error;
	}

private:
	// iterator to itself in the outer index containers (allows faster deletion from said containers when dtor of this takes place)
	typedef typename Topic::index_with_ranges_type::ranges_index_type::iterator index_membership_type;
	::std::vector<index_membership_type, data_processors::misc::alloc::basic_alloc_1<index_membership_type>> index_memberships; 

	bool async_error = false;

public:
	void
	put_into_error_state(::std::exception const & e) 
	{
		assert(t->strand.running_in_this_thread());
		synapse::misc::log(::std::string("exception(=") + e.what() + ")\n");
		t->async_error = async_error = true;
		process_pending_notifications(); ///\note for lightweight reasons (not to burden performance-critical path too much), the callbacks in this case do not carry an 'error' code, the client is responsible for checking the error state... but it has to check for things like end_byte_offset != -1 and own_msg != back_msg anyways...
	}

	range(::boost::shared_ptr<Topic> const & t) 
	: ref_counter(0), t(t) , file(t->strand, t->Meta_file, t->Async_file_handle_wrapper, TargetBufferSize), ranges_membership(t->ranges.end()), index_memberships(typename decltype(index_memberships)::allocator_type{t->sp}) {
		assert(t->strand.running_in_this_thread());
		assert(back_msg_begin_buffer_byte_offset.is_lock_free());
		assert(ref_counter.is_lock_free());

		assert(t->indicies.size() == Topic::indicies_size);
		index_memberships.reserve(Topic::indicies_size);
		for (auto && index : t->indicies)
			index_memberships.push_back(index->ranges.end());

		//synapse::misc::log("new range " + ::boost::lexical_cast<::std::string>(this) + '\n');
	}

	///\note essentially a second part of constructor -- a side-effect of the pool-allocation -caching of 'range' objects by topic object et. al.
	void
	init(uint_fast64_t const & begin_byte_offset, uint_fast64_t const & end_byte_offset)
	{
		assert(t->strand.running_in_this_thread());
		assert(file.async_io_in_progress == false);
		msg.Reset(); 
		this->begin_byte_offset = begin_byte_offset; 
		this->end_byte_offset = end_byte_offset;
		back_msg_begin_buffer_byte_offset = is_null_value_mask;

		if (end_byte_offset != static_cast<decltype(end_byte_offset)>(-1)) {
			loaded_from_disk = true;
			file.ensure_reserved_capacity_no_less_than(synapse::misc::round_up_to_multiple_of_po2<uint_fast64_t>(end_byte_offset, PageSize) - synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(begin_byte_offset, PageSize));
			file.set_buffer(begin_byte_offset, end_byte_offset - begin_byte_offset);
		} else {
			loaded_from_disk = false;
			file.ensure_reserved_capacity_no_less_than(TargetBufferSize);
			file.set_buffer(begin_byte_offset, 0); // sets pagesize-related initial offset (first_data_offset_i), OK not to worry about partial PageSize issues at the beginning because the writing-to-disk part is done by the topic class which manages pagesize atomicity via it's own write-out code
		}

		// Q. Wouldnt there be a problem of one range partially clobbering/overwriting valid data in another range's buffer because there is not a byte-level but rather PageSize level of granularity and so adjacent ranges may have the same "PageSize" block of data partially written/read by each of the concerned range... and so when the write-to-disk event is invoked, one range can have stale/non-current data in it's remainder of the PageSize block and write it out to disk thereby corrupting the data file?
		// A. Nope, it's ok -- for as long as there is only one writer at a time and such writers are performing their 'write-to-disk' duties in ascending order. This is expected behavior, controlled by the "topic" class (e.g. a queue of pending writers, daisy-chain-linked one after another in typical async fashion). Moreover once the range is written, it is only (semantically speaking) readable from then onwards (i.e. only the new range can ever become a writer).

	}

	~range()
	{
		assert(!ref_counter);
		//synapse::misc::log("dtor " + ::boost::lexical_cast<::std::string>(this) + '\n', true);
	
		// TODO think about it some more -- currently the dtor can be invoked on the topic from topic's factory (which does not run in each topic's strands), or from the unit test like topic_test.cpp which also runs in its own thread... theoretically though these "outer callers" ought to ensure that all of the ranges are inactive and the topic's dtor can proceed... etc. will have to think about it some more... 
		//assert(t->strand.running_in_this_thread());
		
		// for the time-being at least release callbacks (no memory leaking)
		decltype(pending_notifications) clear{typename decltype(pending_notifications)::container_type::allocator_type{t->sp}};
		::std::swap(pending_notifications, clear);
	}

	// when transitioning from 'in progress' to being active and ready for interaction with publisher/consumers
	void
	set(uint_fast32_t const & back_msg_begin_buffer_byte_offset) 
	{
		assert(t->strand.running_in_this_thread());
		assert(file.get_meta().get_data_size());
		assert(begin_byte_offset < end_byte_offset);
		assert(end_byte_offset <= file.get_meta().get_data_size());

		assert(back_msg_begin_buffer_byte_offset <= value_mask);

		// default to processing the 'new msg' notification (double-posting and false positives are ok by design anyway)
		// TODO a bit more elegance in future if need be

		this->back_msg_begin_buffer_byte_offset = back_msg_begin_buffer_byte_offset
		// | pending_notification_mask
		;

		map_first_msg();
	}
	
	/*
		 convenience method, NOT thread-safe, mainly for topic looking at existing ranges and trying to see if they were 'set' (e.g. read from disk) -- as per topic.h async resource loop checking mechansim
  */
	database::Message_const_ptr
	get_first_msg() const
	{
		assert(t->strand.running_in_this_thread());
		return msg;
	}

	void
	ensure_reserved_capacity_no_less_than(uint_fast32_t const capacity)
	{
		file.ensure_reserved_capacity_no_less_than(synapse::misc::round_up_to_multiple_of_po2<decltype(begin_byte_offset)>(capacity + begin_byte_offset, PageSize) - synapse::misc::round_down_to_multiple_of_po2<decltype(begin_byte_offset)>(begin_byte_offset, PageSize));
	}

	///\note this method exists because cannot do it in ctor because shared_from_this cannot be called from within ctor of the '::boost::enabled_shared_from_this' class
	void
	obtain_topic_membership()
	{
		assert(t->strand.running_in_this_thread());
		/// \note care must be taken if in future a mixed (partially from file, partially in 'future' RAM data) dirtiness traits will be deployed... in such a case can't use begin_byte_offset -- there must be a value passed indicating how many bytes were already there (end_byte_offset is not used for this at the moment -- e.g. set to -1 for "future" ranges)... moreover at this point in time such variable is only meaningful when end_byte_offset is -1
		assert(t->ranges.find(begin_byte_offset) == t->ranges.end()); 
#ifndef NDEBUG
		auto Emplaced_key_value_pair(t->ranges.emplace(begin_byte_offset, intrusive_from_this())); 
		ranges_membership = Emplaced_key_value_pair.first;
		assert(Emplaced_key_value_pair.second == true);
#else
		ranges_membership = t->ranges.emplace(begin_byte_offset, intrusive_from_this()).first;
#endif
		t->schedule_resource_checking_timer();
	}

	void
	resign_from_topic_membership()
	{
		assert(t->strand.running_in_this_thread());
		assert(index_memberships.size() == t->indicies.size());
		for (unsigned i(0); i != index_memberships.size(); ++i) {
			if (index_memberships[i] != t->indicies[i]->ranges.end()) {
				assert(index_memberships[i]->second == this);
				// cant test for exact index_memberships[i] being the same interator as in index->ranges because such ranges is a multimap container...
				assert(t->indicies[i]->ranges.find(index_memberships[i]->first) != t->indicies[i]->ranges.end());
				t->indicies[i]->ranges.erase(index_memberships[i]);
				index_memberships[i] = t->indicies[i]->ranges.end();
			}
		}
		if (ranges_membership != t->ranges.end()) {
			//synapse::misc::log("resigning from topic membership " + ::boost::lexical_cast<::std::string>(this) + '\n', true);
			assert(ranges_membership->second.get() == this);
			assert(t->ranges.find(ranges_membership->first) == ranges_membership);
			t->ranges.erase(ranges_membership);
			ranges_membership = t->ranges.end();
		}

		// also release all pending notifications... (because dtor will not be invoked when the range object is being pool-cached by the topic class for further re-use later on...)
		// for the time-being at least release callbacks (no memory leaking)
		// TODO, revisit in future to try using brace-initialization syntax
		decltype(pending_notifications) clear{typename decltype(pending_notifications)::container_type(typename decltype(pending_notifications)::container_type::allocator_type{t->sp})};
		::std::swap(pending_notifications, clear);


		// not to hog too many resources... some large buffers would have to be freed I think...
		unsigned const tolerated_size(TargetBufferSize * 1.1);
		if (file.reserved_capacity() > tolerated_size)
			file.ensure_reserved_capacity_no_greater_than(synapse::misc::round_up_to_multiple_of_po2<uint_fast64_t>(tolerated_size, PageSize));
	}

private: 
	void 
	map_first_msg() 
	{
		assert(t->strand.running_in_this_thread());

		// load the 1st message
		msg.Reset(file.buffer_begin());
		assert(t->indicies.size() == Topic::indicies_size);
		// add the range to all indicies...
		uint32_t const Incoming_Message_Indicies_Size(msg.Get_indicies_size());
		for (unsigned i(0); i != Topic::indicies_size; ++i) {
			auto & index(t->indicies[i]);
			if (index->Index_In_Message >= Incoming_Message_Indicies_Size)
				throw ::std::runtime_error("compatibility problem between recorded data file and runtime environment -- number of indicies does not resolve");
			// quantise the key
			uint_fast64_t const quantised_key(msg.Get_index(index->Index_In_Message) & index->key_mask);
			// add to the index and remember added membership position (for easier deletion during destructor)
			index_memberships[i] = index->ranges.emplace(quantised_key, this);
		}
		process_pending_notifications();
	}

	void
	process_pending_notifications()
	{
		assert(t->strand.running_in_this_thread());
		while (pending_notifications.empty() == false) {
			// TODO -- recall why not processing directly in strand... (perhaps because dont want to block on peer's callback processing (i.e. pending_notifications store lambdas/callbacks)... on the other hand all such callbacks usually wrap themselves in their own strand (thereby posting to their own io service implicitly))
			t->strand.get_io_service().post(pending_notifications.front());
			pending_notifications.pop();
		}
	}

public:

	void 
	Invalidate_due_to_sparsification() {
		if ((back_msg_begin_buffer_byte_offset |= Invalidated_by_sparsification_mask) & pending_notification_mask) {
			t->strand.dispatch([this, this_(intrusive_from_this())]() noexcept {
				try {
					process_pending_notifications();
				} catch (::std::exception const & e) { put_into_error_state(e); }
			});
		}
	}

	void Message_canceled() {
		assert(!message_being_appended);
		if (back_msg_begin_buffer_byte_offset & pending_notification_mask) {
			t->strand.dispatch([this, this_(intrusive_from_this())]() noexcept {
				try {
					process_pending_notifications();
				} catch (::std::exception const & e) { put_into_error_state(e); }
			});
		}
	}

	///\TODO fix/clarify/re-verify the semantics of this method. currently I am thinking: guaranteed not to callback more than once, but could callback with same arguments as the supplied one -- i.e. false-positive is possible; or may not call at all (e.g. during dtor).
	///\TODO consider moving 'msg' param to two explicit msg_byte_offset and msg_size_on_disk parameters
	template <typename Callback>
	void
	register_for_notification(database::Message_ptr const msg, Callback && callback, bool const Notification_wanted_due_to_message_being_appended)
	{
		auto this_(intrusive_from_this());
		t->strand.post([this, this_, msg, callback, Notification_wanted_due_to_message_being_appended]() mutable {
			assert(t->strand.running_in_this_thread());
			uint_fast32_t const tmp(back_msg_begin_buffer_byte_offset |= pending_notification_mask); // must be here (due to lockless, double-check, arrangement of checks between subscriber(s) and publisher)
			if (this->msg) {
				database::Message_const_ptr const existing_back_msg(get_existing_back_msg(tmp));
				if (is_open_ended() == true) { // open ended range, message is appended only after the crc32 is calculated
					assert(loaded_from_disk == false);
					if (msg != existing_back_msg || Notification_wanted_due_to_message_being_appended && !is_message_being_appended()) { // OK, logical && has higher precedence than ||
						callback();
						return;
					}
				} else if (loaded_from_disk == true) { // closed range, but message could still be in crc32-verification progress (todo may be deprecate if moving to ReFS/ZFS)
					assert(existing_back_msg);
					assert(existing_back_msg >= this->msg);
					assert (msg != existing_back_msg);
					database::Message_ptr const next_msg(!msg ? this->msg : msg.Get_next());
					assert(next_msg <= existing_back_msg);
					assert(next_msg);
					if (verify_hash(next_msg) == true) {
						callback();
						return;
					} 
				} else { // range closed, but was open before at some point...
					// due to lockless design (client may see range as still open, but here, the range is now closed -- need to send notification... otherwise won't be notified at all), so even if msg == existing_back_msg, still need to send notification
					callback();
					return;
				}
			} else if (Notification_wanted_due_to_message_being_appended && !is_message_being_appended()) {
				callback();
				return;
			}
			pending_notifications.emplace(callback);
		});
	}

	///\about get next message from current for the range class
	///\note can return the same message as supplied (which may be nullptr)
	///\note 'current_msg' can be nullptr
	///\threadsafe(ish)
	database::Message_ptr
	get_next_message_impl(database::Message_ptr const current_msg, database::Message_const_ptr & back_message)
	{
		auto Get_existing_next_message([&]()->database::Message_ptr{
			if (current_msg) {
				assert(back_message);
				assert(current_msg <= back_message);
				assert(current_msg.Get_size_on_disk());
				assert(current_msg.Get_size_on_disk() <= database::MaxMessageSize && (current_msg.Raw() - file.buffer_begin()  + current_msg.Get_size_on_disk() <= file.buffer_capacity()));
				assert(reinterpret_cast<uintptr_t>(current_msg.Raw()) + current_msg.Get_size_on_disk() <= reinterpret_cast<uintptr_t>(back_message.Raw()));
				// the next message to return
				auto rv(current_msg.Get_next());
				assert(!(reinterpret_cast<uintptr_t>(rv.Raw()) % 8));
				return rv;
			} else {
				assert(back_message);
				return file.buffer_begin(); // 'msg' is written to on another thread, non-atomically!
			}
		});
		if (current_msg != back_message) {
			return Get_existing_next_message();
		} else {
			back_message = get_back_msg();
			if (current_msg == back_message) 
				return current_msg;
			else
				return Get_existing_next_message();
		}
	}

private:
	/*
	 * method for dealing (sync or async) with crc verification of data read from HDD (todo -- will be deprecated if moving to ZFS or ReFS)
	 * returns true if was done in-place
	 * false if async process was started
	 */
	bool
	verify_hash(database::Message_ptr const next_msg)
	{
		assert(loaded_from_disk == true);
		assert(next_msg);
		/*
		 * on the subject of calculating hash(crc32) during reading of the messages from disk (for the sake of HDD-stored data integrity, etc.) -- it ideally should be deprecated (i.e. the underlying file-system ought to do this mostly with remaining parts being done at the time of server-startup: i.e. server checking the last message's integrity (in topic_factory, topic.h)
		 * however, currently leaving this in -- because of heavy deployment on pre-W8/ReFS or pre-ZFS (e.g. W7/NTFS) systems... so need to do some work which would have been otherwise done by the FS layer (currently, therefore there is some penalty, performance-wise).
		 * all of the above of course depends on whether the OS and disk drivers guarantee that writing multiple memory-pages to file (e.g. with possible h/w caching in RAID card for performance reasons) will be sequential in case of system failier, or indeed a program's crash (e.g. writing of range data is not per message-boundary but rather per range or so -- once again for performance reasons, so checking just the last message may not be feasible in this case, yet checking the whole of the data is not practical either... so the server's startup could be forced to go through an arbitraty-set 'history-backlog' intergrity checking)
		 */

		///\note an alternative would have been to verify all of the buffer when loading range from disk (thereby no need for any atomics related to hashing)... the only concern so far is that what if the size of range is rather large (e.g. 100Meg)? At the very least, just scanning the buffer will invalidate a whole lot of CPU caches... before the code will 'jump back to start of the buffer' and start serving out messages again..
		auto Verified_hash_status_begin(next_msg.Get_verified_hash_status_begin());
		if (next_msg.Atomicless_get_verified_hash_status(Verified_hash_status_begin) != 0x3) { // opportunistic atomicless try
			// if message is small-enough, do it synchronously ...
			if (next_msg.Get_size_on_wire() < 1024 * 128) {
				if (next_msg.Sequentially_consistent_atomic_get_verified_hash_status(Verified_hash_status_begin) != 0x3) {
					uint32_t const hash(next_msg.Calculate_hash());
					// if computed hash is as expected, then store it for other users of this range/message in RAM presently
					if (hash == next_msg.Get_hash())
						next_msg.Released_atomic_set_verified_hash_status(Verified_hash_status_begin, 0x3);
					else // ... otherwise, if computed hash is not the same as stored on disk -- data corruption occured
						throw ::std::runtime_error("corrupted message on disk via sync. crc32c check -- hash values don't match");
				}
			} else { // ... otherwise, will need to do the async version...
				auto const crc_verification_status(next_msg.Atomic_fetch_or_verified_hash_status(Verified_hash_status_begin, 0x1));
				if (!(crc_verification_status & 0x1)) { // try to signal that this instance of run will evaluate crc
						auto this_(intrusive_from_this());
						next_msg.Async_calculate_hash(t->strand.wrap([this, this_, next_msg, Verified_hash_status_begin](uint32_t calculated_hash) noexcept {
							try {
								if (!async_error) {
									if (calculated_hash == next_msg.Get_hash()) {
										next_msg.Released_atomic_set_verified_hash_status(Verified_hash_status_begin, 0x3);
										process_pending_notifications();
									} else
										throw ::std::runtime_error("corrputed message on disk -- hash values don't match");
								} else
									throw ::std::runtime_error("on async_calculate_hash range obj is already in async_error...");
							} catch (::std::exception const & e) { put_into_error_state(e); }
						}));
						return false;
				} else if (crc_verification_status != 0x3) // someone else is currently calculating
					return false;
			}
		} 
		return true;
	}

	// TODO -- more elegance and thought please...
	bool loaded_from_disk;
public:
	auto Is_loaded_from_disk() const {
		return loaded_from_disk;
	}
	database::Message_ptr
	get_next_message(database::Message_ptr const current_msg, database::Message_const_ptr & back_message)
	{
		assert(!current_msg || back_message && current_msg <= back_message);
		database::Message_ptr next_msg(get_next_message_impl(current_msg, back_message));
		if (loaded_from_disk == true && next_msg) { // ... but first verify the data (via crc/hash values)
			if (verify_hash(next_msg) == true)
				return next_msg;
			else
				return current_msg;
		}
		return next_msg;
	}

	void 
	on_max_key_update(bool error) { // minor utility to keep track of pending index updates/writes
		if (error || async_error)
			put_into_error_state(::std::runtime_error("on_max_key_update"));
	};

	uint_fast32_t
	get_written_size() const 
	{
		uint_fast32_t const tmp(back_msg_begin_buffer_byte_offset);
		if (!(tmp & is_null_value_mask)) {
			assert(get_back_msg());
			assert(get_existing_back_msg(tmp).Raw() >= file.buffer_begin());
			return (tmp & value_mask) + get_existing_back_msg(tmp).Get_size_on_disk();
		} else
			return 0;
	}

	uint_fast32_t
	get_written_size(database::Message_const_ptr & back_msg) const 
	{
		uint_fast32_t const tmp(back_msg_begin_buffer_byte_offset);
		if (!(tmp & is_null_value_mask)) {
			assert(get_back_msg());
			assert(get_existing_back_msg(tmp).Raw() >= file.buffer_begin());
			back_msg = get_existing_back_msg(tmp);
			return (tmp & value_mask) + back_msg.Get_size_on_disk();
		} else {
			back_msg.Reset();
			return 0;
		}
	}

	uint_fast64_t
	get_total_size() const
	{
		return begin_byte_offset + get_written_size();
	}

	uint_fast64_t
	get_total_size(database::Message_const_ptr & back_msg) const
	{
		return begin_byte_offset + get_written_size(back_msg);
	}

	bool
	is_message_being_appended() const noexcept
	{
		return message_being_appended.load();
	}

	/**
		\about lets the range object know that a message has been appended to its buffer (i.e. this->file.buffer)
		\note When receiving the range object, the writer/publisher will be able to query and determine range's max index values and max data end... this way it is up to the publisher to make sure that message is only appended (i.e. written to the range object) if it will fit into the buffers of the range... if not then ask for a different range
		\note this comes from a non-stranded, concurrent, thread (but one at a time -- from a single designated writer/publisher)
		\param 'Appended_message' should be mapped in buffer already (ala zero-copy, cap'n proto style)
		\note overall design is that the file-writing mechanisms (buffer setting etc.) is done by the topic class
		\TODO consider moving 'Appended_message' param to two explicit msg_byte_offset and msg_size_on_disk parameters
		*/
	void
	message_appended(database::Message_const_ptr const Appended_message, scoped_atomic_bool & msg_app_racing_cond) 
	{
		// Note: file.{buffer_begin,buffer_capacity} are immutable at this stage (i.e. set by the topic strand before making the range available to the TCP producer thereby interthread safe with 'message_appended' which is called by the producer.

		uint_fast32_t const back_msg_size(Appended_message.Get_size_on_disk());
		uint_fast32_t const back_msg_begin_buffer_byte_offset(reinterpret_cast<uintptr_t>(Appended_message.Raw()) - reinterpret_cast<uintptr_t>(file.buffer_begin()));
		uint_fast64_t const back_msg_begin_byte_offset(begin_byte_offset + back_msg_begin_buffer_byte_offset);

		if (back_msg_size > database::MaxMessageSize || back_msg_begin_buffer_byte_offset + back_msg_size > file.buffer_capacity())
			throw ::std::runtime_error("faulty message length -- exceedes various buffer sizes et. al.");

		assert(reinterpret_cast<uintptr_t>(Appended_message.Raw()) >= reinterpret_cast<uintptr_t>(file.buffer_begin()));
		assert(back_msg_begin_buffer_byte_offset <= value_mask);


		// here check contribution to disk size...
		uint_fast64_t Message_contribution_to_disk_size(back_msg_size);

		// ... checking special (msg_boundary) index
		uint_fast64_t const back_msg_end_byte_offset(back_msg_begin_byte_offset + back_msg_size);
		uint_fast64_t const candidate_key(back_msg_end_byte_offset & t->msg_boundary_index->key_mask);
		auto const Message_boundary_index_Last_key_by_publisher(t->msg_boundary_index->last_key_by_publisher);
		if (candidate_key != Message_boundary_index_Last_key_by_publisher && Message_boundary_index_Last_key_by_publisher != static_cast<decltype(Message_boundary_index_Last_key_by_publisher)>(-1)) {
			assert(candidate_key > t->msg_boundary_index->last_key_by_publisher);
			Message_contribution_to_disk_size += t->msg_boundary_index->Get_key_distance_size_on_disk(candidate_key - Message_boundary_index_Last_key_by_publisher);
		}
		// ... not forgetting the other, regular, index file(s)
		for (auto const & index : t->indicies) {
			assert(Appended_message.Get_indicies_size() > index->Index_In_Message);
			uint_fast64_t const candidate_key(Appended_message.Get_index(index->Index_In_Message) & index->key_mask);
			auto const Last_key_by_publisher(index->last_key_by_publisher);
			if (candidate_key != Last_key_by_publisher && Last_key_by_publisher != static_cast<decltype(Last_key_by_publisher)>(-1)) {
				assert(candidate_key > index->last_key_by_publisher);
				Message_contribution_to_disk_size += index->Get_key_distance_size_on_disk(candidate_key - Last_key_by_publisher);
			}
		}

		auto const Current_database_size(database::Database_size.fetch_add(Message_contribution_to_disk_size, ::std::memory_order_relaxed));
		if (Current_database_size + Message_contribution_to_disk_size > database::Database_alarm_threshold) {
			database::Database_size.fetch_sub(Message_contribution_to_disk_size, ::std::memory_order_relaxed);
			throw data_processors::Size_alarm_error("Database_alarm_threshold breached. Message is too large for currently set database capacity watemark " + ::std::to_string(Database_alarm_threshold) + ").");
		}

		// Message size accepted, can make it visible to others
		uint32_t const prev(this->back_msg_begin_buffer_byte_offset.exchange(back_msg_begin_buffer_byte_offset)); // atomically modified(by publisher) and observed(by multiple subcribers)

		msg_app_racing_cond.reset(); // racing condition when max_msg_seq_no is incremented for a given message but other message/thread get their back_msg updated and may cause skipping of this message (wher server tries to preserve the sequential order access)...

		// if it is a very first message (the range was empty before this message was added) then load the 'Appended_message' map and notify indicies of the relevant keys...
		if (prev & (is_null_value_mask | pending_notification_mask)) {
			auto this_(intrusive_from_this());
			t->strand.dispatch([this, this_
#ifndef NDEBUG
					, back_msg_begin_byte_offset
#endif
					, prev]() noexcept {
				assert(back_msg_begin_byte_offset - begin_byte_offset <= file.buffer_capacity());
				try {
					assert(t->ranges.find(begin_byte_offset) != t->ranges.end());
					assert(t->strand.running_in_this_thread());
					if (prev & is_null_value_mask) 
						map_first_msg();
					// it's ok if double-posting (second one, eg from 'register_for_notifications' will be a nothnig kind of an affair)
					else if (prev & pending_notification_mask)
						process_pending_notifications();
				} catch (::std::exception const & e) { put_into_error_state(e); }
			});
		}

		// update special (msg_boundary) index ...
		if (candidate_key != Message_boundary_index_Last_key_by_publisher) {
			auto this_(intrusive_from_this());
			assert(t->msg_boundary_index->last_key_by_publisher == static_cast<decltype(t->msg_boundary_index->last_key_by_publisher)>(-1) || candidate_key > t->msg_boundary_index->last_key_by_publisher);
			t->msg_boundary_index->last_key_by_publisher = candidate_key; // non-atomic because only 1 thread modifies it ever (at a time)
			t->msg_boundary_index->Pre_max_key_update(); // expected to be accessed from foreign strand
			t->strand.post([this, this_, back_msg_begin_byte_offset, back_msg_end_byte_offset, candidate_key]() noexcept {
				assert(back_msg_begin_byte_offset - begin_byte_offset <= file.buffer_capacity());
				try {
					if (!async_error) {
						assert(back_msg_end_byte_offset > back_msg_begin_byte_offset);
						t->msg_boundary_index->max_key_update(typename decltype(t->msg_boundary_index)::element_type::max_key_update_type{{{candidate_key, back_msg_begin_byte_offset, back_msg_end_byte_offset}}, ::boost::bind(&range::on_max_key_update, intrusive_from_this(), _1)});
					} else
						throw ::std::runtime_error("on max_key_update");
				} catch (::std::exception const & e) { put_into_error_state(e); }
			});
		}

		// ... not forgetting to update the index file(s), potentially causing them to be written out...
		for (auto & index : t->indicies) {
			assert(Appended_message.Get_indicies_size() > index->Index_In_Message);
			uint_fast64_t const candidate_key(Appended_message.Get_index(index->Index_In_Message) & index->key_mask);
			if (candidate_key != index->last_key_by_publisher) {
				auto this_(intrusive_from_this());
				assert(index->last_key_by_publisher == static_cast<decltype(index->last_key_by_publisher)>(-1) || candidate_key > index->last_key_by_publisher);
				index->last_key_by_publisher = candidate_key;
				index->Pre_max_key_update(); // expected to be accessed from foreign strand
				t->strand.post([this, this_, back_msg_begin_byte_offset, candidate_key, &index]() noexcept {
					assert(back_msg_begin_byte_offset - begin_byte_offset <= file.buffer_capacity());
					try {
						if (!async_error) {
							index->max_key_update(typename decltype(t->indicies)::value_type::element_type::max_key_update_type{{{candidate_key, back_msg_begin_byte_offset}}, ::boost::bind(&range::on_max_key_update, intrusive_from_this(), _1)});
						} else
							throw ::std::runtime_error("on max_key_update");
					} catch (::std::exception const & e) { put_into_error_state(e); }
				});
			}
		}
	}

	/**
		\about publisher uses this to indicate that the last message for the range has just been appended (no more will fit into this range object)
		\pre publisher should perform the following sequence: call message_appended and then, if such an appended message is the last one to be written to the range object, call last_message... 
		*/
	void
	last_message() 
	{
		//::boost::shared_ptr<range> this_(shared_from_this());
		//t->strand.dispatch([this, this_]() noexcept {
			//try {
				assert(t->strand.running_in_this_thread());
				assert(is_open_ended() == true);
				assert(get_back_msg());
				end_byte_offset = get_total_size();
				process_pending_notifications();
				// TODO -- check that this range is an exclusive published-to for the entire topic...
				// something like 't->writer == this' 
			//} catch (::std::exception const & e) { put_into_error_state(e); }
		//});
	}

	///\threadsafe(ish) (even if memory is potentially not visible by other threads, other calls such as get_back_msg will make it visible eventually)...
	bool
	is_open_ended() const 
	{
		return end_byte_offset == static_cast<decltype(end_byte_offset)>(-1);
	}
}; // end of range class

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


