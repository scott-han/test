#include <forward_list>

#include <boost/pool/pool_alloc.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "../misc/alloc.h"
#include "../asio/server.h"

#include "async_file.h"
#include "message.h"
#include "range.h"
#include "index.h"

#ifndef DATA_PROCESSORS_SYNAPSE_DATABASE_TOPIC_H
#define DATA_PROCESSORS_SYNAPSE_DATABASE_TOPIC_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

namespace basio = ::boost::asio;


template <typename MetaTopic, unsigned TargetBufferSize, unsigned PageSize>
class topic : 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
public synapse::misc::Test_new_delete<topic<MetaTopic, TargetBufferSize, PageSize>>,
#endif
public ::boost::enable_shared_from_this<topic<MetaTopic, TargetBufferSize, PageSize> > {

	static_assert(TargetBufferSize > PageSize * 512, "buffer size ought to be reasonable"); // got to do with migrating data with memcpy (non overlap-friendly) from end of buffer to it's start due to partial pagesize boundary possibilities
	static_assert(!(TargetBufferSize % PageSize), "TargetBufferSize must be multiple of PageSize"); 

	typedef ::boost::intrusive_ptr<MetaTopic> Meta_Topic_Pointer_Type;

public: 

	// Because weak_ptr in topic factory does not guarantee lifespan sequentiality between previous and to-be-allocated instances (i.e. just because mt->topic.lock() returns 'null' shared_ptr does not mean that previous instance is finished being deleted). This is especially problematic with CreateFile (should be only 'one' instance per topic at any given time ... so to speak).
	// This code must be declared before any of the following file objects (i.e. this way dtor of this object will imply that others have definitely been already dtored)
	struct Meta_Topic_Shim {
		Meta_Topic_Pointer_Type mt;
		Meta_Topic_Shim(Meta_Topic_Pointer_Type const & mt) : mt(mt)  {
			mt->Previous_Instance_Exists = 
			#ifdef DATA_PROCESSORS_TEST_SYNAPSE_DELAYED_TOPIC_DESTRUCTOR_RACING_CONDITION_1_INVOKE_BUG
				false;
			#else
				true;
			#endif
		}
		~Meta_Topic_Shim() {
			mt.get()->Previous_Instance_Dtored(::std::move(mt));
		}
	} Meta_Topic_Shim;

	unsigned constexpr static indicies_size = 1; // how many indicies in the every message
private:
	static_assert(database::Message_Indicies_Size >= indicies_size, "max indicies size");

	typedef range<TargetBufferSize, PageSize, topic> range_type;
public:
	typedef index<PageSize, topic, range_type, 2> msg_boundary_index_type;
private:

	// Todo more thoughtful tuning...
	uint_fast32_t constexpr static Approximated_topic_memory_consumption_size{
		// Indexing actions can consume RAM...
		(indicies_size + 1)  // include message boundary index
			* (512 * 1024 // some generic fixed RAM amount guard for index object et al
				+ msg_boundary_index_type::Max_key_update_requests_throttling_watermark * 256 // some generic guard per each indexing request
			)
		// now for other topic fields...
		+ 1024 * 1024 // something per generic RAM guard  
	};

	data_processors::misc::alloc::Scoped_contributor_to_virtual_allocated_memory_size<uint_fast32_t> Scoped_contributor_to_virtual_allocated_memory_size {
		#ifdef DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_1
			(data_processors::misc::alloc::Virtual_allocated_memory_size.load(::std::memory_order_relaxed) + DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_1 > data_processors::misc::alloc::Memory_alarm_threshold ? throw data_processors::Size_alarm_error("Memory_alarm_threshold in topic ctor breached.") : DATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_1) +
		#endif
		Approximated_topic_memory_consumption_size,
		data_processors::misc::alloc::Memory_alarm_breach_reaction_throw(),
		"Memory_alarm_threshold in topic ctor breached."
	};

public: // TODO better encapsulation
	::std::string const path; 

	// Used exclusively by topic factory strand.
	bool loaded{false};

	typedef ::boost::intrusive_ptr<range_type> range_ptr_type;

	// TODO more elegant encapsulation 
public:
	using ::boost::enable_shared_from_this<topic<MetaTopic, TargetBufferSize, PageSize>>::shared_from_this; 
	::boost::asio::strand strand;

	Async_random_access_handle_wrapper Async_meta_file_handle_wrapper;
	database::unmanaged_async_file<PageSize> Meta_file;
	Async_random_access_handle_wrapper Async_file_handle_wrapper;
private:
	// actual data file
	synapse::database::async_file<PageSize, database::Data_meta_ptr> file;

	uint_fast64_t
	get_total_size() const
	{
		assert(strand.running_in_this_thread());
		if (ranges.empty() == false)
			return ::std::max(file.get_meta().get_data_size(), ranges.rbegin()->second->get_total_size());
		else
			return file.get_meta().get_data_size();
	}

public:
	// TODO better encapsulation
	bool async_error = false;
private:
	void
	put_into_error_state(::std::exception const & e) 
	{
		assert(strand.running_in_this_thread());
		synapse::misc::log(::std::string("Topic(=" + path + ") exception(=") + e.what() + ")\n", true);
		async_error = true;
		for (auto && r : ranges)
			r.second->put_into_error_state(e);

		// Todo: decide on whether to pursue fine-grained topic-exception handling, or to bail the whole server if/when topic-related errors take place (e.g. database corruption, system-IO issues, etc.)
		// Todo: HOWEVER may at least want to save any of the pending data? ... just a thought/todo for future (if there is time).
		abort();
	}

public:
	data_processors::misc::alloc::basic_segregated_pools_1<> sp;
	typedef ::std::map<uint_fast64_t const, range_ptr_type, ::std::less<uint_fast64_t const>, data_processors::misc::alloc::basic_alloc_1<::std::pair<uint_fast64_t const, range_ptr_type>>> ranges_type;
	::std::unique_ptr<msg_boundary_index_type> msg_boundary_index;
	typedef index_with_ranges<PageSize, topic, range_type> index_with_ranges_type;

	ranges_type ranges{typename ranges_type::allocator_type{sp}}; // active in-RAM ranges (can be searched by index key values)

	// pool allocation (and implicit throttling of an overly fast-publisher -producer)
	uint_fast32_t const cached_ranges_watermark = 5; // how many ranges to hold-on to for the lifetime of the topic. useful for cases when new_range requests are constantly being made (e.g. streaming or generally frequent interaction with the topic) and one wants to prevent new/malloc/delete/free (or server-wide pool alloc/dealloc) from happening all too frequently. TODO more runtime/tunable/dynamic
	uint_fast32_t const unwritten_data_size_watermark = 1024 * 1024 * 300; // max unsaved-to-disk of data that the topic is allowed to have at any given time. if some contributing-to-unsaved-data ranges are eventually freed (data is written) then they will be dropped until the cached_ranges_watermark value. This variable is used to prevent cases of fast producer overwheling the server system and causing continuous RAM allocation thereby draining the whole system of its resources (e.g. when incoming data from producer is faster than servers disk-IO). TODO more runtime/tunable/dynamic
	::std::queue<range_ptr_type, ::std::deque<range_ptr_type, data_processors::misc::alloc::basic_alloc_1<range_ptr_type>>> cached_ranges{typename decltype(cached_ranges)::container_type::allocator_type{sp}};
	::std::deque<::std::function<void()>, data_processors::misc::alloc::basic_alloc_1<::std::function<void()>>> Awaiting_for_free_ranges_due_to_unwritten_data_size;

	// TODO consider having std::array for indicies type (depending on whether one will need to runtime configure number of indicies... unlikely though)... or create 'move-ctor' for the basic_alloc type in the allocation of asio handlers... (otherwise vector emplace does not work -- vector either needs move or copy ctor in case in needs to regrow the container)...
	typedef ::std::vector< ::std::unique_ptr<index_with_ranges_type> > indicies_type;
	indicies_type indicies; // index objects (allowing to locate relevant file portion of the data file based on key values)

	/**
	 * slower, more-periodic and more commited (e.g. writing to disk) invocation of the resource-checking process. takes care of itself to re-invoke itself on periodic basis (i.e. in the 'on_timeout' semantics).
	 */ 
	void
	schedule_resource_checking_timer()
	{
		assert(strand.running_in_this_thread());
		if (resource_checking_timer_pending == false) {
			resource_checking_timer_pending = Resource_checking_async_loop_was_idle_inbetween_scheduled_timeouts = true;
			resource_checking_timer.expires_from_now(::boost::posix_time::seconds(resource_checking_period));
			resource_checking_timer.async_wait(strand.wrap(::boost::bind(&topic::on_resource_checking_timeout, shared_from_this(), ::boost::asio::placeholders::error)));
		}
	}
private:

	bool Sparsification_in_progress{false};

	bool resource_checking_timer_pending{false};
	bool Resource_checking_async_loop_was_idle_inbetween_scheduled_timeouts{true};
	bool resource_checking_async_loop_should_write_to_disk{false};
	bool resource_checking_async_loop_is_writing_to_disk{false};


	bool resource_checking_async_loop_active = false;
	/**
	 * concerns itself with a more immediate invocation of the resource-checking process (e.g. for the sake of the awaiting-ranges etc.)
	 * will not call the 'schedule_resource_checking_async_loop'
	 */ 
	void
	actuate_resource_checking_async_loop(::boost::shared_ptr<topic> const & this_) noexcept  // TODO re-check the design (use of this method) manually to make sure that this design does not cause too many 'false negative' posts/trips around the world :)
	{
		assert(strand.running_in_this_thread());
		try {
			if (!async_error) {
				if (resource_checking_async_loop_active == false) {
					resource_checking_async_loop_active = true;
					resource_checking_async_loop_is_writing_to_disk = resource_checking_async_loop_should_write_to_disk;
					Resource_checking_async_loop_was_idle_inbetween_scheduled_timeouts = false;
					resource_checking_async_loop(ranges.begin(), this_);
				} else
					resource_checking_async_loop_should_run = true;
			} else
				throw ::std::runtime_error("on actuate_resource_checking_async_loop strand post");
		} catch (::std::exception const & e) { put_into_error_state(e); }
	}

	bool resource_checking_async_loop_should_run = false;
public:
	void 
	notify_range_became_possibly_available()
	{
		auto this_(shared_from_this());
		strand.post([this, this_]() noexcept {
			assert(strand.running_in_this_thread());
			try {
				if (!async_error) {
					if (ranges.size() > cached_ranges_watermark)
						actuate_resource_checking_async_loop(this_);
				} else
					throw ::std::runtime_error("on notify_range_became_possibly_available strand post");
			} catch (::std::exception const & e) { put_into_error_state(e); }
		});
	}

private:
	unsigned constexpr static default_resource_checking_period{15}; 
	unsigned constexpr static quitting_resource_checking_period{1}; 
	static_assert(default_resource_checking_period > quitting_resource_checking_period, "resource checking periods");
	unsigned resource_checking_period{default_resource_checking_period}; // TODO make it larger in value later on (this is just for likely testing that is about to follow)
	::boost::asio::deadline_timer resource_checking_timer;
	// receive timeout from asio and kickstart the async processing loop
	void
	on_resource_checking_timeout(::boost::system::error_code const & error) noexcept
	{
		assert(strand.running_in_this_thread());
		assert(resource_checking_timer_pending == true);
		if (!error) {
			try {
				if (!async_error) {
					auto this_(shared_from_this());
					strand.post([this, this_](){ // this is to break-up accumulated (from deadline_timer callback) reference counting of 'this_' as such is used to determine when for the topic to go out of scope...
						assert(resource_checking_timer_pending == true);
						resource_checking_timer_pending = false;
						resource_checking_async_loop_should_write_to_disk = true;
						// Note. not testing for this_.use_count() > 1 + cached_ranges.size() because it becomes more confound type of a mechanism -- the 'end()' part of the resource_checking_async_loop() will clear the cached_ranges but will also add to the indicies the copy of this_ for the flush-requests. Currently such index-flushing may be completed without async-post at the end of the index-flushing process, but if this changes in future then this loop may go on and cause topic not to be unloaded during server uptime.  
						if (!ranges.empty() || !cached_ranges.empty() || !this_.unique()) { // using 'ranges.empty()' etc. befor 'unique()' call, because it does not need to dereference (pointer jump) to the control-block counter in the shared pointer (this_) as well access an atomic counter.
							if (!resource_checking_async_loop_active && Resource_checking_async_loop_was_idle_inbetween_scheduled_timeouts)
								actuate_resource_checking_async_loop(this_);
							schedule_resource_checking_timer();
						}
					});
				} else
					throw ::std::runtime_error("on_resource_checking_timeout");
			} catch (::std::exception const & e) { put_into_error_state(e); }
		}
	}

	uint_fast64_t Cached_meta_begin_byte_offset;
	uint_fast64_t Cached_meta_target_data_consumption_limit;
	uint_fast64_t Cached_meta_requested_sparsify_upto_timestamp;
	uint_fast64_t Cached_meta_processed_sparsify_upto_timestamp;
	uint_fast64_t Cached_meta_sparsify_upto_byte_offset;
	bool Update_metadata_on_file{false};
	void Apply_cached_meta_sparsification_variables() {
		assert(Update_metadata_on_file);
		file.get_meta().Set_begin_byte_offset(Cached_meta_begin_byte_offset);
		file.get_meta().Set_target_data_consumption_limit(Cached_meta_target_data_consumption_limit);
		file.get_meta().Set_requested_sparsify_upto_timestamp(Cached_meta_requested_sparsify_upto_timestamp);
		file.get_meta().Set_processed_sparsify_upto_timestamp(Cached_meta_processed_sparsify_upto_timestamp);
		file.get_meta().Set_sparsify_upto_byte_offset(Cached_meta_sparsify_upto_byte_offset);
		Update_metadata_on_file = false;
	}
public:

	bool Is_Quitting_In_Progress() const noexcept {
		return resource_checking_period == quitting_resource_checking_period;
	}

	void Set_target_data_consumption_limit(uint_fast64_t Target_limit, range_ptr_type const & Range) {
		strand.dispatch([this, this_(shared_from_this()), Target_limit, Range](){
			try {
				if (!async_error) {
					auto const Data_target_limit(::std::max<uint_fast64_t>(Target_limit, 2 * database::MaxMessageSize + 1)); // +1 is for later checks/assertions of > (not >=)... so a hack really...
					auto const Index_target_limit(::std::max<uint_fast64_t>(Target_limit, 2 * PageSize));
					assert(!indicies.empty());
					if (Index_target_limit != indicies[0]->Cached_meta_target_data_consumption_limit) {
						for (auto && Index : indicies) {
							Index->Cached_meta_target_data_consumption_limit = Index_target_limit;
							Index->Update_metadata_on_file = true;
						}
					}
					if (Data_target_limit != Cached_meta_target_data_consumption_limit) {
						Cached_meta_target_data_consumption_limit = Data_target_limit;
						Update_metadata_on_file = true;
					}
				} else
					throw ::std::runtime_error("on_resource_checking_timeout");
			} catch (::std::exception const & e) { put_into_error_state(e); }
		});
	}

	void Set_sparsify_upto_timestamp(uint_fast64_t Timestamp, range_ptr_type const & Range) {
		strand.dispatch([this, this_(shared_from_this()), Timestamp, Range](){
			try {
				if (!async_error) {
					if (Cached_meta_requested_sparsify_upto_timestamp < Timestamp) {
						Cached_meta_requested_sparsify_upto_timestamp = Timestamp;
						Update_metadata_on_file = true;
					}
				} else
					throw ::std::runtime_error("on_resource_checking_timeout");
			} catch (::std::exception const & e) { put_into_error_state(e); }
		});
	}

private:
	void
	resource_checking_async_loop(typename ranges_type::iterator const & from, ::boost::shared_ptr<topic> const & this_)
	{
		assert(!async_error);
		assert(Async_open_complete);
		assert(!Async_open_in_progress);
		assert(resource_checking_async_loop_active == true);
		//synapse::misc::log("resource checking loop\n", true);
		assert(strand.running_in_this_thread());
		if (from != ranges.end()) {

			#ifndef NDEBUG
			{
				bool Found(false);
				for (auto && i(ranges.begin()); i != ranges.end(); ++i) {
					if (i == from) {
						Found = true; 
						break;
					}
				}
				assert(Found);
			}
			#endif

			auto & range(from->second);

			database::Message_const_ptr back_msg;
			auto const adjusted_end_byte_offset(range->get_total_size(back_msg));
			assert(adjusted_end_byte_offset <= get_total_size());

			auto const File_end_on_disk(file.get_meta().get_data_size());

			// Here preempt everything by an additional check for sparsification...
			// Note, this is somewhat of a simple solution (since it is only for the lower-end systems which worry about disk-size, etc.) The sparsification will only happen if none is currently being done, but if the previous is still pending, then this one will be skipped (as opposed to being cached and 'picked-up' on the completion of currently-in-progress one). The next (after current) sparsification will sparsify the whole lot (i.e. up to the latest/most-recent value).
			// Also todo -- decision on when to start truncation should also be affected by the indicies consumption of disk space...
			if (from == ranges.begin() && File_end_on_disk && Sparsification_in_progress == false) {
				// Sparsify upto timestamp
				if (Cached_meta_requested_sparsify_upto_timestamp != Cached_meta_processed_sparsify_upto_timestamp) {
					auto && Time_index(*indicies[0]); // Note, 0 is now temstamp index as we are no longer indexing sequence number
					uint_fast64_t const Time_index_key_offset(Time_index.Has_entries() ? Time_index.Unquantised_key_value_to_quantised_byte_offset(Cached_meta_requested_sparsify_upto_timestamp) : -1);
					if (Time_index.Key_at_byte_offset_within_end_limit(Time_index_key_offset)) {
						Sparsification_in_progress = true;
						Time_index.async_get_key(Time_index_key_offset, [this, 
							this_, // should capture by copy here (runs overlappingly with other code (e.g. writing-out))

							// Note, design decision here... we either need to hold on to the, potentially dummy, range whilst in transit, or to refactor async resource checking loop to check for empty ranges container and still proceed if other flags/logic indicates that this needs to be done... Given that loop iteration is done per every active topic per every existing range, yet explicit sparsification is expected to be done extremely rarely (in comparison to frequency of the non-timebased-sparsifying actions) current deployment uses 'holding on to the range' approach.
							// However TODO: if it proves that memory consumption of this, potentially rudendant, range causes issues later on (with respect to the RAM capacity etc.) then we can refactor.
							range, 

							Requested_timestamp(Cached_meta_requested_sparsify_upto_timestamp)
						](void const * buffer) noexcept {
							assert(strand.running_in_this_thread());
							try {
								if (!async_error) {
									if (buffer != nullptr) {
										uint_fast64_t const File_offset(synapse::misc::Get_alias_safe_value<uint64_t>(buffer));
										if (File_offset > Cached_meta_sparsify_upto_byte_offset)
											Cached_meta_sparsify_upto_byte_offset = File_offset;
									}
									// 	A bit tricky here -- index may have been sparsified itself (e.g. due to its onw large size) yet deleting the data file up to the first available index may yield loss of data (because the timestamp, even in as simple of as racing conditions, may not be caught up to the 1st non-sparsified index value).
									Cached_meta_processed_sparsify_upto_timestamp = Requested_timestamp;
									Update_metadata_on_file = true;
									Sparsification_in_progress = false;
								} else
									throw ::std::runtime_error("on reading msg_boundary_index in preparing sparsifiction action in resource checking loop");
							} catch (::std::exception const & e) { put_into_error_state(e); }
						});
					}
				} else if (File_end_on_disk > Cached_meta_sparsify_upto_byte_offset) { // Sparsify based on the configured limits
					assert(File_end_on_disk > Cached_meta_begin_byte_offset);
					auto const Existing_sparsification_end(Cached_meta_begin_byte_offset);
					auto const File_size_on_disk(File_end_on_disk - Existing_sparsification_end);
					assert(Cached_meta_target_data_consumption_limit > static_cast<uint_fast64_t>(2 * database::MaxMessageSize));
					auto const Max_topic_element_consumption_on_disk(::std::min( // Taking most truncating of:
						// rolling 'target_data_consumption_limit'
						Cached_meta_target_data_consumption_limit, 
						// and 'up to offset (e.g. derived from timestamp) ... whilst making sure that such satisfies "> MaxMessageSize * 2" requirement
						::std::max(File_end_on_disk - Cached_meta_sparsify_upto_byte_offset, static_cast<uint_fast64_t>(2 * database::MaxMessageSize + 1))
					));
					if (File_size_on_disk > 
						::std::max(Max_topic_element_consumption_on_disk, Max_topic_element_consumption_on_disk + database::MaxMessageSize) 
						// max is to prevent overflow/wraparound because numeric_limits::max (-1) et.al. are valid indeed
						// and + database::MaxMessageSize is for quantized/chunkified sparsification (as not to be triggered all too often, even if user supplies ever so incremental change)
					) {
						assert(Max_topic_element_consumption_on_disk > 2 * database::MaxMessageSize);

						// It may not be indexed at all -- i.e. the very first index may still be in "update max key" posted io callback
						uint_fast64_t const Message_boundary_key_offset(msg_boundary_index->Has_entries() ? msg_boundary_index->Unquantised_key_value_to_quantised_byte_offset(File_end_on_disk - Max_topic_element_consumption_on_disk) : -1);
						if (msg_boundary_index->Key_at_byte_offset_within_end_limit(Message_boundary_key_offset) == true) {
							assert(Message_boundary_key_offset != static_cast<decltype(Message_boundary_key_offset)>(-1));
							assert(Message_boundary_key_offset < File_end_on_disk - database::MaxMessageSize);
							assert(msg_boundary_index->Has_entries());
							Sparsification_in_progress = true;

							// get the key first
							msg_boundary_index->async_get_key(Message_boundary_key_offset, [this, this_, Existing_sparsification_end
								#ifndef NDEBUG
									, File_size_on_disk
									, File_end_on_disk
								#endif
							](void const * buffer) noexcept {
								assert(strand.running_in_this_thread());
								assert(File_size_on_disk);
								try {
									if (buffer != nullptr && !async_error) {
										uint_fast64_t const Rebase_message_begin(synapse::misc::Get_alias_safe_value<uint64_t>(buffer));
										assert(Rebase_message_begin < File_end_on_disk - database::MaxMessageSize);
										if (Rebase_message_begin <= Existing_sparsification_end) {
											Sparsification_in_progress = false;
											return;
										}
										uint_fast64_t const Rebase_message_end(synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(buffer) + 1));
										// Now, after getting the key, issue this->get_range_for_byte_offset...
										async_get_range_from_byte_offset(Rebase_message_begin, [this, this_, Rebase_message_begin, Rebase_message_end, Existing_sparsification_end](range_ptr_type const & Range) noexcept {
											assert(strand.running_in_this_thread());
											try {
												if (!async_error) {
													if (Range) {
														assert(Sparsification_in_progress == true);
														assert(Range->is_open_ended() == false);
														// may still be loading from disk...
														Range->register_for_notification(nullptr, strand.wrap([this, Range, Existing_sparsification_end, Rebase_message_begin, Rebase_message_end]() {
															assert(strand.running_in_this_thread());
															try {
																if (!async_error) {
																	// approximation of setting begin key for indicies (so that later on when there's time we can truncate the indicies files as well)
																	auto const Message(Range->get_first_msg());
																	assert(Message);
																	// Here will make sure that all indicies are AOK (target begin key is a part of already present data (on disk or ram)... but if not, then will not proceed at all (i.e. will pick-up on the next sparsification timeout/pass)).
																	// This is because, otherwise, begin key may not be set properly and then the index may be serving out message-location which has already been zeroed out.
																	bool All_indicies_have_their_begin_key_existing(true);
																	for (auto && Index : indicies) {
																		assert(Message.Get_indicies_size() > Index->Index_In_Message);
																		auto Index_value(Message.Get_index(Index->Index_In_Message));
																		uint_fast64_t const Key_byte_offset(Index->Has_entries() ? Index->Unquantised_key_value_to_quantised_byte_offset(Index_value) : -1);
																		if (Index->Key_at_byte_offset_within_end_limit(Key_byte_offset) == false) {
																			All_indicies_have_their_begin_key_existing = false;
																			break;
																		}
																	}

																	if (All_indicies_have_their_begin_key_existing) {
																		for (auto && Index : indicies) {
																			assert(Message.Get_indicies_size() > Index->Index_In_Message);
																			auto Index_value(Message.Get_index(Index->Index_In_Message));
																			Index->Set_begin_key_conditionally(Index_value & Index->key_mask) ;
																		}
																		msg_boundary_index->Set_begin_key_conditionally(Rebase_message_end & msg_boundary_index->key_mask);

																		assert(Cached_meta_begin_byte_offset < Rebase_message_begin);
																		Cached_meta_begin_byte_offset = Rebase_message_begin;
																		Cached_meta_sparsify_upto_byte_offset = Rebase_message_begin;
																		Update_metadata_on_file = true;
																		auto this_(shared_from_this());
																		synapse::misc::log("SPARSIFYING file!!! from " + ::std::to_string(Existing_sparsification_end) + ", to " + ::std::to_string(Rebase_message_begin) + '\n', true);
																		assert(Existing_sparsification_end < Rebase_message_begin);
																		file.Async_sparsify_beginning(Existing_sparsification_end, Rebase_message_begin, [this, this_, Existing_sparsification_end, Rebase_message_begin](bool error) {
																			assert(strand.running_in_this_thread());
																			try {
																				if (!error && !async_error) {
																					Meta_Topic_Shim.mt->Approximate_Topic_Size_On_Disk.fetch_sub(Rebase_message_begin - Existing_sparsification_end, ::std::memory_order_relaxed);
																					synapse::misc::log(" DOEN SPARSIFYING file!!! \n", true);
																					Sparsification_in_progress = false;
																				} else
																					throw ::std::runtime_error("on file.Async_sparsify_beginning via resource checking async loop");
																			} catch (::std::exception const & e) { put_into_error_state(e); }
																		});
																	} else
																		Sparsification_in_progress = false;
																} else
																	throw ::std::runtime_error("On register_for_notification from range in topic, during sparsification preparation. The topic is already in error state.");
															} catch (::std::exception const & e) { put_into_error_state(e); }
													}), false); // Last arg (notification wanted due to message being appended is noop here because range is already closed, not open ended).
													} else {
														synapse::misc::log("Postponing sparsification of topic: " + ::boost::lexical_cast<::std::string>(this) + " path: " + path + " due to nullptr Range being returned.\n", true);
														Sparsification_in_progress = false;
													}
												} else
													throw ::std::runtime_error("async_get_range_from_byte_offset error via resource-checking async loop, sparsification section");
											} catch (::std::exception const & e) { put_into_error_state(e); }
										});
									} else
										throw ::std::runtime_error("on reading msg_boundary_index in preparing sparsifiction action in resource checking loop");
								} catch (::std::exception const & e) { put_into_error_state(e); }
							});
						}
						// not returning -- this one runs overlappingly
					}
				}
			}

			if ( 
				// needs writing
				(adjusted_end_byte_offset > File_end_on_disk || 
				// or, for the last range iteration, will update just the target data consumption if needed (last iteration because of efficiency, but may be reconsidered later on). 
				Update_metadata_on_file && from == ::std::prev(ranges.end()))
				&&
				// note this is a bit tricky, unique() must be teste for here, otherwise next 'else' will reappropriate it without writing! (right now this chick seems reasonable enough and saves on extra code/time)
				(resource_checking_async_loop_is_writing_to_disk == true || range->is_open_ended() == false || range->unique() == true)
			) {

				auto After_write_handler([this](::boost::shared_ptr<topic> const & this_, typename ranges_type::iterator const & from, range_ptr_type const & range, bool error){
					assert(strand.running_in_this_thread());
					assert(range->unique() == false);
					if (!error && !async_error) {

						// Note: no need to call/update file.get_meta().set_data_size() because async_write (if relevant) will automatically take care of it...

						assert(file.get_meta() == range->file.get_meta());
						assert(file.get_meta().get_data_size() >= range->begin_byte_offset);
						assert(get_total_size() >= file.get_meta().get_data_size());

						assert(!ranges.empty());
						if (!Awaiting_for_free_ranges_due_to_unwritten_data_size.empty()) {
							auto const Total_size(get_total_size());
							auto const File_size_on_disk(file.get_meta().get_data_size());
							assert(Total_size >= File_size_on_disk);
							auto const Unwritten_data_size(Total_size - File_size_on_disk);
							if (Unwritten_data_size <= unwritten_data_size_watermark) {
								// already in a non-recursive 'post' because async_file.async_write uses boost.asio async implications... (in that it should come back later on, not in the same stack so to speak).
								auto Current_size(Awaiting_for_free_ranges_due_to_unwritten_data_size.size());
								assert(Current_size);
								assert(resource_checking_async_loop_active == true);
								do {
									// Note, it is OK if the callback issues 'actuate_resource_checking_async_loop' because such checks for 'resource_checking_async_loop_active' flag which at this point is already set... (to only the flag "should run" would be set).
									Awaiting_for_free_ranges_due_to_unwritten_data_size.front()();
									Awaiting_for_free_ranges_due_to_unwritten_data_size.pop_front();
								} while (--Current_size);
							}
						}

						assert(resource_checking_async_loop_active == true);
						if (resource_checking_async_loop_should_run == true) {
							resource_checking_async_loop_should_run = false;
							resource_checking_async_loop(ranges.begin(), this_);
						} else if (range->is_open_ended() == true)
							resource_checking_async_loop(ranges.end(), this_);
						else
							resource_checking_async_loop(from, this_);

					} else
						throw ::std::runtime_error("on_resource_checking_async_loop, After_write_handler");
				});

				if (adjusted_end_byte_offset > File_end_on_disk) {

					assert(back_msg);
					assert(file.get_meta().get_data_size() >= range->begin_byte_offset);

					// presumes that partial pagesize migration is already done!
					///\NOTE that me must not use begin_byte_offset in the actual writing/calculation of data because the same range may be written incrementally (e.g. the publisher may stop publishing half-way through filling up the range and we still want to save whatever has been published to disk on timely basis)
					auto writing_out_code([this, this_, from, range, adjusted_end_byte_offset, back_msg, After_write_handler]() {
						assert(strand.running_in_this_thread());
						assert(resource_checking_async_loop_active == true);
						assert(range->unique() == false);
						assert(adjusted_end_byte_offset > range->begin_byte_offset);
						assert(adjusted_end_byte_offset > file.get_meta().get_data_size());
						assert(file.get_meta() == range->file.get_meta());

						uint_fast32_t const bytes_to_write(adjusted_end_byte_offset - file.get_meta().get_data_size());

						assert(range->file.buffer_capacity() >= bytes_to_write);
						assert(file.get_meta().get_data_size() >= range->begin_byte_offset);

						auto const back_msg_seq_no(back_msg.Get_index(0));
						auto const back_msg_timestamp(back_msg.Get_index(1));
						file.get_meta().Set_maximum_sequence_number(back_msg_seq_no);
						file.get_meta().set_latest_timestamp(back_msg_timestamp);

						if (Update_metadata_on_file) 
							Apply_cached_meta_sparsification_variables();
						range->file.set_buffer_size(adjusted_end_byte_offset - range->begin_byte_offset);
						range->file.async_write(file.get_meta().get_data_size(), bytes_to_write, [this, this_, from, range, After_write_handler, bytes_to_write](bool error) noexcept {
							try {
								for (auto & i : indicies)
									i->flush([](){});
								msg_boundary_index->flush([](){});
								After_write_handler(this_, from, range, error);
								Meta_Topic_Shim.mt->Approximate_Topic_Size_On_Disk.fetch_add(bytes_to_write, ::std::memory_order_relaxed);
							} catch (::std::exception const & e) { put_into_error_state(e); }
						});
					});

					// firstly see if we need to deal with PageSize overlap...
					// can only happen if range->begin_byte_offset is not on the PageSize boundary ...
					if (synapse::misc::is_multiple_of_po2<uint_fast64_t>(range->begin_byte_offset, PageSize) == false) {
						assert(range->file.buffer_alignment_offset());
						// ... and the very 1st page of the 'range' is involved.
						uint_fast64_t const writeout_begin_page_offset(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(file.get_meta().get_data_size(), PageSize));
						if (synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(range->begin_byte_offset, PageSize) == writeout_begin_page_offset) {
							// at this stage migration is necessary...
							assert(range->begin_byte_offset > writeout_begin_page_offset);
							uint_fast32_t const bytes_to_migrate(range->begin_byte_offset - writeout_begin_page_offset);
							// if we can get the page from RAM...
							if (from != ranges.begin()) {
								auto & prev_range(*::std::prev(from)->second);
								assert(prev_range.is_open_ended() == false);
								if (prev_range.get_first_msg() // exsiting boundaries-set range may not have been 'read/loaded' from disk yet... (we want valid/loaded data only)
									&& prev_range.end_byte_offset >= range->begin_byte_offset && prev_range.begin_byte_offset <= writeout_begin_page_offset) {
									assert(synapse::misc::is_multiple_of_po2<uint_fast64_t>(writeout_begin_page_offset, PageSize) == true);
									assert(prev_range.file.buffer_offset() + prev_range.file.buffer_size() >= range->begin_byte_offset);

									::memcpy(range->file.buffer(), prev_range.file.buffer() + (writeout_begin_page_offset - prev_range.file.buffer_offset() + prev_range.file.buffer_alignment_offset()), bytes_to_migrate);
									writing_out_code();
									return;
								}
							}
							// if got to here, then will need to read from disk...
							file.async_read(writeout_begin_page_offset, bytes_to_migrate, [this, this_, writing_out_code, bytes_to_migrate, range](bool error) noexcept {
								try {
									assert(strand.running_in_this_thread());
									assert(range->unique() == false);
									if (!error && !async_error) {
										::memcpy(range->file.buffer(), file.buffer_begin(), bytes_to_migrate);
										writing_out_code();
									} else
										throw ::std::runtime_error("on_file_read during pagesize migration");
								} catch (::std::exception const & e) {
									put_into_error_state(e);
								} 
							});
							return; // will pick-up later on in time (on page-size reading completion)
						} 
					}
					// no need to do anything about PageSize -- will write out immediately
					writing_out_code();
				} else {
					assert(from == ::std::prev(ranges.end()));
					Apply_cached_meta_sparsification_variables();
					range->file.async_write_meta([this, this_, from, range, After_write_handler](bool error) noexcept {
						try {
							assert(strand.running_in_this_thread());
							assert(range->unique() == false);
							if (!error && !async_error) {
								for (auto & i : indicies)
									i->flush([](){});
								msg_boundary_index->flush([](){});
								After_write_handler(this_, from, range, error);
							} else
								throw ::std::runtime_error("on_file_read during pagesize migration");
						} catch (::std::exception const & e) {
							put_into_error_state(e);
						} 
					});
				}
			} else { // "clean-up only" stage
				auto next(::std::next(from));
				#if 0
					::std::string log_entry("is unique " + ::boost::lexical_cast<::std::string>(range->unique()) + ", use count " + ::boost::lexical_cast<::std::string>(range->use_count()) + + ", is open ended " + ::boost::lexical_cast<::std::string>(range->is_open_ended()) + ", total ranges " + ::boost::lexical_cast<::std::string>(ranges.size()) + '\n');

					#if 0
						for (auto & i : indicies)
							log_entry += " indx pending max_keys: " + ::std::to_string(i->max_key_update_requests.size());
						log_entry += " indx pending max_keys: " + ::std::to_string(msg_boundary_index->max_key_update_requests.size());
					#endif

					if (resource_checking_async_loop_previously_logged_entry != log_entry) {
						resource_checking_async_loop_previously_logged_entry = ::std::move(log_entry);
						synapse::misc::log(resource_checking_async_loop_previously_logged_entry, true);
					}
				#endif
				// range no longer in use (no subscribers or publisher are active for this area in the data)
				if (range->unique() == true) {
					assert(range->ranges_membership == from);
					if (cached_ranges.size() < cached_ranges_watermark) {
						cached_ranges.emplace(range);
						assert(!range->unique());
						range->resign_from_topic_membership(); // implicitly takes itself out of 'this->ranges' container, and consequently invalidates the 'from' variable here
						assert(cached_ranges.back()->unique());
					} else {
						range_ptr_type tmp(range);
						range->resign_from_topic_membership(); // implies deleting self really... (and can take from multiple points in that method... ish)
						assert(tmp->unique());
					}
				}
				assert(resource_checking_async_loop_active == true);
				strand.post([this, this_, next]() noexcept {
					assert(strand.running_in_this_thread());
					assert(resource_checking_async_loop_active == true);
					try {
						if (!async_error) {
							resource_checking_async_loop(next, this_);
						} else
							throw ::std::runtime_error("on_resource_checking strand post after cleanup check");
					} catch (::std::exception const & e) { put_into_error_state(e); }
				});
			}
		} else { // end of ranges iteration. IMPORTANT -- this part must be omnipotent!
			assert(static_cast<unsigned>(this_.use_count()) != 1 + cached_ranges.size() || ranges.empty() == true);
			if (resource_checking_async_loop_should_run == true) {
				resource_checking_async_loop_should_run = false;
				strand.post([this, this_](){
					actuate_resource_checking_async_loop(this_);
				});
			} else if (ranges.empty() == true && static_cast<unsigned>(this_.use_count()) == 1 + cached_ranges.size()) { // no ranges left and not quitting...  try to deallocate
				assert(Awaiting_for_free_ranges_due_to_unwritten_data_size.empty() == true);
				{
					decltype(cached_ranges) clear{typename decltype(cached_ranges)::container_type::allocator_type{sp}};
					::std::swap(cached_ranges, clear);
				}
				// cannot assert for uniqueness here -- topic_factory can concurrently 'get' the topic again...?
				// assert(this_.unique());
				synapse::misc::log("Topic is likely to go out of scope as a part of cleanup in resource-checking loop\n", true);

				// indicies ought to be flushed before topic dtor...
				auto on_index_flushed([this, this_](){ 
					assert(strand.running_in_this_thread()); // index ought to be running on my thread in the first place (by design)...
					try {
						if (!async_error) {
							synapse::misc::log("Topic(=" + ::boost::lexical_cast<::std::string>(this) + "), an index got flushed\n", true);
						} else
							throw ::std::runtime_error("on flushing of indicies...");
					} catch (::std::exception const & e) { put_into_error_state(e); }
				});
				assert(!this_.unique());
				for (auto & i : indicies)
					i->flush(on_index_flushed);
				msg_boundary_index->flush(on_index_flushed);
			}
			if (resource_checking_async_loop_should_write_to_disk == true && resource_checking_async_loop_is_writing_to_disk == true)
				resource_checking_async_loop_should_write_to_disk = false;
			resource_checking_async_loop_is_writing_to_disk = resource_checking_async_loop_active = false;
		}
	}

	#if 0
		// used for debugging in the above (resource checking async loop)
		::std::string resource_checking_async_loop_previously_logged_entry;
	#endif

	template <typename CallbackType>
	void 
	range_getter(uint_fast64_t const & begin_byte_offset, uint_fast64_t const & end_byte_offset, bool sequential_scan_exception, uint_fast64_t previous_message_boundary, ::boost::shared_ptr<topic> const & this_, CallbackType && callback)
	{
		assert(strand.running_in_this_thread());
		assert(end_byte_offset != static_cast<uint_fast64_t>(-1));
		assert(end_byte_offset > begin_byte_offset);

		// at this stage a high probability guesstimate is that no existing ranges are present, proceed with creating one
		assert(begin_byte_offset < get_total_size());
		assert(end_byte_offset <= get_total_size());

		// NOTE, todo verify more rigorously whether the following reasoning holds: do not need to check for ranges currently referencing the location (in the async resource checking loop) -- may just zero-out, because by that stage the data is already in RAM of range (has been loaded). With regards to concern of: some range may still be in 'write-pending' mode and such may be zeroed-out at the same/concurrent/overlapped time (and same for indicies)... On surface one would need to make sure that there are no pending writes still for the to-be zeroed out area (i.e. during async_write need to adjust the written-from offset and the size of written data), but thinking about it some more in may not be a big deal if those are indeed concurrently clobbered (w.r.t. zero vs valid data). This is because -- if zero-version wins then all is good as such is implicitly 'taken out' due to space constrains (for the same reasons as why it is impossible to prevent unexpected subscriptions being canceled); and if valid-data version wins then the async-loop-*next*-iteration will pick up on the file-too-large values and correct it again (this time without racing condition). Given that general 'safety buffer' for timer periods is implicit in the very architecture (i.e. the trigerring watermarks for sparsification ought not to be too close to physical limits of HDD and subsequent crash) this may be quite a feasible solution.
		
		if (Cached_meta_begin_byte_offset <= begin_byte_offset) {

			// use pool allocation...
			range_ptr_type new_range; 
			if (cached_ranges.empty() == false) {
				new_range = cached_ranges.front();
				cached_ranges.pop();
			} else {
				new_range.reset(new range_type(this_)); 
				if (ranges.size() == cached_ranges_watermark) {
					strand.post([this, this_](){ // posting here because dont want to slow-down the process of returning the actually-needed range (the claim-back resource-checking-loop can take place later on at the earliest opportunity)
						actuate_resource_checking_async_loop(this_);
					});
				}
			}
			assert(new_range->unique());

			if (ranges.empty()) 
				schedule_resource_checking_timer();
			new_range->init(begin_byte_offset, end_byte_offset);
			new_range->obtain_topic_membership();
			assert(begin_byte_offset < file.get_meta().get_data_size());
			assert(end_byte_offset <= file.get_meta().get_data_size());
			// read 1st message in file range (to get starting key values)
			new_range->file.async_read(begin_byte_offset, end_byte_offset - begin_byte_offset, [this, this_, new_range, begin_byte_offset, end_byte_offset, sequential_scan_exception, previous_message_boundary, callback(::std::forward<CallbackType>(callback))](bool error) mutable noexcept {
				assert(strand.running_in_this_thread());
				try {
					if (!error && !async_error) {
						// check for Begin_byte_offset (sparsification issue) and then do not proceed if contains invalid region
						if (Cached_meta_begin_byte_offset <= begin_byte_offset) {
							assert(end_byte_offset != static_cast<decltype(end_byte_offset)>(-1));
							assert(new_range->is_open_ended() == false);
							if (sequential_scan_exception == true) {
								for (uint_fast64_t i(begin_byte_offset);;) {
									uint_fast32_t const back_msg_byte_offset(i - begin_byte_offset);
									database::Message_const_ptr const msg(new_range->file.buffer_begin() + back_msg_byte_offset);
									// given the restrictions on coarseness of the index vs. max-msg/buffer-sizes, the sequential_scan should only occur when the buffer is past the file.get_meta().get_data_size() or when the buffer is past the next-already-loaded-range -- in both of such cases the 'end_byte_offset' ought to be pointing to the valid, exact, end-of-message boundary...
									if (!msg.Get_size_on_disk() || (i += msg.Get_size_on_disk()) > end_byte_offset)
										throw ::std::runtime_error("corrupted main data file -- some messages report erroneous sizes");
									else if (i == end_byte_offset) { // got to the last valid (i.e. back_msg) message...
										previous_message_boundary = begin_byte_offset + back_msg_byte_offset;
										break;
									}
								}
							}
							assert(new_range->is_open_ended() == false);
							new_range->set(previous_message_boundary - begin_byte_offset);
							callback(new_range);
						} else {

							synapse::misc::log(::std::string("Refusing to provide range due to invalid reading of the next range for topic: ") + ::boost::lexical_cast<::std::string>(this) + " path(=" + path + "). This could be, ligitimately, due to concurrent sparsification process.\n", true);
							callback(nullptr);

							// In case some other clients have already attached (when range was reading-in-flight), then we must set sparsified_invalid mask on range (which will set bitmask for sparsification invalid state in the range). alternative is to maintain a vector of all requests for pending-read range (i.e. callback is issued only after the range is fully read). this may not be as efficient though and could lead to greater code maintenance requirements. current approach is to have a 'guard' for sparsification (even time-wise) so that any sparification-invalidated range is guaranteed to be before any user request (e.g. sparsify-by-timestamp).
							new_range->Invalidate_due_to_sparsification();
						}
					} else
						throw ::std::runtime_error("on reading new_range file...");
				} catch (::std::exception const & e) { put_into_error_state(e); }
			});
		} else {
			synapse::misc::log(::std::string("Refusing to provide range due to invalid range request for topic: ") + ::boost::lexical_cast<::std::string>(this) + " path(=" + path + "). This could be, ligitimately, due to concurrent sparsification process.\n", true);
			callback(nullptr);
		}
	}

