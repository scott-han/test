#include <atomic>
#include <thread>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "../Version.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { 
namespace misc { namespace alloc {
::std::atomic_uint_fast64_t static Virtual_allocated_memory_size{0};
uint_fast64_t static Memory_alarm_threshold{static_cast<uint_fast64_t>(-1)};
}}
namespace synapse { 
namespace asio {
::std::atomic_bool static Exit_error{false};
}
namespace database {
::std::atomic_uint_fast64_t static Database_size{0};
uint_fast64_t static Database_alarm_threshold{static_cast<uint_fast64_t>(-1)};
int static nice_performance_degradation = 0;
}
namespace amqp_0_9_1 {
unsigned constexpr static page_size = 4096;
}
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../asio/io_service.h"
#include "../misc/sysinfo.h"
#include "../misc/alloc.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { 
// Todo: better inclusion style (no time now).
data_processors::misc::alloc::Large_page_preallocator<amqp_0_9_1::page_size> static Large_page_preallocator;
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "async_file.h"

#include "topic.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

struct Topic_Factory_Type {
	::boost::asio::strand strand{synapse::asio::io_service::get()};
};

struct meta_topic {
		::std::atomic<uint_fast32_t> Intrusive_Pointer_Reference_Size{0};

		bool Is_Unique() const noexcept {
			return Intrusive_Pointer_Reference_Size.load(::std::memory_order_relaxed) == 1;
		}

		friend void intrusive_ptr_add_ref(meta_topic * Meta_Topic) {
			auto const Previous_Reference_Size(::std::atomic_fetch_add_explicit(&Meta_Topic->Intrusive_Pointer_Reference_Size, 1u, ::std::memory_order_relaxed));
			if (Previous_Reference_Size == ::std::numeric_limits<uint_fast32_t>::max())
				throw ::std::runtime_error("too many shared references of meta_topic object -- past 32bit max capabilities, need to recompile");
		}

		friend void intrusive_ptr_release(meta_topic * Meta_Topic) {
			assert(Meta_Topic);
			auto const Previous_reference_counter(::std::atomic_fetch_sub_explicit(&Meta_Topic->Intrusive_Pointer_Reference_Size, 1u, ::std::memory_order_release));
			switch (Previous_reference_counter) {
			case 1 :
			::std::atomic_thread_fence(::std::memory_order_acquire); // force others to be done on memory access before actual deletion
			delete Meta_Topic;
			}
		}
	meta_topic(meta_topic const &) = delete;
	Topic_Factory_Type & Topic_Factory;
	meta_topic(Topic_Factory_Type & Topic_Factory) :Topic_Factory(Topic_Factory) {
	}
	::std::atomic_uint_fast64_t indicies_begin[1] = {};
	bool Previous_Instance_Exists{false};
	::std::function<void(::boost::intrusive_ptr<meta_topic> const &)> On_Previous_Instance_Dtor;

	::std::atomic_uint_fast64_t Approximate_Topic_Size_On_Disk{0};

	template <typename Meta_Topic_Pointer_Type>
	void Previous_Instance_Dtored(Meta_Topic_Pointer_Type && Meta_Topic) {
		Topic_Factory.strand.post([this, this_(::std::forward<Meta_Topic_Pointer_Type>(Meta_Topic))]() mutable {
			Previous_Instance_Exists = false;
			if (On_Previous_Instance_Dtor) {
				auto To_Call(::std::move(On_Previous_Instance_Dtor));
				To_Call(::std::move(this_));
			}
		});
	}
};

typedef database::topic<meta_topic, 32 * 1024 * 1024, synapse::amqp_0_9_1::page_size> topic_type;
::boost::shared_ptr<topic_type> t;


unsigned constexpr static max_payload_size = database::estimate_max_payload_size() - 5;

unsigned constexpr static max_preroll = 
	+ 8 // database message header 
	+ 8 * database::Message_Indicies_Size // indicies
	- 3
;

static_assert(max_payload_size + max_preroll <= database::MaxMessageSize , "payload size too large");

uint_fast64_t publish_from;
uint_fast64_t publish_until;
uint_fast64_t wait_until;

struct publisher : ::boost::enable_shared_from_this<publisher> {
	::boost::shared_ptr<topic_type> t_ = database::t;
	::boost::asio::strand strand;

	::boost::random::mt19937 rng;
	::boost::random::uniform_int_distribution<uint_fast64_t> rng_range;

