#include <tuple>
#include <queue>

#include "../misc/alloc.h"
#include "../misc/misc.h"

#include "async_file.h"

#ifndef DATA_PROCESSORS_SYNAPSE_DATABASE_INDEX_H
#define DATA_PROCESSORS_SYNAPSE_DATABASE_INDEX_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

namespace basio = ::boost::asio;

template <bool Is_mutable>
struct Basic_index_meta_ptr {
	template <bool, bool> struct Memory_type_traits;
	template<bool Ignored> struct Memory_type_traits<true, Ignored> { typedef void type; };
	template<bool Ignored> struct Memory_type_traits<false, Ignored> { typedef void const type; };

	unsigned constexpr static version_number{1};

	typename Memory_type_traits<Is_mutable, true>::type * Memory;

	typedef Basic_index_meta_ptr<false> Constant_type;

#if DP_ENDIANNESS != LITTLE
#error current code only supports LITTLE_ENDIAN archs. -- extending to BIG_ENDIAN is trivial but there wasnt enough time at this stage...
#endif

	unsigned constexpr static size{48};
	static_assert(!(size % 8), "meta size ought to be multiple of 64bits"); 

	void
	initialise(uint_fast64_t const quantise_shift_mask, uint_fast64_t const key_entry_size) const
	{
		assert(quantise_shift_mask < sizeof(uint64_t) * 8);
		if (key_entry_size > 0xffffff || quantise_shift_mask > 0xff)
			throw ::std::runtime_error("Basic_index_meta_ptr supplied quantise_shift_mask or key_entry_size is too large for current version of data format");
		new (Memory) uint64_t (version_number | quantise_shift_mask << 32 | key_entry_size << 40);
		set_data_size(0);
		Set_start_of_file_key(-1);
		Set_begin_key(-1);
		Set_target_data_consumption_limit(-1);
		rehash();
	}
	uint_fast64_t
	get_data_size() const
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1);
	}
	void
	set_data_size(uint_fast64_t const & size) const
	{
		new (reinterpret_cast<uint64_t*>(Memory) + 1) uint64_t(size);
	}

	uint8_t 
	get_quantise_shift_mask() const
	{
		return reinterpret_cast<char unsigned const *>(Memory)[4];
	}

	uint_fast32_t 
	get_key_entry_size() const
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(Memory) >> 40;
	}

	void
	Set_start_of_file_key(uint_fast64_t const & key) const
	{
		new (reinterpret_cast<uint64_t*>(Memory) + 2) uint64_t(key);
	}

	uint_fast64_t
	Get_start_of_file_key() const
	{
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2);
	}

	// allows sparsifying/truncating-from-beginning of the index file itself (todo for the future)
	void
	Set_begin_key(uint_fast64_t const & key) const  
	{
		assert(key >= Get_start_of_file_key());
		new (reinterpret_cast<uint64_t*>(Memory) + 3) uint64_t(key);
	}
	uint_fast64_t
	Get_begin_key() const
	{
		assert(Get_start_of_file_key() <= synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3);
	}

	void Set_target_data_consumption_limit(uint_fast64_t const & Target_limit) const {
		new (reinterpret_cast<uint64_t*>(Memory) + 4) uint64_t(Target_limit);
	}

	uint_fast64_t Get_target_data_consumption_limit() const {
		return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4);
	}

	void
	verify_hash() const
	{
		uint64_t hash(synapse::misc::crc32c(synapse::misc::Get_alias_safe_value<uint64_t>(Memory), synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1)));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4));
		if ((synapse::misc::Get_alias_safe_value<uint64_t>(Memory) & 0xffffffff) != version_number || hash != synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 5))
			throw ::std::runtime_error("Basic_index_meta_ptr hash verification in index file has failed");
	}

	void
	rehash() const
	{
		uint64_t hash(synapse::misc::crc32c(synapse::misc::Get_alias_safe_value<uint64_t>(Memory), synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1)));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2));
		hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
		new (reinterpret_cast<uint64_t*>(Memory) + 5) uint64_t(synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4)));
	}

	Basic_index_meta_ptr(typename Memory_type_traits<Is_mutable, true>::type * Memory)
	: Memory(Memory) 
	{
	}

	template <bool X>
	bool operator == (Basic_index_meta_ptr<X> const & Compare_with) const
	{
		return Memory == Compare_with.Memory;
	}
};

typedef Basic_index_meta_ptr<true> Index_meta_ptr;
typedef Basic_index_meta_ptr<false> Index_meta_const_ptr;

// key-ordered requests and existing ranges
// TODO check that have put strand on all of the indicies and ranges classes (more or less) (because such will need to be written when publisher is on)
template <unsigned PageSize, typename Topic, typename Range, unsigned ValuesSize>