public:
	/**
		\pre at this stage, it is presumed that all of the index-based search has been done and no indicies have been located as of yet...
	*/
	template <typename CallbackType>
	void
	async_get_range_from_byte_offset(uint_fast64_t const & begin_byte_offset, CallbackType && callback)
	{
		assert(strand.running_in_this_thread());
		try {
			if (!async_error) {

				uint_fast64_t end_byte_offset;
				{
					auto already_there(get_covering_range(begin_byte_offset, end_byte_offset));
					if (already_there) {
						callback(already_there);
						return;
					}
				} 

				if (!file.get_meta().get_data_size()) { // nothing exists (no ram search earlier (at the start) and no file size recorded.. pretty confident need to get from future
					ensure_last_open_end_range(callback);
				} else {

					end_byte_offset = ::std::min(file.get_meta().get_data_size(), ::std::min(end_byte_offset, begin_byte_offset + TargetBufferSize)); 

					uint_fast64_t const msg_boundary_read_end(msg_boundary_index->Has_entries() ? msg_boundary_index->Unquantised_key_value_to_quantised_byte_offset(end_byte_offset) : -1);
					if (msg_boundary_index->Key_at_byte_offset_within_end_limit(msg_boundary_read_end) == false) { // if indexing is not yet done on this offset (on disk)... then read as much as can directly from file and use sequential scan...
						assert(begin_byte_offset < file.get_meta().get_data_size());
						range_getter(begin_byte_offset, file.get_meta().get_data_size(), true, -1, shared_from_this(), callback); // last arg is a dummy TODO more elagance please
					} else {
						assert(msg_boundary_index->Has_entries());
						auto this_(shared_from_this());
						msg_boundary_index->async_get_key(msg_boundary_read_end, [this, begin_byte_offset, end_byte_offset, this_, callback](void const * buffer) mutable noexcept {
							assert(strand.running_in_this_thread());
							try {
								if (!async_error) {
									if (buffer != nullptr) {
										uint_fast64_t previous_message_boundary(synapse::misc::Get_alias_safe_value<uint64_t>(buffer));
										uint_fast64_t tmp_end_byte_offset(synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(buffer) + 1)); 

										assert(tmp_end_byte_offset <= file.get_meta().get_data_size());
										assert(tmp_end_byte_offset > previous_message_boundary);

										// due to quantisation nature of any index, the msg_boundary index may return end_byte_offset that is earlier that our begin_byte_offset, the saving grace of this issue is that it will only happen when there is less than index-quantization amount memory to deal with -- so sequential message-boundaries-only scan will be fast.
										bool sequential_scan_exception;
										if (tmp_end_byte_offset > begin_byte_offset) {
											assert(previous_message_boundary >= begin_byte_offset);
											sequential_scan_exception = false;
											end_byte_offset = tmp_end_byte_offset;
										} else {
											sequential_scan_exception = true;
											assert(begin_byte_offset + TargetBufferSize != end_byte_offset);
										}

										assert(begin_byte_offset < file.get_meta().get_data_size());
										assert(begin_byte_offset < end_byte_offset);

										// need to make sure that while the async thingies are going that no new requests will generate yet another instance of the range for the same key
										auto already_existing_and_acceptable_range(get_covering_range(begin_byte_offset, tmp_end_byte_offset));
										if (!already_existing_and_acceptable_range) {

											if (sequential_scan_exception == true) 
												end_byte_offset = ::std::min(end_byte_offset, tmp_end_byte_offset);

											range_getter(begin_byte_offset, end_byte_offset, sequential_scan_exception, previous_message_boundary, this_, callback);

										} else {
											callback(already_existing_and_acceptable_range);
										}
									} else {
										synapse::misc::log(::std::string("Refusing to provide range due to invalid reading of msg_boundary_index for topic: ") + ::boost::lexical_cast<::std::string>(this) + " path(=" + path + "). This could be, ligitimately, due to concurrent sparsification process.\n", true);
										callback(nullptr);
									}
								} else
									throw ::std::runtime_error("on reading msg_boundary_index");
							} catch (data_processors::Size_alarm_error const & e) {
								synapse::misc::log(::std::string("Refusing to provide range (Size_alarm_error ") + e.what() + ") for topic: " + ::boost::lexical_cast<::std::string>(this) + " path: " + path + '\n', true);
								callback(nullptr);
							} catch (::std::exception const & e) { put_into_error_state(e); }
						}); // reading of special index
					}
				}
			} else
				throw ::std::runtime_error("index file read");
		} catch (data_processors::Size_alarm_error const & e) {
			synapse::misc::log(::std::string("Refusing to provide range (Size_alarm_error ") + e.what() + ") for topic: " + ::boost::lexical_cast<::std::string>(this) + " path: " + path + '\n', true);
			// Todo -- do check if can just call or need to post first (to prevent possible immediate recursion)!
			callback(nullptr);
		} catch (::std::exception const & e) { put_into_error_state(e); }
	}

