/**
	\about The whole process of indexing: 
	(*) client would ask the server for messages starting from the server's indexed value (e.g. server-side timestamp). 
	(*) Such a request from client is seen as a general ballpark hint *only* and the server may return messages +- of some arbitrary amount. 
	(*) The server will, however, communicate the startding index value before streaming raw payload data (e.g. AMQP frames as received by the publishing peer at some stage). 
	(*) The idea then is that the client will use it's own understanding of internal (opaque to the server) AMQP payload frames which are expected to contain the exact and 'per-message' index values (e.g. publisher's timestamps). 
	(*) The client then is able to "park" it's processing mechanisms precisely "on the dot" (having filtered potentially "outside of area of intereset" frames sent by the server.

	\note Such an appoarch allows for the follownig characteristics:
	(*) AMQP payload data is opaque to the server;
	(*) AMQP processing/streaming is still fast (performance-oriented) as there is no need to send more-expensive "content-header" frames for each of the "content-body" frame;
	(*) There is no burden placed on the client-side to understand any of the server-specific message formatting.
	*/

/**
	\note
	The thing to consider in future is how some topic values/fields may not be a part of AMQP payload/headers -- indicies such as universal message sequence, server-side timestamps, etc. 
	Moreover, it may be desirable to have an indepndent format and "on the fly" translation to AMQP or any other protocol deployed in the future -- becoming forward compatible with multi-protocol support options...
	Any of the tranlation-to-AMQP may incur some performance penalties -- e.g. even if raw AMQP frames were to be stored, the things like channel-number may need to be overwritten by the subscribing-peer-related code. 
	So, it would appear, that the notion of communicating/parsing 1-stored-message-at-a-time still holds.
*/

/**
	\note At the moment each message does indeed store, on top of the payload, it's own server-side indicies (timestamp for example)... another approach could have index file denoting certains 'boundary' points in the data stream with selected index charateristics and would save on having any indicies stored with the data message (on data file) other than it's total size of course. but this would come at a price of not ever having individual message's timestamp (as index files need to be both -- not as large as data files, much smaller if possible, and fixed-length quantisation allowing an "array-subscript"-like seeking in the underlying hard drive storage)... such as loss of informatino would also impact on the ability to service "atomically unique message counter" at the server side... 
	So currently we are indeed storing all of the index data with each of the data message in the data file, but we may consider alternative approaches later on if need/want be :)
*/


#include "../misc/misc.h"