	::boost::random::uniform_int_distribution<unsigned> payload_size_rng_range;


	::boost::asio::deadline_timer io_simulator;

	typedef topic_type::range_ptr_type range_ptr;
	range_ptr range;

	::std::aligned_storage<max_preroll + max_payload_size + 5>::type payload;
	uint_fast32_t const message_preroll;

	uint_fast64_t constexpr static total_bytes_to_write = 500 * 1024 * 1024;
	uint_fast64_t total_bytes_written = 0;
	uint_fast64_t written_in_range = 0; // used primarily as performance caching mechanism (not to reference too frequently the atomic var touched by 'get_written_size()' in range class... as well as nullptr check for msg that get_written_size() performs)

	uint_fast64_t messages_size{0};


	::boost::shared_ptr<publisher> & container;
	publisher(::boost::shared_ptr<publisher> & container)
	: strand(synapse::asio::io_service::get()), rng_range(50, 200000), payload_size_rng_range(max_payload_size >> 1, max_payload_size), io_simulator(synapse::asio::io_service::get()), message_preroll(
			// our fields size prior to user's payload are:
			4 // size field
			+ 4 // number of indicies field
			+ database::Message_Indicies_Size * 8 ///\note in current reality, so far our index is just 1 field -- timestamp...
			- 3 
		)
	 , container(container)
	{
	}

	void
	async_message_loop(::boost::system::error_code const &) {
		assert(strand.running_in_this_thread());
		///\note we can do "zero-copy" writing from something like socket's AMQP body content frame... by leveraging the fact that body content needs only 7 leading bytes (we will need no less, so can 'clobber' the space later on)...

		assert(range);
		unsigned const payload_size(payload_size_rng_range(rng));
		assert(payload_size <= max_payload_size && payload_size >= max_payload_size >> 1);
		///\note cant use range->file.buffer_size() thread corcurrency (file is being written-to by the topic's resource-checking thread)
		uint_fast32_t const estimated_size_on_disk(database::estimate_size_on_disk(payload_size + 9)); // theoretically one is able to get exact size on disk due to known number of indicies...	TODO refactor the call to this method to leverage this info!!!

		auto message_writeout([this, payload_size, estimated_size_on_disk](){

			if (!this->range->get_back_msg()) { // if current range is empty
				assert(!written_in_range);
				assert(this->range->is_open_ended() == true);
				this->range->ensure_reserved_capacity_no_less_than(estimated_size_on_disk);
			}

			// imaginary buffer data as if written for AMQP socket writer (simulating content composed by client on the other side of the network)
			///\note this is of course very slow, but done here for examplification and testing purposes (in reality this will be written by the remote client connection)
			char unsigned * tmp(reinterpret_cast<char unsigned*>(&payload) + message_preroll);
			tmp[0] = 3;
			assert(!(reinterpret_cast<uintptr_t>(tmp + 3) % alignof(uint32_t)));
			new (tmp + 1) uint16_t(htobe16(5)); // some channel number
			new (tmp + 3) uint32_t(htobe32(payload_size)); // payload of the AMQP user frame (actual content) 
			unsigned static message_count(0);
			tmp[7] = ++message_count;// a bit of cycling message's data through between different messages for the sake of verification on the subscriber's side...
			for (unsigned i(1); i != payload_size; ++i) // write some ascii text
				tmp[7 + i] = ((tmp[7] + i) % 11) + 'a';
			tmp[7 + payload_size] = 0xce; // frame-end octet 
			// end 'remote-client-connection' writing simulation
			
			// now -- as if we have just read it from wire...
			char unsigned * amqp_raw_buf(this->range->file.buffer_begin() + written_in_range + message_preroll);
			::memcpy(amqp_raw_buf, reinterpret_cast<char unsigned*>(&payload) + message_preroll, payload_size + 8);

			assert(!(reinterpret_cast<uintptr_t>(amqp_raw_buf + 3) % alignof(uint32_t)));
			uint_fast32_t const body_content_size(be32toh(synapse::misc::Get_alias_safe_value<uint32_t>(amqp_raw_buf + 3)));
			assert(body_content_size == payload_size);

			// now can create our message -- with payload already being there (from amqp socket's writing action)
			database::Message_ptr msg(this->range->file.buffer_begin() + written_in_range, body_content_size + 16 + message_preroll);

#ifndef NDEBUG
			{
				auto payload(msg.Get_payload() + 4);
				assert(payload == this->range->file.buffer_begin() + written_in_range + message_preroll + 7);
				assert(msg.Get_payload_size() == payload_size + 5);
				for (unsigned i(1); i != payload_size; ++i)
					assert(payload[i] == ((payload[0] + i) % 11) + 'a');
			}
#endif

			typename ::std::remove_reference<decltype(*this->range.get())>::type::scoped_atomic_bool msg_app_racing_cond(this->range.get());

			// stamp our index for the message (seq, timestamp)
			msg.Set_index(0, ++messages_size);
			msg.Set_index(1, synapse::misc::cached_time::get());
			uint_fast32_t const bytes_written(msg.Get_size_on_disk());

			total_bytes_written += bytes_written;
			written_in_range += bytes_written;

			if (bytes_written < 1024 * 1024 * 3) {
				msg.Set_hash(msg.Calculate_hash());
				this->range->message_appended(msg, msg_app_racing_cond); // todo -- analysis -- atomic access
				schedule_next_loop_iteration();
			} else {
				auto this_(shared_from_this());
				msg.Async_calculate_hash(strand.wrap([this, this_, msg, msg_app_racing_cond](uint32_t calculated_hash) mutable {
					assert(strand.running_in_this_thread());
					msg.Set_hash(calculated_hash);
					this->range->message_appended(msg, msg_app_racing_cond); // todo -- analysis -- atomic access
					schedule_next_loop_iteration();
				}));
			}

		});

		if (estimated_size_on_disk + written_in_range <= range->file.buffer_capacity() || !written_in_range) {
			message_writeout();
		} else {
			auto this_(shared_from_this());
			t_->async_get_next_range(range, strand.wrap([this, this_, message_writeout](range_ptr const & r) {
				if (!r)
					throw ::std::runtime_error("not expecting null ptr on get next range async return");
				synapse::misc::log("publisher obtained a range: " + ::boost::lexical_cast<::std::string>(r) + '\n', true);
				assert(!r->get_written_size());
				written_in_range = 0;
				this->range = r;
				message_writeout();
			}));
		}
	}