private:
	/**
		\about determine if given byte offset (i.e. start of the message) is covered by already-existing-in-RAM ranges
	*/
	range_ptr_type
	get_covering_range(uint_fast64_t const & begin_byte_offset, uint_fast64_t & next_begin_byte_offset) const noexcept
	{
		if (ranges.empty() == false) {
			auto found(ranges.lower_bound(begin_byte_offset));
			if (found != ranges.end()) {
				if (found->second->begin_byte_offset != begin_byte_offset) { // if greater than the start of what we want...
					if (found != ranges.begin())
						--found;
					else {
						next_begin_byte_offset = found->second->begin_byte_offset;
						return nullptr;
					}
				}
				if (found->second->end_byte_offset > begin_byte_offset)
					return found->second;
			} else {
				--found;
				if (found->second->begin_byte_offset <= begin_byte_offset && found->second->end_byte_offset > begin_byte_offset)
					return found->second;
			}
		} 
		next_begin_byte_offset = -1;
		return nullptr;
	}

public:
	/**
		\note possibly comes from different thread
		*/
	template <typename CallbackType>
	void
	async_get_range(uint_fast32_t index, uint_fast64_t key, CallbackType callback) noexcept
	{
		::boost::shared_ptr<topic> this_(shared_from_this());
		// TODO proper type for memory allocation in asio's handler/calbacks (instead of binding)... if need be
		strand.post([this, this_, index, key, callback(::std::forward<CallbackType>(callback))]() mutable noexcept {
			try {
				if (!async_error) {
					// TODO, just assert index < indicies_size instead of runtime check...
					if (index < indicies.size()) {
						indicies[index]->post_pending_request(key, [this, callback](range_ptr_type const & Range, void const * Buffer, bool const Error) mutable noexcept {
							try {
								assert(strand.running_in_this_thread());
								if (!async_error && !Error) {
									if (Range) {
										callback(Range);
									} else if (Buffer != nullptr) {
										// issue the actual async. process of getting the ranges
										async_get_range_from_byte_offset(
											// NOTE -- this is currently possible because all indicies have 1st entry for message begin byte offset. If this is to change later on -- will need to infer semantics from the 'index' varable (as to which index we are dealing with and what it means). 
											::std::max(synapse::misc::Get_alias_safe_value<uint64_t>(Buffer), Cached_meta_begin_byte_offset), 
											callback
										);
									} else // if nothing has been recorded (e.g. even before the very 1st call to 'max_key_update') -- just go for the earliest possible
										async_get_range_from_byte_offset(Cached_meta_begin_byte_offset, callback); 
								} else
									throw ::std::runtime_error("async_get_range callback finds object already in error");
							} catch (::std::exception const & e) { 
								put_into_error_state(e); 
								callback(nullptr);
							}
						});
					} else
						callback(nullptr);
				} else
					throw ::std::runtime_error("error on io.post from async_get_range");
			} catch (::std::exception const & e) {
				put_into_error_state(e);
				callback(nullptr);
			}
		});
	}

	template <typename CallbackType, typename Index_throttling_callback_type>
	void
	async_get_first_range_for_connected_publisher(CallbackType && callback, Index_throttling_callback_type && Index_throttling_callback) noexcept
	{
		::boost::shared_ptr<topic> this_(shared_from_this());
		// TODO proper type for memory allocation in asio's handler/calbacks (instead of binding)... if need be
		strand.post([this, this_, callback(::std::forward<CallbackType>(callback)), Index_throttling_callback(::std::forward<Index_throttling_callback_type>(Index_throttling_callback))]() mutable noexcept {
			try {
				if (!async_error) {
					msg_boundary_index->Set_max_key_update_requests_throttling_callback(Index_throttling_callback);
					for (auto && Index : indicies)
						Index->Set_max_key_update_requests_throttling_callback(Index_throttling_callback);
					ensure_last_open_end_range(callback);
				} else
					throw ::std::runtime_error("error on io.post from async_get_first_range_for_connected_publisher");
			} catch (::std::exception const & e) {
				put_into_error_state(e);
				callback(nullptr);
			}
		});
	}
	
	// TODO!!! make some of these methosd explicitly 'dispatch' capable (for performance reasons)... although some code (like subscribers) cannot handle it because of re-entrancy issues... (yet other code, like publisher) may indeed handle it just fine
	/**
		\about accelerated version for subsequent (after the 1st) requests in streaming scenarios
		\note possibly comes from different thread
		\pre producer does not call it 'midway through previous range' -- only when in actually needs a new range (consumers can do either, but probbaly only on a closed range...)
		*/
	template <typename CallbackType>
	void 
	async_get_next_range(range_ptr_type & current_range, CallbackType && callback) noexcept
	{
		::boost::shared_ptr<topic> this_(shared_from_this());
		strand.post([this, this_, current_range, callback(::std::forward<CallbackType>(callback))]() mutable noexcept {
			try {
				if (!async_error) {
					assert(strand.running_in_this_thread());
					// there can only be one range in RAM capable of satisfying the byte offset requirements... and that is the 'next' of the ranges_membership of the range object
					assert(current_range->ranges_membership != ranges.end());
					auto next_range(::std::next(current_range->ranges_membership));
					assert(next_range == ranges.end() || next_range->second->begin_byte_offset > current_range->begin_byte_offset);
					//assert(next_range == ranges.end() || next_range->second->end_byte_offset > current_range->end_byte_offset); // not sure about this one (as msg_boundary index may extend the end_byte_offset in an 'overlapping/tailing' sorta way... TODO will consider some more when there's time
					assert(next_range == ranges.end() || next_range->second->end_byte_offset > next_range->second->begin_byte_offset);
					if (next_range != ranges.end() && next_range->second->begin_byte_offset <= current_range->end_byte_offset) {
						assert(current_range->is_open_ended() == false);
						// let interested parties know of found range
						callback(next_range->second);
					} else { // at this stage there are basically no RAM-existing range objects to satsify the request, so do a disk-io search

						uint_fast64_t const begin_byte_offset(current_range->end_byte_offset);
						if (file.get_meta().get_data_size() > begin_byte_offset) {
							assert(current_range->is_open_ended() == false);
							async_get_range_from_byte_offset(begin_byte_offset, ::std::move(callback));
						} else { // future request (e.g. data will come eventualy from possible publisher source)
							// note: the 1st PageSize merging  (from last PageSize of the preceeding range) will be done by topic in the "write-out" mechanisms
							
							// this is a somewhat inelegant hack (later TODO -- refactor to async_get_next_open_ended_range method), works here even w.r.t. consumers calling 'async_get_next_range' because consumers are not to call 'async_get_next_range' on an open ended range anyway... TODO some more assertions to make sure this will hold!!!
							bool resource_checking_async_loop_already_actuated(false);	
							if (current_range->is_open_ended() == true) {
								current_range->last_message();
								actuate_resource_checking_async_loop(shared_from_this());
								resource_checking_async_loop_already_actuated = true;
							}
							ensure_last_open_end_range(::std::move(callback), resource_checking_async_loop_already_actuated);
						}
					}
				} else
					throw ::std::runtime_error("async_get_next_range topic is already in error state");
			} catch (::std::exception const & e) { put_into_error_state(e); }
		});
	}
	
	template <typename CallbackType>
	void
	ensure_last_open_end_range(CallbackType && callback, bool resource_checking_async_loop_already_actuated = false)
	{
		try {
			assert(strand.running_in_this_thread());
			auto last_range_i(ranges.rbegin());
			if (last_range_i == ranges.rend() || last_range_i->second->is_open_ended() == false) {
				auto const Total_size(get_total_size());
				auto const File_size_on_disk(file.get_meta().get_data_size());
				//synapse::misc::log("new last open publishing range\n", true);
				// TODO -- review and possibly reduce this pool-allocation-code commonality with other places in this source file (wrap in a helper method)
				assert(Total_size >= File_size_on_disk);
				auto const Unwritten_data_size(Total_size - File_size_on_disk);
				if (Unwritten_data_size > unwritten_data_size_watermark) {
					auto this_(shared_from_this());
					Awaiting_for_free_ranges_due_to_unwritten_data_size.emplace_back([this, this_, callback]() mutable {
						ensure_last_open_end_range(::std::move(callback));
					});
					if (resource_checking_async_loop_already_actuated == false)
						actuate_resource_checking_async_loop(this_);
				} else {
					range_ptr_type new_range;
					if (cached_ranges.empty() == false) {
						new_range = cached_ranges.front();
						cached_ranges.pop();
					} else { 
						auto this_(shared_from_this());
						new_range.reset(new range_type(this_)); 
						if (resource_checking_async_loop_already_actuated == false && ranges.size() == cached_ranges_watermark) {
							strand.post([this, this_](){ // posting here because dont want to slow-down the process of returning the actually-needed range (the claim-back resource-checking-loop can take place later on at the earliest opportunity)
								actuate_resource_checking_async_loop(this_);
							});
						}
					}
					assert(new_range->unique());

					if (ranges.empty()) 
						schedule_resource_checking_timer();
					new_range->init(Total_size, -1);
					new_range->obtain_topic_membership();
					assert(new_range->use_count() == 2);
					callback(new_range);
				}
			} else {
				assert(last_range_i->second->is_open_ended() == true);
				callback(last_range_i->second);
			}
		} catch (data_processors::Size_alarm_error const & e) {
			synapse::misc::log("Refusing to provide range in ensure_last_open_end_range (client error or memory alarm) for topic: " + ::boost::lexical_cast<::std::string>(this) + " path: " + path + ' ' + e.what() + '\n', true);
			// Todo -- do check if can just call or need to post first (to prevent possible immediate recursion)!
			callback(nullptr);
		}
	}

	struct Scoped_quit_fencer {
		Scoped_quit_fencer() {
			++synapse::asio::quitter::get().quit_fence_size;
		}
		~Scoped_quit_fencer() {
			--synapse::asio::quitter::get().quit_fence_size;
		}
	} Scoped_quit_fencer;

	topic(topic const &) = delete; 
	void operator = (topic const &) = delete; 
	/// \note -- make sure that the "container of topics" is locked when adding and newing-up individual topics

	topic(::std::string const & path, Meta_Topic_Pointer_Type const & mt)
	: Meta_Topic_Shim(mt), path(path), strand(synapse::asio::io_service::get()), Async_meta_file_handle_wrapper(strand, path + DATA_PROCESSORS_PATH_SEPARATOR "data_meta", false), Meta_file(strand, Async_meta_file_handle_wrapper, PageSize), Async_file_handle_wrapper(strand, path + DATA_PROCESSORS_PATH_SEPARATOR "data", true), file(strand, Meta_file, Async_file_handle_wrapper, PageSize), 
	// ideally this would be brace-initialized in class, but clang-cl/msvc treat this as explicit ctor which takes an initializer list (even though it is not of value_type, obviously). Gcc, on the other hand, is happy..  but for the sake of multi-compiler happiness we shall, at least temporarily, compromise... TODO -- revisit later in future.
	// this appears to be *only* done for casses of 'dequeue' container... perhaps MS c++ lib is not all that compliant/great...
	// TODO, revisit in future to try using brace-initialization syntax
	Awaiting_for_free_ranges_due_to_unwritten_data_size(typename decltype(Awaiting_for_free_ranges_due_to_unwritten_data_size)::allocator_type{sp}),