struct index 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
: synapse::misc::Test_new_delete<index<PageSize, Topic, Range, ValuesSize>>
#endif
{
	uint_fast32_t constexpr static key_entry_size = sizeof(uint64_t) * (ValuesSize + 1); // +1 is for crc32 and the space for it's runtime 'calculate-once' for multiple readers scenario...
	static_assert(key_entry_size < PageSize, "Reasonable size of index exceeded (larger than page size).");

	/**
		\note
		for as long as this index object is in scope then so should be it's controlling topic (this is by design). the idea is to pass around shared_ptr of the controlling object to all async calls.
	 */
	Topic & t; 
	unsigned const indicies_i;

	bool async_error = false;
	void
	put_into_error_state(::std::exception const & e) 
	{
		if (!async_error) {
			assert(t.strand.running_in_this_thread());
			synapse::misc::log(::std::string("exception(=") + e.what() + ")\n");
			t.async_error = async_error = true;
			max_key_writing_in_progress = false;
			// send all pending max_update requests with error callback
			while (max_key_update_requests.empty() == false) {
				auto & request(max_key_update_requests.front());
				if (request.second)
					request.second(true);
				Pop_max_key_update_requests();
			}
			if (flush_request) {
				flush_request(); // above t.async_error already gets set...
				flush_request = nullptr;
			}
			while (pending_requests.empty() == false) {
				pending_requests.front().second(nullptr);
				pending_requests.pop();
			}
		}
	}

	// need this (albeit simplified w.r.t. r50 in repo) because async_get_key can be in 'reading mode' for current request when another one is arriving and issuing 'multiplexed' call async_read whirst the previous one isn't done yet...
	typedef ::std::pair<uint_fast64_t, ::std::function<void(void const *)> > pending_request_type;
	typedef ::std::queue<pending_request_type, ::std::deque<pending_request_type, data_processors::misc::alloc::basic_alloc_1<pending_request_type>>> pending_requests_type;
	pending_requests_type pending_requests{pending_requests_type::container_type::allocator_type{t.sp}};

	uint_fast64_t const Max_indexable_gap;
	uint_fast8_t quantise_shift_mask;
	uint_fast64_t key_mask; 

	static_assert(key_entry_size <= PageSize, "key_entry_size < PageSize");
	synapse::database::async_duplex_buffered_file<PageSize, Index_meta_ptr> file; 

	::boost::asio::deadline_timer Write_Out_Throttling_Timer;

	// max-key management (when writing/extending the index)
	typedef ::std::pair< ::std::array<uint_fast64_t, ValuesSize + 1>, // +1 is for actual key value (not saved to disk but used in async. and icremental updates in max_key_update code)
		::std::function<void(bool)> > max_key_update_type;
	static_assert(::std::tuple_size<typename max_key_update_type::first_type>::value == ValuesSize + 1, "max_key update requsets should have actial key together with the indexed values");
	::std::queue<max_key_update_type, ::std::deque<max_key_update_type, data_processors::misc::alloc::basic_alloc_1<max_key_update_type>>> max_key_update_requests{typename decltype(max_key_update_requests)::container_type::allocator_type{t.sp}};

	uint_fast32_t constexpr static Max_key_update_requests_throttling_watermark{
#ifdef TEST_SYNAPSE_INDEX_MAX_KEY_UPDATE_REQUESTS_THROTTLING_WATERMARK_1
		TEST_SYNAPSE_INDEX_MAX_KEY_UPDATE_REQUESTS_THROTTLING_WATERMARK_1
#else
		1024
#endif
	};
	::std::atomic_uint_fast32_t Max_key_update_requests_size{0};
	static_assert(Max_key_update_requests_throttling_watermark > 0, "Max_key_update_requests_throttling_watermark should be > 0");
	::std::function<void(bool)> Max_key_update_requests_throttling_callback;
	template <typename Callback_type>
	void Set_max_key_update_requests_throttling_callback(Callback_type && Callback) {
		assert(t.strand.running_in_this_thread());
		Max_key_update_requests_throttling_callback = ::std::forward<Callback_type>(Callback);
		if (Max_key_update_requests_throttling_callback && Max_key_update_requests_size.load(::std::memory_order_relaxed) >= Max_key_update_requests_throttling_watermark)
			Max_key_update_requests_throttling_callback(true);
	}
	void Pop_max_key_update_requests() {
		assert(t.strand.running_in_this_thread());
		max_key_update_requests.pop();
		auto const Previous_max_key_update_requests_size(Max_key_update_requests_size.fetch_sub(1, ::std::memory_order_relaxed));
		assert(Previous_max_key_update_requests_size);
		if (Max_key_update_requests_throttling_callback && Previous_max_key_update_requests_size == Max_key_update_requests_throttling_watermark)
			Max_key_update_requests_throttling_callback(false);
	}

	uint_fast64_t max_key = 0;
	uint_fast64_t last_key_by_publisher = -1; // injected use by writer (writers may change, index object stays the same/in-scope)

	bool max_key_writing_in_progress = false;
	::std::function<void()> flush_request;

	/**
		Note: ought to be called from publisher strand! Just before posting to actually max_key_update()
	*/
	void Pre_max_key_update() noexcept {
		auto const Previous_max_key_update_requests_size(Max_key_update_requests_size.fetch_add(1, ::std::memory_order_relaxed));
		if (Max_key_update_requests_throttling_callback && (Previous_max_key_update_requests_size + 1 == Max_key_update_requests_throttling_watermark))
			Max_key_update_requests_throttling_callback(true);
	}

	/**
		\pre must have been dispatched with t.strand
		*/
	void
	max_key_update(max_key_update_type && update_request) noexcept
	{
		assert(t.strand.running_in_this_thread());
		assert(update_request.first[0] == (update_request.first[0] & key_mask));

#ifndef NDEBUG
		// index values ought to rise undconditionally...
		if (max_key_update_requests.empty() == false) {
			auto & request(max_key_update_requests.back());
			assert(request.first[1] < update_request.first[1]);
		}
#endif

		max_key_update_requests.emplace(::std::forward<max_key_update_type>(update_request));
		if (max_key_writing_in_progress == false) {
			max_key_writing_in_progress = true;
			on_process_max_key_update();
		}
	}

	template <typename Callback>
	void
	flush(Callback && callback) 
	{
		// assert(!flush_request); // now not asserting, allowing overwrite with latest request
		assert(t.strand.running_in_this_thread());
		flush_request = ::std::forward<Callback>(callback);
		if (max_key_writing_in_progress == false) {
			max_key_writing_in_progress = true;
			on_process_max_key_update();
		}
	}

	bool Sparsification_in_progress{false};
	// Note: cannot just use current begin key during actual sparsification. Due to simplified accumulation of when sparsification takes place together with the fact that 'Set_begin_key' is called from multiple locations in code (either due to actual data file growing large so that it needs to chomp off some indicies, or due to the index-itself (e.g. fine-resolutions) becoming very large to the point of having to be chomped off) -- will need to keep 'previous/last' sparse explicitly.
	uint_fast64_t Existing_sparsification_end;
	void
	Async_sparsify_beginning(uint_fast64_t Target_sparsification_end)
	{
		assert(Sparsification_in_progress == true);
		auto t_(t.shared_from_this());
		assert(!(Existing_sparsification_end % key_entry_size));
		assert(Existing_sparsification_end < Target_sparsification_end);
		file.Async_sparsify_beginning(Existing_sparsification_end, Target_sparsification_end, [this, t_, Target_sparsification_end](bool error) mutable {
			assert(t.strand.running_in_this_thread());
			try {
				if (!error && !async_error) {
					t.Meta_Topic_Shim.mt->Approximate_Topic_Size_On_Disk.fetch_sub(Target_sparsification_end - Existing_sparsification_end, ::std::memory_order_relaxed);
					assert(Sparsification_in_progress == true);
					synapse::misc::log(" DOEN SPARSIFYING index!!! \n", true);
					Existing_sparsification_end = Target_sparsification_end;
					auto const & Meta(file.get_meta());
					assert(Cached_meta_begin_key >= Meta.Get_start_of_file_key());
					Target_sparsification_end = Get_key_distance_size_on_disk(Cached_meta_begin_key - Meta.Get_start_of_file_key());
					assert(Target_sparsification_end >= Existing_sparsification_end);
					if (Target_sparsification_end > Existing_sparsification_end && Target_sparsification_end < Meta.get_data_size())
						Async_sparsify_beginning(Target_sparsification_end);
					else
						Sparsification_in_progress = false;
				} else
					throw ::std::runtime_error("error in index on after_file_buffer_flush from file.Async_sparsify_beginning ");
			} catch (::std::exception const & e) { put_into_error_state(e); }
		});
	}

	uint_fast64_t Get_key_distance_size_on_disk(uint_fast64_t const & Distance) const {
		assert(!(Distance % (static_cast<uint_fast64_t>(1u) << quantise_shift_mask)));
		return (Distance >> quantise_shift_mask) * key_entry_size;
	}

	void Set_corresponding_indicies_begin_in_meta_topic(uint_fast64_t const & Quantized_key) {
		assert(t.strand.running_in_this_thread());
		assert(!(Quantized_key & ~key_mask));
		if (indicies_i != static_cast<decltype(indicies_i)>(-1)) { // todo -- more elegance
			assert(indicies_i < Topic::indicies_size);
			t.Meta_Topic_Shim.mt->indicies_begin[indicies_i].store(Quantized_key, ::std::memory_order_relaxed); 
		}
	}

	uint_fast64_t Cached_meta_begin_key;
	uint_fast64_t Cached_meta_target_data_consumption_limit;
	bool Update_metadata_on_file{false};
	void Apply_cached_meta_sparsification_variables() {
		assert(Update_metadata_on_file);
		file.get_meta().Set_begin_key(Cached_meta_begin_key);
		file.get_meta().Set_target_data_consumption_limit(Cached_meta_target_data_consumption_limit);
		Update_metadata_on_file = false;
	}

	void
	on_process_max_key_update()
	{
		assert(t.strand.running_in_this_thread());
		assert(max_key_writing_in_progress == true);

		auto Conditionally_sparsify([this](){
			auto && Meta(file.get_meta());
			auto const File_end_on_disk(Meta.get_data_size());
			assert(!(File_end_on_disk % key_entry_size));

			assert(Cached_meta_begin_key >= Meta.Get_start_of_file_key());
			auto const Begin_key_end(Get_key_distance_size_on_disk(Cached_meta_begin_key - Meta.Get_start_of_file_key()));
			assert(Begin_key_end >= Existing_sparsification_end);

			auto Target_sparsification_end(Existing_sparsification_end);
			if (Begin_key_end < File_end_on_disk) {
				if (Begin_key_end > key_entry_size << 1 && Begin_key_end > Existing_sparsification_end)
					Target_sparsification_end = Begin_key_end;
				assert(Cached_meta_target_data_consumption_limit > 2 * key_entry_size);
				auto const File_size_on_disk(File_end_on_disk - Begin_key_end);
				if (File_size_on_disk > Cached_meta_target_data_consumption_limit) {
					Target_sparsification_end = File_end_on_disk - Cached_meta_target_data_consumption_limit + key_entry_size;
					Target_sparsification_end -= Target_sparsification_end % key_entry_size;
					assert(!(Target_sparsification_end % key_entry_size));
					assert(Target_sparsification_end >= Begin_key_end);
					auto const New_begin_key(Meta.Get_start_of_file_key() + (Target_sparsification_end / key_entry_size << quantise_shift_mask));
					assert(Cached_meta_begin_key <= New_begin_key);
					assert(File_end_on_disk - Target_sparsification_end >= key_entry_size);
					Set_corresponding_indicies_begin_in_meta_topic(Cached_meta_begin_key = New_begin_key);
					Update_metadata_on_file = true;
				}
				assert(Target_sparsification_end >= Existing_sparsification_end);
			}

			if (Target_sparsification_end > Existing_sparsification_end) {
				if (Sparsification_in_progress == false) {
					Sparsification_in_progress = true;
					Async_sparsify_beginning(Target_sparsification_end);
					auto t_(t.shared_from_this());
					t.strand.post([this, t_](){
						flush([](){}); 
					});
				}
			} 
		});

		auto after_file_buffer_flush = [this, Conditionally_sparsify](uint_fast64_t const Written_Bytes){
			assert(Written_Bytes);
			t.Meta_Topic_Shim.mt->Approximate_Topic_Size_On_Disk.fetch_add(Written_Bytes, ::std::memory_order_relaxed);
			assert(t.strand.running_in_this_thread());
			// if not aligned on PageSize, will need to migrate (memcpy) part of the page...
			// TODO may be later on further optimize other write-out code by writing out at page-size aligned chunks most of the time...
			uint_fast64_t const end_valid_data_in_buffer(file.writing_buffer_alignment_offset() + file.get_meta().get_data_size() - file.writing_buffer_offset());
			if (end_valid_data_in_buffer > PageSize && synapse::misc::is_multiple_of_po2<uint_fast64_t>(end_valid_data_in_buffer, PageSize) == false) {
				assert(file.writing_buffer_alignment_offset() < PageSize);
				assert(file.get_meta().get_data_size() > file.writing_buffer_offset());
				uint_fast64_t const end_valid_data_in_buffer_page_boundary(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(end_valid_data_in_buffer, PageSize));
				assert(end_valid_data_in_buffer_page_boundary >= PageSize);
				uint_fast64_t const bytes_to_migrate(end_valid_data_in_buffer - end_valid_data_in_buffer_page_boundary);
				assert(bytes_to_migrate < PageSize);
				::memcpy(file.writing_buffer(), file.writing_buffer() + end_valid_data_in_buffer_page_boundary, bytes_to_migrate);
			}
			auto && Meta(file.get_meta());
			auto const File_end_on_disk(Meta.get_data_size());
			file.set_writing_buffer(File_end_on_disk, 0);
			assert(Has_entries());
			assert(!(File_end_on_disk % key_entry_size));

			Conditionally_sparsify();
		};

		if (max_key_update_requests.empty() == false) {
			auto & request(max_key_update_requests.front());
			
			bool can_pop(true);
			if (file.get_meta().Get_start_of_file_key() != static_cast<decltype(file.get_meta().Get_start_of_file_key())>(-1)) { // most likely event, file already existed and begin_key was loaded ok
				assert(Has_entries());
				assert(request.first[0] > max_key);

				// at this stage must see how many entries will need to be made...
				uint_fast64_t const diff(request.first[0] - max_key);
				assert(!(diff % (static_cast<uint_fast64_t>(1u) << quantise_shift_mask)));

				uint_fast32_t const number_of_entries(diff >> quantise_shift_mask);
				assert(number_of_entries);

				uint_fast32_t const Diff_Size(number_of_entries * key_entry_size);
				uint_fast32_t write_size(Diff_Size);

				uint_fast32_t free_buffer_space(file.writing_buffer_capacity() - file.writing_buffer_size());

				uint_fast32_t const possible_entries(::std::min(free_buffer_space / key_entry_size, number_of_entries));

				// won't fit into current file's buffer
				if (write_size > free_buffer_space) {
					can_pop = false;
					// adjust to how many will fit (due to opportunistic file-writeout above, there will definitely be a fit for at least one entry)
					assert(possible_entries);
					write_size = possible_entries * key_entry_size;
					assert(write_size <= free_buffer_space);
				}

				// serialse to the wire (file's buffer)...
				uint64_t * ptr(reinterpret_cast<uint64_t*>(file.writing_buffer_begin() + file.writing_buffer_size())); 
				assert(file.writing_buffer_capacity() - file.writing_buffer_size() >= write_size);
				file.set_writing_buffer_size(file.writing_buffer_size() + write_size);
				uint_fast64_t tmp_key(max_key);

				// TODO -- if will use large buffers for indicies (unlikely though -- may not be a good idea), then will also need to make this loop async. in nature (to prevent possibly long(ish) CPU-reservation to do this loop)
				for (uint_fast32_t i(0); i != possible_entries; ++i) {
					tmp_key += (static_cast<uint_fast64_t>(1u) << quantise_shift_mask);
					assert(!(tmp_key % (static_cast<uint_fast64_t>(1u) << quantise_shift_mask)));
					assert(!(tmp_key & ~key_mask));
					uint64_t hash(0);
					for (unsigned j(0); j != ValuesSize; ++j)
						hash = synapse::misc::crc32c(hash, * new (ptr++) uint64_t(request.first[j + 1]));
					new (ptr++) uint64_t(hash);
				}

				assert(can_pop == false || tmp_key == request.first[0]);

				// file buffer is getting full, or flush-buffer has been requested
				if (file.writing_buffer_size() > file.writing_buffer_capacity() - key_entry_size) {
					auto t_(t.shared_from_this());
					if (Update_metadata_on_file) 
						Apply_cached_meta_sparsification_variables();
					auto const Bytes_Size_To_Write(file.writing_buffer_size());
					file.async_write(file.get_meta().get_data_size(), Bytes_Size_To_Write, [this, t_, &request, Diff_Size, can_pop, tmp_key, after_file_buffer_flush, Bytes_Size_To_Write](bool error) noexcept {
						assert(t.strand.running_in_this_thread());
						assert(max_key_writing_in_progress == true);
						try {
							if (!error && !async_error) {
								max_key = tmp_key;
								after_file_buffer_flush(Bytes_Size_To_Write);
								if (can_pop == true) {
									assert(tmp_key == request.first[0]);
									request.second(false);
									Pop_max_key_update_requests();
								}
								if (max_key_update_requests.empty() == false) {
									// TODO, later on also factor-in the overall write-out activity (if such is low then perhaps can avoid async-wait even if data-to-be-written by gap is huuuge.
									if (Diff_Size < 
										#ifdef TEST_SYNAPSE_INDEX_THROTTLE_ON_PUBLISHED_GAP_SIZE_1
											TEST_SYNAPSE_INDEX_THROTTLE_ON_PUBLISHED_GAP_SIZE_1
										#else
											30 * 1024 * 1024 
										#endif
										|| t.Is_Quitting_In_Progress()
									) // We dont delay writing out if consecutive messages are not too far apart or if we are quitting... (Note: Is_Quitting test may be replaced by accessing atomic flag (in which case it will have to non-flag type but rather an atomic bool) in various quitter initiatiors in the io_service.h (on x64_64 arch this may be free as it is readonly process). Current implementation though leverages already existing quitting handlers in the topic.
										on_process_max_key_update();
									else { // ... otherwise we throttle (e.g. users trying to publish with huge gaps apart for consecutive messages)
										synapse::misc::log(::std::string("WARNING Index, in-ram-hash-id(=" + ::std::to_string(indicies_i) + "), throttling write-out activated due to large amount of index data(=") + ::std::to_string(Diff_Size) + ") for topic(=" + t.path +")\n");
										Write_Out_Throttling_Timer.expires_from_now(::boost::posix_time::milliseconds(
											#ifdef TEST_SYNAPSE_INDEX_THROTTLE_ON_PUBLISHED_GAP_THROTTLING_WAIT_1
												TEST_SYNAPSE_INDEX_THROTTLE_ON_PUBLISHED_GAP_THROTTLING_WAIT_1
											#else
												250
											#endif
										));
										Write_Out_Throttling_Timer.async_wait(t.strand.wrap([this, t_](::boost::system::error_code const & Error) noexcept {
											assert(t.strand.running_in_this_thread());
											assert(max_key_writing_in_progress == true);
											try {
												if (!Error && !async_error)
													on_process_max_key_update();
												else
													throw ::std::runtime_error("async_awaiting on throttling timer");
											} catch (::std::exception const & e) { put_into_error_state(e); }
										}));
									}
								} else {
									if (flush_request) {
										if (!Update_metadata_on_file) {
											flush_request();
											flush_request = nullptr;
										} else {
											auto t_(t.shared_from_this());
											t.strand.post([this, t_](){on_process_max_key_update();});
											return;
										}
									}
									max_key_writing_in_progress = false;
								}
							} else 
								throw ::std::runtime_error("writing index file");
						} catch (::std::exception const & e) { put_into_error_state(e); }
					});
					return;
				} else
					max_key = tmp_key;

				assert(request.first[0] >= max_key);

			} else { // 1st time initialisation in case index was created without existing file at that stage (i.e. starting from scratch)
				assert(!file.writing_buffer_size());
				file.get_meta().Set_start_of_file_key(max_key = request.first[0]);
				Cached_meta_begin_key = max_key;
				Existing_sparsification_end = 0;
				assert(!(file.get_meta().Get_start_of_file_key() & ~key_mask));
				// simple case -- only 1 entry is needed, as it is the very start of the index file
				uint64_t * ptr(reinterpret_cast<uint64_t*>(file.writing_buffer_begin()));
				uint64_t hash(0);
				for (unsigned j(0); j != ValuesSize; ++j)
					hash = synapse::misc::crc32c(hash, * new (ptr++) uint64_t(request.first[j + 1]));
				new (ptr) uint64_t(hash);
				assert(!file.writing_buffer_size());
				assert(file.writing_buffer_capacity() >= key_entry_size);
				file.set_writing_buffer_size(key_entry_size);
				assert(Has_entries());
				Set_corresponding_indicies_begin_in_meta_topic(max_key);
				Update_metadata_on_file = true;
			}
			if (can_pop == true) {
				assert(max_key == request.first[0]);
				request.second(false);
				Pop_max_key_update_requests();
			}
			if (max_key_update_requests.empty() == false || flush_request) {
				auto t_(t.shared_from_this());
				t.strand.post([this, t_](){on_process_max_key_update();});
				return;
			}
		} else if (flush_request) { // check for the flush file buffer request callback...
			auto t_(t.shared_from_this()); // used by both if/else below (flush may be the only scope retained in the 'else')
			auto Tail_handler([this](){
				if (max_key_update_requests.empty() == false) {
					on_process_max_key_update();
				} else {
					assert(max_key_writing_in_progress == true);
					if (!Update_metadata_on_file) {
						flush_request();
						flush_request = nullptr;
						max_key_writing_in_progress = false;
					} else {
						auto t_(t.shared_from_this());
						t.strand.post([this, t_](){on_process_max_key_update();});
					}
				}
			});
			if (file.writing_buffer_size()) {
				if (Update_metadata_on_file) 
					Apply_cached_meta_sparsification_variables();
				auto const Bytes_Size_To_Write(file.writing_buffer_size());
				file.async_write(file.get_meta().get_data_size(), Bytes_Size_To_Write, [this, t_, after_file_buffer_flush, Bytes_Size_To_Write, Tail_handler](bool error) noexcept {
					assert(t.strand.running_in_this_thread());
					try {
						if (!error && !async_error) {
							after_file_buffer_flush(Bytes_Size_To_Write);
							Tail_handler();
						} else 
							throw ::std::runtime_error("writing(flushing) index file after file.async_write");
					} catch (::std::exception const & e) { put_into_error_state(e); }
				});
				return;
			} else if (Update_metadata_on_file) { // no pending max_key updateds and nothing is buffered (all written to disk)
				Apply_cached_meta_sparsification_variables();
				file.async_write_meta([this, t_, Conditionally_sparsify, Tail_handler](bool error) noexcept {
					assert(t.strand.running_in_this_thread());
					try {
						if (!error && !async_error) {
							Conditionally_sparsify();
							Tail_handler();
						} else 
							throw ::std::runtime_error("writing(flushing) index file after file.async_write_meta");
					} catch (::std::exception const & e) { put_into_error_state(e); }
				});
				return;
			} else {
				flush_request();
				flush_request = nullptr;
			}
		}
		assert(max_key_writing_in_progress == true);
		max_key_writing_in_progress = false;
	}
	// end of max key management

	/**
		\note quantisation = 2^quantise_shift_mask 
		*/
	index(Topic & t, unsigned const indicies_i, ::std::string const & meta_filepath, ::std::string const & filepath, unsigned const read_buffer_size, unsigned const write_buffer_size, uint_fast8_t const quantise_shift_mask, uint_fast64_t const Max_indexable_gap = 0)
	: t(t), indicies_i(indicies_i), Max_indexable_gap(Max_indexable_gap), quantise_shift_mask(quantise_shift_mask), key_mask(synapse::misc::po2_mask<uint_fast64_t>(1u) << quantise_shift_mask), file(t.strand, meta_filepath, filepath, read_buffer_size, write_buffer_size), Write_Out_Throttling_Timer(t.strand.get_io_service())
	{
		if (quantise_shift_mask >= sizeof(uint64_t) * 8)
			throw ::std::runtime_error("Server configuration/compilation error. Quantise shift mask exceeds 64bit int width (undefined behaviour).");
		if (((Max_indexable_gap >> quantise_shift_mask) + 1) >= ::std::numeric_limits<uint32_t>::max() / key_entry_size)
			throw ::std::runtime_error("Server configuration/compilation error. Maximum indexable gap is too much for the numeric overflow.");
	}
	template <typename CallbackType>
	void async_open(CallbackType && on_ready_callback) {
		auto t_(t.shared_from_this());
		file.async_open([this, t_, on_ready_callback(::std::forward<CallbackType>(on_ready_callback))] (bool error) mutable noexcept {
			try {
				if (!error && !async_error) {
					if (file.get_meta().get_quantise_shift_mask() != this->quantise_shift_mask) {
						if (file.get_meta().get_quantise_shift_mask() > 0xff)
							throw ::std::runtime_error("Index_meta_ptr read from disk quantise_shift_mask is too large for current version of data format");
						// another check for current uint64_t limit...
						else if (file.get_meta().get_quantise_shift_mask() >= sizeof(uint64_t) * 8)
							throw ::std::runtime_error("Server configuration/compilation error. Quantise shift mask exceeds 64bit int width (undefined behaviour).");
						key_mask = synapse::misc::po2_mask<uint_fast64_t>(static_cast<uint_fast64_t>(1u) << (this->quantise_shift_mask = file.get_meta().get_quantise_shift_mask()));
						if (((this->Max_indexable_gap >> this->quantise_shift_mask) + 1) >= ::std::numeric_limits<uint32_t>::max() / key_entry_size)
						throw ::std::runtime_error("Server configuration/compilation error. Maximum indexable gap is too much for the numeric overflow.");
					}
					if (key_entry_size != file.get_meta().get_key_entry_size())
						throw ::std::runtime_error("underlying index file is not compatible (key_entry_size) with codebase. codebase(=" + ::std::to_string(key_entry_size) + ", file(=" + ::std::to_string(file.get_meta().get_key_entry_size()) + ')');

					Cached_meta_begin_key = file.get_meta().Get_begin_key();
					Cached_meta_target_data_consumption_limit = file.get_meta().Get_target_data_consumption_limit();

					file.set_writing_buffer_offset(file.get_meta().get_data_size());
					// if file is not empty then need to read the 1st index key value
					if (file.get_meta().get_data_size()) {
						file.async_read(0, ::std::min(static_cast<decltype(file.get_meta().get_data_size())>(PageSize), file.get_meta().get_data_size()), [this, t_, on_ready_callback](bool error) mutable noexcept {
							try {
								if (!error && !async_error) {
									if (file.get_meta().Get_start_of_file_key() & ~key_mask || Cached_meta_begin_key & ~key_mask) // minor safety check (not much really)
										throw ::std::runtime_error("index file is corrupt, key value does not match quantisation");

									// also set the last key value (max_key) TODO -- later on check if can do the whole (begin and max) set of keys in one read if such fits into PageSize (relatively small-benefit optimisation as most likely the index size will grow well beyond a pagesize)
									assert(!(file.get_meta().get_data_size() % key_entry_size));
									assert(file.get_meta().get_data_size() >= key_entry_size);
									Existing_sparsification_end = ((Cached_meta_begin_key - file.get_meta().Get_start_of_file_key()) >> this->quantise_shift_mask) * key_entry_size;
									last_key_by_publisher = max_key = file.get_meta().Get_start_of_file_key() + ((file.get_meta().get_data_size() / key_entry_size - 1) << this->quantise_shift_mask);
									if (max_key & ~key_mask) // minor safety check (not much really)
										throw ::std::runtime_error("index file is corrupt, last key value does not match quantisation");

									// also migrate read buffer to the file writing buffer if offset is not page-boundary TODO -- later on check if can do the whole (begin and max AND page-size align amount) in one go... the saving grace on this issue is that the index file is only loaded once per topic-loading which is usually only very very rarely... still though later on some elegance and perfection would be nice...
									uint_fast64_t const file_size_page_boundary_begin(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(file.get_meta().get_data_size(), PageSize));
									if (file_size_page_boundary_begin != file.get_meta().get_data_size()) {
										uint_fast64_t const bytes_to_migrate(file.get_meta().get_data_size() - file_size_page_boundary_begin);
										file.async_read(file_size_page_boundary_begin, bytes_to_migrate, [this, t_, on_ready_callback, bytes_to_migrate](bool error) mutable noexcept {
											assert(this->t.strand.running_in_this_thread());
											try { 
												if (!error && !async_error) {
													::memcpy(file.writing_buffer(), file.reading_buffer_begin(), bytes_to_migrate);
													on_ready_callback(false);
												} else 
													throw ::std::runtime_error("could not read page_boundary at the end of the index file");
											} catch (::std::exception const & e) { 
												put_into_error_state(e); 
												on_ready_callback(true);
											}
										});
									} else
										on_ready_callback(false);
								} else
									throw ::std::runtime_error("on_file_read last index value");
							} catch (::std::exception const & e) {
								put_into_error_state(e);
								on_ready_callback(true);
							} 
						});
					} else { // empty but ready to go... i think... (TODO -- must re-verify)
						assert(!file.writing_buffer_size());
						on_ready_callback(false);
					}
				} else
					throw ::std::runtime_error("on_file_open index");
			} catch (::std::exception const & e) {
				put_into_error_state(e);
				on_ready_callback(true);
			}
		}, [this](Index_meta_ptr meta) { // guaranteed by design to be called before the main 'callback', so the scope should remain...
			meta.initialise(this->quantise_shift_mask, key_entry_size);
		});
	}

	uint_fast64_t
	Unquantised_key_value_to_quantised_byte_offset(uint_fast64_t const & key) const
	{
		assert(t.strand.running_in_this_thread());
		assert(Has_entries());
		assert(!(file.get_meta().Get_start_of_file_key() & ~key_mask));
		assert(!(Cached_meta_begin_key & ~key_mask));
		assert(static_cast<uint64_t>(file.get_meta().Get_start_of_file_key()) != static_cast<uint64_t>(-1));
		assert(Has_entries());
		return ((::std::max(key, file.get_meta().Get_start_of_file_key()) - file.get_meta().Get_start_of_file_key()) >> quantise_shift_mask) * key_entry_size;
	}

	// NOTE, relied-upon property is that if -1 is supplied then this returns false
	bool
	Key_at_byte_offset_within_end_limit(uint_fast64_t const & x)
	{
		assert(t.strand.running_in_this_thread());
		return !(x + key_entry_size > total_size());
	}

	uint_fast64_t
	total_size() const
	{
		return ::std::max(file.writing_buffer_offset() + file.writing_buffer_size(), file.get_meta().get_data_size());
	}

	bool
	Has_entries() const
	{
		assert(Cached_meta_begin_key == static_cast<decltype(Cached_meta_begin_key)>(-1) || file.get_meta().Get_start_of_file_key() != static_cast<decltype(file.get_meta().Get_start_of_file_key())>(-1));
		return Cached_meta_begin_key != static_cast<decltype(Cached_meta_begin_key)>(-1);
	}

	void
	Set_begin_key_conditionally(uint_fast64_t const Quantized_begin_key) 
	{
		assert(t.strand.running_in_this_thread());
		assert(!(Quantized_begin_key & ~key_mask));

		if (Quantized_begin_key > Cached_meta_begin_key) {
			Cached_meta_begin_key = Quantized_begin_key;
			Set_corresponding_indicies_begin_in_meta_topic(Quantized_begin_key);
			assert(Key_at_byte_offset_within_end_limit(Get_key_distance_size_on_disk(Quantized_begin_key - file.get_meta().Get_start_of_file_key())));
			Update_metadata_on_file = true;
		}
	}

	template <typename Callback>
	void 
	async_get_key(uint_fast64_t byte_offset_begin, Callback && callback) 
	{
		assert(t.strand.running_in_this_thread());
		assert(!(file.get_meta().get_data_size() % key_entry_size)); // file size is expected to be multiple of key_entry_size
		assert(!(byte_offset_begin % key_entry_size)); // as is the key position
		assert(Unquantised_key_value_to_quantised_byte_offset(file.get_meta().Get_start_of_file_key()) <= byte_offset_begin);

		bool const initiate(pending_requests.empty());
		pending_requests.emplace(::std::piecewise_construct, ::std::forward_as_tuple(byte_offset_begin), ::std::forward_as_tuple(::std::forward<Callback>(callback)));
		if (initiate) {
			auto t_(t.shared_from_this());
			t.strand.post([this, t_](){async_get_key_impl(t_);});
		}
	}

	void 
	async_get_key_impl(::boost::shared_ptr<Topic> const & t_) 
	{
		assert(t.strand.running_in_this_thread());
		assert(pending_requests.empty() == false);
		assert(total_size() >= pending_requests.front().first + key_entry_size);
		assert(!(pending_requests.front().first % key_entry_size));

		auto const Begin_key_byte_offset(Get_key_distance_size_on_disk(Cached_meta_begin_key - file.get_meta().Get_start_of_file_key()));
		auto const byte_offset_begin(::std::max(pending_requests.front().first, Begin_key_byte_offset));
		if (file.writing_buffer_offset() <= byte_offset_begin && file.writing_buffer_offset() + file.writing_buffer_size() >= byte_offset_begin + key_entry_size) { // read from ram
		 assert(total_size() >= byte_offset_begin + key_entry_size);
		 pending_requests.front().second(file.writing_buffer_begin() + (byte_offset_begin - file.writing_buffer_offset()));
		} else if (file.reading_buffer_offset() <= byte_offset_begin && file.reading_buffer_offset() + file.reading_buffer_size() >= byte_offset_begin + key_entry_size) { // read from reading buffer
			// TODO in future have multiple (small) reading buffers -- this way multiple subscriptions from different time-points may still prevent additional disk-IO when getting next range, etc.
		 pending_requests.front().second(file.reading_buffer_begin() + (byte_offset_begin - file.reading_buffer_offset()));
		} else { // read form file
			assert(file.get_meta().get_data_size() >= byte_offset_begin + key_entry_size);
			assert(t.strand.running_in_this_thread());
			assert(pending_requests.empty() == false);
			assert(file.get_meta().get_data_size() >= pending_requests.front().first + key_entry_size);
			assert(!(pending_requests.front().first % key_entry_size));
			file.async_read(pending_requests.front().first, key_entry_size, [this, t_](bool error) noexcept {
				try {
					assert(t.strand.running_in_this_thread());
					if (!error && !async_error) {
						auto const Begin_key_byte_offset(Get_key_distance_size_on_disk(Cached_meta_begin_key - file.get_meta().Get_start_of_file_key()));
						if (pending_requests.front().first >= Begin_key_byte_offset) {
							// verify hash/crc
							auto const * const buffer_begin(file.reading_buffer_begin()); 
							auto const * const hash_on_disk_ptr(reinterpret_cast<uint64_t const*>(buffer_begin + (key_entry_size - 8)));
							auto const hash_on_disk(synapse::misc::Atomic_load_alias_safe_value<uint64_t>(hash_on_disk_ptr));
							uint64_t hash(0);
							for (uint64_t const * i_ptr(reinterpret_cast<uint64_t const*>(buffer_begin)); i_ptr != hash_on_disk_ptr; ++i_ptr)
								hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(i_ptr));
							if (hash != hash_on_disk)
								throw ::std::runtime_error("corrputed index on disk -- hash values don't match");

							pending_requests.front().second(buffer_begin);
						} else 
							pending_requests.front().second(nullptr);
						pending_requests.pop();
						if (pending_requests.empty() == false)
							t.strand.post([this, t_](){async_get_key_impl(t_);});
					} else 
						throw ::std::runtime_error("async_get_key file.async_read");
				} catch (::std::exception const & e) {
					put_into_error_state(e);
				}
			});
			return;
		}
		pending_requests.pop();
		if (pending_requests.empty() == false)
			t.strand.post([this, t_](){async_get_key_impl(t_);});
	}

}; 