	void
	schedule_next_loop_iteration()
	{
		if (total_bytes_written < total_bytes_to_write || publish_until > synapse::misc::cached_time::get()) { // roughly
			io_simulator.expires_from_now(::boost::posix_time::microseconds(rng_range(rng)));
			io_simulator.async_wait(strand.wrap(::boost::bind(&publisher::async_message_loop, this, ::boost::asio::placeholders::error)));
		} else {
			//range.reset();
			synapse::misc::log("publisher done streaming\n", true);
			container.reset();
		}
	}

	void
	go()
	{
		auto this_(shared_from_this());
		t_->async_get_first_range_for_connected_publisher(strand.wrap([this, this_](range_ptr const & range) {
			synapse::misc::log("publisher obtained an opend-end range: " + ::boost::lexical_cast<::std::string>(range) + '\n', true);
			if (range == nullptr)
				throw ::std::runtime_error("publisher: nullptr range object delivered on async_get_range");
			auto const & timestamp_index(t_->indicies[0]);
			uint_fast64_t const tmp_key_test(timestamp_index->key_mask & synapse::misc::cached_time::get());
			if (timestamp_index->file.get_meta().get_data_size() && tmp_key_test < timestamp_index->max_key)
				throw ::std::runtime_error("publisher: can't publish into the past");

			written_in_range = range->get_written_size(); // ok here to use this method (even though it is using 'msg' as a check in it's implementation because the 'open ended range' is expected to have msg as nullptr and there is only one 'publishing' thread allowed semantically speaking
			// in our test case, written_size is really expected to be zero
			assert(!written_in_range);
			assert(!range->get_back_msg());
			this->range = range;
			io_simulator.expires_from_now(::boost::posix_time::microseconds(rng_range(rng)));
			io_simulator.async_wait(strand.wrap(::boost::bind(&publisher::async_message_loop, this, ::boost::asio::placeholders::error)));
		}), nullptr);
	}
};

::std::atomic_bool test_quitter{false};
struct subscriber : ::boost::enable_shared_from_this<subscriber>  {
	::boost::shared_ptr<topic_type> t_ = t;
	::boost::asio::strand strand;
	::boost::asio::deadline_timer io_simulator;
	typedef topic_type::range_ptr_type range_ptr;
	range_ptr range;
	range_ptr next_range;
	database::Message_ptr my_msg;
	database::Message_const_ptr My_back_message;
	uint_fast32_t read_from_current_range;
	bool next_range_pending = false;