resource_checking_timer(strand.get_io_service()) {
		// Note -- Important! To make management of memory-alarm both: efficient (fine grained, reactive, etc. as opposed to polling system calls) as well as reasonably simple to maintain -- it is important that if there are any memory-alarm excetpions/issues then those ought to take place here in ctor (i.e. they should take place synchronously, as opposed to async 'dangling' callbacks grabbing 'shared-this' pointer and thereby making topic object still 'linger in a zombee state (half-valid, etc.)' 
		// This is why the 'async_open' in index is separate (otherwise 1st index may be 'OK and opening in-flight with shared-topic-ptr', whilst second index ctor may throw Size_alarm_error -- so the 1st index is still 'valid' whilst the topic itself is not (and then topic would need to handle 'async_error' propagation instead of currently doing 'abort()' -- would need to call meta_topic with pending_requests being notified, making sure that other 'life-scope' instances are ok, making sure to also set 'async_error' in the first place when any of the indicies are thrown, etc. etc. etc.)
		// So currently trying to keep it simple -- topic Size_alarm_error excetpion is throw only in ctor (the only other case of range.init() is handled explicitly and without putting the topic into invalid state -- i.e. topic still remains valid and ready for interaction).

		// bytestream to message boundary index
		msg_boundary_index.reset(new msg_boundary_index_type(*this, -1, path + DATA_PROCESSORS_PATH_SEPARATOR "index_0_data_meta", path + DATA_PROCESSORS_PATH_SEPARATOR "index_0_data", 
			PageSize * 2, TargetBufferSize,  // read/write buffer sizes
			19 // ~0.5 meg quantization 
		));
		assert(TargetBufferSize >= static_cast<uint64_t>(1u) << msg_boundary_index->quantise_shift_mask);

		// load up the indicies...
		// for the time being hardcoding the indicies...
		// \TODO will move later to runtime config/specifications (a must really)
		indicies.reserve(indicies_size);

		// Note: absolute sequence number (server-wide) is no longer indexed...

		// timestamp (microseconds)
		indicies.emplace_back(new index_with_ranges_type(*this, 
			0, // index in the RAM infrastructure (Note: we dont index sequence number any longer, just the timestamp).
			path + DATA_PROCESSORS_PATH_SEPARATOR "index_2_data_meta", path + DATA_PROCESSORS_PATH_SEPARATOR "index_2_data", 
			PageSize * 2, TargetBufferSize,  // read/write buffer sizes
			1000000ull * 60 * 60 * 24 * 100, // max indexable gap of 100 days 
			20 // in micros ~= 1 second(ish) 
			, 1 // index in Message (index 0 in Database::Message is sequence number, and 1 is for timestamp)
		));
		assert(indicies_size == indicies.size());

		assert(resource_checking_period != quitting_resource_checking_period);
	}

	~topic()
	{
		#ifdef DATA_PROCESSORS_TEST_SYNAPSE_DELAYED_TOPIC_DESTRUCTOR_RACING_CONDITION_1
			::std::this_thread::sleep_for(::std::chrono::milliseconds(DATA_PROCESSORS_TEST_SYNAPSE_DELAYED_TOPIC_DESTRUCTOR_RACING_CONDITION_1));
		#endif
		// need this for sparsification process getting its own range...
		synapse::misc::log("topic dtor: " + ::boost::lexical_cast<::std::string>(this) + " path: " + path + '\n', true);
		assert(ranges.empty() == true);
		#ifndef NDEBUG
			// Note: if it turns out the overall design does not want to guarantee this as an assertion then there may be a possibility to simply test and callback (with null, null).
			// Essentially saying that it should be empty (but posting to strand because cant check in own thread, because of possibly concurrency).
			Meta_Topic_Shim.mt->Topic_Factory.strand.post([Previous_Inflight_Publisher_Request(Previous_Inflight_Publisher_Request)]{
				assert(!Previous_Inflight_Publisher_Request);
			});
		#endif
	}

	// async_opet et al
	//\about opening of the topic files. mainly due to the fact that one cannot call shared_from_this from the very ctor of the enable_shared_from_this class
	// ... some util vars helping to detetrmine when all of the necessary async files have been opened

	unsigned constexpr static total_opened_expected_size = indicies_size + 2; // includes all indicies as well as the actual data file
	unsigned total_openened_check = 0;