/**
	\about most frequently used type of index. things like timestamps will be indexed by this class. essentially allows associating memory-map (range) ojbects with keys
	*/
template <unsigned PageSize, typename Topic, typename Range>
struct index_with_ranges : 
#ifndef NDEBUG
// Important! MUST be the FIRST in the inheritance list if used at all
synapse::misc::Test_new_delete<index_with_ranges<PageSize, Topic, Range>>,
#endif
index<PageSize, Topic, Range, 1> 
{
	typedef index<PageSize, Topic, Range, 1> base_type;
#ifndef NDEBUG
	static void * operator new (::std::size_t sz) {
		return synapse::misc::Test_new_delete<index_with_ranges<PageSize, Topic, Range>>::operator new(sz);
	}
#endif
	// TODO -- try with hashmaps later on in various places here...
	// TODO -- more efficient memory usage on this container (may write our own later, to take advantage of the single-threaded or rather single-stranded semantics)
	typedef ::std::multimap<uint_fast64_t const, Range*, ::std::less<uint_fast64_t const>, data_processors::misc::alloc::basic_alloc_1<::std::pair<uint_fast64_t const, Range*>>> ranges_index_type;
	typedef typename ranges_index_type::iterator range_index_iterator_type;
	using base_type::t;
	using base_type::indicies_i;
	using base_type::async_error;
	using base_type::file; 
	using base_type::quantise_shift_mask; 
	using base_type::key_entry_size;
	using base_type::total_size;
	using base_type::put_into_error_state;
	using base_type::key_mask;
	using base_type::async_get_key;
	using base_type::Cached_meta_begin_key;

	unsigned const Index_In_Message;

	ranges_index_type ranges{typename ranges_index_type::allocator_type{t.sp}}; // already-processed, existing and mapped pointers to ranges

	index_with_ranges(Topic & t, unsigned const indicies_i, ::std::string const & meta_filepath, ::std::string const & filepath, unsigned const read_buffer_size, unsigned const write_buffer_size, uint_fast64_t const Max_indexable_gap, unsigned const quantise_shift_mask, unsigned const Index_In_Message)
	: base_type(t, indicies_i, meta_filepath, filepath, read_buffer_size, write_buffer_size, quantise_shift_mask, Max_indexable_gap), Index_In_Message(Index_In_Message) {
		assert(Index_In_Message < database::Message_Indicies_Size);
		assert(this->indicies_i != static_cast<decltype(this->indicies_i)>(-1));
	}

	/**
		\about determine whether current key value may already be contained by one of the in-RAM ranges
		*/
	range_index_iterator_type
	find_in_current_ranges(uint_fast64_t const & quantised_key) noexcept
	{
		assert(t.strand.running_in_this_thread());
		assert(!(quantised_key & ~key_mask));
		if (ranges.empty() == false) {
			auto found(ranges.lower_bound(quantised_key));
			if (found != ranges.end()) {
				assert(Index_In_Message <= found->second->msg.Get_indicies_size());
				if (found->second->msg.Get_index(Index_In_Message) != quantised_key) {
					if (found != ranges.begin())
						--found;
					else 
						return ranges.end();
				}
				assert(found->second->msg.Get_index(Index_In_Message) <= quantised_key);
				if (found->second->is_open_ended() == true || found->second->get_back_msg().Get_index(Index_In_Message) >= quantised_key)
					return found;
			} else {
				--found;
				assert(Index_In_Message <= found->second->msg.Get_indicies_size());
				if (found->second->msg.Get_index(Index_In_Message) <= quantised_key && (found->second->is_open_ended() == true || found->second->get_back_msg().Get_index(Index_In_Message) >= quantised_key))
					return found;
			}
		} 
		return ranges.end();
	}

	template <typename CallbackType>
	void
	post_pending_request(uint_fast64_t key, CallbackType && callback) noexcept
	{
		auto t_(t.shared_from_this());
		t.strand.post([this, t_, key, callback(::std::forward<CallbackType>(callback))]() mutable {
			on_pending_request(key & key_mask, ::std::move(callback));
		});
	}

	/**
		\about index-key based requests for topic data (the requests come from subscriber sockets). the requests are general ballpark area and in server-terms only at this stage (clients' payload is opaque semantically speaking). 
		\post async return of the memory window (i.e. a range) into the relevant portion of the file data for the topic; or (if no ranges existed in ram) the key-data of the index so that the caller can appropriate as needed; or (if no ranges existed in ram and no key-data is yet available/indexed) then nullptr for both but with error still set to false
		*/
	template <typename CallbackType>
	void
	on_pending_request(uint_fast64_t key, CallbackType && callback) noexcept
	{
		assert(t.strand.running_in_this_thread());
		try {
			if (!async_error) { // TODO think about error-handling some more... (need to have a uniform policy on these things)...
				// get the range object by first trying to find it in RAM...
				assert(!(key & ~key_mask));
				if (file.get_meta().Get_start_of_file_key() != static_cast<decltype(file.get_meta().Get_start_of_file_key())>(-1)) {
					assert(base_type::Has_entries());
					if (key < Cached_meta_begin_key)
						key = Cached_meta_begin_key;
					auto found_range(find_in_current_ranges(key));
					if (found_range != ranges.end()) { // found already-existing ranges in RAM
						callback(found_range->second->intrusive_from_this(), nullptr, false);
						return;
					} else { // at this stage there are basically no 'obvious (with indicies built-up)' RAM-existing range objects to satsify the request, so do a disk-io search
						uint_fast64_t index_offset_to_read(base_type::Get_key_distance_size_on_disk(key - file.get_meta().Get_start_of_file_key()));

						assert(!total_size() || !(total_size() % key_entry_size));

						// the thing is that async. nature of pubisher vs index-building mechanisms may mean that given query may return 'future request' yet it is actually still in RAM (indicies have not been async. built as of yet)...
						if (total_size() && total_size() <= index_offset_to_read + key_entry_size) {
							assert(total_size() >= key_entry_size);
							index_offset_to_read = total_size() - key_entry_size; // so default to earliest-available if any so far have been recorded
						}

						if (total_size() >= index_offset_to_read + key_entry_size) {
							auto t_(t.shared_from_this());
							async_get_key(index_offset_to_read, [this, t_, callback](void const * buffer) mutable noexcept {
								try {
									assert(t.strand.running_in_this_thread());
									if (!async_error)
										callback(nullptr, buffer, false);
									else
										throw ::std::runtime_error("could not read key from index file");
								} catch (::std::exception const & e) { 
									put_into_error_state(e); 
									callback(nullptr, nullptr, true);
								}
							}); 
							return;
						} 
					}
				}
				callback(nullptr, nullptr, false);
			} else 
				throw ::std::runtime_error("on_pending_request, index object is already in error");
		} catch (::std::exception const & e) { 
			put_into_error_state(e); 
			callback(nullptr, nullptr, true);
		}
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


