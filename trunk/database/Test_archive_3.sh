#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh
. `dirname ${0}`/Test_archive_common.sh
# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}
# This variable is how many message will be published while testing.
TOTAL_MESSAGE=9000
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

START_TIME="$(date +%s)"

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# Print_logs_in_background
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
sleep 1
Create_archive_fifo_info

# Start archive_maker at first.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive1 --subscribe_from "20160101T000000" --subscription test.* --startpoint_filepath archive1.bin < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_0_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
sleep 5

# Start Publish client.
for (( I=1; I<=10; I++ )); do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/${BASENAME}.py  Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} ${I}topic 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $! ) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done

# Wait for clients to be done
for pid in ${CLIENT_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for clients to exit ok..."
	fi
done

Wait_archive_maker archive1

#A="$(xxd -g8 -l8 archive1.bin)"
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 30 2>&1 | tee ${BASENAME}_client_11_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

Wait_and_stop_archive_maker archive1

grep -e "Got ACK for topic test.11topic1, timestamp is" ${BASENAME}_client_0_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Got ACK for topic test.11topic1 is not found in log file."
fi

#COMMENT_OUT
#These tests are not applicable for the new version of Archive_maker.exe, but as it shows how to read/process binary data
#from a file, therefore I leave them here.
#B="$(xxd -g8 -l8 archive1.bin)"
## To get the hex result of the first 8 bytes in file
#AA=${A:10:16}
## To convert to decimal from the hex string.
#Get_as_decimal $AA
#ADEC=$DECIMAL
## To get the hex result of the first 8 bytes after historical data is received.
#AA=${B:10:16}
## To convert the hex string to decimal.
#Get_as_decimal $AA
#BDEC=$DECIMAL
#if [ $ADEC -lt $BDEC ]; then
#	Bail "Save point file is not updated as expected."
#else
#	current_time="$(date +%s)"
#	# Convert it to seconds.
#	BDEC=$(($BDEC/1000000))
#	# Add 30 days (When we publish history data, we published at as 30 days ago)
#	BDEC=$(($BDEC+720*60*60))
#	if [ $current_time -le $BDEC ] || [ $BDEC -le $START_TIME ]; then
#		Bail "Something is wrong with save point file, the assumed timestamp for historical data is not be saved."
#	else
#		echo "Save point file is saved as expected."
#		echo $ADEC
#		echo $BDEC
#	fi
#fi
#COMMENT_OUT

sleep 10

(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive11 --subscribe_from "20160101T000000" --subscription test.* --startpoint_filepath archive1.bin < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_maker_0_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo

sleep 15

timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

sleep 1

${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive11 --subscribe_from "20160101T000000" --subscription test.* --startpoint_filepath archive1.bin --terminate_when_no_more_data 2>&1 | tee -a ${BASENAME}_maker_1_log.txt 

# To check if timestamp and sequence is find in savepoint file for this topic.
grep -e "Find timestamp and sequence for topic: test.11topic1" ${BASENAME}_maker_1_log.txt

if [ "$?" -ne "0" ]; then
	Bail "Grep could not find 'Find timestamp and sequence for topic: test.11topic1' as expected"
fi

Stop_synapse

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive11 --dst joiner1 2>&1 | tee -a ${BASENAME}_client_12_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( I=1; I<=10; I++ )); do
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		dir_name="joiner1/test.${I}topic${F}"
		Check_directory $dir_name
		file_name="joiner1/test.${I}topic${F}/data"
		Check_file $file_name
	done
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner1 --dst joiner1 2>&1 | tee -a ${BASENAME}_client_13_log.txt
# To check if all index and meta files are created as expected.
for (( I=1; I<=10; I++ )); do
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		file_name="joiner1/test.${I}topic${F}/data_meta"
		Check_file $file_name
		for (( J=0; J<=2; J++ )); do
			# we are NOT indexing sequenc number now
			if [ ${J} -ne 1 ]; then
				file_name="joiner1/test.${I}topic${F}/index_${J}_data"
				Check_file $file_name
				file_name="joiner1/test.${I}topic${F}/index_${J}_data_meta"
				Check_file $file_name
			fi
		done
	done
done
# Start our Synapse server with database pointing to join/recover result.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner1
sleep 1
# Subscribe to the Synapse server and check all messages having correct sequence number, timestamp, stream_id and type name.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} topic 0 10 2>&1 | tee -a ${BASENAME}_client_14_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client to exit with error..."
fi

/c/Python27/python.exe ${MYDIR}/${BASENAME}.py SubStrict 1 1 test.11topic 30 2>&1 | tee ${BASENAME}_client_15_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client to exit with error..."
fi

Stop_synapse

rm -fr ./joiner1
rm -fr ./archive1
rm -fr ./archive11
rm -fr ./db
#rm -fr ./log_root
rm -f  ./archive1.bin
rm -f  ./archive_stdin_fifo

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