#ifndef NDEBUG
	bool Async_open_complete{false};
	bool Async_open_in_progress{false};
#endif
	template <typename CallbackType>
	void
	async_open(CallbackType && callback) 
	{
		assert(!Async_open_complete);
		assert(!Async_open_in_progress);
#ifndef NDEBUG
		Async_open_in_progress = true;
#endif
		auto this_(shared_from_this());

		auto On_topic_loaded([this, this_, callback(::std::forward<CallbackType>(callback))](bool error) mutable noexcept {
			assert(strand.running_in_this_thread());
			try {
				if (!error && !async_error) {
					if (++total_openened_check == total_opened_expected_size) {

						auto const & meta(file.get_meta());

						Cached_meta_begin_byte_offset = meta.Get_begin_byte_offset();
						Cached_meta_target_data_consumption_limit = meta.Get_target_data_consumption_limit();
						Cached_meta_requested_sparsify_upto_timestamp = meta.Get_requested_sparsify_upto_timestamp();
						Cached_meta_processed_sparsify_upto_timestamp = meta.Get_processed_sparsify_upto_timestamp();
						Cached_meta_sparsify_upto_byte_offset = meta.Get_sparsify_upto_byte_offset();

						assert(!Async_open_complete);
						assert(Async_open_in_progress);
						#ifndef NDEBUG
							Async_open_in_progress = false;
							Async_open_complete = true;
						#endif
						if (TargetBufferSize < static_cast<uint64_t>(1u) << msg_boundary_index->quantise_shift_mask)
							throw ::std::runtime_error("msg_boundary_index quantization should be less than BufferSize of the range");
						callback(false);
					}
				} else 
					throw ::std::runtime_error("topic data file(s) could not be openend");
			} catch (::std::exception const & e) { put_into_error_state(e); callback(true); }
		});

		// bytestream to message boundary index
		msg_boundary_index->async_open(On_topic_loaded);
		// other indicies
		assert(indicies_size == indicies.size());
		for (auto && Index : indicies) 
			Index->async_open(On_topic_loaded);

		// now also load up the data files itself
		file.async_open(On_topic_loaded, &database::Data_meta_ptr::initialise_);

		// setup quitting process
		::boost::weak_ptr<topic> weak_scope(this_); 
		synapse::asio::quitter::get().add_quit_handler([this, weak_scope](synapse::asio::quitter::Scoped_Membership_Registration_Type && Scoped_Membership_Registration){
			if (auto scope = weak_scope.lock())
				strand.post([this, scope, On_Quit_Scoped_Membership_Registration(::std::move(Scoped_Membership_Registration))]() mutable {
					this->On_Quit_Scoped_Membership_Registration = ::std::move(On_Quit_Scoped_Membership_Registration);
					assert(!On_Quit_Scoped_Membership_Registration);
					if (!this->On_Quit_Scoped_Membership_Registration && !Is_Quitting_In_Progress()) {
						if (Awaiting_for_free_ranges_due_to_unwritten_data_size.empty() == false) {
							// TODO, revisit in future to try using brace-initialization syntax
							decltype(Awaiting_for_free_ranges_due_to_unwritten_data_size) clear((typename decltype(Awaiting_for_free_ranges_due_to_unwritten_data_size)::allocator_type(sp)));
							::std::swap(Awaiting_for_free_ranges_due_to_unwritten_data_size, clear);
						}
						resource_checking_timer.cancel();
						resource_checking_timer.expires_from_now(::boost::posix_time::seconds(resource_checking_period = topic<MetaTopic, TargetBufferSize, PageSize>::quitting_resource_checking_period));
						resource_checking_timer_pending = Resource_checking_async_loop_was_idle_inbetween_scheduled_timeouts = true;
						resource_checking_timer.async_wait(strand.wrap(::boost::bind(&topic::on_resource_checking_timeout, scope, ::boost::asio::placeholders::error)));
					}
				});
		});
	}
	synapse::asio::quitter::Scoped_Membership_Registration_Type On_Quit_Scoped_Membership_Registration;

	// These ones are controlled by the topic_factory strand!
	::boost::weak_ptr<data_processors::synapse::asio::Any_Connection_Type> Active_Publisher;
	::boost::weak_ptr<data_processors::synapse::asio::Any_Connection_Type> Previous_Inflight_Publisher_Wannabe;
	::std::function<void(Meta_Topic_Pointer_Type const &, ::boost::shared_ptr<topic> const &)> Previous_Inflight_Publisher_Request;
	//


	/**
	Slightly special iterpretation of the params: zero means do not set (saves on separating the method into 2 and potentially causing async-plumbing to be called twice)...
	*/
	template <typename Callback_type>
	void Set_sparsification_parameters_from_subscriber(uint_fast64_t const Target_data_consumption_limit, uint_fast64_t const Timestamp, Callback_type && Callback) noexcept {
		strand.post([Target_data_consumption_limit, Timestamp, Callback(::std::forward<Callback_type>(Callback)), this, this_(shared_from_this())]() mutable noexcept {
			try {
				if (!async_error) {
					// NOTE: this 'redundant range' obtaining call is only to satisfy current design of the async resource checking loop which decide on what to do depending on whether there are ranges or not... later may refactor otherwise (but then also make sure to kick-start the resource checking loop).
					ensure_last_open_end_range([Target_data_consumption_limit, Timestamp, Callback, this, this_](range_ptr_type const & Range) mutable {
						assert(strand.running_in_this_thread());
						try {
							if (!async_error && Range) {
								if (Target_data_consumption_limit)
									Set_target_data_consumption_limit(Target_data_consumption_limit, Range);
								if (Timestamp)
									Set_sparsify_upto_timestamp(Timestamp, Range);
								Callback(false);
							} else 
								throw ::std::runtime_error("topic ensure_last_open_end_range via strand.post via Set_sparsification_parameters_from_subscriber");
						} catch (::std::exception const & e) {
							put_into_error_state(e); 
							Callback(true);
						}
					});
				} else 
					throw ::std::runtime_error("topic strand.post via Set_sparsification_parameters_from_subscriber");
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				Callback(true);
			}
		});
	}

};

template <unsigned TargetBufferSize, unsigned PageSize>
struct topic_factory {

	struct meta_topic;
	typedef topic<meta_topic, TargetBufferSize, PageSize> topic_type;
	typedef ::boost::intrusive_ptr<meta_topic> Meta_Topic_Pointer_Type;

	unsigned constexpr static Sparse_file_guard_size{5 * 1024 * 1024}; // really for sparsification as well as the 'logical vs physical/fs' sector size differentiation...
	uint_fast32_t constexpr static Database_size_contribution_guard {
		// Not super precise, but a bit of over-estimation for HDD "consumption offset per topic" is ok...
		(topic_type::indicies_size + 1) * (PageSize * 2 + TargetBufferSize + Sparse_file_guard_size) 
		+ TargetBufferSize + Sparse_file_guard_size
	};

	// TODO make this container into an asynchronous counterpart -- the more topics there are the more 'blocking' the behaviour will become
	// TODO also consider hash-mapping
	typedef ::std::map<::std::string const, Meta_Topic_Pointer_Type, ::std::less<::std::string const>, ::boost::fast_pool_allocator<::std::pair<::std::string const, Meta_Topic_Pointer_Type>>> meta_topics_type;

	struct meta_topic {

		::std::atomic<uint_fast32_t> Intrusive_Pointer_Reference_Size{0};

		bool Is_Unique() const noexcept {
			return Intrusive_Pointer_Reference_Size.load(::std::memory_order_relaxed) == 1;
		}