	::boost::random::mt19937 rng;

	void
	put_into_error_state(::std::exception const & e) 
	{
		throw e;
	}

	::std::vector<::boost::shared_ptr<subscriber>> & container;
	unsigned const container_membership_i;
	subscriber(::std::vector<::boost::shared_ptr<subscriber>> & container, unsigned container_membership_i)
	: strand(synapse::asio::io_service::get()), io_simulator(synapse::asio::io_service::get()), rng(container_membership_i % 11), container(container), container_membership_i(container_membership_i) {
	}
	
	~subscriber()
	{
		if (test_quitter == true)
			--synapse::asio::quitter::get().quit_fence_size;
	}

	bool do_quit{false};
	uint_fast64_t double_postings_size = 0;
	bool pending_range_notification = false;
	void
	on_msg_notification() noexcept
	{
		try {
			assert(strand.running_in_this_thread());
			if (do_quit == false && !range->get_error()) {
				assert(!my_msg || my_msg.Get_index(1) >= first_received_timestamp);
				assert(!my_msg || My_back_message);
				assert(!messages_read || last_received_timestamp >= first_received_timestamp);

				auto next_msg(range->get_next_message(my_msg, My_back_message));

				if (next_msg != my_msg) { // got new message

					assert(next_msg);

					my_msg = next_msg;
					assert(strand.running_in_this_thread());
					assert(my_msg);
					assert(My_back_message);
					// verify contents...
					auto payload(my_msg.Get_payload() + 4);
					unsigned const payload_size(my_msg.Get_payload_size() - 5);
					assert(payload_size <= max_payload_size && payload_size >= max_payload_size >> 1);
					for (unsigned i(1); i != payload_size; ++i)
						if (payload[i] != ((payload[0] + i) % 11) + 'a')
							throw ::std::runtime_error("subscriber detected invalid/corrupt payload data");
					read_from_current_range += my_msg.Get_size_on_disk();


					if (asked_for_initial_timestamp >= my_msg.Get_index(1) && my_msg.Get_index(1) >= last_received_timestamp) {

						if(!messages_read++)
							first_received_timestamp = my_msg.Get_index(1);

						assert(my_msg.Get_index(1) >= last_received_timestamp);
						last_received_timestamp = my_msg.Get_index(1);

						assert(last_received_timestamp >= first_received_timestamp);
					}

					// no point in 'async getting' next range if current one is an open-ended kind (won't be able to read from disk anyway as the contents of the current range haven't even been written to disk yet (completely))
					if (range->is_open_ended() == false && range->file.buffer_capacity() < read_from_current_range * 2 && next_range_pending == false && !next_range) {
						next_range_pending = true;
						t_->async_get_next_range(range, strand.wrap(::boost::bind(&subscriber::on_new_range, shared_from_this(), _1)));
					}

					synapse::asio::io_service::get().post(strand.wrap(::boost::bind(&subscriber::on_msg_notification, shared_from_this())));

				} else { 
					if (range->is_open_ended() == false && next_msg == range->get_back_msg() && next_msg) {
						if (next_range) {
							synapse::misc::log("active range assignment\n", true);
							assert(next_range.get() != range.get());
							read_from_current_range = 0;
							range = ::std::move(next_range);
							next_range.reset();
							assert(!next_range);
							my_msg.Reset();
							My_back_message.Reset();
							synapse::asio::io_service::get().post(strand.wrap(::boost::bind(&subscriber::on_msg_notification, shared_from_this())));
						} else  {// need to get a new range...
							if (next_range_pending == false) {
								next_range_pending = true;
								auto this_(shared_from_this());
								t_->async_get_next_range(range, strand.wrap([this, this_](range_ptr const & range){
									this->range.reset();
									on_new_range(range);
								}));
							}
						}
					} else { // because double-posting could happen due to lockless(ish) performance design...
						//synapse::misc::log("double posting\n", true);
						if (pending_range_notification == true)
							++double_postings_size;
						register_for_notification();
					}
				}
				pending_range_notification = false;
			}
		} catch (::std::runtime_error const & e) { put_into_error_state(e); }
	}

