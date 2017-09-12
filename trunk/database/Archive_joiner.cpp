#include <iostream>
#include <memory>
#include <atomic>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <openssl/md5.h>

#include "../Version.h"
#include "../misc/misc.h"

// Todo: refactor the headers structures because this is not really needed to be in the this util (I think)... so just putting it here for the time being to make it build (no time for more flexible approach at the moment).
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
data_processors::misc::alloc::Large_page_preallocator<synapse::database::page_size> static Large_page_preallocator;
}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "message.h"
#include "index.h"

#if DP_ENDIANNESS != LITTLE
#error Only works on little-endian systems for the time being.
#endif 

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
	namespace data_processors {
		namespace synapse {
			namespace database {
				::std::atomic_bool static Exit_with_error{ false };
				/**
				* A simple class which will be used as the key of a std::map<>.
				*
				*/
				struct Archive_file_key
				{
					Archive_file_key(uint_fast64_t t, uint_fast64_t seq, uint_fast32_t s, uint_fast32_t date, uint_fast32_t pos, const ::std::string& md5 = "") : Timestamp(t), Sequence(seq), Segment(s), FileDate(date), Position(pos), MD5Hash(md5)
					{
					}
					/**
					* To compare with another Archive_file_key as this will be used as key of std::map.
					*/
					bool operator <(const Archive_file_key& rhs) const
					{
						if (Timestamp == rhs.Timestamp) {
							if (Sequence == rhs.Sequence) {
								if (FileDate == rhs.FileDate) {
									if (Segment == rhs.Segment) {

										if (Position == rhs.Position)
											return MD5Hash < rhs.MD5Hash;
										else
											return Position < rhs.Position;

									} // end if Segment
									else
										return Segment < rhs.Segment;
								}
								else
									return FileDate < rhs.FileDate;
							} // end if Sequence
							else
								return Sequence < rhs.Sequence;
						} // end if Timestamp
						else
							return Timestamp < rhs.Timestamp;
					}

					// Timestamp of the first record in a data segment file.
					uint_fast64_t Timestamp;
					uint_fast64_t Sequence;
					// The segment sequence number, which is started from zero.
					uint_fast32_t Segment;
					// File date as integer.
					uint_fast32_t FileDate;
					// Actual data start position.
					uint_fast32_t Position;
					// MD5 hash of this file
					::std::string MD5Hash;
				};
				// code borrowed from Archive_maker
				struct Segment_file {
					unsigned static constexpr File_buffer_size{ 4 * 1024 * 1024 }; // From testing, huge buffer doesn't improve performance, therefore we use a small one.
					::std::unique_ptr<char> Output_file_buffer{ new char[File_buffer_size] };
					::std::unique_ptr<char> Previous_output_file_buffer{ new char[File_buffer_size] };

					::std::ofstream Output_file;
					::boost::gregorian::date Output_file_date;
					::std::ofstream Previous_output_file;

					::std::string Topic_name;
					::std::string Synapse_name;
					::boost::filesystem::path Output_root;
					::std::string MD5_hash;

					Segment_file(const ::std::string& Topic_name, const ::std::string& Synapse, const ::boost::filesystem::path& root)
						: Topic_name(Topic_name)
						, Synapse_name(Synapse)
						, Output_root(root)
					{
						Output_file.rdbuf()->pubsetbuf(Output_file_buffer.get(), File_buffer_size);
						Previous_output_file.rdbuf()->pubsetbuf(Previous_output_file_buffer.get(), File_buffer_size);
						MD5_CTX ctx;
						if (!MD5_Init(&ctx))
							throw ::std::runtime_error("Failed to initialize MD5 context");
						::std::string temp(Synapse_name + "|" + Topic_name);
						if (!MD5_Update(&ctx, temp.c_str(), temp.length()))
							throw ::std::runtime_error("Failed to update MD5 hash");

						unsigned char buff[MD5_DIGEST_LENGTH];
						if (!MD5_Final(buff, &ctx))
							throw ::std::runtime_error("Failed to finalize MD5 hash");

						::std::ostringstream oss;
						oss << std::hex << std::nouppercase << std::setfill('0');
						for (auto k(0); k < MD5_DIGEST_LENGTH; ++k) {
							oss << std::setw(2) << static_cast<unsigned>(buff[k]);
						}
						MD5_hash = "_" + oss.str();
					}

					Segment_file(Segment_file const &) = delete;

					void Set_output_files_state(uint_fast64_t const Timestamp) {
						if (Previous_output_file.is_open())
							Previous_output_file.close();
						auto const Current_date((data_processors::synapse::misc::epoch + ::boost::posix_time::microseconds(Timestamp)).date());
						if (Current_date != Output_file_date) {
							Output_file_date = Current_date;
							Previous_output_file.swap(Output_file);
							auto const Date_as_string(to_iso_string(Current_date));
							auto const Archive_file_prefix(Date_as_string + "_segment_");
							auto const Archive_path(Output_root / Date_as_string / Topic_name);
							if (!::boost::filesystem::exists(Archive_path))
								::boost::filesystem::create_directories(Archive_path);
							::std::string temp(Synapse_name + "|" + Topic_name);
							for (unsigned i(0);; ++i) {
								auto const Output_file_name(Archive_file_prefix + ::std::to_string(i) + MD5_hash);
								auto const Output_filepath(Archive_path / (Output_file_name));
								if (::boost::filesystem::exists(Output_filepath))
									continue;
								Output_file.open(Output_filepath.string(), ::std::ios_base::binary | ::std::ios_base::trunc);
								// Embed server, and topic name into archived file.
								int len(-1);
								Output_file.write(reinterpret_cast<char*>(&len), sizeof(len));
								len = static_cast<int>(temp.length());
								Output_file.write(reinterpret_cast<char*>(&len), sizeof(len));
								Output_file.write(temp.c_str(), len);
								int align_len(synapse::misc::round_up_to_multiple_of_po2<uint_fast32_t>(len, 8));
								char c[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
								Output_file.write(c, align_len - len);
								Output_file.write(reinterpret_cast<const char*>(&Timestamp), sizeof(Timestamp));
								uint64_t crc(0);
								crc = ::data_processors::synapse::misc::crc32c(crc, static_cast<uint64_t>(-1));
								crc = ::data_processors::synapse::misc::crc32c(crc, static_cast<uint64_t>(len));
								for (int m(0); m < len; ++m)
									crc = ::data_processors::synapse::misc::crc32c(crc, temp.at(m));
								crc = ::data_processors::synapse::misc::crc32c(crc, Timestamp);
								Output_file.write(reinterpret_cast<char *>(&crc), sizeof(crc));
								break;
							}
						}
					}

					void Write_output_files(void const * Data, unsigned Size) {
						if (!Output_file.is_open())
							throw ::std::runtime_error("File IO on closed ::std::ofstream.");
						Output_file.write(reinterpret_cast<char const *>(Data), Size);
						if (Previous_output_file.is_open())
							Previous_output_file.write(reinterpret_cast<char const *>(Data), Size);
					}

					void Flush_output_files() {
						if (Output_file.is_open())
							Output_file.flush();
						if (Previous_output_file.is_open())
							Previous_output_file.flush();
					}
				};

				/**
				* To find the topic name from the file name. The file name looks like a.b.c.d.e_YYYYMMDD_segment_n
				*
				* @param[file_name]: the file name to be processed.
				*
				* @return: the topic name, the file's segment number and date as int as a tuple.
				*
				*/
				::std::tuple<::std::string, uint_fast32_t, uint_fast32_t, uint_fast32_t, ::std::string>
					get_topic_internal(const std::string& file_name, uint_fast32_t file_pos = 0, const ::std::string& md5 = "")
				{
					auto pos = ::std::string::npos;
					// find the last '_' and ensure the result is valid.
					pos = file_name.find_last_of('_', pos);
					if (pos == ::std::string::npos || pos == 0)
						throw ::std::runtime_error("Cannot find a '_' in file name " + file_name);
					const auto & Segment(file_name.substr(pos + 1));

					// find the second last '_' and ensure the result is valid.
					pos = file_name.find_last_of('_', --pos);
					if (pos == ::std::string::npos || pos == 0)
						throw ::std::runtime_error(file_name + " doesn't follow our archive file name convention.");
					// find the third last '_' and ensure the result is valid.
					auto pos1 = pos;
					pos = file_name.find_last_of('_', --pos);
					if (pos == ::std::string::npos || pos == 0)
						throw ::std::runtime_error(file_name + " doesn't follow our archive file name convention.");
					// get the topic, segment number, date as string and return them as tuple.
					return{ file_name.substr(0, pos), ::std::stoi(Segment), ::std::stoi(file_name.substr(pos + 1, pos1 - pos - 1)), file_pos, md5 };
				}
				/**
				* To find the topic name from the file name. The file name looks like a.b.c.d.e_YYYYMMDD_segment_n or XXXX_XXXX_XXXX_XXXX (new)
				* when the super long string is embedded in file.
				*
				* @param[file_name]: the file name to be processed.
				*
				* @return: the topic name, the file's segment number date, start position of the actual data and md5 hash string as string as a tuple.
				*
				*/
				::std::tuple<::std::string, uint_fast32_t, uint_fast32_t, uint_fast32_t, ::std::string>
					get_topic(const std::string& file_name, const ::std::string& full_path)
				{
					::std::ifstream file(full_path, ::std::ios_base::binary);
					int length;
					file.read(reinterpret_cast<char*>(&length), sizeof(length));
					if (length == -1)
					{
						char name[521];
						file.read(reinterpret_cast<char*>(&length), sizeof(length));
						assert(length > 0 && length < 513);
						int align_len = synapse::misc::round_up_to_multiple_of_po2<uint_fast32_t>(length, 8);
						assert(align_len >= length && align_len % 8 == 0);

						file.read(name, align_len);
						uint_fast64_t Msg_timestamp;
						file.read(reinterpret_cast<char*>(&Msg_timestamp), sizeof(Msg_timestamp));
						name[length] = 0;
						uint64_t Hash, Hash_cal = 0;
						Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, static_cast<uint64_t>(-1));
						Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, static_cast<uint64_t>(length));
						file.read(reinterpret_cast<char*>(&Hash), sizeof(Hash));
						for (int k = 0; k < length; ++k)
							Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, name[k]);
						Hash_cal = ::data_processors::synapse::misc::crc32c(Hash_cal, Msg_timestamp);

						assert(Hash == Hash_cal);
						::std::string tempName(name);
						auto pos(tempName.find_last_of('|'));
						if (pos != tempName.npos) {
							if (Hash == Hash_cal && file_name.length() > 33/*_MD5hash*/)
								return get_topic_internal(tempName.substr(pos + 1) + "_" + file_name.substr(0, file_name.length() - 33), static_cast<uint_fast32_t>(align_len + 24), file_name.substr(file_name.length() - 32));
							else
								return get_topic_internal(file_name);
						}
						else
						{
							return get_topic_internal(file_name);
						}
					}
					else
					{
						return get_topic_internal(file_name);
					}
				}

int static
run(int argc, char* argv[])
{
	try
	{
		synapse::misc::log(">>>>>>> BUILD info: " BUILD_INFO " main source: " __FILE__ ". date: " __DATE__ ". time: " __TIME__ ".\n", true);

		::std::string Save_point_path("savepoints");
		::boost::filesystem::path Src_path;
		::boost::filesystem::path Dst_path;
		::std::string Synapse_name;
		bool upgrade = false;
		for (int i(1); i != argc; ++i) {
			if (i == argc - 1 && strcmp(argv[i], "--upgrade") != 0)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!::strcmp(argv[i], "--src"))
				Src_path = argv[++i];
			else if (!::strcmp(argv[i], "--dst"))
				Dst_path = argv[++i];
			else if (!::strcmp(argv[i], "--savepoints"))
				Save_point_path = argv[++i];
			else if (!::strcmp(argv[i], "--upgrade"))
				upgrade = true;
			else if (!::strcmp(argv[i], "--synapse"))
				Synapse_name = argv[++i];
			else
				throw ::std::runtime_error("Unknown option: " + ::std::string(argv[i]));
		}

		if (upgrade && Synapse_name.empty())
			throw ::std::runtime_error("--synapse is needed if --upgrade is specified");

		if (Src_path.empty() || Dst_path.empty())
			throw ::std::runtime_error("both --src and --dst args are needed");

		if (::boost::filesystem::exists(Dst_path))
			throw ::std::runtime_error("Destination path already exists");

		// create based on chosen runtime parameters
		uint_fast64_t topics_size(0);
		::std::map<::std::string, ::std::map<Archive_file_key, ::std::string>> all_files;

		{ // this code block is to ensure the following 200M memroy is freed once they are out of scope.
			unsigned constexpr ifs_buffer_size(32 * 1024 * 1024);
			::std::unique_ptr<char> ifs_buffer(new char[ifs_buffer_size]);

			unsigned constexpr ofs_buffer_size(32 * 1024 * 1024);
			::std::unique_ptr<char> ofs_buffer(new char[ofs_buffer_size]);

			::std::unique_ptr<char> Message_buffer(new char[MaxMessageSize]);
			// TODO: Currently a very simple design (much like basic_recover) in that 1st error bails the segment out, *and* no multimap is used (i.e. no multiple current topics are being scanned with best possible data being picked from any given one...) in case of identical starting timestamps first one would win (other discarded).
			// Generally-speaking recovery of potentially-lost data is semantically different from joining what is presumed to be healthy file segments (i.e. if recovery is needed then such could be applied as a separate utility -- basic_recover etal).

			// First pass -- get all of the segments in time-ascending order... (based on 1st message in each segment).
			for (::boost::filesystem::recursive_directory_iterator Segment_path_iterator(Src_path); Segment_path_iterator != ::boost::filesystem::recursive_directory_iterator(); ++Segment_path_iterator) {
				auto const & Path(Segment_path_iterator->path());
				if (::boost::iequals(Path.filename().string(), Save_point_path)) {
					Segment_path_iterator.no_push();
					continue;
				}
				if (::boost::filesystem::is_directory(Path)) {
					continue;
				}

				if (Path.extension().string() == ".sha256") {
					// To skip .sha265 hash file.
					::data_processors::synapse::misc::log("File " + Path.filename().string() + " is not a file hash, will be skipped.\n", true);
					continue;
				}

				::std::ifstream Segment_file;
				Segment_file.rdbuf()->pubsetbuf(ifs_buffer.get(), ifs_buffer_size);
				auto const  & File_name(Segment_path_iterator->path().filename().string());
				auto const & Topic_and_Segment(get_topic(File_name, Path.string()));
				auto const & Topic_name(::std::get<0>(Topic_and_Segment));
				auto const & Segment(::std::get<1>(Topic_and_Segment));
				auto const & FileDate(::std::get<2>(Topic_and_Segment));
				auto const & Position(::std::get<3>(Topic_and_Segment));
				auto const & Md5Hash(::std::get<4>(Topic_and_Segment));

				Segment_file.open(Path.string(), ::std::ios::binary);
				synapse::misc::log("Scanning (for initial timestamp and sorting): " + Path.filename().string() + '\n', true);
				if (Segment_file) {
					if (Position != 0) {
						Segment_file.seekg(Position, ::std::ios_base::beg);
					}
					if (Segment_file.read(Message_buffer.get(), 8)) {
						database::Message_const_ptr const Message(Message_buffer.get());
						auto const msg_size_on_disk(Message.Get_size_on_disk());
						if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= MaxMessageSize) {
							auto const Extra_bytes_to_read(msg_size_on_disk - 8);
							if (!Extra_bytes_to_read || Segment_file.read(Message_buffer.get() + 8, Extra_bytes_to_read)) {
								if (Message.Get_hash() == Message.Calculate_hash()) {
									if (Message.Get_indicies_size() ==  database::Message_Indicies_Size) {
										if (Message.Get_index(1)) {
											auto const & Segment_filepath(Path.string());
											auto const & Timestamp(Message.Get_index(1));
											auto const & Sequence(Message.Get_index(0));
											synapse::misc::log("Found segment " + Segment_filepath + "(" + Topic_name + "," + ::std::to_string(FileDate) + "," + ::std::to_string(Segment) + "," + ::std::to_string(Position) + ") @ " + ::std::to_string(Timestamp) + ".\n");
											auto const & ret = all_files.emplace(Topic_name, ::std::map<Archive_file_key, ::std::string>{ { { Timestamp, Sequence, Segment, FileDate, Position, Md5Hash }, Segment_filepath} });
											if (ret.second) {
												// add succeeded.
												++topics_size;
											}
											else {
												// The ret.first is a iterator of std::pair<std::string, std::map<Archive_file_key, file_name>>, 
												// we need to add a new element to this map.
												ret.first->second.emplace(::std::piecewise_construct, ::std::forward_as_tuple(Timestamp, Sequence, Segment, FileDate, Position, Md5Hash), ::std::forward_as_tuple(Segment_filepath));
											}
										}
										else
											throw ::std::runtime_error("info/error no timestamp is allowed to be zero at the moment, early exiting: " + Topic_name);
									}
									else
										throw ::std::runtime_error("info/error indicies-size, early exiting: " + Topic_name);
								}
								else
									throw ::std::runtime_error("info/error crc32 check: " + Topic_name);
							}
							else
								throw ::std::runtime_error("Corruput segment.");
						}
						else
							throw ::std::runtime_error("Corruput segment.");
					}
				}
			}
		}

		::boost::filesystem::create_directory(Dst_path);
		auto const concurrency_hint(synapse::misc::concurrency_hint());
		asio::io_service::init(concurrency_hint);
		// Second pass -- for each topic, go through each segment and create one large data file.
		for (auto && topic(all_files.begin()); topic != all_files.end(); ++topic) {
			auto const & Topic_name(topic->first);
			synapse::asio::io_service::get().post(
				[topic, Topic_name, Dst_path, Synapse_name, upgrade]()
			{
				try {
					auto const Destination_topic_path(Dst_path / Topic_name);
					unsigned constexpr ifs_buffer_size(32 * 1024 * 1024);
					::std::unique_ptr<char> ifs_buffer(new char[ifs_buffer_size]);

					unsigned constexpr ofs_buffer_size(32 * 1024 * 1024);
					::std::unique_ptr<char> ofs_buffer(new char[ofs_buffer_size]);

					::std::unique_ptr<char> Message_buffer(new char[MaxMessageSize]);
					if (!upgrade)::boost::filesystem::create_directory(Destination_topic_path);

					::std::ofstream Output_topic_file;
					::std::unique_ptr<Segment_file> Output_seg_file;
					if (!upgrade) {
						Output_topic_file.rdbuf()->pubsetbuf(ofs_buffer.get(), ofs_buffer_size);
						Output_topic_file.open((Destination_topic_path / "data").string().c_str(), ::std::ios::binary);
					}
					else {
						Output_seg_file.reset(new Segment_file(Topic_name, Synapse_name, Dst_path));
					}

					uint_fast64_t latest_timestamp(0);
					uint_fast64_t latest_sequence(0);
					for (auto && i(topic->second.begin()); i != topic->second.end(); ++i) {
						::std::ifstream Segment_file;
						uintmax_t file_size(::boost::filesystem::file_size(i->second));
						synapse::misc::log("Scanning (actual joining/processing): " + i->second + '\n', true);
						
						Segment_file.rdbuf()->pubsetbuf(ifs_buffer.get(), ifs_buffer_size);
						Segment_file.open(i->second, ::std::ios::binary);
						if (i->first.Position != 0)
							Segment_file.seekg(i->first.Position, ::std::ios_base::beg);
						while (Segment_file.read(Message_buffer.get(), 8)) {
							int_fast64_t curpos = (int_fast64_t)Segment_file.tellg() - 8;
							assert(curpos >= 0 && file_size >= (uint_fast64_t)curpos);
							database::Message_const_ptr const Message(Message_buffer.get());
							auto const msg_size_on_disk(Message.Get_size_on_disk());
							if (msg_size_on_disk && !(msg_size_on_disk % 8) && msg_size_on_disk <= MaxMessageSize) {
								auto const Extra_bytes_to_read(msg_size_on_disk - 8);
								if (!Extra_bytes_to_read || Segment_file.read(Message_buffer.get() + 8, Extra_bytes_to_read)) {
									if (Message.Get_hash() == Message.Calculate_hash()) {
										if (Message.Get_indicies_size() == database::Message_Indicies_Size) {
											if (Message.Get_index(1)) {
												if (latest_timestamp <= Message.Get_index(1)) {
													latest_timestamp = Message.Get_index(1);
													latest_sequence = Message.Get_index(0);
													if (!upgrade) {
														if (!Output_topic_file.write(reinterpret_cast<char const *>(Message.Raw()), msg_size_on_disk))
															throw ::std::runtime_error("error could not write message for : " + Topic_name);
													}
													else {
														Output_seg_file->Set_output_files_state(latest_timestamp);
														Output_seg_file->Write_output_files(reinterpret_cast<char const*>(Message.Raw()), msg_size_on_disk);
													}

													// If current timestamp and sequence are equal to 1st message of the next segment, then do the overlapping test and switch to the next segment.
													auto Lookahead_peek(::std::next(i));
													while (Lookahead_peek != topic->second.end() && latest_timestamp == Lookahead_peek->first.Timestamp && latest_sequence == Lookahead_peek->first.Sequence) {
														i = Lookahead_peek;

														if (file_size - curpos  <= ::boost::filesystem::file_size(i->second) - i->first.Position) {
															decltype(Message_buffer) Previous_message_buffer(::std::move(Message_buffer));
															Message_buffer.reset(new char[MaxMessageSize]);
															// Only move to the next segment file if the remain file size is less than the next segment file size.
															// This is to avoid the next segment file is fully included in current segment, which causes some data is lost.
															Segment_file.close();
															synapse::misc::log("Scanning (actual processing): " + i->second + '\n', true);
															file_size = ::boost::filesystem::file_size(i->second);
															Segment_file.rdbuf()->pubsetbuf(ifs_buffer.get(), ifs_buffer_size);
															Segment_file.open(i->second, ::std::ios::binary);
															if (i->first.Position != 0)
																Segment_file.seekg(i->first.Position, ::std::ios_base::beg);
															curpos = Segment_file.tellg();
															if (!Segment_file.read(Message_buffer.get(), msg_size_on_disk))
																throw ::std::runtime_error("info/error could not read next segment, early exiting: " + Topic_name);
															database::Message_const_ptr const Message(Message_buffer.get());
															database::Message_const_ptr const Pre_message(Previous_message_buffer.get());

															if (::memcmp(Message_buffer.get(), Previous_message_buffer.get(), msg_size_on_disk))
																throw ::std::runtime_error("info/error overlapping segments do not agree on the content: " + Topic_name);
															else {
																// Current message is identical with the 1st message in next segment file, keep searching in 
																// next file along the map -- sorted by Timestamp and segment number.
																synapse::misc::log("File:" + i->second + " has the identical record (sequence number is: " + ::std::to_string(latest_sequence) + ")\n", true);
																Lookahead_peek = ::std::next(i);
															}
														}
														else {
															synapse::misc::log("File:" + i->second + " has the identical record (sequence number is: " + ::std::to_string(latest_sequence) + ", file size is: " + ::std::to_string(file_size) + ", curpos: " + ::std::to_string(curpos) + ", but its length is shorter, so it is skipped.\n", true);
															Lookahead_peek = ::std::next(i);
														}
													} // end while (Lookahead_peek != topic->second.send() && ...)
												}
												else
													throw ::std::runtime_error("info/error timestamp jumped back, early exiting: " + Topic_name);
											}
											else
												throw ::std::runtime_error("info/error no timestamp is allowed to be zero at the moment, early exiting: " + Topic_name);
										}
										else
											throw ::std::runtime_error("info/error indicies-size, early exiting: " + Topic_name);
									}
									else
										throw ::std::runtime_error("info/error crc32 check, early exiting: " + Topic_name);
								}
								else
									throw ::std::runtime_error("Corruput segment.");
							}
							else
								throw ::std::runtime_error("Corruput segment.");
						}
						
						if (Segment_file.eof() && ::std::next(i) != topic->second.end()) {
							// End of file and not the last file, we should add a hole in the output stream
							synapse::misc::log("There is no overlap after file " + i->second + ", insert a hole\n");
							int64_t hole{ 0 };
							assert(Output_topic_file.is_open());
							Output_topic_file.write(reinterpret_cast<char const*>(&hole), sizeof(hole));
						}
					}
				}
				catch (::std::exception const & e)
				{
					synapse::misc::log("oops: " + ::std::string(e.what()) + '\n');
					synapse::misc::log.flush();
					Exit_with_error = true;
					synapse::asio::io_service::get().stop();
				}
				catch (...)
				{
					synapse::misc::log("oops: uknown exception\n");
					synapse::misc::log.flush();
					Exit_with_error = true;
					synapse::asio::io_service::get().stop();
				}
			});
		}
		::std::vector< ::std::thread> pool(concurrency_hint - 1);
		for (auto & i : pool)
			i = ::std::thread([]()->void { synapse::asio::io_service::get().run(); });
		synapse::asio::io_service::get().run();

		for (auto & i : pool)
			i.join();
		synapse::misc::log("ok, total_topics: " + ::std::to_string(topics_size) + '\n');
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