		friend void intrusive_ptr_add_ref(meta_topic * Meta_Topic) {
			auto const Previous_Reference_Size(::std::atomic_fetch_add_explicit(&Meta_Topic->Intrusive_Pointer_Reference_Size, 1u, ::std::memory_order_relaxed));
			if (Previous_Reference_Size == ::std::numeric_limits<uint_fast32_t>::max())
				throw ::std::runtime_error("too many shared references of meta_topic object -- past 32bit max capabilities, need to recompile");
			assert(Previous_Reference_Size !=1 || !Meta_Topic->Marked_For_Deletion.load(::std::memory_order_relaxed)); // Topic factory strand should be the only one responsible for transitioning from unique() to non-unique(), i.e. refcount from 1 to 1+; and it is also only one responsible for transitioning Marked_For_Deletion flag from 0 to >0
		}

		friend void intrusive_ptr_release(meta_topic * Meta_Topic) {
			assert(Meta_Topic);
			auto const Previous_reference_counter(::std::atomic_fetch_sub_explicit(&Meta_Topic->Intrusive_Pointer_Reference_Size, 1u, ::std::memory_order_release));
			switch (Previous_reference_counter) {
			case 2 :
			{
				uint8_t Dummy(1);
				if (Meta_Topic->Marked_For_Deletion.compare_exchange_strong(Dummy, 2, ::std::memory_order_relaxed))
					Meta_Topic->Topic_Factory.Delete_Topic_By_Iterator(::std::move(Meta_Topic->Meta_Topics_Membership));
			}
			break;
			case 1 :
			::std::atomic_thread_fence(::std::memory_order_acquire); // force others to be done on memory access before actual deletion
			delete Meta_Topic;
			}
		}

		meta_topic(meta_topic const &) = delete;

		meta_topic(topic_factory & Topic_Factory) : Topic_Factory(Topic_Factory) {
			auto const Current_database_size(database::Database_size.fetch_add(topic_factory::Database_size_contribution_guard, ::std::memory_order_relaxed));
			if (Current_database_size > database::Database_alarm_threshold) {
				database::Database_size.fetch_sub(topic_factory::Database_size_contribution_guard, ::std::memory_order_relaxed);
				throw data_processors::Size_alarm_error("Database_alarm_threshold breached in meta_topic ctor. Watermark: " + ::std::to_string(Database_alarm_threshold) + ").");
			}
		}
		~meta_topic() {
			database::Database_size.fetch_sub(topic_factory::Database_size_contribution_guard, ::std::memory_order_relaxed);
		}
		struct pending_request_type {
			uint_fast8_t index_i;
			uint_fast64_t index_begin;
			::boost::weak_ptr<data_processors::synapse::asio::Any_Connection_Type> is_publisher;
			typedef ::std::function<void(Meta_Topic_Pointer_Type const &, ::boost::shared_ptr<topic_type> const &)> callback_type;
			callback_type callback;
			template <typename Weak_Connection_Info>
			pending_request_type(uint_fast8_t index_i, uint_fast64_t index_begin, Weak_Connection_Info && is_publisher, callback_type && callback) 
			: index_i(index_i), index_begin(index_begin), is_publisher(::std::forward<Weak_Connection_Info>(is_publisher)), callback(::std::move(callback)) {
			}
		};
		typedef ::std::queue<pending_request_type, ::std::deque<pending_request_type, ::boost::fast_pool_allocator<pending_request_type>>> pending_requests_type;
		pending_requests_type pending_requests;

		// TODO -- abit hard-coded at the moment (will need to parameterize later on)
		// Atomicity is sort of "in the air" at the moment... it's either atomic or making sure that things like asyc_get_topic_for_subscriber are delivered on 'topic' strand (if topic is non-nullptr) or topic_factor strand (and client having to do more complicated 'if/else' to ensure that it is not accessing indicies_begin from outside the 'topic' strand because index can alter the value during max index key writeout during 1st key/publishing)
		::std::atomic_uint_fast64_t indicies_begin[topic_type::indicies_size] = {}; // timestamp, later may be some more indicies. TODO: if timestamp proves the only index then take out the 'array' type.

		::boost::weak_ptr<topic_type> topic;

		::std::atomic_uint_fast64_t Approximate_Topic_Size_On_Disk{0};

		#ifndef SYNAPSE_SINGLE_THREADED
			alignas(data_processors::synapse::misc::Cache_line_size)
		#endif
		::std::atomic_uint_fast64_t Maximum_Sequence_Number{0};
		::std::atomic_uint_fast64_t Latest_Timestamp{0};

		::std::atomic_uint_fast64_t Maximum_Indexable_Gaps[topic_type::indicies_size] = {}; // 100 days for timestamp. TODO: if timestamp proves the only index then take out the 'array' type.

		topic_factory & Topic_Factory;
		typename topic_factory::meta_topics_type::iterator Meta_Topics_Membership;

		::std::function<void(Meta_Topic_Pointer_Type const &)> On_Previous_Instance_Dtor;
		bool Previous_Instance_Exists{false}; // Created by (and initialised) on the Topic_Factory strand!
		::std::atomic<uint8_t> Marked_For_Deletion{0};

		template <typename Meta_Topic_Pointer_Type>
		void Previous_Instance_Dtored(Meta_Topic_Pointer_Type && Meta_Topic) {
			Topic_Factory.strand.post([this, this_(::std::forward<Meta_Topic_Pointer_Type>(Meta_Topic))]() mutable {
				Previous_Instance_Exists = false; // It's ok -- topic factory strand had assigned it in the first place.
				if (On_Previous_Instance_Dtor) {
					auto To_Call(::std::move(On_Previous_Instance_Dtor));
					To_Call(::std::move(this_));
				}
			});
		}

	};

	::boost::asio::strand strand;

	::boost::filesystem::path topics_path;

	//~~~ notifications about new topics etc. (e.g. for wildcarded subscriptions or anything else applicable)
	typedef ::std::list<::std::function<void(Meta_Topic_Pointer_Type const &)>, ::boost::fast_pool_allocator<::std::function<void(Meta_Topic_Pointer_Type const & )>>> topic_notification_listeners_type;
	topic_notification_listeners_type topic_notification_listeners;
	void register_for_notifications(::std::function<void(typename topic_notification_listeners_type::iterator const &)> const & registered_cbk, ::std::function<void(Meta_Topic_Pointer_Type const &)> const & listener_cbk)
	{
		strand.post([this, registered_cbk, listener_cbk]() mutable noexcept {
				topic_notification_listeners.emplace_front(listener_cbk);
				registered_cbk(topic_notification_listeners.begin());
		});
	}
	// note currently doing sync. like processing due to the same reason as in other comments -- if callbacks themselves are 'strand.post's then it should mostly be ok for the time being.
	void
	notify_listeners(Meta_Topic_Pointer_Type const & Meta_Topic)
	{
		assert(strand.running_in_this_thread());
		for (auto && i : topic_notification_listeners)
			i(Meta_Topic);
	}
	void
	deregister_from_notifications(typename topic_notification_listeners_type::iterator const &it)
	{
		strand.post([this, it]() mutable noexcept {
			topic_notification_listeners.erase(it);
		});
	}
	//```

	topic_factory(::std::string const & topics_path)
	: strand(synapse::asio::io_service::get()), topics_path(topics_path) {
		// read through all topics and get their max message size -- nice thing is that it may be done synchronously this time around
		typename ::std::aligned_storage<database::Data_meta_const_ptr::size>::type buf;
		for (::boost::filesystem::directory_iterator i(topics_path); i != ::boost::filesystem::directory_iterator(); ++i) {
			auto const & path(i->path());
			auto const & topic_name(path.filename().string());
			Validate_topic_name(topic_name);

			// cannot use this: ::boost::allocate_shared<meta_topic>(::boost::fast_pool_allocator<meta_topic>(), *this) because fast_pool_allocator in currently-deployed version of Boost does not uspport move semantics. TODO.
			auto && emplaced(meta_topics.emplace(topic_name, new meta_topic(*this)));
			assert(emplaced.second == true);
			auto && mt(*(emplaced.first->second));
			mt.Meta_Topics_Membership = emplaced.first;
			synapse::misc::log("added existing topic during start-up scan: " + topic_name + '\n');
			::std::ifstream ifs((path / "data_meta").string().c_str(), ::std::ios::binary);
			if (ifs.read(reinterpret_cast<char*>(&buf), database::Data_meta_const_ptr::size)) {
				database::Data_meta_const_ptr meta(&buf);
				meta.verify_hash();

				auto const Tmp_maximum_sequence_number(meta.Get_maximum_sequence_number());
				if (Tmp_maximum_sequence_number >= database::Maximum_sequence_number) {
					database::Maximum_sequence_number = Tmp_maximum_sequence_number + 1;
					synapse::misc::log("Updated maximum messages sequence number to " + ::std::to_string(Tmp_maximum_sequence_number) + '\n', true);
				}

				mt.Maximum_Sequence_Number.store(Tmp_maximum_sequence_number, ::std::memory_order_relaxed);
				mt.Latest_Timestamp.store(meta.get_latest_timestamp(), ::std::memory_order_relaxed);

				auto Appropriate_for_database_size_alarm([&mt](::std::string const & Filepath, uint_fast64_t const Data_size, uint_fast64_t const Sparse_size){

					auto const File_Attributes(::GetFileAttributes(Filepath.c_str()));
					if (File_Attributes == INVALID_FILE_ATTRIBUTES || !(File_Attributes & FILE_ATTRIBUTE_SPARSE_FILE))
						throw ::std::runtime_error("Could not validate that data file is sparse (must be). " + Filepath);

					if (Sparse_size && Sparse_size >= Data_size)
						throw ::std::runtime_error("corrupt topic, data size > sparse size. " + Filepath);

					auto const Logical_size(::boost::filesystem::file_size(Filepath));
					if (Logical_size < Sparse_size)
						throw ::std::runtime_error("corrupt topic, Logical_size > Sparse_size. " + Filepath);

					if (Data_size > Logical_size)
						throw ::std::runtime_error("corrupt topic, Data_size > Logical_size. " + Filepath);

					DWORD High_word;
					DWORD const Low_word(::GetCompressedFileSize(Filepath.c_str(), &High_word));
					if (Low_word == INVALID_FILE_SIZE && ::GetLastError() != NO_ERROR)
						throw ::std::runtime_error("::GetCompressedFileSize on " + Filepath);
					uint_fast64_t const Filesystem_size(static_cast<uint_fast64_t>(High_word) << 32 | Low_word);

					auto const Logical_data_size(Logical_size - Sparse_size);
					if (Filesystem_size > Logical_data_size && Filesystem_size - Logical_data_size > Sparse_file_guard_size)
						synapse::misc::log("SERVER STARTUP WARNING (should be corrected by recompiling). the Sparse_file_guard_size turned out to be too small for the given realistic deployment scenario. " + Filepath);

					database::Database_size.fetch_add(Filesystem_size, ::std::memory_order_relaxed);
					mt.Approximate_Topic_Size_On_Disk.fetch_add(Filesystem_size, ::std::memory_order_relaxed);

				});

				Appropriate_for_database_size_alarm((path / "data").string(), meta.get_data_size(), synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(meta.Get_begin_byte_offset(), PageSize));

				{ // load message boundary index also (for sparsification size thingy)
					typename ::std::aligned_storage<database::Index_meta_const_ptr::size>::type buf;
					::std::ifstream ifs((path / "index_0_data_meta").string().c_str(), ::std::ios::binary);
					if (ifs.read(reinterpret_cast<char*>(&buf), database::Index_meta_const_ptr::size)) {
						database::Index_meta_const_ptr const meta(&buf);
						meta.verify_hash();
						auto && quantise_shift_mask(meta.get_quantise_shift_mask());
						if (quantise_shift_mask >= sizeof(uint64_t) * 8)
							throw ::std::runtime_error("Server configuration/compilation error. Quantise shift mask exceeds 64bit int width (undefined behaviour).");
						if (quantise_shift_mask > 0xff)
							throw ::std::runtime_error("message boundary index: index_data_meta_traits read from disk quantise_shift_mask is too large for current version of data format");
						if (topic_type::msg_boundary_index_type::key_entry_size != meta.get_key_entry_size())
							throw ::std::runtime_error("underlying message boundary index file is not compatible (key_entry_size) with codebase. codebase(=" + ::std::to_string(topic_type::msg_boundary_index_type::key_entry_size) + ", file(=" + ::std::to_string(meta.get_key_entry_size()) + ')');

						Appropriate_for_database_size_alarm((path / "index_0_data").string(), meta.get_data_size(), synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(((meta.Get_begin_key() - meta.Get_start_of_file_key()) >> quantise_shift_mask) * topic_type::msg_boundary_index_type::key_entry_size, PageSize));
					} else
						throw ::std::runtime_error("reading meta for message boundary index");
				}


				// TODO --if deprecating the CRC-reverification on reading of every message (e.g. in range.h) and moving to the ReFS/W8 (away from NTFS/W7) then: here also verify (synchronously) the hash of the last message (can probably start with the last msg_boundary_index value and proceed validating until the end

				// load the first available index value from each of the index (except perhaps the very 1st, special, one)
				static_assert(topic_type::indicies_size, "message boundary index should always be present");
				auto begin_key_loader([&path, &mt, Tmp_maximum_sequence_number, &Appropriate_for_database_size_alarm](unsigned index_i) { //~~~
					// TODO -- this is very hard-coded at the moment (essentially mimics code from topic and index classes... will need to parameterize later on)
					typename ::std::aligned_storage<database::Index_meta_const_ptr::size>::type buf;
					::std::ifstream ifs((path / ("index_" + ::std::to_string(index_i + 2) + "_data_meta")).string().c_str(), ::std::ios::binary);
					if (ifs.read(reinterpret_cast<char*>(&buf), database::Index_meta_const_ptr::size)) {
						database::Index_meta_const_ptr const meta(&buf);
						meta.verify_hash();

						if (!meta.get_data_size() && Tmp_maximum_sequence_number)
							throw ::std::runtime_error("possibly corrupt index file: " + (path / ("index_" + ::std::to_string(index_i + 2) + "_data_meta")).string());

						auto && quantise_shift_mask(meta.get_quantise_shift_mask());
						if (quantise_shift_mask >= sizeof(uint64_t) * 8)
							throw ::std::runtime_error("Server configuration/compilation error. Quantise shift mask exceeds 64bit int width (undefined behaviour).");
						if (quantise_shift_mask > 0xff)
							throw ::std::runtime_error("index_data_meta_traits read from disk quantise_shift_mask is too large for current version of data format");

						if (topic_type::index_with_ranges_type::key_entry_size != meta.get_key_entry_size())
							throw ::std::runtime_error("underlying index file is not compatible (key_entry_size) with codebase. codebase(=" + ::std::to_string(topic_type::index_with_ranges_type::key_entry_size) + ", file(=" + ::std::to_string(meta.get_key_entry_size()) + ')');

						// minor semantic conversion issue. Index defaults its set_begin_key to -1 when no data is written (empty), yet various subscriptions/lazy-retirements/etc denote/use 0 as default (or unknown) key value.
						auto const Index_begin_key(meta.Get_begin_key());
						mt.indicies_begin[index_i].store(Index_begin_key == static_cast<decltype(meta.Get_begin_key())>(-1) ? 0 : Index_begin_key, ::std::memory_order_relaxed);
						Appropriate_for_database_size_alarm((path / ("index_" + ::std::to_string(index_i + 2) + "_data")).string(), meta.get_data_size(), synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(((meta.Get_begin_key() - meta.Get_start_of_file_key()) >> quantise_shift_mask) * topic_type::index_with_ranges_type::key_entry_size, PageSize));

					} else
						throw ::std::runtime_error("topic_factory could not read index metadata upon startup: " + (path / ("index_" + ::std::to_string(index_i + 2) + "_data_meta")).string());
				});//```
				// Note: sequence number is no longer indexed.
				begin_key_loader(0); // timestamp. TODO -- in future if it is the only index really, then refactor the above lambda into explicit code.

				{
					auto const Estimated_database_size(database::Database_size.load(::std::memory_order_relaxed));
					synapse::misc::log("Current estimated(ish) database size is " + (Estimated_database_size > 1000000000 ? ::std::to_string(Estimated_database_size * 0.000000001) + "GB" : Estimated_database_size > 1000000 ? ::std::to_string(Estimated_database_size * 0.000001) + "MB" : ::std::to_string(Estimated_database_size)) + '\n', true);
				}
			} else
				throw ::std::runtime_error("topic_factory could not read metadata upon startup of the topic: " + (path / "data_meta").string());
		}
	}

