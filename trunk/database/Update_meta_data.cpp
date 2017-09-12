#include <iostream>
#include <memory>
#include <atomic>
#include <thread>

#include <boost/filesystem.hpp>
#if defined(__GNUC__)  || defined(__clang__) 
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#endif
#include "../Version.h"
#include "../misc/misc.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
	namespace data_processors {
		namespace misc {
			namespace alloc {
				::std::atomic_uint_fast64_t static Virtual_allocated_memory_size{ 0 };
				uint_fast64_t static Memory_alarm_threshold{ static_cast<uint_fast64_t>(-1) };
			}
		}
		namespace synapse {
			namespace asio {
				::std::atomic_bool static Exit_error{ false };
			}
			namespace database {
				#if defined(__GNUC__) && !defined(__clang__)
					// otherwise clang complains of unused variable, GCC however wants it to be present.
					::std::atomic_uint_fast64_t static Database_size{0};
				#endif
				int static nice_performance_degradation = 0;
				unsigned constexpr static page_size = 4096;
			}
		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "../asio/io_service.h"
#include "../misc/sysinfo.h"
#include "../misc/alloc.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
	namespace data_processors {
		namespace synapse {
			data_processors::misc::alloc::Large_page_preallocator<synapse::database::page_size> static Large_page_preallocator;
		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#include "async_file.h"
#include "Async_file_3.h"

#if DP_ENDIANNESS != LITTLE
#error Only works on little-endian systems for the time being.
#endif 

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
	namespace data_processors {
		namespace synapse {
			namespace database {
				void static find_all(::boost::filesystem::path const & Src_path, ::std::vector<::std::string>& files, ::std::string const & name = "data_meta")
				{
					for (::boost::filesystem::recursive_directory_iterator Segment_path_iterator(Src_path); Segment_path_iterator != ::boost::filesystem::recursive_directory_iterator(); ++Segment_path_iterator)
					{
						auto const & Path(Segment_path_iterator->path());
						if (::boost::filesystem::is_directory(Path))
							continue;
						else if (Path.filename().string() == name)
							files.emplace_back(Path.string());
					}
				}
				void static restore(::boost::filesystem::path const & Src_path)
				{
					::std::vector<::std::string> files;
					find_all(Src_path, files);
					for (auto const & File_Path : files)
					{
						::std::string old_file(File_Path + "_old");
						if (::boost::filesystem::exists(old_file))
						{
							::boost::filesystem::remove(File_Path);
							::boost::filesystem::rename(old_file, File_Path);
						}
						else
						{
							std::cout << "Cannot find old meta_data file for file:" << File_Path << std::endl;
						}
					}
				}
				void static clean(::boost::filesystem::path const & Src_path)
				{
					::std::vector<::std::string> files;
					find_all(Src_path, files);
					for (auto const & File_Path : files)
					{
						::std::string old_file(File_Path + "_old");
						if (::boost::filesystem::exists(old_file))
						{
							::boost::filesystem::remove(old_file);
						}
						else
						{
							std::cout << "Cannot find old meta_data file for file:" << File_Path << std::endl;
						}
					}
				}
				void static verify(::boost::filesystem::path const & Src_path)
				{
					char buff1[database::Data_meta_ptr::size];
					::std::vector<::std::string> files;
					find_all(Src_path, files);
					for (auto const & File_Path : files) {
						::std::ifstream data_meta(File_Path, ::std::ios_base::binary);
							if (data_meta)
							{
								if (data_meta.read(buff1, database::Data_meta_ptr::size))
								{
									database::Data_meta_ptr New_meta_data(buff1);
									New_meta_data.verify_hash();
								}
							}
						}
					}
				int static run(int argc, char** argv)
				{
					synapse::misc::log(">>>>>>> BUILD info: " BUILD_INFO " main source: " __FILE__ ". date: " __DATE__ ". time: " __TIME__ ".\n", true);
					try {
						if (argc < 3)
						{
							::std::cout << "Usage:" << std::endl;
							::std::cout << ::std::string(argv[0]) << " --dbroot \"synapse database root folder\" [--restore | --clean | --verify]" << std::endl;
							::std::cout << "--restore: optional, it is to restore the old meta_data file." << std::endl;
							::std::cout << "--clean: optional, it is to remove all backup old meta_data file." << std::endl;
							::std::cout << "--verify: optional, it is to verify hash is correct in new meta_data file." << std::endl;
						}
						else
						{
							if (strcmp(argv[1], "--dbroot") == 0)
							{
								::boost::filesystem::path Src_path(argv[2]);
								if (Src_path.empty())
									throw ::std::runtime_error("Src path must be provided.");
								if (!::boost::filesystem::exists(Src_path))
									throw ::std::runtime_error("Src path must an exist directory.");
								if (argc >= 4) {
									if (strcmp(argv[3], "--restore") == 0)
										restore(Src_path);
									else if (strcmp(argv[3], "--clean") == 0)
										clean(Src_path);
									else if (strcmp(argv[3], "--verify") == 0)
										verify(Src_path);
									else
										throw ::std::runtime_error("Unknown command line parameter: \"" + ::std::string(argv[3]) + "\"");
								}
								else {
									char buff1[::data_processors::synapse::database::version3::Data_meta_ptr::size];
									char buff2[::data_processors::synapse::misc::round_up_to_multiple_of_po2(database::Data_meta_ptr::size, database::page_size)] = {};
									::std::vector<::std::string> files;
									find_all(Src_path, files);

									for (auto const & File_Path : files) {
										// meta_data_old already existed, 
										if (::boost::filesystem::exists(File_Path + "_old"))
											throw ::std::runtime_error("Found renamed old meta_data file.");
										::std::ifstream data_meta(File_Path, ::std::ios_base::binary);
										if (data_meta)
										{
											if (data_meta.read(buff1, ::data_processors::synapse::database::version3::Data_meta_ptr::size))
											{
												::data_processors::synapse::database::version3::Data_meta_ptr Old_meta_data(buff1);
												// Only can upgrade from version 1 to version 2.
												if (synapse::misc::Get_alias_safe_value<uint64_t>(reinterpret_cast<uint64_t const*>(buff1)) != 1)
													throw ::std::runtime_error("Current meta_data file is not at version 1.");
												Old_meta_data.verify_hash();

												database::Data_meta_ptr New_meta_data(buff2);
												New_meta_data.initialise();
												New_meta_data.set_data_size(Old_meta_data.get_data_size());
												New_meta_data.Set_maximum_sequence_number(Old_meta_data.Get_maximum_sequence_number());
												New_meta_data.set_latest_timestamp(Old_meta_data.get_latest_timestamp());
												New_meta_data.Set_begin_byte_offset(Old_meta_data.Get_begin_byte_offset());
												New_meta_data.Set_target_data_consumption_limit(Old_meta_data.Get_target_data_consumption_limit());
												New_meta_data.rehash();
												::std::string new_file_name(File_Path + "_new");
												::std::ofstream New_meta(new_file_name, ::std::ios::binary | ::std::ios::trunc);
												if (!New_meta.write(buff2, sizeof(buff2)))
													throw ::std::runtime_error("Failed to store new meta_data file as " + new_file_name);
												else
												{
													// All good, we will have to close them before renaming them.
													New_meta.close();
													data_meta.close();
													::boost::filesystem::rename(File_Path, File_Path + "_old");
													::boost::filesystem::rename(new_file_name, File_Path);
												}
											}
											else
												throw ::std::runtime_error("Failed to read meta_data file " + File_Path);
										}
										else
											throw ::std::runtime_error("Failed to open meta_data file " + File_Path);
									}
								}
							} else
								throw ::std::runtime_error("Unknow command line parameter: \"" + ::std::string(argv[1]) + "\"");
						}
						return 0;
					} catch (::std::exception& e) {
						::std::cerr << "Exception: " << ::std::string(e.what()) << ::std::endl;
						return -1;
					} catch (...) {
						::std::cerr << "Unknow exception." << ::std::endl;
						return -1;
					}
				}
			}
		}
	}
#ifndef NO_WHOLE_PROGRAM
}
#endif

int main(int argc, char** argv)
{
	return data_processors::synapse::database::run(argc, argv);
}
