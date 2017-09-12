all : \
			Tests \
			Binaries 

Tests : \
			${OUTPUT_DIR}database/async_file_test.okrun \
			${OUTPUT_DIR}database/topic_test.okrun \
			${OUTPUT_DIR}database/Test_basic_recover_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Publishable_Gap_Controller_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Topic_Deletion_Controller_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_delta_poison_1.okrun \
			${OUTPUT_DIR}database/Test_archive_1.okrun \
			${OUTPUT_DIR}database/Test_archive_2.okrun \
			${OUTPUT_DIR}database/Test_archive_3.okrun \
			${OUTPUT_DIR}database/Test_archive_4.okrun \
			${OUTPUT_DIR}database/Test_archive_5.okrun \
			${OUTPUT_DIR}database/Test_archive_6.okrun \
			${OUTPUT_DIR}database/Test_archive_7.okrun \
			${OUTPUT_DIR}database/Test_sparsify_truncate_by_timestamp_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_subscribe_to_topic_ending_with_space_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_lazy_load_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/wildcard_subscription_test_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/preroll_if_begin_timestamp_in_future_test_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_multiple_subscriptions_from_different_points_in_time.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_subscribe_to_empty_topic_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_publish_out_of_indexable_gap_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_subscribe_unsubscribe_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_shutdown_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_on_quit_handlers_cleanup_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_log_rotation_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_cached_time_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_database_size_alarm_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_alarms_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_index_max_key_update_requests_throttling_watermark_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Index_Throttle_On_Published_Gap_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_sparsification_of_files_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_sparsification_of_files_2.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_sparsification_of_files_3.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_publishing_out_of_order_not_stalling_existing_consumers_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Delayed_Topic_Destructor_Racing_Condition_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Different_Message_Sizes_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Multiple_Modes_Of_Subscription_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Multiple_Modes_Of_Publishing_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_topic_mirror_1.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_extend_ack_1.okrun \
			${OUTPUT_DIR}database/Test_Basic_Recover_2.okrun \
			${OUTPUT_DIR}database/Test_Basic_Recover_3.okrun \
			${OUTPUT_DIR}amqp_0_9_1/Test_synapse_multiple_publishers_to_same_topic.okrun

Binaries : \
			${OUTPUT_DIR}amqp_0_9_1/server.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Archive_maker.exe \
			${OUTPUT_DIR}database/Archive_joiner.exe \
			${OUTPUT_DIR}database/basic_recover.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Benchmarking_1.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Benchmarking_2.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Publishable_Gap_Controller.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Topic_Deletion_Controller.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Topic_mirror.exe \
			${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Bench_utility.exe \
			${OUTPUT_DIR}database/Update_meta_data.exe

# todo -- bring up to date these csharp thingies...
# ${OUTPUT_DIR}amqp_0_9_1/clients/csharp/basic_example_1/publish/Program.okbuild \
# ${OUTPUT_DIR}amqp_0_9_1/clients/csharp/basic_example_1/subscribe/Program.okbuild \
# ${OUTPUT_DIR}amqp_0_9_1/clients/csharp/basic_example_1/transform/Program.okbuild

# Synapse server...
# -lavrt for multimedia scheduler class in windows
# TODO -- make more Unix-friendly later...
# TODO -- do not include irrelevant libs for specifict targets
${OUTPUT_DIR}amqp_0_9_1/server.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -lpsapi -latomic
${OUTPUT_DIR}amqp_0_9_1/server.exe : $(call Use_pch,${OUTPUT_DIR}amqp_0_9_1/server_pch.h)

# Archive maker...
${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Archive_maker.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32
${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Archive_maker.exe : $(call Use_pch,${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Archive_maker_pch.h)

# Topic mirror...
${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Topic_mirror.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32
${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Topic_mirror.exe : $(call Use_pch,${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Topic_mirror_pch.h)

# Bench_utility ...
${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Bench_utility.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

# Archive joiner...
# TODO -- clean up libraries, currently largely copied from server.exe and may be needed by virtue of some common header inclusions which themselves were not super-cleanly separated w.r.t. asio/socket/date_time etc...
${OUTPUT_DIR}database/Archive_joiner.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -ladvapi32 -lgdi32 -lntdll 

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -ladvapi32 -lgdi32 -lntdll 

${OUTPUT_DIR}udt_relay/udt_relay.exe : BUILD_LIBS = -lboost_chrono -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -latomic

# Basic recover...
# TODO -- clean up libraries, currently largely copied from server.exe and may be needed by virtue of some common header inclusions which themselves were not super-cleanly separated w.r.t. asio/socket/date_time etc...
${OUTPUT_DIR}database/basic_recover.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -lgdi32 -lntdll -lAdvapi32 -lpsapi

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Benchmarking_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Benchmarking_2.exe : BUILD_LIBS = -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Throughput_Benchmarking_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Publishable_Gap_Controller.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -lgdi32 -lntdll -lAdvapi32 -lpsapi

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Topic_Deletion_Controller.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -lgdi32 -lntdll -lAdvapi32 -lpsapi

${OUTPUT_DIR}database/Update_meta_data.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -lgdi32 -lntdll -lAdvapi32 -lpsapi

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_delta_poison_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_delta_poison_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_Publishable_Gap_Controller_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_Topic_Deletion_Controller_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_basic_recover_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_2.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_3.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_4.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_5.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_6.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_archive_7.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_sparsify_truncate_by_timestamp_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/Test_performance_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_subscribe_to_topic_ending_with_space_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_lazy_load_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/wildcard_subscription_test_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/preroll_if_begin_timestamp_in_future_test_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_shutdown_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_multiple_subscriptions_from_different_points_in_time.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_on_quit_handlers_cleanup_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_subscribe_to_empty_topic_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_publish_out_of_indexable_gap_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_subscribe_unsubscribe_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_sparsification_of_files_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_sparsification_of_files_2.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_sparsification_of_files_3.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_publishing_out_of_order_not_stalling_existing_consumers_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_publishing_out_of_order_not_stalling_existing_consumers_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Synapse_Delayed_Topic_Destructor_Racing_Condition_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Delayed_Topic_Destructor_Racing_Condition_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Synapse_Different_Message_Sizes_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Different_Message_Sizes_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Extended_Ack_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_log_rotation_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_cached_time_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_database_size_alarm_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_alarms_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_index_max_key_update_requests_throttling_watermark_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_synapse_multiple_publishers_to_same_topic.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_topic_mirror_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/Test_extend_ack_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Synapse_Multiple_Modes_Of_Subscription_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Multiple_Modes_Of_Subscription_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Synapse_Multiple_Modes_Of_Publishing_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Multiple_Modes_Of_Publishing_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Basic_Recover_2.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}database/Test_Basic_Recover_2.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Basic_Recover_3.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}database/Test_Basic_Recover_3.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Test_Synapse_Index_Throttle_On_Published_Gap_1.exe : BUILD_LIBS = -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -latomic -luser32

${OUTPUT_DIR}amqp_0_9_1/Test_Synapse_Index_Throttle_On_Published_Gap_1.okrun : ${PHONY_OR_ELSE}

${OUTPUT_DIR}database/async_file_test.okrun database/async_file_test.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -lpsapi 

${OUTPUT_DIR}database/topic_test.okrun database/topic_test.exe : BUILD_LIBS = -lboost_date_time -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -luser32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -lAdvapi32 -lpsapi -latomic

${OUTPUT_DIR}amqp_0_9_1/clients/Cpp/Basic_test_1.exe : BUILD_LIBS =  -lboost_date_time -lboost_regex -lboost_filesystem -lboost_system -lssl -lcrypto -mwindows -lws2_32 -lgdi32 -lole32 -lntdll -liphlpapi -lwsock32 -latomic

${OUTPUT_DIR}amqp_0_9_1/clients/csharp/basic_example_1/publish/Program.okbuild : amqp_0_9_1/clients/csharp/basic_example_1/publish/Program.cs amqp_0_9_1/clients/csharp/basic_example_1/common/*.cs

${OUTPUT_DIR}amqp_0_9_1/clients/csharp/basic_example_1/subscribe/Program.okbuild : amqp_0_9_1/clients/csharp/basic_example_1/publish/Program.cs amqp_0_9_1/clients/csharp/basic_example_1/common/*.cs

${OUTPUT_DIR}amqp_0_9_1/clients/csharp/basic_example_1/transform/Program.okbuild : amqp_0_9_1/clients/csharp/basic_example_1/publish/Program.cs amqp_0_9_1/clients/csharp/basic_example_1/common/*.cs