	/**
		returns false if request was not processed (e.g. 'topic_lock' was null yet it was needed to process the request) 
		note: actually decides whether to send null, or not for the topic as well as whether topic needs to be loaded
		*/
	template <typename CallbackType, typename Weak_Connection_Info>
	bool static
	process_pending_request(uint_fast8_t index_i, uint_fast64_t index_begin, Weak_Connection_Info && is_publisher, CallbackType && callback, Meta_Topic_Pointer_Type const & mt, ::boost::shared_ptr<topic_type> const & t)
	{
		assert(mt);
		// TODO -- this is a bit hard-coded for the moment (parameterize later on if theres time)
		assert(!t || mt->topic.lock() == t && t->loaded == true); 

		if (index_i == static_cast<decltype(index_i)>(-1)) { // publisher
			auto const Publisher_Wannabe(is_publisher.lock());
			if (Publisher_Wannabe) {
				if (!t) {
					assert(!mt->topic.lock() || mt->topic.lock()->loaded == false);
					return false; // need topic, but such may not be loaded yet...
				} else { // within reasonable range (e.g. time or sequence number)
					auto & Previous_Inflight_Publisher_Request(t->Previous_Inflight_Publisher_Request);
					if (Previous_Inflight_Publisher_Request) {
						Previous_Inflight_Publisher_Request(nullptr, nullptr);
						Previous_Inflight_Publisher_Request = ::std::forward<CallbackType>(callback);
						t->Previous_Inflight_Publisher_Wannabe = ::std::forward<Weak_Connection_Info>(is_publisher);
					} else { 
						auto const Active_Publisher(t->Active_Publisher.lock());
						if (!Active_Publisher) {
							// must be done by the factory (not the subscriber), even though the subscirber is the only one allowed to resign (to allow for syrc. behaviour when 'closing-handshake' is done at amqp level and immediate re-connection with publishing for the same topic takes place (unlikely, but possible)
							t->Active_Publisher = Publisher_Wannabe;
							callback(mt, t);
						} else {
							if (!(Active_Publisher->Get_Publisher_Sticky_Mode_Atomic() & static_cast<typename ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type>(data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask::Replace_Self)) || !(Publisher_Wannabe->Get_Publisher_Sticky_Mode_Atomic() & static_cast<typename ::std::underlying_type<data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask>::type>(data_processors::synapse::asio::Any_Connection_Type::Publisher_Sticky_Mode_Bitmask::Replace_Another))) {
								synapse::misc::log("CLIENT (=" + Publisher_Wannabe->Get_Description() + ") ERROR, cant have multiple publishers to the same topic (Last known publisher= " + Active_Publisher->Get_Description() + "). Either existing connection is sticky or incoming connection is too nice (or both).\n", true);
								callback(nullptr, nullptr);
							} else {
								synapse::misc::log("CLIENT (=" + Publisher_Wannabe->Get_Description() + ") WARNING, cant have multiple publishers to the same topic. Will disconnect last known non-sticky publisher (= " + Active_Publisher->Get_Description() + ")\n", true);
								assert(!Previous_Inflight_Publisher_Request);
								Previous_Inflight_Publisher_Request = ::std::forward<CallbackType>(callback);
								t->Previous_Inflight_Publisher_Wannabe = ::std::forward<Weak_Connection_Info>(is_publisher);
								Active_Publisher->Async_Destroy();
							}
						}
					}
				}
			}
		} else { // subscriber
			assert(index_i < topic_type::indicies_size);
			auto const Index_begin(mt->indicies_begin[index_i].load(::std::memory_order_relaxed));
			if (!t && Index_begin <= index_begin) {
				assert(!mt->topic.lock() || mt->topic.lock()->loaded == false);
				return false; // need topic, but such may not be loaded yet...
			} else if (Index_begin > index_begin) { // too early
				callback(mt, nullptr);
			} else { // within reasonable range (e.g. time or sequence number)
				assert(t);
				callback(mt, t);
			}
		}
		return true;
	}

	template <typename Topic_Pointer, typename Any_Connection_Pointer>
	void Resign_Publisher(Topic_Pointer && Topic, Any_Connection_Pointer && Connection) {
		strand.post([this, Topic(::std::forward<Topic_Pointer>(Topic)), Connection(::std::forward<Any_Connection_Pointer>(Connection))]() {
			assert(Topic);
			auto & Previous_Inflight_Publisher_Request(Topic->Previous_Inflight_Publisher_Request);
			if (Previous_Inflight_Publisher_Request) {
				Topic->Active_Publisher = ::std::move(Topic->Previous_Inflight_Publisher_Wannabe);
				auto To_Call(::std::move(Previous_Inflight_Publisher_Request));
				assert(!Previous_Inflight_Publisher_Request);
				assert(!Topic->Previous_Inflight_Publisher_Request);
				assert(!Topic->Previous_Inflight_Publisher_Wannabe.lock());
				To_Call(Topic->Meta_Topic_Shim.mt, Topic);
				#ifdef DATA_PROCESSORS_TEST_SYNAPSE_MULTIPLE_MODES_OF_PUBLISHING_1
					synapse::misc::log("Resign_Publisher seen resignation with pending new request being ready.\n", true);
				#endif
			} else if (Connection) {  // because resignation can happen whilst the actual peer is still active (e.g. only channel is closed, etc. but the actual connection is still intact).
				assert(Topic->Active_Publisher.lock());
				assert(Connection);
				assert(static_cast<decltype(Topic->Active_Publisher.lock().get())>(Connection.get()) == Topic->Active_Publisher.lock().get());
				Topic->Active_Publisher.reset();
				#ifdef DATA_PROCESSORS_TEST_SYNAPSE_MULTIPLE_MODES_OF_PUBLISHING_1
					synapse::misc::log("Resign_Publisher seen resignation from active/live peer.\n", true);
				#endif
			}
		});
	}

	void static 
	Validate_topic_name(::std::string const & Key) 
	{
		// equivalent of Key.find_first_not_of("abcdefghijklmnopqrstuvwxyz.@0123456789+-_ ") == ::std::string::npos
		auto Scan_string([&Key]()->bool{
			for (auto const & Char : Key) {
				if (
					! (Char > '`' && Char < '{' // a-z
							|| Char == '.' || Char == '@' 
							|| Char > '/' && Char < ':' // 0-9
							|| Char == '+' || Char == '-' || Char == '_' || Char == ' '
						)
				)
					return false;
			}
			return true;
		});
		if (
			Key.front() == '.' || Key.front() == ' ' 
			|| Key.back() == '.' || Key.back() == ' '
			|| !Scan_string()
		)
			throw ::std::runtime_error("Topic name(=" + Key + ") contains invalid character.");
	}

	/**
	 * \about returns a pointer to the existing, and already opened, topic. or nullptr on error.
	*/
	//~~~ for publisher
	template <typename CallbackType, typename Weak_Connection_Info>
	void 
	async_get_topic_for_publisher(::std::string const & topic_name, CallbackType && callback, Weak_Connection_Info && Publisher_Info) 
	{
		assert(topic_name.empty() == false);
		Validate_topic_name(topic_name);
		strand.dispatch([this, topic_name, callback(::std::forward<CallbackType>(callback)), Publisher_Info(::std::forward<Weak_Connection_Info>(Publisher_Info))]() mutable noexcept {
			try {
				assert(strand.running_in_this_thread());
				if (!async_error) {
					auto Maybe_Existing_Element(meta_topics.lower_bound(topic_name));
					if (Maybe_Existing_Element == meta_topics.end() || Maybe_Existing_Element->first != topic_name) {

						// cannot use this: ::boost::allocate_shared<meta_topic>(::boost::fast_pool_allocator<meta_topic>(), *this) because fast_pool_allocator in currently-deployed version of Boost does not uspport move semantics. TODO.
						auto New_Element(meta_topics.emplace_hint(Maybe_Existing_Element, topic_name, new meta_topic(*this)));
						New_Element->second->Meta_Topics_Membership = New_Element;
						notify_listeners(New_Element->second);
						get_topic(Publisher_Info, topic_name, callback, New_Element->second);
					} else { 
						if (!Maybe_Existing_Element->second->Marked_For_Deletion.load(::std::memory_order_relaxed))
							get_topic(Publisher_Info, topic_name, callback, Maybe_Existing_Element->second);
						else {
							synapse::misc::log("Refusing to provide topic to publisher (marked for deletion) : " + topic_name + '\n', true);
							callback(nullptr, nullptr);
						}
					}
				} else
					throw ::std::runtime_error("erroneous state in async_get_topic_for_publisher from topic_name");
			} catch (data_processors::Size_alarm_error const & e) {
				synapse::misc::log("Refusing to provide topic (Size_alarm_error) : " + topic_name + ' ' + e.what() + '\n', true);
				// Todo -- do check if can just call or need to post first (to prevent possible immediate recursion)!
				callback(nullptr, nullptr);
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				callback(nullptr, nullptr);
			}
		});
	}//```

	// Helpers to infer non-null on arguments of type lamdba, nullptr_t
	bool Register_Listener_For_New_Topics_If_Callbacks_Are_Valid(::std::nullptr_t, ::std::nullptr_t) {
		return false;
	}
	template <typename On_Registered_Listener_For_New_Topics_Callback = ::std::nullptr_t, typename On_New_Topic_Callback = ::std::nullptr_t>
	bool Register_Listener_For_New_Topics_If_Callbacks_Are_Valid(On_Registered_Listener_For_New_Topics_Callback && On_Registered_Listener_For_New_Topics = nullptr, On_New_Topic_Callback && On_New_Topic = nullptr) {
		topic_notification_listeners.emplace_front(::std::forward<On_New_Topic_Callback>(On_New_Topic));
		On_Registered_Listener_For_New_Topics(topic_notification_listeners.begin());
		return true;
	}
	// If second or third callbacks are called, it is guaranteed that 1st one will not be.
	template <typename On_Gotten_Topic_Callback, typename On_Registered_Listener_For_New_Topics_Callback = ::std::nullptr_t, typename On_New_Topic_Callback = ::std::nullptr_t>
	void 
	async_get_topic_for_subscriber(uint_fast8_t index_i, uint_fast64_t index_begin, ::std::string const & topic_name, On_Gotten_Topic_Callback && On_Gotten_Topic, Meta_Topic_Pointer_Type const & mt = Meta_Topic_Pointer_Type(), On_Registered_Listener_For_New_Topics_Callback && On_Registered_Listener_For_New_Topics = nullptr, On_New_Topic_Callback && On_New_Topic = nullptr) 
	{
		assert(topic_name.empty() == false);
		if (!mt)
			Validate_topic_name(topic_name);
		strand.post([this, index_i, index_begin, topic_name, On_Gotten_Topic(::std::forward<On_Gotten_Topic_Callback>(On_Gotten_Topic)), mt(mt), On_Registered_Listener_For_New_Topics(::std::forward<On_Registered_Listener_For_New_Topics_Callback>(On_Registered_Listener_For_New_Topics)), On_New_Topic(::std::forward<On_New_Topic_Callback>(On_New_Topic))]() mutable noexcept {
			try {
				assert(strand.running_in_this_thread());
				if (!async_error) {
					if (!mt) {
						// Todo -- may be async version of such...
						auto Try_Find_Meta_Topic(meta_topics.find(topic_name));
						if (Try_Find_Meta_Topic != meta_topics.end()) { // Exists...
							if (Try_Find_Meta_Topic->second->Marked_For_Deletion.load(::std::memory_order_relaxed)) {
								if (!Register_Listener_For_New_Topics_If_Callbacks_Are_Valid(::std::move(On_Registered_Listener_For_New_Topics), ::std::move(On_New_Topic))) {
									synapse::misc::log("Refusing to provide topic to subscriber (marked for deletion) : " + topic_name + '\n', true);
									On_Gotten_Topic(nullptr, nullptr);
								}
								return;
							} else
								mt = Try_Find_Meta_Topic->second;
						} else if (Register_Listener_For_New_Topics_If_Callbacks_Are_Valid(::std::move(On_Registered_Listener_For_New_Topics), ::std::move(On_New_Topic))) // ... missing and 'no-create if missing' ...
							return;
						else { // ... missing and creat if missing.
							// cannot use this: ::boost::allocate_shared<meta_topic>(::boost::fast_pool_allocator<meta_topic>(), *this) because fast_pool_allocator in currently-deployed version of Boost does not uspport move semantics. TODO.
							mt.reset(new meta_topic(*this));
							auto && new_mt(meta_topics.emplace(topic_name, mt));
							assert(new_mt.second == true);
							mt->Meta_Topics_Membership = new_mt.first;
						}
					}
					get_topic(::boost::weak_ptr<data_processors::synapse::asio::Any_Connection_Type>(), topic_name, On_Gotten_Topic, mt, index_i, index_begin);
				} else
					throw ::std::runtime_error("erroneous state in async_get_topic_for_subscriber");
			} catch (data_processors::Size_alarm_error const & e) {
				synapse::misc::log("Refusing to provide topic (Size_alarm_error) : " + topic_name + ' ' + e.what() + '\n', true);
				// Todo -- do check if can just call or need to post first (to prevent possible immediate recursion)!
				On_Gotten_Topic(nullptr, nullptr);
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				On_Gotten_Topic(nullptr, nullptr);
			}
		});
	}