#ifndef DATA_PROCESSORS_SYNAPSE_DATABASE_MESSAGE_H
#define DATA_PROCESSORS_SYNAPSE_DATABASE_MESSAGE_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {
	unsigned constexpr static MaxMessageSize = 1024 * 1024 * 128; 
	static_assert(MaxMessageSize < 1 << 30, "atomic/lockless access to range's messages implies MaxMessageSize-1 being less that 2^30");
	unsigned constexpr static Message_Indicies_Size = 2; 

	uint_fast32_t static inline
	estimate_size_on_disk(uint_fast32_t payload_size)
	{
		return synapse::misc::round_up_to_multiple_of_po2<uint_fast32_t>(payload_size + 8 * (2 + Message_Indicies_Size), 8);
	}

	uint_fast32_t constexpr static inline
	estimate_max_payload_size()
	{
		return MaxMessageSize - 8 * (2 + Message_Indicies_Size);
	}

	/**
		\About
		on the wire representation:
		uint32_t -- total message size (including self, and all the crc stuff but NOT including any padding-to-8 byte alignments etc.)
		uint32_t -- number of index entries
		uint64_t* -- index key values
		void * -- payload data as received from the client 
		uint32_t -- hash of all the preceeding stuff (not including self)
		uint32_t -- zeros (reserved -- will be populated by runtime-calculated crc32 during verification -- only once by one of the readers and reused by others)
		*/
	/**
		\Example (use wide-enough window and monospaced font)
		Given the following AMQP body frame:
		0      1         3             7            sze+7  sze+8
		+------+---------+-------------+----   -----+----------+
		| type | channel |  PAYLDSIZE  |  PAYLOAD   | FRAMEEND |
		+------+---------+-------------+----   -----+----------+

		... and our database having 2 indicies we have the following STORED ON THE DATABASE:

		0       4        8             16            24       28            sze+7  sze+8  pad-to-8   8-aligned +4       +8
		+-------+--------+-------------+-------------+---------+----   -----+----------+----    -----+---------+---------+
		|MsgSize|IndxSize| First index |  Sec index  |PAYLDSIZE|  PAYLOAD   | FRAMEEND |   Padding   |  crc32  | reserved|
		+-------+--------+-------------+-------------+---------+----   -----+----------+----    -----+---------+---------+
		
		... in other words we currently do not store type and channel (TODO, in future may ALSO CONSIDER DROPPING 'PAYLDSIZE' -- trade-off between htonl conversion vs stream-reading extra 4 bytes off disk... our code has evolved to a point that I have a feeling that we should probably drop PAYLDSIZE from disk storage because even if saving on extra htonl, we pay extra 4-byte CPU cycle iteration when doing crc32 calculation anyways).

		And PAYLOAD is where something like the Thrift envelope is stored (albeit synapse is Thrift-unaware as it is a completely independent and higher-level layer in our infrastructure).

		Also, for performance reasons, we do not zero-out padding (pad-to-8) meaning that current CRC validity is only within given server. The security implicatinons are also there in that in case of a recycled memory data bleeding between two different topics may occur (however ought not to escape in terms of one subscription getting data from another, possibly restricted, topic)... so also is confined within a given instance of server/depoylment.

	*/
	template <bool Is_mutable>
	class Basic_message_ptr {
		template <bool, bool> struct Memory_type_traits;
		template<bool Ignored> struct Memory_type_traits<true, Ignored> { typedef void type; typedef char unsigned Memory_type; };
		template<bool Ignored> struct Memory_type_traits<false, Ignored> { typedef void const type; typedef char unsigned const Memory_type; };
		typename Memory_type_traits<Is_mutable, true>::Memory_type * Memory;
	public:
		static_assert(alignof(uint32_t) == 4, "uint32_t alginment for the internal message format");
		static_assert(alignof(uint64_t) == 8, "uint32_t alginment for the internal message format");


		// number of index entries (only need 16 bit really, so if additional field of 16 bit is needed, might as well store it here) 
		auto
		Get_indicies_size() const 
		{
			assert(Memory);
			return synapse::misc::Get_alias_safe_value<uint32_t>(reinterpret_cast<uint32_t const *>(Memory) + 1);
		}

		auto
		Get_index(unsigned Offset) const
		{
			assert(Memory);
			return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + Offset + 1);
		}

		void
		Set_index(unsigned Offset, uint_fast64_t const & Value) const
		{
			assert(Memory);
			new (reinterpret_cast<uint64_t *>(Memory) + Offset + 1) uint64_t(Value);
		}

		auto 
		Get_hash() const
		{
			assert(Memory);
			assert(!((reinterpret_cast<uintptr_t>(Memory) + Get_size_on_disk() - 8) % 8));
			assert(!(Get_size_on_disk() % 8));
			return synapse::misc::Get_alias_safe_value<uint32_t>(Memory + (Get_size_on_disk() - 8));
		}

		void
		Set_hash(uint_fast32_t const & Value) const
		{
			assert(Memory);
			assert(!((reinterpret_cast<uintptr_t>(Memory) + Get_size_on_disk() - 8) % 8));
			new (Memory + (Get_size_on_disk() - 8)) uint32_t(Value);
		}

		void
		Clear_verified_hash_status() const 
		{
			assert(Memory);
			new (Memory + (Get_size_on_disk() - 4)) uint32_t(0);
		}

		auto
		Get_verified_hash_status_begin() const
		{
			return Memory + (Get_size_on_disk() - 4);
		}

		char unsigned
		Atomicless_get_verified_hash_status(typename Memory_type_traits<Is_mutable, true>::Memory_type * Verified_hash_status_begin) const
		{
			return *reinterpret_cast<char unsigned const *>(Verified_hash_status_begin);
		}

		char unsigned
		Sequentially_consistent_atomic_get_verified_hash_status(typename Memory_type_traits<Is_mutable, true>::Memory_type * Verified_hash_status_begin) const
		{
			return synapse::misc::atomic_load_sequentially_consistent(Verified_hash_status_begin);
		}

		void
		Released_atomic_set_verified_hash_status(typename Memory_type_traits<Is_mutable, true>::Memory_type * Verified_hash_status_begin, char unsigned Value) const
		{
			synapse::misc::atomic_store_release(Verified_hash_status_begin, Value);
		}

		char unsigned
		Atomic_fetch_or_verified_hash_status(typename Memory_type_traits<Is_mutable, true>::Memory_type * Verified_hash_status_begin, char unsigned Value) const
		{
			return synapse::misc::atomic_fetch_or_relaxed(Verified_hash_status_begin, Value);
		}

		// complete, total message size (inclulding this very size variable), but NOT quantised (quantised is inferred as (size + 7) & ~static_cast<uint32_t>(7)
		auto 
		Get_size_on_wire() const 
		{
			return synapse::misc::Get_alias_safe_value<uint32_t>(Memory);
		}

		// rounded-up, quantized, version to 8 bytes
		auto 
		Get_size_on_disk() const noexcept
		{
			return synapse::misc::round_up_to_multiple_of_po2<uint_fast32_t>(Get_size_on_wire(), 8);
		}

		// TODO -- thisi rather very explicit/unmanaged, the size_on_wire should probably become just the payload size or similar...
		Basic_message_ptr(void * Memory, uint_fast32_t const size_on_wire)
		: Memory(static_cast<typename Memory_type_traits<Is_mutable, true>::Memory_type*>(Memory)) 
		{
			assert(Memory);
			new (Memory) uint32_t(size_on_wire);
			new (reinterpret_cast<uint32_t *>(Memory) + 1) uint32_t(database::Message_Indicies_Size);
		}

		Basic_message_ptr(typename Memory_type_traits<Is_mutable, true>::type * Memory)
		: Memory(static_cast<typename Memory_type_traits<Is_mutable, true>::Memory_type*>(Memory)) 
		{
		}

		Basic_message_ptr()
		: Memory(nullptr) 
		{
		}

		template <bool> friend class Basic_message_ptr;

		template <bool X>
		Basic_message_ptr(Basic_message_ptr<X> const & Construct_from) 
		: Memory(Construct_from.Memory)
		{
		}

		char unsigned const *
		Raw() const 
		{
			assert(Memory);
			return Memory;
		}

		void 
		Reset(typename Memory_type_traits<Is_mutable, true>::type * Memory = nullptr) 
		{
			this->Memory = static_cast<typename Memory_type_traits<Is_mutable, true>::Memory_type*>(Memory);
		}

		explicit operator bool() const 
		{
			return Memory != nullptr;
		}

		bool operator!() const 
		{
			return Memory == nullptr;
		}

		template <bool X>
		bool operator == (Basic_message_ptr<X> const & Compare_with) const
		{
			return Memory == Compare_with.Memory;
		}

		template <bool X>
		bool operator != (Basic_message_ptr<X> const & Compare_with) const
		{
			return Memory != Compare_with.Memory;
		}

		template <bool X>
		bool operator < (Basic_message_ptr<X> const & Compare_with) const
		{
			return Memory < Compare_with.Memory;
		}

		template <bool X>
		bool operator <= (Basic_message_ptr<X> const & Compare_with) const
		{
			return Memory <= Compare_with.Memory;
		}

		template <bool X>
		bool operator > (Basic_message_ptr<X> const & Compare_with) const
		{
			return Memory > Compare_with.Memory;
		}

		template <bool X>
		bool operator >= (Basic_message_ptr<X> const & Compare_with) const
		{
			return Memory >= Compare_with.Memory;
		}

		char unsigned const *
		Get_payload() const
		{
			return reinterpret_cast<char unsigned const *>(reinterpret_cast<uint64_t const *>(Memory) + 1 + Get_indicies_size());
		}

		char unsigned *
		Get_payload() 
		{
			return reinterpret_cast<char unsigned *>(reinterpret_cast<uint64_t *>(Memory) + 1 + Get_indicies_size());
		}

		auto 
		Get_payload_size() const
		{
			assert(reinterpret_cast<uintptr_t>(Get_payload()) - reinterpret_cast<uintptr_t>(Memory) == 8 * (1 + Get_indicies_size()));
			return Get_size_on_wire() - 8 * (2 + Get_indicies_size());
		}

		auto
		Calculate_hash() const noexcept
		{
			auto const * const Hashes_begin(Memory + Get_size_on_disk() - 8);
			uint64_t hash(0);
			for (auto const * i_ptr(Memory); i_ptr != Hashes_begin; i_ptr += 8) {
				assert(!(reinterpret_cast<uintptr_t>(i_ptr) % 8));
				hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(i_ptr));
			}
			return hash;
		}

		template <typename CallbackType>
		void
		Async_calculate_hash(CallbackType && callback) const noexcept
		{
			auto const * const Hashes_begin(Memory + Get_size_on_disk() - 8);
			Async_calculate_hash_impl(Memory, Hashes_begin, 0, ::std::forward<CallbackType>(callback));
		}

		Basic_message_ptr
		Get_next() const
		{
			return Basic_message_ptr(Memory + Get_size_on_disk());
		}

	private:
		template <typename CallbackType>
		void static
		Async_calculate_hash_impl(char unsigned const * const begin, char unsigned const * const end, uint64_t accumulated_hash, CallbackType && callback) noexcept
		{
			if (begin != end) {
				assert(end > begin);
				//assert(!(reinterpret_cast<uintptr_t>(begin) % sizeof(::std::remove_reference<decltype(*begin)>::type)));
				assert(!(reinterpret_cast<uintptr_t>(begin) % 8));
				assert(!(reinterpret_cast<uintptr_t>(end) % 8));

				auto tmp_end(begin + ::std::min(static_cast<uintptr_t>(end - begin), static_cast<uintptr_t>(1024 * 256)));
				assert(!(reinterpret_cast<uintptr_t>(tmp_end) % 8));

				assert(tmp_end > begin);
				for (auto const * i_ptr(begin); i_ptr != tmp_end; i_ptr += 8)
					accumulated_hash = synapse::misc::crc32c(accumulated_hash, synapse::misc::Get_alias_safe_value<uint64_t>(i_ptr));

				synapse::asio::io_service::get().post([tmp_end, end, accumulated_hash, callback]() mutable noexcept {
						Async_calculate_hash_impl(tmp_end, end, accumulated_hash, ::std::forward<CallbackType>(callback));
				});

			} else
				callback(accumulated_hash);
		}
	};
	typedef Basic_message_ptr<true> Message_ptr;
	typedef Basic_message_ptr<false> Message_const_ptr;
}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif

