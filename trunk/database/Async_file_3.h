
#include <string>

#include "../misc/misc.h"
//TODO move this endian file (it is pretty much re-written anyways)
#include "../amqp_0_9_1/foreign/copernica/endian.h"

#ifndef DATA_PROCESSORS_SYNAPSE_DATABASE_ASYNC_FILE_3_H
#define DATA_PROCESSORS_SYNAPSE_DATABASE_ASYNC_FILE_3_H


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
		namespace data_processors {
			namespace synapse {
				namespace database {
					namespace version3 {
					template <bool Is_mutable>
					class Basic_data_meta_ptr {
						template <bool, bool> struct Memory_type_traits;
						template<bool Ignored> struct Memory_type_traits<true, Ignored> { typedef void type; };
						template<bool Ignored> struct Memory_type_traits<false, Ignored> { typedef void const type; };

						unsigned constexpr static version_number{ 1 };

						typename Memory_type_traits<Is_mutable, true>::type * Memory;

					public:

#if DP_ENDIANNESS != LITTLE
#error current code only supports LITTLE_ENDIAN archs. -- extending to BIG_ENDIAN is trivial but there wasnt enough time at this stage...
#endif

						unsigned constexpr static size{ 56 };
						static_assert(!(size % 8), "meta size ought to be multiple of 64bits");

						void
							initialise() const
						{
							assert(Memory);
							new (Memory) uint64_t(version_number);
							set_data_size(0);
							Set_maximum_sequence_number(0);
							set_latest_timestamp(0);
							Set_begin_byte_offset(0);
							Set_target_data_consumption_limit(-1);
							rehash();
						}
						void static
							initialise_(Basic_data_meta_ptr meta)
						{
							meta.initialise();
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
						void
							Set_maximum_sequence_number(uint_fast64_t const & size) const
						{
							new (reinterpret_cast<uint64_t*>(Memory) + 2) uint64_t(size);
						}
						uint_fast64_t
							Get_maximum_sequence_number() const
						{
							return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 2);
						}
						void
							set_latest_timestamp(uint_fast64_t const & timestamp) const
						{
							new (reinterpret_cast<uint64_t*>(Memory) + 3) uint64_t(timestamp);
						}
						uint_fast64_t
							get_latest_timestamp() const
						{
							return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 3);
						}

						void
							Set_begin_byte_offset(uint_fast64_t const & Begin_byte_offset) const
						{
							new (reinterpret_cast<uint64_t*>(Memory) + 4) uint64_t(Begin_byte_offset);
						}
						uint_fast64_t
							Get_begin_byte_offset() const
						{
							return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 4);
						}

						void Set_target_data_consumption_limit(uint_fast64_t const & Target_limit) const {
							new (reinterpret_cast<uint64_t*>(Memory) + 5) uint64_t(Target_limit);
						}

						uint_fast64_t Get_target_data_consumption_limit() const {
							return synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const *>(Memory) + 5);
						}

						void
							verify_hash() const
						{
							uint64_t hash(synapse::misc::crc32c(synapse::misc::Get_alias_safe_value<uint64_t>(Memory), synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1)));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 5));
							if ((synapse::misc::Get_alias_safe_value<uint64_t>(Memory) & 0xffffffff) != version_number || hash != synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 6))
								throw ::std::runtime_error("Basic_data_meta_ptr hash verification in index file has failed");
						}
						void
							rehash() const
						{
							uint64_t hash(synapse::misc::crc32c(synapse::misc::Get_alias_safe_value<uint64_t>(Memory), synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 1)));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 2));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 3));
							hash = synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 4));
							new (reinterpret_cast<uint64_t*>(Memory) + 6) uint64_t(synapse::misc::crc32c(hash, synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(Memory) + 5)));
						}

						typedef Basic_data_meta_ptr<false> Constant_type;

						Basic_data_meta_ptr(typename Memory_type_traits<Is_mutable, true>::type * Memory)
							: Memory(Memory)
						{
						}

						template <bool> friend class Basic_data_meta_ptr;

						template <bool X>
						Basic_data_meta_ptr(Basic_data_meta_ptr<X> const & Construct_from)
							: Memory(Construct_from.Memory) {
						}

						template <bool X>
						bool operator == (Basic_data_meta_ptr<X> const & Compare_with) const
						{
							return Memory == Compare_with.Memory;
						}

					};
					typedef Basic_data_meta_ptr<true> Data_meta_ptr;
					typedef Basic_data_meta_ptr<false> Data_meta_const_ptr;
				}
			}
		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
