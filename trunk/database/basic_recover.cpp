// todo put those into precompiler header when ready to make one
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include <thrift/Thrift.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TCompactProtocol.h>

#include "../Version.h"

#include "../misc/misc.h"

// Todo: refactor the headers structures because this is not really needed to be in the basic_recover.exe (I think)... so just putting it here for the time being to make it build.
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
#if defined(__GNUC__) && !defined(__clang__)
	// otherwise clang complains of unused variable, GCC however wants it to be present.
	::std::atomic_uint_fast64_t static Database_size{0};
#endif
int static nice_performance_degradation = 0;
unsigned constexpr static page_size = 4096;
}}}
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
data_processors::misc::alloc::Large_page_preallocator<synapse::database::page_size> static Large_page_preallocator;
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "message.h"
#include "index.h"

/*
Currently, I think, idea may well be that each server functions on its own native arch. If database migration is needed, then use sub/pub mechanisms of mirror-maker, etc.
Todo for the future: if using this util to convert between enidanness then cater of be64toh et. al.
*/
#if DP_ENDIANNESS != LITTLE
#error Only works on little-endian systems for the time being.
#endif 

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace database {

unsigned constexpr Delta_flag_rewrite_extra_space(8); // Really a ballpark thingy: to allow for rare yet possible (if considering TCompactProtocol future changes) difference in size when/if rewriting delta poison bit in Thrift flags.


// todo -- import from commonly-shared header file or specify at CLI level
unsigned constexpr Message_boundaries_key_size(24);
unsigned constexpr msg_boundaries_quantize_shift(19);
// Note: sequence number index is no longer done.
unsigned constexpr Time_key_size(16);
unsigned constexpr timestamp_quantize_shift(20);

static_assert(msg_boundaries_quantize_shift <= sizeof(uint64_t) * 8, "Quantise shift mask exceeds 64bit int width (undefined behaviour).");
// Note: sequence number index is no longer done.
static_assert(timestamp_quantize_shift <= sizeof(uint64_t) * 8, "Quantise shift mask exceeds 64bit int width (undefined behaviour).");

uint_fast64_t constexpr msg_boundaries_key_mask(synapse::misc::po2_mask<uint_fast64_t>(1u << msg_boundaries_quantize_shift));
// Note: sequence number index is no longer done.
uint_fast64_t constexpr timestamp_key_mask(synapse::misc::po2_mask<uint_fast64_t>(1u << timestamp_quantize_shift));

struct Message_processing_helper {
	template <unsigned Total_buffer_capacity>
	void static Rewrite_delta_poison(database::Message_ptr Message, bool const On) {

		// Getting to the mutable region (thrift envelope header)
		auto Thrift_buffer(::boost::make_shared<::apache::thrift::transport::TMemoryBuffer>());
		Thrift_buffer->resetBuffer(Message.Get_payload() + 4, // '4' is to skip the AMQP's frame-field 'payload size' which we store in our database message (and which is a part of the payload as far as Synapse is concerned).
			Message.Get_payload_size() - 4, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::OBSERVE
		);

		int32_t Flags;
		{
			::apache::thrift::protocol::TCompactProtocol Thrift_protocol(Thrift_buffer); 
			int8_t Version;
			Thrift_protocol.readByte(Version);
			if (static_cast<uint8_t>(Version) != 1u)
				throw ::std::runtime_error("Unsupported version number in our message envelope.");
			Thrift_protocol.readI32(Flags);
		}
		auto const Mutable_region_size(Thrift_buffer->readEnd());

		// Setup the new (rewritten) flags buffer
		auto Thrift_rewritten_buffer(::boost::make_shared<::apache::thrift::transport::TMemoryBuffer>(Mutable_region_size + database::Delta_flag_rewrite_extra_space));
		::apache::thrift::protocol::TCompactProtocol Thrift_protocol(Thrift_rewritten_buffer); 
		Thrift_protocol.writeByte(1);
		if (On)
			Thrift_protocol.writeI32(Flags | 0x10);
		else
			Thrift_protocol.writeI32(Flags & ~static_cast<uint32_t>(0x10));
		uint8_t * Mutable_region_Rewritten_buffer;
		uint32_t Mutable_region_Rewritten_size;
		Thrift_rewritten_buffer->getBuffer(&Mutable_region_Rewritten_buffer, &Mutable_region_Rewritten_size);


		// All (including our indicies and some AMQP stuff) up to the Thrift flags field.
		auto const Prefix_region_begin(reinterpret_cast<uintptr_t>(Message.Raw())); 
		auto const Prefix_region_size(reinterpret_cast<uintptr_t>(Message.Get_payload()) - Prefix_region_begin + 4);

		// Flags field (on wire -- not necessarily 4 bytes)
		auto const Mutable_region_begin(Prefix_region_begin + Prefix_region_size);
		assert(Message.Get_payload_size() > Mutable_region_size + 4);

		// Unlikely, but should be taken care of.
		if (Mutable_region_Rewritten_size != Mutable_region_size) {


			// essentially everything of the original payload-data which is at the end of the Mutable_region
			auto const Post_mutable_region_size(Message.Get_payload_size() - Mutable_region_size - 4); 

			// bytes offset from start of the message up to the start of CRC location (still unaligned though)...
			auto const Rewritten_pre_crc_size(Prefix_region_size + Mutable_region_Rewritten_size + Post_mutable_region_size);

			// ... find CRC-32 8-byte aligned offset
			auto const Crc_offset_begin(synapse::misc::round_up_to_multiple_of_po2<uint_fast64_t>(Rewritten_pre_crc_size, 8));
			assert(!(Crc_offset_begin % 8));

			// Total new message size
			if (Crc_offset_begin + 8 > Total_buffer_capacity) // No buffer overruns please
				throw ::std::runtime_error("Crc_offset_begin + 8 > Total_buffer_capacity");

			// Park/move post mutable amount of data.
			::memmove(reinterpret_cast<void*>(Mutable_region_begin + Mutable_region_Rewritten_size), reinterpret_cast<void const *>(Mutable_region_begin + Mutable_region_size), Post_mutable_region_size);


			// Update various size fields:
			// ... for the AMQP native field (AMQP's payload is the Thrift payload, and it begins where Mutable_region begins),
			auto const Amqp_payload_size(Post_mutable_region_size + Mutable_region_Rewritten_size - 1); // '-1' because end-octet is not inclnuded in AMQP payload-size field

			auto Payload_ptr(Message.Get_payload());
			new (Payload_ptr) uint32_t(htobe32(Amqp_payload_size));

			// ... and for the Synapse native field (hack really, slightly tentative to either make Raw() non-const, or add method to Set_size_on_wire directly... time will tell. 
			assert(reinterpret_cast<char unsigned *>(reinterpret_cast<uint64_t *>(Payload_ptr) - 1 - Message.Get_indicies_size()) == Message.Raw());
			new (reinterpret_cast<char unsigned *>(reinterpret_cast<uint64_t *>(Payload_ptr) - 1 - Message.Get_indicies_size())) uint32_t(Rewritten_pre_crc_size + 8);

		}

		// Splice-in rewritten data.
		::memcpy(reinterpret_cast<void*>(Mutable_region_begin), reinterpret_cast<void const *>(Mutable_region_Rewritten_buffer), Mutable_region_Rewritten_size);

		// Now update hash/etc.
		Message.Set_hash(Message.Calculate_hash());
		Message.Clear_verified_hash_status();
	}
};

void static
Seek_file(::std::fstream & File, uint_fast64_t Offset) {
	File.clear();
	File.seekg(Offset);
	File.seekp(Offset);
}

template <typename Ofstream_or_fstream_concept>
void static
pad_file(Ofstream_or_fstream_concept & file)
{
	uint_fast64_t const current_size(file.tellp());
	auto const target_size(synapse::misc::round_up_to_multiple_of_po2<decltype(current_size)>(current_size, page_size));
	auto const pad_size(target_size - current_size);
	if (pad_size) {
		char const static bfr[page_size] = {};
		if (!file.write(bfr, pad_size))
			throw ::std::runtime_error("error padding file");
	}
}

template <unsigned Key_size, unsigned Message_start_within_index_key, unsigned Index_buffer_size, unsigned Quantize_shift, uint64_t Key_mask>
struct Index_processing_helper {
	uint_fast64_t Last_quantized_key;
	::std::string File_path;
	::std::string Meta_path;
	::std::unique_ptr<char> Buffer; 
	::std::fstream File; 
	::std::unique_ptr<char> Buffer_meta; 
	database::Index_meta_ptr Meta; 
	::std::fstream File_meta;
	uint64_t First_index_key[Key_size / sizeof(uint64_t)];
	Index_processing_helper(::std::string const & File_path, ::std::string const & Meta_path, uint_fast64_t & Initial_recovery_from_offset, uint_fast64_t const Initial_recovery_rewind_by_key_value = 0) : File_path(File_path), Meta_path(Meta_path), Buffer(new char[Index_buffer_size]), Buffer_meta(new char[database::Index_meta_ptr::size]), Meta(Buffer_meta.get()) {
		File.rdbuf()->pubsetbuf(Buffer.get(), Index_buffer_size);
		auto Open_mode(::std::ios::in | ::std::ios::out | ::std::ios::binary);
		assert(::boost::filesystem::exists(File_path));
		File.open(File_path.c_str(), Open_mode);
		// Start with metadata.
		if (!::boost::filesystem::exists(Meta_path))
			Open_mode |= ::std::ios::trunc;
		File_meta.open(Meta_path.c_str(), Open_mode);
		if (Initial_recovery_from_offset) try {
			File_meta.read(Buffer_meta.get(), database::Index_meta_ptr::size);
			// ... currently supporting 'from middle' recovery only if at least valid metadata blocks are available
			// TODO later on can add additional 'scrubbing' to reconstruct metadata from raw file contents...
			Meta.verify_hash();
			if (!File_meta || Meta.get_quantise_shift_mask() != Quantize_shift || Meta.get_key_entry_size() != Key_size || Meta.Get_start_of_file_key() == static_cast<decltype(Meta.Get_start_of_file_key()) const>(-1))
				throw 0;
		} catch (...) {
			synapse::misc::log(Meta_path + " scan indicates invalid metadata\n", true);
			Initial_recovery_from_offset = 0;
		}
		if (Initial_recovery_from_offset) {
			// Infer the key offset from the 'recovery from' value for the index
			auto Last_key_value(Meta.Get_start_of_file_key() + (Meta.get_data_size() / Key_size - 1 << Quantize_shift));
			Last_key_value -= ::std::min(Initial_recovery_rewind_by_key_value & Key_mask, Last_key_value - Meta.Get_begin_key());

			auto Last_key_offset(Key_size * (
				( // Number of key entries to the 'recover from' from start of file
					Last_key_value - Meta.Get_start_of_file_key()  // First key value
					>> Quantize_shift
				) 
			));
			uint_fast64_t Sparseness_end_offset(Key_size * (Meta.Get_begin_key() - Meta.Get_start_of_file_key() >> Quantize_shift));
			if (!(Last_key_offset % Key_size) && !(Sparseness_end_offset % Key_size)) {
				// Check sparse validity...
				Seek_file(File, Sparseness_end_offset);
				File.read(reinterpret_cast<char *>(First_index_key), Key_size);
				if (File) {
					// verify hash/crc
					auto const * const Hash_on_disk_ptr(First_index_key + (sizeof(First_index_key) / sizeof(uint64_t) - 1));
					auto const Hash_on_disk(*Hash_on_disk_ptr);
					uint64_t Hash(0);
					for (uint64_t const * I_ptr(First_index_key); I_ptr != Hash_on_disk_ptr; ++I_ptr)
						Hash = synapse::misc::crc32c(Hash, *I_ptr);
					if (Hash != Hash_on_disk) {
						synapse::misc::log(File_path + " first index value after sparsification does not compute\n", true);
						Initial_recovery_from_offset = 0;
					}
				} else {
					synapse::misc::log(File_path + " index file could not be read at the sparsification end mark\n", true);
					Initial_recovery_from_offset = 0;
				}
				if (Initial_recovery_from_offset) {
					// Wind back until a valid key is found.  
					Sparseness_end_offset -= Key_size;
					uint64_t Index_key[Key_size / sizeof(uint64_t)];
					do {
						Seek_file(File, Last_key_offset);
						File.read(reinterpret_cast<char *>(Index_key), Key_size);
						if (File) {
							// verify hash/crc
							auto const * const Hash_on_disk_ptr(Index_key + (sizeof(Index_key) / sizeof(uint64_t) - 1));
							auto const Hash_on_disk(*Hash_on_disk_ptr);
							uint64_t Hash(0);
							for (uint64_t const * I_ptr(Index_key); I_ptr != Hash_on_disk_ptr; ++I_ptr)
								Hash = synapse::misc::crc32c(Hash, *I_ptr);
							if (Hash == Hash_on_disk) {
								Initial_recovery_from_offset = ::std::min<uint_fast64_t>(Index_key[Message_start_within_index_key], Initial_recovery_from_offset);
								break;
							}
						}
					} while ((Last_key_offset -= Key_size) != Sparseness_end_offset);
					if (Last_key_offset == Sparseness_end_offset) {
						synapse::misc::log(File_path + " scan indicates no valid place to park\n", true);
						Initial_recovery_from_offset = 0;
					}
				}
			} else { 
				synapse::misc::log(File_path + " various metadata values do not compute\n", true);
				Initial_recovery_from_offset = 0;
			}
		}
	}
	bool Park(uint_fast64_t const & Key) {
		assert(Meta.Get_start_of_file_key() != static_cast<decltype(Meta.Get_start_of_file_key()) const>(-1));
		assert(Meta.Get_begin_key() != static_cast<decltype(Meta.Get_start_of_file_key()) const>(-1));
		assert(Meta.Get_begin_key() >= Meta.Get_start_of_file_key());
		Last_quantized_key = Key & Key_mask;
		if (Last_quantized_key >= Meta.Get_begin_key()) {
			auto const Target_data_size(Key_size * ((Last_quantized_key - Meta.Get_start_of_file_key() >> Quantize_shift) + 1));
			if (Meta.get_data_size() >= Target_data_size) {
				Meta.set_data_size(Target_data_size);
				assert(File_meta);
				Seek_file(File, Target_data_size);
				return true;
			}
		}
		synapse::misc::log(Meta_path + " indicates that index cannot be parked ok\n", true);
		return false;
	}
	template <typename Callback_type>
	void Update_latest_key(uint_fast64_t Key, Callback_type && Callback) {
		decltype(Key) Temporary_key;
		auto const Incoming_quantized_key(Key & Key_mask);
		assert(Incoming_quantized_key != static_cast<decltype(Incoming_quantized_key)>(-1));
		if (Incoming_quantized_key != Last_quantized_key) {
			if (Last_quantized_key == static_cast<decltype(Last_quantized_key)>(-1))  {
				Meta.Set_start_of_file_key(Incoming_quantized_key);
				Meta.Set_begin_key(Incoming_quantized_key);
				Temporary_key = Incoming_quantized_key - (1u << Quantize_shift);
			} else
				Temporary_key = Last_quantized_key;
			Last_quantized_key = Incoming_quantized_key;

			uint_fast32_t const Number_of_entries((Incoming_quantized_key - Temporary_key) >> Quantize_shift);
			for (unsigned i(0); i != Number_of_entries; ++i) {
				Meta.set_data_size(Meta.get_data_size() + Key_size);

				auto constexpr Index_fields_size(Key_size / sizeof(uint64_t));
				uint64_t Index_key[Index_fields_size];
				Callback(Index_key);

				Temporary_key += (1u << Quantize_shift);

				assert(Index_fields_size > 1);
				auto constexpr Index_fields_before_crc(Index_fields_size - 1);
				assert(Index_fields_before_crc);
				uint64_t Hash(0);
				for (unsigned i(0); i != Index_fields_before_crc; ++i)
					Hash = synapse::misc::crc32c(Hash, Index_key[i]);
				Index_key[Index_fields_before_crc] = Hash;

				if (!File.write(reinterpret_cast<char const *>(Index_key), Key_size))
					throw ::std::runtime_error("error could not write index data for : " + File_path);
			}
			assert(Temporary_key == Incoming_quantized_key);
		}
	}
	void Flush_meta() {
		Seek_file(File_meta, 0);
		Meta.rehash();
		if (!File_meta.write(Buffer_meta.get(), database::Index_meta_ptr::size))
			throw ::std::runtime_error("error could not write index meta for : " + Meta_path);
	}
	void Resize_files_on_close() {
		uint_fast64_t current_size(File.tellp());
		File.close();
		::boost::filesystem::resize_file(File_path, current_size);
		current_size = File_meta.tellp();
		File_meta.close();
		::boost::filesystem::resize_file(Meta_path, current_size);
	}
	void Initialize() {
		Meta.initialise(Quantize_shift, Key_size);
		Last_quantized_key = -1;
		Seek_file(File, 0);
		Seek_file(File_meta, 0);
	}
};

struct Scoped_file_handle {
	HANDLE File_handle;
	Scoped_file_handle(::std::string const & File_path, uint_fast64_t const Sparsification_end = 0) 
	: File_handle(::CreateFile(
			File_path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, 
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		)) {
		if (File_handle == INVALID_HANDLE_VALUE)
			throw ::std::runtime_error("CreateFile (=" + File_path + ')');
		DWORD Tmp;
		if (!::DeviceIoControl(File_handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &Tmp, NULL)) {
			::CloseHandle(File_handle);
			throw ::std::runtime_error("FSCTL_SET_SPARSE (=" + File_path + ')');
		}
		if (Sparsification_end) {

			auto const Adjusted_Sparsification_End(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(Sparsification_end, database::page_size));
			assert(!(Adjusted_Sparsification_End % database::page_size));

			::FILE_ZERO_DATA_INFORMATION Meta_zero_data;
			Meta_zero_data.FileOffset.QuadPart = 0;
			Meta_zero_data.BeyondFinalZero.QuadPart = Adjusted_Sparsification_End;
			DWORD Tmp;
			if (!::DeviceIoControl(File_handle, FSCTL_SET_ZERO_DATA, &Meta_zero_data, sizeof(Meta_zero_data), NULL, 0, &Tmp, NULL)) 
				throw ::std::runtime_error("error could not sparsify FSCTL_SET_ZERO_DATA (=" + File_path + ')');
	}
	}
	~Scoped_file_handle() {
		::CloseHandle(File_handle);
	}
};


int static
run(int argc, char* argv[])
{
	try
	{
		synapse::misc::log(">>>>>>> BUILD info: " BUILD_INFO " main source: " __FILE__ ". date: " __DATE__ ". time: " __TIME__ ".\n", true);

		unsigned concurrency_hint(0);

		::boost::filesystem::path src_topics_path;
		::boost::filesystem::path dst_topics_path;

		enum Rewrite_sequence_numbers_mode : unsigned { None = 0, Global_reset, Local_reset, Local_continuation};
		Rewrite_sequence_numbers_mode Rewrite_sequence_numbers{None};

		uint_fast64_t Recover_from_eof(0);
		bool Sparsify_only(false);
		bool Inhibit_delta_poisoning(true);

		for (int i(1); i != argc; ++i) {
			if (!::strcmp(argv[i], "--sparsify_only"))
				Sparsify_only = true;
			else if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!::strcmp(argv[i], "--src"))
				src_topics_path = argv[++i];
			else if (!::strcmp(argv[i], "--dst")) 
				dst_topics_path = argv[++i];
			else if (!::strcmp(argv[i], "--rewrite_sequence_numbers")) {
				::std::string Value(argv[++i]);
				::boost::algorithm::to_lower(Value);
				if (Value == "none")
					Rewrite_sequence_numbers = None;
				else if (Value == "global_reset")
					Rewrite_sequence_numbers = Global_reset;
				else if (Value == "local_reset")
					Rewrite_sequence_numbers = Local_reset;
				else if (Value == "local_continuation")
					Rewrite_sequence_numbers = Local_continuation;
				else
					throw ::std::runtime_error("Unknown option for Rewrite_sequence_numbers: " + Value);
			} else if (!::strcmp(argv[i], "--recover_from_eof"))
				Recover_from_eof = synapse::misc::Parse_string_as_suffixed_integral_duration<uint_fast64_t>(argv[++i]);
			else if (!::strcmp(argv[i], "--concurrency_hint"))
				concurrency_hint = ::boost::lexical_cast<unsigned>(argv[++i]);
			else if (!::strcmp(argv[i], "--inhibit_delta_poisoning")) {
				::std::string Value(argv[++i]);
				::boost::algorithm::to_lower(Value);
				if (Value == "true")
					Inhibit_delta_poisoning = true;
				else if (Value == "false")
					Inhibit_delta_poisoning = false;
				else
					throw ::std::runtime_error("Unknown option for inhibit_delta_poisoning: " + Value);
			} else
				throw ::std::runtime_error("Unknown option: " + ::std::string(argv[i]));
		}

		// create based on chosen runtime parameters
		if (!concurrency_hint)
			concurrency_hint = synapse::misc::concurrency_hint();
		asio::io_service::init(concurrency_hint);

		if (src_topics_path.empty() || dst_topics_path.empty())
			throw ::std::runtime_error("both --src and --dst args are needed");

		bool const In_place_recovery(dst_topics_path == src_topics_path);

		if (!In_place_recovery) {
			if (::boost::filesystem::exists(dst_topics_path))
				throw ::std::runtime_error("destination database path already exists");
			::boost::filesystem::create_directory(dst_topics_path);
		} else if (Rewrite_sequence_numbers != None)
			throw ::std::runtime_error("--rewrite_sequence_numbers must be none (or not specified at all) if/when in-place recovery is used.");

		::std::atomic_uint_fast64_t Global_maximum_sequence_number{0};

		// for every subdir in source dir
		uint_fast64_t topics_size(0);
		for (::boost::filesystem::directory_iterator i(src_topics_path); i != ::boost::filesystem::directory_iterator(); ++i) {
			++topics_size;

			auto const src_topic_path(i->path());
			synapse::asio::io_service::get().post([&Rewrite_sequence_numbers, &Recover_from_eof, &Sparsify_only, &Inhibit_delta_poisoning, src_topic_path, dst_topics_path, &Global_maximum_sequence_number, In_place_recovery]() {
				try {
					auto constexpr Total_message_buffer_size(MaxMessageSize + database::Delta_flag_rewrite_extra_space); 

					::std::unique_ptr<char> msg_buffer(new char[Total_message_buffer_size]); 

					auto const & topic_name(src_topic_path.filename().string());
					synapse::misc::log(topic_name  + " scan started\n", true);

					auto const dst_topic_path(dst_topics_path / topic_name);

					// 0 means recover from start
					uint_fast64_t Initial_recovery_from_offset(Recover_from_eof && In_place_recovery ? -1 : 0); 

					if (!In_place_recovery) 
						::boost::filesystem::create_directory(dst_topic_path);
					else if (Sparsify_only) {
						char Buffer_data_meta[database::Data_meta_ptr::size];
						database::Data_meta_ptr const Data_meta(Buffer_data_meta);
						auto const Path_prefix((dst_topic_path / "data").string());
						auto const File_data_meta_path(Path_prefix + "_meta");
						::std::ifstream File_data_meta(File_data_meta_path.c_str(), ::std::ios::binary);
						if (File_data_meta.read(Buffer_data_meta, database::Data_meta_ptr::size)) {
							Data_meta.verify_hash();
							auto const Sparsification_End(Data_meta.Get_begin_byte_offset());
							if (!(Sparsification_End % 8)) {
								// File data is our last resourt, so we are a bit more careful about truncating it without validating 1st message... 
								if (Sparsification_End) {
									::std::ifstream File_data(Path_prefix, ::std::ios::binary);

									// Wind out through zeros to make sure that all are indeed zeros (TODO, in future make this an configurable value of the Sparsify_only option so that users can opt not to scan the region as this could be slow for largely truncated data).
									// Currently going back by max message size
									auto const Start_Of_Scan(Sparsification_End - ::std::min<uint_fast64_t>(MaxMessageSize, Sparsification_End));
									if (Start_Of_Scan % 8)
										throw ::std::runtime_error("Incorrect start of scan when sparsifying-only. file " + Path_prefix);
									File_data.seekg(Start_Of_Scan);
									uint64_t Expected_Zero;
									while (File_data.read(reinterpret_cast<char *>(&Expected_Zero), 8) && !Expected_Zero);

									auto const Adjusted_Sparsification_End(synapse::misc::round_down_to_multiple_of_po2<uint_fast64_t>(Sparsification_End, database::page_size));
									assert(!(Adjusted_Sparsification_End % database::page_size));
									if (static_cast<decltype(Adjusted_Sparsification_End)>(File_data.tellg()) < Adjusted_Sparsification_End) 
										throw ::std::runtime_error("Non-zero data before metadata implied Sparsification_End. file " + Path_prefix);

									File_data.seekg(Sparsification_End);
									if (File_data.read(msg_buffer.get(), 8)) {
										database::Message_const_ptr const msg(msg_buffer.get());
										auto const msg_size_on_disk(msg.Get_size_on_disk());
										if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= MaxMessageSize) {
											auto const Extra_bytes_to_read(msg_size_on_disk - 8);
											if ((Extra_bytes_to_read && !File_data.read(msg_buffer.get() + 8, Extra_bytes_to_read)) || msg.Get_hash() != msg.Calculate_hash())
												throw ::std::runtime_error("1st message does not appear to contain vaild data. file " + Path_prefix);
										} else
											throw ::std::runtime_error("1st message cannot be read properly. file " + Path_prefix);
									} else
										throw ::std::runtime_error("error could not read " + Path_prefix);
								}
								Scoped_file_handle(Path_prefix, Sparsification_End);
							} else
								throw ::std::runtime_error("incorrect Sparsification_end for " + Path_prefix);
						} else
							throw ::std::runtime_error("error could not read " + File_data_meta_path);
						auto Sparsify_index_helper([](auto const & Path_prefix, auto Key_size, auto Quantize_shift) {
							char Buffer_meta[database::Index_meta_ptr::size];
							database::Index_meta_ptr Meta(Buffer_meta); 
							auto const File_data_meta_path(Path_prefix + "_meta");
							::std::ifstream File_meta(File_data_meta_path.c_str(), ::std::ios::binary);
							if (File_meta.read(Buffer_meta, database::Index_meta_ptr::size)) {
								Meta.verify_hash();
								auto const Sparsification_End(Key_size * (Meta.Get_begin_key() - Meta.Get_start_of_file_key() >> Quantize_shift));
								if (!(Sparsification_End % 8))
									Scoped_file_handle(Path_prefix, Sparsification_End);
							} else
								throw ::std::runtime_error("error could not read " + File_data_meta_path);
						});
						Sparsify_index_helper((dst_topic_path / "index_0_data").string(), Message_boundaries_key_size, msg_boundaries_quantize_shift);
						// Note: sequence number index is no longer done.
						Sparsify_index_helper((dst_topic_path / "index_2_data").string(), Time_key_size, timestamp_quantize_shift);
						return; 
					} else if (!Initial_recovery_from_offset) {
						// Todo more efficient reuse of src_topic_path
						assert(src_topic_path == dst_topic_path);
						for (::boost::filesystem::directory_iterator i(dst_topic_path); i != ::boost::filesystem::directory_iterator(); ++i) {
							if (i->path().filename().string() != "data") {
								::boost::filesystem::remove_all(i->path());
							}
						}
					}

					// Here set all relevant files (data) as sparse...
					{ Scoped_file_handle((dst_topic_path / "data").string()); }
					{ Scoped_file_handle((dst_topic_path / "index_0_data").string()); }
					// Note: Sequence number index appropriation is no longer done
					{ Scoped_file_handle((dst_topic_path / "index_2_data").string()); }

					/*
						NOTE: current 'robustness' strategy is to use only actual data file contents. because there is no 'valid end' of data in such a file (one uses data_meta file for this), the end of file is determined implicitly by virtue of non-matching crc32 and or 0-size message size (or end of file in case of delta-poison inference (skipping over holes)).
						Probability-wise this could carry false-positive -- where inappropriate message is introduced at the end of the stream.
						An alternative would be to use 'data_meta' file which carries 'length' of the actual data. But such field may itself be corrupt -- implying that in such cases one may get false negative and not recover all of the available data (and the danger of false positive would still exist).
						... a TODO for the future to come-up with a more robust recovery method (albeit CRC32 checking is indeed the baseline of correctness anyways -- the false-positivity is essintially a data collision and, if so, such would also be at the core of the synapse runtime... so it would seem its always a matter of probability-vs-performance tradeoff).
					*/
					::std::fstream File_data_input;
					// TODO write own buffered io objects, pubsetbuf appears to degrade badly when seekg et.al. are used (as if buf obj gets invalidated with subsequent advancing by 8 bytes becoming much slower, etc.)
					// ... for the time being this is only used during initial non-streaming-yet part (resetting on any thrown exception)...
					bool File_Data_Input_Has_Large_Buffer{true};
					unsigned constexpr Buffer_data_size(35 * 1024 * 1024);
					::std::unique_ptr<char> Buffer_data(new char[Buffer_data_size]);
					File_data_input.rdbuf()->pubsetbuf(Buffer_data.get(), Buffer_data_size);
					File_data_input.open((src_topic_path / "data").string().c_str(), ::std::ios::in | ::std::ios::binary);
					if (File_data_input) {

						::std::unique_ptr<char> Buffer_data_meta(new char[database::Data_meta_ptr::size]);
						database::Data_meta_ptr const Data_meta(Buffer_data_meta.get());
						auto const File_data_meta_path(dst_topic_path / "data_meta");
						auto Open_mode(::std::ios::in | ::std::ios::out | ::std::ios::binary);
						if (!::boost::filesystem::exists(File_data_meta_path))
							Open_mode |= ::std::ios::trunc;
						::std::fstream File_data_meta(File_data_meta_path.string().c_str(), Open_mode);

						if (Initial_recovery_from_offset) try {
							File_data_meta.read(Buffer_data_meta.get(), database::Data_meta_ptr::size);
							// ... currently supporting 'from middle' recovery only if at least valid metadata blocks are available
							// TODO later on can add additional 'scrubbing' to reconstruct metadata from raw file contents...
							Data_meta.verify_hash();
							if (!File_data_meta)
								throw 0;

							// Begin byte offset must point to the valid message.
							Seek_file(File_data_input, Data_meta.Get_begin_byte_offset());
							if (File_data_input.read(msg_buffer.get(), 8)) {
								database::Message_const_ptr const msg(msg_buffer.get());
								auto const msg_size_on_disk(msg.Get_size_on_disk());
								if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= MaxMessageSize) {
									auto const Extra_bytes_to_read(msg_size_on_disk - 8);
									if ((Extra_bytes_to_read && !File_data_input.read(msg_buffer.get() + 8, Extra_bytes_to_read)) || msg.Get_hash() != msg.Calculate_hash())
										throw 0;
								} else
									throw 0;
							} else
								throw 0;
						} catch (...) {
							synapse::misc::log(File_data_meta_path.string() + " scan indicates invalid metadata\n", true);
							Initial_recovery_from_offset = 0;
						}

						// Message boundaries index appropriation
						Index_processing_helper<Message_boundaries_key_size, 0, 64 * 1024, msg_boundaries_quantize_shift, msg_boundaries_key_mask> Message_boundaries_index((dst_topic_path / "index_0_data").string(), (dst_topic_path / "index_0_data_meta").string(), Initial_recovery_from_offset);

						// Note: Sequence number index appropriation is no longer done

						// Timestamp index appropriation...
						Index_processing_helper<Time_key_size, 0, 8 * 1024 * 1024, timestamp_quantize_shift, timestamp_key_mask> Timestamp_index((dst_topic_path / "index_2_data").string(), (dst_topic_path / "index_2_data_meta").string(), Initial_recovery_from_offset, Recover_from_eof);

						if (Initial_recovery_from_offset) {
							bool Parked_ok(false);

							// all indicies pointing to the right/valid message...
							if (Message_boundaries_index.First_index_key[0] >= Data_meta.Get_begin_byte_offset()
								// Note: Sequence number index is no longer done
								&& Timestamp_index.First_index_key[0] >= Data_meta.Get_begin_byte_offset()
							) {
							if (Data_meta.Get_begin_byte_offset() <= Initial_recovery_from_offset) {
								Seek_file(File_data_input, Initial_recovery_from_offset);
								if (File_data_input.read(msg_buffer.get(), 8)) {
									database::Message_const_ptr const msg(msg_buffer.get());
									auto const msg_size_on_disk(msg.Get_size_on_disk());
									if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= MaxMessageSize) {
										auto const Extra_bytes_to_read(msg_size_on_disk - 8);
										if ((!Extra_bytes_to_read || File_data_input.read(msg_buffer.get() + 8, Extra_bytes_to_read)) && msg.Get_hash() == msg.Calculate_hash()) {
											if (msg.Get_indicies_size() ==  database::Message_Indicies_Size) {

												Data_meta.set_data_size(File_data_input.tellg());
												Data_meta.Set_maximum_sequence_number(msg.Get_index(0));
												Data_meta.set_latest_timestamp(msg.Get_index(1));

												if (Message_boundaries_index.Park(File_data_input.tellg()) 
													// Note: sequence number index is no longer done.
													&& Timestamp_index.Park(msg.Get_index(1))
												) {
													assert(File_data_input);
													assert(File_data_meta);
													Parked_ok = true;
												}
											}
										}
									}
								}
							}
							}
							if (!Parked_ok) {
								Initial_recovery_from_offset = 0;
								synapse::misc::log(topic_name + " was not parked ok\n", true);
							}
						}

						if (!Initial_recovery_from_offset) {
							Data_meta.initialise();
							Seek_file(File_data_input, 0);
							Seek_file(File_data_meta, 0);
							Message_boundaries_index.Initialize();
							// Note: sequence number index is no longer done.
							Timestamp_index.Initialize();
							if (Recover_from_eof)
								synapse::misc::log(topic_name + " info/error " + topic_name + " is NOT being recovered 'closer to EOF'!\n", true);
						}

						::std::fstream File_data_output;
						//File_data_output.rdbuf()->pubsetbuf(Output_buffer_data.get(), Buffer_data_size);
						File_data_output.open((dst_topic_path / "data").string().c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary); // if using ofstream the file is truncated by default and this gets rid of the sparse attribute (at least on windows)...
						if (In_place_recovery)
							Seek_file(File_data_output, File_data_input.tellg());

						uint_fast64_t Last_Known_Timestamp(0);
						uint_fast64_t Last_Known_Sequence_Number(0);

						uint_fast64_t Local_maximum_sequence_number{1};
						uint_fast64_t Maximum_sequence_number{0};

						uint_fast64_t Messages_size(0);

						bool Is_streaming{!!Initial_recovery_from_offset}; // without !! Visual studio errors with narrowing auto conversion... optimizer should make it a noop.

						uint_fast64_t Delta_Poison_Triggers(0);
						bool Rewrite_delta_poison(false);
						while (!0) {
							assert(!Rewrite_delta_poison || Is_streaming);
							static_assert(sizeof(uint64_t) == 8, "sizeof(uint64_t)");
							if (File_data_input.read(msg_buffer.get(), 8)) {
								uint_fast64_t Current_file_byte_offset(File_data_input.tellg());
								try {
									database::Message_ptr const msg(msg_buffer.get());
									auto const msg_size_on_disk(msg.Get_size_on_disk());
									if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= MaxMessageSize) {
										auto const Extra_bytes_to_read(msg_size_on_disk - 8);
										if ((!Extra_bytes_to_read || File_data_input.read(msg_buffer.get() + 8, Extra_bytes_to_read)) && msg.Get_hash() == msg.Calculate_hash()) {
											if (!Is_streaming) {
												if (In_place_recovery && !Initial_recovery_from_offset) {
													Current_file_byte_offset = File_data_input.tellg();
													uint_fast64_t const Valid_start_of_file_byte_offset(Current_file_byte_offset - 8 - Extra_bytes_to_read);
													assert(Valid_start_of_file_byte_offset <= static_cast<decltype(Valid_start_of_file_byte_offset)>(File_data_input.tellg()));
													assert(!(Valid_start_of_file_byte_offset % 8));

													if (Valid_start_of_file_byte_offset) {

														Data_meta.Set_begin_byte_offset(Valid_start_of_file_byte_offset);
														Data_meta.set_data_size(Valid_start_of_file_byte_offset);

														// adjust for sparsification
														File_data_input.close();
														File_data_output.close();
														auto const Data_filepath((src_topic_path / "data").string());
														{ Scoped_file_handle File_handle(Data_filepath, Valid_start_of_file_byte_offset); }
														File_data_input.open(Data_filepath.c_str(), ::std::ios::in | ::std::ios::binary);
														Seek_file(File_data_input, Current_file_byte_offset);
														File_data_output.open(Data_filepath.c_str(), ::std::ios::in | ::std::ios::out | ::std::ios::binary);
														Seek_file(File_data_output, Valid_start_of_file_byte_offset);
													}
												}
											}
											if (msg.Get_indicies_size() ==  database::Message_Indicies_Size) {
												// TODO more other indicies checks
												if (msg.Get_index(1)) {
													bool Errata_in_sequence_numbers(false);
													if (Rewrite_sequence_numbers == Global_reset) {
														msg.Set_index(0, ++Global_maximum_sequence_number);
														msg.Set_hash(msg.Calculate_hash());
													} else if (Rewrite_sequence_numbers == Local_reset) {
														msg.Set_index(0, Local_maximum_sequence_number);
														msg.Set_hash(msg.Calculate_hash());
													} else {
														if (Maximum_sequence_number < msg.Get_index(0))
															Maximum_sequence_number = msg.Get_index(0);
														else {
															if (Rewrite_sequence_numbers == Local_continuation) {
																msg.Set_index(0, ++Maximum_sequence_number);
																msg.Set_hash(msg.Calculate_hash());
															} else
																Errata_in_sequence_numbers = true;
														}
													}
													if (!Errata_in_sequence_numbers) {
													
														if (Last_Known_Timestamp <= msg.Get_index(1)) {
															Last_Known_Timestamp = msg.Get_index(1);

															auto Possibly_rewritten_msg_size_on_disk(Rewrite_delta_poison ? (Message_processing_helper::Rewrite_delta_poison<Total_message_buffer_size>(msg, true), msg.Get_size_on_disk()) : msg_size_on_disk);
															if (In_place_recovery && Possibly_rewritten_msg_size_on_disk > msg_size_on_disk)
																throw ::std::runtime_error("error, inplace recovery and delta-poison rewriting yields exapnsion of the data -- meaning next (potentially valid) message will be clobered in  " + topic_name + ". Processing of such configuration is not yet supported. Consider not recovering in-place (time/space permitting).");


															auto const msg_end_byte_offset(Possibly_rewritten_msg_size_on_disk + Data_meta.get_data_size());

															Message_boundaries_index.Update_latest_key(msg_end_byte_offset, [&](uint64_t * Index_key){
																	Index_key[0] = Data_meta.get_data_size();
																	Index_key[1] = msg_end_byte_offset;
															});

															Last_Known_Sequence_Number = msg.Get_index(0);

															// Note: Sequence number is no longer being indexed.

															Timestamp_index.Update_latest_key(msg.Get_index(1),  [&](uint64_t * Index_key) {
																	Index_key[0] = Data_meta.get_data_size();
															});

															if (!In_place_recovery || Rewrite_delta_poison || static_cast<uint_fast64_t>(File_data_output.tellp()) + Possibly_rewritten_msg_size_on_disk != static_cast<uint_fast64_t>(File_data_input.tellg())) {
																if (!File_data_output.write(reinterpret_cast<char const *>(msg.Raw()), Possibly_rewritten_msg_size_on_disk))
																	throw ::std::runtime_error("error could not write message for : " + topic_name);
															} else
																Seek_file(File_data_output, msg_end_byte_offset);

															Data_meta.set_data_size(msg_end_byte_offset);
															Data_meta.Set_maximum_sequence_number(msg.Get_index(0));
															Data_meta.set_latest_timestamp(Last_Known_Timestamp);

															++Local_maximum_sequence_number;
															++Messages_size;

															if (!Is_streaming) {
																Is_streaming = true;
																synapse::misc::log(topic_name + " streaming started @ message t(=" + ::std::to_string(Last_Known_Timestamp) + "), seqno(=" + ::std::to_string(Last_Known_Sequence_Number) + ")\n", true);
															} else if (Rewrite_delta_poison) {
																Rewrite_delta_poison = false;
																assert(Is_streaming);
																++Delta_Poison_Triggers;
																synapse::misc::log(topic_name + " Streaming resumed @ message t(=" + ::std::to_string(Last_Known_Timestamp) + "), seqno(=" + ::std::to_string(Last_Known_Sequence_Number) + ")\n", true);
																synapse::misc::log(topic_name + " Delta poison set @ message t(=" + ::std::to_string(Last_Known_Timestamp) + "), seqno(=" + ::std::to_string(Last_Known_Sequence_Number) + ")\n", true); 
															}

															continue;

														} else
															throw ::std::string("timestamp jumped back");
													} else
														throw ::std::string("errata in sequence numbers");
												} else
													throw ::std::string("no timestamp is allowed to be zero at the moment");
											} else
												throw ::std::string("indicies-size");
										} else {
											// Todo: signed/unsigned appropriation.
											File_data_input.clear();
											throw ::std::string("crc32 check");
										}
									} else if (!Is_streaming || Rewrite_delta_poison) // Performance acceleration shortcut. For cases of travelling over initial sparsification area (lots and lots of semantic zero bytes) -- because throwing exception for every 8 bytes of read data may be costly...
										continue;
									else
										throw ::std::string("initial message portion");
								} catch (::std::string e) { // Todo -- in future make it a proper type (e.g. continuation_with_delta_rewrite, which contains string filed for logging
									if (!Inhibit_delta_poisoning || !Is_streaming) {
										if (File_Data_Input_Has_Large_Buffer) {
											File_Data_Input_Has_Large_Buffer = false;
											File_data_input.rdbuf()->pubsetbuf(nullptr, 0);
										}
										if (Is_streaming && !Rewrite_delta_poison) {
											assert(!Inhibit_delta_poisoning);
											Rewrite_delta_poison = true;
											::std::string Log_Text(topic_name + " streaming is interrupted due to error(=" + ::std::move(e) + ").");
											if (Last_Known_Timestamp && Last_Known_Sequence_Number)
												Log_Text += " Last known message: t(=" + ::std::to_string(Last_Known_Timestamp) + "), seqno(=" + ::std::to_string(Last_Known_Sequence_Number) + ").\n";
											else
												Log_Text += '\n';
											synapse::misc::log(Log_Text, true);
										}
										File_data_input.seekg(Current_file_byte_offset);
										continue;
									} else
										synapse::misc::log(topic_name + " processing ends with reason(=" + ::std::move(e) + ").\n", true);
								}
							} else
								synapse::misc::log(topic_name + " processing ends with not more data being available for reading.\n", true);
							break;
						}
						Seek_file(File_data_meta, 0);
						Data_meta.rehash();
						if (!File_data_meta.write(Buffer_data_meta.get(), database::Data_meta_ptr::size))
							throw ::std::runtime_error(topic_name + " error could not write metadata");

						Message_boundaries_index.Flush_meta();
						// Note: sequence number index is no longer done.
						Timestamp_index.Flush_meta();

						uint_fast64_t current_size;

						Seek_file(File_data_output, Data_meta.get_data_size());
						pad_file(File_data_output);
						current_size = File_data_output.tellp();
						if (!File_data_output)
							throw ::std::runtime_error(topic_name + " error could not pad 'data' file whilst in_place_recovery mode");
						File_data_input.close();
						File_data_output.close();
						::boost::filesystem::resize_file(dst_topic_path / "data", current_size);

						pad_file(File_data_meta);
						current_size = File_data_meta.tellp();
						File_data_meta.close();
						::boost::filesystem::resize_file(dst_topic_path / "data_meta", current_size);

						pad_file(Message_boundaries_index.File);
						pad_file(Message_boundaries_index.File_meta);
						// Note: sequence number index is no longer done.
						pad_file(Timestamp_index.File);
						pad_file(Timestamp_index.File_meta);

						Message_boundaries_index.Resize_files_on_close();
						// Note: sequence number index is no longer done.
						Timestamp_index.Resize_files_on_close();

						synapse::misc::log(topic_name  + " written ok\n");
						synapse::misc::log(topic_name  + "\tMaximum sequence number: " + ::std::to_string(Data_meta.Get_maximum_sequence_number()) + '\n');
						synapse::misc::log(topic_name  + "\tLatest_timestamp: " + ::std::to_string(Data_meta.get_latest_timestamp()) + '\n');
						synapse::misc::log(topic_name  + "\tMessages: " + ::std::to_string(Messages_size) + '\n');
						synapse::misc::log(topic_name  + "\tDelta Poisons: " + ::std::to_string(Delta_Poison_Triggers) + '\n');
						synapse::misc::log(topic_name  + "\tTopic size on disk: " + ::std::to_string(Data_meta.get_data_size()) + '\n', true);

					} else 
						throw ::std::runtime_error(topic_name + " error 'data' file could not be opened");
				} catch (::std::exception const & e) {
					synapse::misc::log("oops: " + ::std::string(e.what()) + '\n');
					synapse::asio::Exit_error = true;
					synapse::asio::io_service::get().stop();
				} catch (...) {
					synapse::misc::log("oops: unknown exception\n");
					synapse::asio::Exit_error = true;
					synapse::asio::io_service::get().stop();
				}
			});
		}
		synapse::misc::log("Total topics: " + ::std::to_string(topics_size) + '\n');

		::std::vector< ::std::thread> pool(concurrency_hint - 1);
		for (auto & i : pool)
			i = ::std::thread([]()->void{ synapse::asio::io_service::get().run(); });
		synapse::asio::io_service::get().run();

		for (auto & i: pool)
			i.join();

		synapse::misc::log.flush();

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

int
main(int argc, char* argv[])
{
	#if !defined(NDEBUG) && defined(__WIN32__)
	::SetErrorMode(0);
	#endif
	return ::data_processors::synapse::database::run(argc, argv);
}
