#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh
. `dirname ${0}`/Test_archive_common.sh
# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}
# This variable is how many message will be published while testing.
TOTAL_MESSAGE=3000
# This variable is to control how many topics we will have during testing.
TOTAL_TOPIC=5


if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
make database/basic_recover.exe || Bail
make amqp_0_9_1/server.exe || Bail
make database/Archive_joiner.exe || Bail
make amqp_0_9_1/clients/Cpp/Archive_maker.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/clients/Cpp/Archive_maker.exe\ ./database/Archive_joiner.exe\ ./database/basic_recover.exe\ ./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# Print_logs_in_background
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
sleep 1
Create_archive_fifo_info

# 1st test, make sure the command line parameter checking is working as expected.
${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive1 --subscribe_from "20160101T000000" --subscription test.* --timediff_tolerance 1 2>&1 | tee -a ${BASENAME}_client_1_log.txt 
grep -i "oops: Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set." ${BASENAME}_client_1_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception(Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set.)..."
fi

# 2nd test, make sure the command line parameter checking is working as expected.
${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive2 --subscribe_from "20160101T000000" --subscription test.* --timediff_alert "echo Timediff_alert is executed." 2>&1 | tee -a ${BASENAME}_client_2_log.txt 
grep -i "oops: Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set." ${BASENAME}_client_2_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception(Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set.)..."
fi

# 3rd test, make sure the command line parameter checking is working as expected.
${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive3 --subscribe_from "20160101T000000" --subscription test.* --timediff_tolerance -5 --timediff_alert "echo Timediff_alert is executed." 2>&1 | tee -a ${BASENAME}_client_3_log.txt 
grep -i "oops: Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set." ${BASENAME}_client_3_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception(Timediff_tolerance and Timediff_alert must be both set and timediff_tolerance must be greater than 0 or neither is set.)..."
fi

# 4th test, to test log_root is generated as expected.
subscription=""
for (( T=1; T<=${TOTAL_TOPIC}; T++ )); do
	subscription="${subscription} --subscription test.topic${T}"
done
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive4 --log_root log_root --subscribe_from "20160101T000000" ${subscription} < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_4_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo


sleep 10
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

# check log files structure is created as expected.(log_root/YYYYMMDD_log.txt)
dir_name="log_root"
Check_directory $dir_name
temp="$(date +%s)" #Get utc time
today="$(date -u --date @${temp} +%Y%m%d)"
file_name="${dir_name}/${today}_log.txt"
Check_file $file_name

# 5th test, to check timediff_tolerance/timediff_alert/check_timestamp_period are working as expected.
# Publish 3000 messages (timestamp is couple of days ago)
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub $((2*$TOTAL_MESSAGE)) ${TOTAL_TOPIC} 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive5 --subscribe_from "20160101T000000" --subscription test.* --startpoint_filepath archive5.bin --timediff_tolerance 1 --timediff_alert "echo Timediff_alert is executed." --check_timestamp_period 1 --alarm_interval 1 < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_5_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_and_stop_archive_maker archive5

for (( I=1; I<=5; I++ )); do
	grep -e "\[test.topic${I}, \([1-9]\|[1-9][0-9]\) days behind\]" ${BASENAME}_client_5_log.txt
	if [ "$?" -ne 0 ]; then
		Bail "Grep could not find [test.topic${I}, [1-9]|1[0-9] days behind] as expected."
		break
	fi
done

total="$(grep -c "Timediff_alert is executed." ${BASENAME}_client_5_log.txt)"
if [ ${total} -lt 4 ]; then # To ensure the timer is actually working.
	Bail "Timediff_alert is not executed as expected."
else
	echo "Found Timediff_alert ${total} times."
fi

grep "Got Current_subscription_topics_size: 5" ${BASENAME}_client_5_log.txt
if [ "$?" -ne 0 ]; then
	Bail "Grep could not find 'Got Current_subscription_topics_size: 5' as expected."
fi

file_name="./archive5.bin"
Check_file $file_name
size="$(stat -c %s ./archive5.bin)"
if [ ${size} -le 0 ]; then
	Bail "Save point file is empty, which is not expected."
fi

grep -i "At least one of settings is/are invalid (Days_to_delete_archived_files = -1, Days_to_truncate_topic = -1), archived files will not be checked" ${BASENAME}_client_5_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected text(Both settings are invalid (Days_to_delete_archived_files = -1, Days_to_truncate_topic = -1), archived files will not be checked)..."
fi


# 6th case, to ensure time difference is only checked exactly 1 time, no more and no less.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive6 --subscribe_from "20160101T000000" --subscription test.* --timediff_tolerance 1 --check_timestamp_period 5 --timediff_alert "echo Timediff_alert is executed." < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_6_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_and_stop_archive_maker archive6

total="$(grep -c "Timediff_alert is executed." ${BASENAME}_client_6_log.txt)"
if [ ${total} -le 0 ]; then 
	Bail "Timediff_alert is not executed as expected."
else
	echo "Found Timediff_alert ${total} times."
fi

# 7,8 and 9th case, ensure mutex on topic is actually working.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive7 --subscribe_from "20160101T000000" --subscription test.* < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_7_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_archive_maker ./archive7

(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive7 --subscribe_from "20160101T000000" --subscription test.* 2>&1 | tee -a ${BASENAME}_client_8_log.txt & wait $!) &
CLEAN_AWAIT_EXTRA_ARCHIVE_MAKERS="${!}"

sleep 10 # Cannot wait PID here as if something is wrong, the archive_maker is actually store data to the same folder, then we cannot detect it.

grep -e "Failed to acquire lock, cannot archive topic test.topic[1-5] to folder archive7" -e "Failed to create mutex for topic" ${BASENAME}_client_8_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception('Failed to acquire lock, cannot archive topic test.topic[1-5] to folder archive7' OR 'Failed to create mutex for topic')"
fi

# Now test with full path of archive7 as archive_root.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root ${ODIR}/archive7 --subscribe_from "20160101T000000" --subscription test.* 2>&1 | tee -a ${BASENAME}_client_8_1_log.txt & wait $!) &
CLEAN_AWAIT_EXTRA_ARCHIVE_MAKERS="${CLEAN_AWAIT_EXTRA_ARCHIVE_MAKERS} ${!}"

sleep 10 # Cannot wait PID here as if something is wrong, the archive_maker is actually store data to the same folder, then we cannot detect it.

grep -e "Failed to acquire lock, cannot archive topic test.topic[1-5] to folder" -e "Failed to create mutex for topic" ${BASENAME}_client_8_1_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception('Failed to acquire lock, cannot archive topic test.topic[1-5] to folder' OR 'Failed to create mutex for topic')"
fi

# Now we shutdown the first instance of Archive_maker
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

# Start another archive_maker instance, this time it should work.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive7 --subscribe_from "20160101T000000" --subscription test.* < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_9_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_and_stop_archive_maker archive7

grep -e "Failed to acquire lock, cannot archive topic test.topic[1-5] to folder archive7" -e "Failed to create mutex for topic" ${BASENAME}_client_9_log.txt
if [ "$?" -eq "0" ]
then
	Bail "Grep finds exception('Failed to acquire lock, cannot archive topic test.topic[1-5] to folder archive7' OR 'Failed to create mutex for topic'), which is not expected."
fi

wait ${CLEAN_AWAIT_EXTRA_ARCHIVE_MAKERS}


# Stop synapse server
Stop_synapse
sleep 5
# 10th case, to ensure same topic with different stream_id/type_name working fine
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root stream_test
sleep 1

/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} test_stream 2>&1 | tee ${BASENAME}_client_10_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive10 --subscribe_from "20160101T000000" --subscription test.* < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_10_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_and_stop_archive_maker archive10

Stop_synapse
# check if we get some ACK related message.
for (( I=1; I <=5; I++ )); do
	total="$(grep -c "Got ACK for topic test.topic${I}, timestamp is " ${BASENAME}_client_10_log.txt)"
	if [ ${total} -ne 1 ]; then
		Bail "Grep could not find ACK log for topic test.topic${I} as expected"
		break
	fi
done

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive10 --dst joiner10 2>&1 | tee -a ${BASENAME}_client_10_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	dir_name="joiner10/test.topic${F}"
	Check_directory $dir_name
	file_name="joiner10/test.topic${F}/data"
	Check_file $file_name
done
# To check if savepoint file is created for all topics.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	file_name="archive10/savepoints/test.topic${F}"
	Check_file $file_name
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner10 --dst joiner10 2>&1 | tee -a ${BASENAME}_client_10_log.txt
# To check if all index and meta files are created as expected.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	file_name="joiner10/test.topic${F}/data_meta"
	Check_file $file_name
	for (( J=0; J<=2; J++ )); do
		# we are NOT indexing sequenc number now
		if [ ${J} -ne 1 ]; then
		file_name="joiner10/test.topic${F}/index_${J}_data"
		Check_file $file_name
		file_name="joiner10/test.topic${F}/index_${J}_data_meta"
		Check_file $file_name
		fi
	done
done
# Start our Synapse server with database pointing to join/recover result.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner10
sleep 1
# Subscribe to the Synapse server and check all messages having correct sequence number, timestamp, stream_id and type name.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 2>&1 | tee -a ${BASENAME}_client_10_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client to exit with error..."
fi

Stop_synapse

rm -fr ./archive10
rm -fr ./stream_test
rm -fr ./joiner10
rm -fr ./archive7
rm -fr ./archive6
rm -fr ./archive5
rm -fr ./archive4
rm -fr ./db
rm -fr ./log_root
rm -f  ./archive5.bin
rm -f  ./archive_stdin_fifo

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