	void
	register_for_notification()
	{
		assert(range);
		pending_range_notification = true;
		::boost::weak_ptr<subscriber> this_(shared_from_this());
		range->register_for_notification(my_msg, [this, this_]() {
			if (auto me_ = this_.lock()) {
				me_->strand.dispatch([this, me_]() noexcept {
					on_msg_notification();
				});
			}
		}, false);
	}

	void
	on_new_range(range_ptr const & range)
	{
		try {
			assert(strand.running_in_this_thread());
			assert(next_range_pending == true);
			next_range_pending = false;
			synapse::misc::log("subscriber obtained a range: " + ::boost::lexical_cast<::std::string>(range) + '\n', true);
			if (range == nullptr)
				throw ::std::runtime_error("subscriber: nullptr range object delivered on async_get_range\n");
			if (this->range) {
				assert(!next_range);
				assert(this->range != range);
				next_range = range;
			} else {
				synapse::misc::log("Active range assignment, range ptr:" + ::boost::lexical_cast<::std::string>(range) + '\n', true);
				read_from_current_range = 0;
				this->range = range;
				my_msg.Reset();
				My_back_message.Reset();
			}
			on_msg_notification();
		} catch (::std::runtime_error const & e) { put_into_error_state(e); }
	}

	uint_fast64_t messages_read = 0;
	uint_fast64_t asked_for_initial_timestamp;
	uint_fast64_t first_received_timestamp = 0;
	uint_fast64_t last_received_timestamp = 0;

	void
	on_quit()
	{
		assert(strand.running_in_this_thread());
		synapse::misc::log("in subscriber " + ::boost::lexical_cast<::std::string>(this) + " ending subscribing duration covered: " + ::boost::lexical_cast<::std::string>((last_received_timestamp - first_received_timestamp) / 1000000u) + ", messages read " + ::boost::lexical_cast<::std::string>(messages_read) + ", double postings " + ::boost::lexical_cast<::std::string>(double_postings_size) + '\n', true);
		do_quit = true;
		//range.reset();
		//next_range.reset();
		container[container_membership_i].reset();
	}