	template <typename Meta_Topic_Pointer_Type, typename CallbackType>
	void
	async_enumerate_meta_topics(Meta_Topic_Pointer_Type && Meta_Topic, CallbackType && callback)
	{
		strand.post([this, Meta_Topic(::std::forward<Meta_Topic_Pointer_Type>(Meta_Topic)), callback]() mutable noexcept {
			try {
				if (!async_error) {
					auto Call_With_Next_Available_Iterator([this](typename meta_topics_type::iterator Meta_Topics_Iterator, auto & Callback){
						if (Meta_Topics_Iterator != meta_topics.end()) while(Meta_Topics_Iterator->second->Marked_For_Deletion.load(::std::memory_order_relaxed) && ++Meta_Topics_Iterator != meta_topics.end());
						Callback(Meta_Topics_Iterator != meta_topics.end() ? Meta_Topics_Iterator->second : nullptr, false);
					});
					if (!Meta_Topic)
						Call_With_Next_Available_Iterator(meta_topics.begin(), callback);
					else
						Call_With_Next_Available_Iterator(::std::next(Meta_Topic->Meta_Topics_Membership), callback);
				} else 
					throw ::std::runtime_error("topic_factory in error state during async_enumerate_meta_topics");
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				callback(nullptr, true);
			}
		});
	}

	template <typename Callback_type>
	void
	Async_set_topic_sparsification_parameters_from_subscriber(::std::string const & Topic_name, uint_fast64_t Target_data_consumption_limit, uint_fast64_t Timestamp, Callback_type && Callback) 
	{
		async_get_topic_for_subscriber(0, // 0 is now RAM-based index_i for timestamp (Note: we are no longer indexing sequence numbers).
			-1, Topic_name, [this, Target_data_consumption_limit, Timestamp, Callback(::std::forward<Callback_type>(Callback))](Meta_Topic_Pointer_Type const & Meta_topic, ::boost::shared_ptr<topic_type> const & Topic) mutable {
			assert(strand.running_in_this_thread());
			try {
				if (!async_error && Topic)
						Topic->Set_sparsification_parameters_from_subscriber(Target_data_consumption_limit, Timestamp, [this, Meta_topic, Callback](bool const Error) mutable {
							strand.post([this, Callback(::std::move(Callback)), Error](){
								assert(strand.running_in_this_thread());
								try {
									if (!async_error) {
										Callback(Error);
									} else 
										throw ::std::runtime_error("topic_factory topic->Set_sparsification_parameters_from_subscriber via Async_set_topic_sparsification_end_limit");
								} catch (::std::exception const & e) {
									put_into_error_state(e); 
									Callback(true);
								}
							});
						});
				else 
					throw ::std::runtime_error("topic_factory async_get_topic_for_subscriber via Async_set_topic_sparsification_end_limit");
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				Callback(true);
			}
		});
	}

	template <typename Callback_type>
	void Async_Set_Topic_Maximum_Timestamp_Gap(::std::string const & Topic_Name, uint_fast64_t Maximum_Timestamp_Gap, Callback_type && Callback) {
		strand.post([this, Topic_Name, Maximum_Timestamp_Gap, Callback(::std::forward<Callback_type>(Callback))]() noexcept {
			try {
				if (!async_error) {
					auto Try_Find_Meta_Topic(meta_topics.find(Topic_Name));
					if (Try_Find_Meta_Topic != meta_topics.end()) {
						Try_Find_Meta_Topic->second->Maximum_Indexable_Gaps[0].store(Maximum_Timestamp_Gap, ::std::memory_order_relaxed);
						synapse::misc::log("WARNING setting Topic_Maximum_Timestamp_Gap(=" + ::std::to_string(Maximum_Timestamp_Gap)+ ") for " + Topic_Name + '\n', true);
						Callback(false);
					} else
						Callback(true);
				} else 
					throw ::std::runtime_error("topic_factory in error state during Async_Set_Topic_Maximum_Timestamp_Gap");
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				Callback(true);
			}
		});
	}

	template <typename Iterator_Type>
	void Delete_Topic_By_Iterator(Iterator_Type && Iterator_To_Erase) {
		strand.post([this, Iterator_To_Erase(::std::forward<Iterator_Type>(Iterator_To_Erase))]() mutable {
			assert(strand.running_in_this_thread());
			if (!Topics_Deletion_Manager) 
				Topics_Deletion_Manager.reset(new Topics_Deletion_Manager_Type(*this, ::std::move(Iterator_To_Erase)));
			else
				Topics_Deletion_Manager->To_Erase.emplace_front(::std::move(Iterator_To_Erase));
		});
	}

	template <typename String_Type>
	void Delete_Topic(String_Type && Topic_Name) {
		strand.post([this, Topic_Name(::std::forward<String_Type>(Topic_Name))]() { 
			auto Iterator_To_Erase(meta_topics.find(Topic_Name)); // Todo -- consider async style traversal...
			if (Iterator_To_Erase != meta_topics.end()) {
				auto Meta_Topic(Iterator_To_Erase->second);
				assert(!Meta_Topic->Is_Unique());
				uint8_t Dummy(0);
				if (Meta_Topic->Marked_For_Deletion.compare_exchange_strong(Dummy, 1, ::std::memory_order_relaxed))
					++synapse::asio::quitter::get().quit_fence_size;
				// above should automatically invoke deleteion on ref count lowering
			}
		});
	}

private:

	struct Topics_Deletion_Manager_Type {
		topic_factory & Topic_Factory;
		::boost::asio::deadline_timer Retry_Deletion_Timer{synapse::asio::io_service::get()};
		void On_Retry_Deletion_Timeout(::boost::system::error_code const & Error) {
			assert(Topic_Factory.strand.running_in_this_thread());
			if (!Error) {
				auto && Before_To_Erase_Iterator(To_Erase.before_begin());
				for(auto To_Erase_Iterator(To_Erase.begin());  To_Erase_Iterator != To_Erase.end(); ++To_Erase_Iterator, ++Before_To_Erase_Iterator) {
					auto Meta_Topic_Iterator_To_Erase(*To_Erase_Iterator);
					auto const topic_path(Topic_Factory.topics_path / Meta_Topic_Iterator_To_Erase->first);
					assert(Meta_Topic_Iterator_To_Erase->second->Is_Unique()); // Topic factory strand should be the only one responsible for transitioning from unique() to non-unique(), i.e. refcount from 1 to 1+
					::boost::system::error_code Remove_Error;
					::boost::filesystem::remove_all(topic_path, Remove_Error);
					if (Remove_Error)
						synapse::misc::log("WARNING, could not delete topic: " + topic_path.string() + " due to filesystem issues (will try again soon).\n", true);
					else {
						Topic_Factory.meta_topics.erase(Meta_Topic_Iterator_To_Erase);
						To_Erase.erase_after(Before_To_Erase_Iterator);
						synapse::misc::log("Deleted topic: " + topic_path.string() + '\n', true);
						--synapse::asio::quitter::get().quit_fence_size;
						break;
					} 
				}
				if (To_Erase.empty()) 
					Topic_Factory.Topics_Deletion_Manager.reset(); // nuke self
				else {
					Retry_Deletion_Timer.expires_from_now(::boost::posix_time::seconds(1));
					Retry_Deletion_Timer.async_wait(Topic_Factory.strand.wrap(::boost::bind(&Topics_Deletion_Manager_Type::On_Retry_Deletion_Timeout, this, ::boost::asio::placeholders::error)));
				}
			}
		}

		::std::forward_list<typename meta_topics_type::iterator, ::boost::fast_pool_allocator<typename meta_topics_type::iterator>> To_Erase;
		Topics_Deletion_Manager_Type(topic_factory & Topic_Factory, typename meta_topics_type::iterator && Iterator_To_Erase) : Topic_Factory(Topic_Factory) {
			assert(Topic_Factory.strand.running_in_this_thread());
			To_Erase.emplace_front(::std::move(Iterator_To_Erase));
			Topic_Factory.strand.post([this](){
				On_Retry_Deletion_Timeout(::boost::system::error_code());
			});
		}
	};
	::std::unique_ptr<Topics_Deletion_Manager_Type> Topics_Deletion_Manager;

	meta_topics_type meta_topics;

	// returns false if not all requests could have been processed  (e.g. some need topic_lock being loaded)
	//~~~ common code to process current request and any accumulated ones...
	// TODO -- may be important for future -- make async (essentially has to do with balance of how many cores, how many threads and how much async. behaviour is implemented on any given system... although if 'callback' itself is an act of 'posting to target strand' then current sync-looping is ok because the actual body of the callaback method may well end up being run by another thread/core/etc.)
	bool static
	process_pending_requests(Meta_Topic_Pointer_Type const & mt, ::boost::shared_ptr<topic_type> const & t) 
	{
		while (mt->pending_requests.empty() == false) {
			auto & request(mt->pending_requests.front());
			if (process_pending_request(request.index_i, request.index_begin, request.is_publisher, 
				request.callback, // do not potentially move this because the whole process_pending_request method may return false and then retried later with invalid object
				mt, t) == true)
				mt->pending_requests.pop();
			else 
				return false;
		}
		return true;
	}//```

	template <typename CallbackType, typename Weak_Connection_Info>
	void Instantiate_Topic(Weak_Connection_Info && is_publisher, ::std::string const & topic_name, CallbackType && callback, Meta_Topic_Pointer_Type const & mt, uint_fast8_t index_i, uint_fast64_t index_begin, bool pending_request_processd_rv) {

		auto const topic_path(topics_path / topic_name);

		if (!::boost::filesystem::exists(topic_path))
			::boost::filesystem::create_directory(topic_path);

		synapse::misc::log("topic ctor: " + topic_name + '\n', true);
		auto && topic_lock(::boost::shared_ptr<topic_type>(new topic_type(topic_path.string(), mt)) // not using make_shared because weak_ptr used later on and with make_shared the topic_type would cause control block to stay in scope, yet we dont want the topic_type object itself consuming memory...
		);
		mt->topic = topic_lock;
		topic_lock->async_open(strand.wrap([this, index_i, index_begin, is_publisher(::std::forward<Weak_Connection_Info>(is_publisher)), callback(::std::forward<CallbackType>(callback)), mt, topic_lock, pending_request_processd_rv](bool error) mutable noexcept {
			try {
				assert(strand.running_in_this_thread());
				if (!error && !async_error) {
					topic_lock->loaded = true;
					#ifndef NDEBUG
						bool rv; 
					#endif
					if (pending_request_processd_rv == false) {
						#ifndef NDEBUG
							rv = 
						#endif
						process_pending_request(index_i, index_begin, ::std::move(is_publisher), callback, mt, topic_lock); 
						assert(rv == true);
					}
					#ifndef NDEBUG
						rv = 
					#endif
					process_pending_requests(mt, topic_lock);
					assert(rv == true);
				} else 
					throw ::std::runtime_error("erroneous state in async_open for new topic");
			} catch (::std::exception const & e) {
				put_into_error_state(e); 
				callback(nullptr, nullptr);
			}
		}));
	}

	template <typename CallbackType, typename Weak_Connection_Info>
	void 
	get_topic(Weak_Connection_Info && is_publisher, ::std::string const & topic_name, CallbackType && callback, Meta_Topic_Pointer_Type const & mt, uint_fast8_t index_i = -1, uint_fast64_t index_begin = -1) 
	{
		assert(strand.running_in_this_thread());
		auto && topic_lock(mt->topic.lock());
		if (topic_lock && topic_lock->loaded == true) {
			#ifndef NDEBUG
				bool rv = 
			#endif
			process_pending_request(index_i, index_begin, ::std::forward<Weak_Connection_Info>(is_publisher), ::std::forward<CallbackType>(callback), mt, topic_lock);
			assert(rv == true);
		} else {

			if (!topic_lock) {
				bool const pending_request_processd_rv(process_pending_request(index_i, index_begin, is_publisher, callback, mt, topic_lock));
				if (pending_request_processd_rv == false || process_pending_requests(mt, topic_lock) == false) {

					if (!mt->On_Previous_Instance_Dtor) {
						if (!mt->Previous_Instance_Exists)
							Instantiate_Topic(::std::forward<Weak_Connection_Info>(is_publisher), topic_name, ::std::forward<CallbackType>(callback), mt, index_i, index_begin, pending_request_processd_rv);
						else
							mt->On_Previous_Instance_Dtor = [this, index_i, index_begin, is_publisher(::std::forward<Weak_Connection_Info>(is_publisher)), callback(::std::forward<CallbackType>(callback)), pending_request_processd_rv, topic_name](Meta_Topic_Pointer_Type const & mt) mutable {
								assert(strand.running_in_this_thread());
								assert(mt);
								try {
									Instantiate_Topic(::std::move(is_publisher), topic_name, callback, mt, index_i, index_begin, pending_request_processd_rv);
								} catch (data_processors::Size_alarm_error const & e) {
									synapse::misc::log("Refusing to provide topic (Size_alarm_error) : " + topic_name + ' ' + e.what() + '\n', true);
									// Todo -- do check if can just call or need to post first (to prevent possible immediate recursion)!
									callback(nullptr, nullptr);
									while (mt->pending_requests.empty() == false) {
										auto & request(mt->pending_requests.front());
										request.callback(nullptr, nullptr);
										mt->pending_requests.pop();
									}
								} catch (::std::exception const & e) {
									put_into_error_state(e); 
									callback(nullptr, nullptr);
								}
							};
					} else
						mt->pending_requests.emplace(index_i, index_begin, ::std::forward<Weak_Connection_Info>(is_publisher), ::std::forward<CallbackType>(callback));

				}
			} else {
				mt->pending_requests.emplace(index_i, index_begin, ::std::forward<Weak_Connection_Info>(is_publisher), ::std::forward<CallbackType>(callback));
			}
		}
	}
	// TODO better encapsulation
	bool async_error = false;
	void
	put_into_error_state(::std::exception const & e) 
	{
		assert(strand.running_in_this_thread());
		synapse::misc::log(::std::string("topic_factory exception(=") + e.what() + ')', true);
		for (auto & i : meta_topics) {
			auto & mt(*i.second);
			while (mt.pending_requests.empty() == false) {
				auto & request(mt.pending_requests.front());
				request.callback(nullptr, nullptr);
				mt.pending_requests.pop();
			}
		}
		async_error = true;

		// Todo: decide on whether to pursue fine-grained topic-exception handling, or to bail the whole server if/when topic-related errors take place (e.g. database corruption, system-IO issues, etc.)
		abort();
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