	void
	go()
	{
		if (test_quitter == true) {
			++synapse::asio::quitter::get().quit_fence_size;
			::boost::weak_ptr<subscriber> weak_scope(shared_from_this()); 
			synapse::asio::quitter::get().add_quit_handler([this, weak_scope](synapse::asio::quitter::Scoped_Membership_Registration_Type && Scoped_Membership_Registration){
				if (auto scope = weak_scope.lock())
					strand.post([this, scope, On_Quit_Scoped_Membership_Registration(::std::move(Scoped_Membership_Registration))]() mutable {
						this->On_Quit_Scoped_Membership_Registration = ::std::move(On_Quit_Scoped_Membership_Registration);
						if (!this->On_Quit_Scoped_Membership_Registration)
							on_quit();
					});
			});
		}
		// wait initially...
		::boost::random::uniform_int_distribution<uint_fast64_t> rng_range(1000, (publish_until - publish_from) / 2);
		unsigned const wait_for(rng_range(rng));
		io_simulator.expires_from_now(::boost::posix_time::microseconds(wait_for));
		auto this_(shared_from_this());
		io_simulator.async_wait(strand.wrap([this, this_](::boost::system::error_code const &){
			// ask for reasonably random(ish) timestamp
			::boost::random::uniform_int_distribution<uint_fast64_t> rng_range(publish_from, publish_until); 
			assert(next_range_pending == false);
			next_range_pending = true;
			asked_for_initial_timestamp = rng_range(rng);
			t_->async_get_range(0, asked_for_initial_timestamp, strand.wrap(::boost::bind(&subscriber::on_new_range, this_, _1)));


			// monitoring for publish until expiring...
			io_simulator.expires_from_now(::boost::posix_time::seconds(1));
			io_simulator.async_wait(strand.wrap(::boost::bind(&subscriber::monitor_for_publish_expiration, this_, _1)));
		}));
	}
	synapse::asio::quitter::Scoped_Membership_Registration_Type On_Quit_Scoped_Membership_Registration;
	void
	monitor_for_publish_expiration(::boost::system::error_code const & e)
	{
		if (!e) {
			assert(strand.running_in_this_thread());
			if (wait_until < synapse::misc::cached_time::get()) {
				if (test_quitter == true)
					synapse::asio::quitter::get().quit([](){
						synapse::misc::log("Quttier quitting\n", true);
					});
				else
					on_quit();
			} else {
				io_simulator.expires_from_now(::boost::posix_time::seconds(1));
				io_simulator.async_wait(strand.wrap(::boost::bind(&subscriber::monitor_for_publish_expiration, shared_from_this(), _1)));
			}
		}
	}
};


int static
run(int, char* argv[])
{
	try
	{
		boost::filesystem::path path(argv[0]);
		path = path.parent_path() / ("." + path.filename().string() + ".tmp");
		if (::boost::filesystem::exists(path))
			::boost::filesystem::remove_all(path);
		::boost::filesystem::create_directory(path);

		synapse::Large_page_preallocator.Initialize(1024ull * 1024 * 1024 * 3);

#ifndef SYNAPSE_SINGLE_THREADED
		unsigned const concurrency_hint(synapse::misc::concurrency_hint());
		synapse::asio::io_service::init(concurrency_hint);
#else
		synapse::asio::io_service::init(1);
#endif
		synapse::misc::cached_time::init(1000 * 100); // 100 milliseconds accuracy/efficiency trade-off for our timing resolution/updates


		{ // publisher and subscribers at once (although difficult to hit 'off the disk' test interaction...

			Topic_Factory_Type Topic_Factory;

			synapse::misc::log("PUB AND SUBs\n", true);
			auto mt(::boost::intrusive_ptr<meta_topic>(new meta_topic(Topic_Factory)));
			t.reset(new topic_type(path.string(), mt));

			::boost::shared_ptr<publisher> p;
			::std::vector<::boost::shared_ptr<subscriber>> subscribers;

			publish_from = synapse::misc::cached_time::get();
			wait_until = publish_until = publish_from + 21 * 1000000;
			t->async_open([&p, &subscribers](bool error) {
					synapse::misc::log("have opened topic, error state: " + ::boost::lexical_cast<::std::string>(error) + '\n', true);
				p.reset(new publisher(p));
				p->go();
				for (unsigned i(0); i != 21; ++i)
					subscribers.emplace_back(new subscriber(subscribers, i));
				for (auto & i : subscribers)
					i->go();
				t.reset();
			});

#ifndef SYNAPSE_SINGLE_THREADED
			::std::vector<::std::thread> pool(concurrency_hint * 3 - 1);
			for (auto & i : pool)
				i = ::std::thread([](){synapse::asio::run();});
#endif

			synapse::asio::run();

#ifndef SYNAPSE_SINGLE_THREADED
			for (auto & i: pool)
				i.join();
#endif
			assert(!t);
		}

		{ // subscribers-only (easier to hit soe 'off the disk' test interaction...
			test_quitter = true;
			synapse::misc::log("SUBs FROM DISK\n", true);
			synapse::asio::io_service::get().reset();

			Topic_Factory_Type Topic_Factory;

			wait_until = synapse::misc::cached_time::get() + 21 * 1000000;

			auto mt(::boost::intrusive_ptr<meta_topic>(new meta_topic(Topic_Factory)));
			t.reset(new topic_type(path.string(), mt));

			::std::vector<::boost::shared_ptr<subscriber>> subscribers;

			t->async_open([&subscribers](bool error) {
				synapse::misc::log("have opened topic, error state: " + ::boost::lexical_cast<::std::string>(error) + '\n', true);
				for (unsigned i(0); i != 21; ++i)
					subscribers.emplace_back(new subscriber(subscribers, i));
				for (auto & i : subscribers)
					i->go();
				t.reset();
			});


#ifndef SYNAPSE_SINGLE_THREADED
			::std::vector<::std::thread> pool(concurrency_hint * 3 - 1);
			for (auto & i : pool)
				i = ::std::thread([](){synapse::asio::run();});
#endif

			synapse::asio::run();

#ifndef SYNAPSE_SINGLE_THREADED
			for (auto & i: pool)
				i.join();
#endif
		}

		return synapse::asio::Exit_error;
	}
	catch (::std::exception const & e)
	{
		synapse::misc::log("oops: " + ::std::string(e.what()) + '\n');
		synapse::misc::log.flush();
		return 1;
	}
	catch (...)
	{
		synapse::misc::log("oops: uknown exception\n");
		synapse::misc::log.flush();
		return 1;
	}
}
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char* argv[])
{
	return ::data_processors::synapse::database::run(argc, argv);
}
