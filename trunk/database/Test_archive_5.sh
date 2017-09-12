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

Print_logs_in_background
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
sleep 1
Create_archive_fifo_info

sleep 1
 /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

# 1st test, make sure it works as usual when neither --terminate_when_no_more_data nor --subscribe_to is provided.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive1 --subscribe_from "20160101T000000" --subscription test.* < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_1_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_and_stop_archive_maker archive1

grep -i " Archive_maker quits automatically (All data is received)" ${BASENAME}_client_1_log.txt
if [ "$?" -eq "0" ]
then
	Bail "Grep should not find this in log file(Archive_maker quits automatically (All data is received))."
fi

# 2nd test, make sure --terminate_when_no_more_data works as expected.
${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive2 --subscribe_from "20160101T000000" --subscription test.* --terminate_when_no_more_data 2>&1 | tee -a ${BASENAME}_client_2_log.txt 
grep -i " Archive_maker quits automatically (All data is received)" ${BASENAME}_client_2_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception( Archive_maker quits automatically (All data is received))..."
fi

# 3rd test, make sure --subscribe_to is working as expected.
${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive3 --subscribe_from "20160101T000000" --subscription test.* --subscribe_to "20160215T000000" 2>&1 | tee -a ${BASENAME}_client_3_log.txt 
grep -i " Archive_maker quits automatically (All data is received)" ${BASENAME}_client_3_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception( Archive_maker quits automatically (All data is received))..."
fi

# 4th test, it is working as expected when both --terminate_when_no_more_data and --subscribe_to are provided, subscribe_to will be reached at first.

${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive4 --subscribe_from "20160101T000000" --subscription test.* --subscribe_to "20160215T000000" --terminate_when_no_more_data 2>&1 | tee -a ${BASENAME}_client_4_log.txt 
grep -i " Archive_maker quits automatically (All data is received)" ${BASENAME}_client_4_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception( Archive_maker quits automatically (All data is received))..."
fi

# 5th test, working well when both are specified, but this time, terminate_when_no_more_data will be reached at first.
${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive5 --subscribe_from "20160101T000000" --subscription test.* --subscribe_to "20160715T000000" --terminate_when_no_more_data 2>&1 | tee -a ${BASENAME}_client_5_log.txt 
grep -i " Archive_maker quits automatically (All data is received)" ${BASENAME}_client_5_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected exception( Archive_maker quits automatically (All data is received))..."
fi

Stop_synapse

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive2 --dst joiner2 2>&1 | tee -a ${BASENAME}_client_6_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	dir_name="joiner2/test.topic${F}"
	Check_directory $dir_name
	file_name="joiner2/test.topic${F}/data"
	Check_file $file_name
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner2 --dst joiner2 2>&1 | tee -a ${BASENAME}_client_7_log.txt
# To check if all index and meta files are created as expected.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	file_name="joiner2/test.topic${F}/data_meta"
	Check_file $file_name
	for (( J=0; J<=2; J++ )); do
		# we are NOT indexing sequenc number now
		if [ ${J} -ne 1 ]; then
			file_name="joiner2/test.topic${F}/index_${J}_data"
			Check_file $file_name
			file_name="joiner2/test.topic${F}/index_${J}_data_meta"
			Check_file $file_name
		fi
	done
done
# Start our Synapse server with database pointing to join/recover result.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner2
sleep 1
# Subscribe to the Synapse server and check all messages having correct sequence number and timestamp. As only terminate_when_no_more_data is provided,
# therefore all messages should be received.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 2>&1 | tee -a ${BASENAME}_client_8_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client (joiner2) to exit with error..."
fi

Stop_synapse

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive3 --dst joiner3 2>&1 | tee -a ${BASENAME}_client_9_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	dir_name="joiner3/test.topic${F}"
	Check_directory $dir_name
	file_name="joiner3/test.topic${F}/data"
	Check_file $file_name
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner3 --dst joiner3 2>&1 | tee -a ${BASENAME}_client_10_log.txt
# To check if all index and meta files are created as expected.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	file_name="joiner3/test.topic${F}/data_meta"
	Check_file $file_name
	for (( J=0; J<=2; J++ )); do
		# we are NOT indexing sequenc number now
		if [ ${J} -ne 1 ]; then
			file_name="joiner3/test.topic${F}/index_${J}_data"
			Check_file $file_name
			file_name="joiner3/test.topic${F}/index_${J}_data_meta"
			Check_file $file_name
		fi
	done
done
# Start our Synapse server with database pointing to join/recover result.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner3
sleep 1
# Subscribe to the Synapse server and check all messages having correct sequence number and timestamp. As only subscribe_to is provided, therefore
# only 1081 messages will be received for each topic.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py SubLimit 1082 ${TOTAL_TOPIC} 2>&1 | tee -a ${BASENAME}_client_11_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client (joiner3) to exit with error..."
fi

Stop_synapse

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive4 --dst joiner4 2>&1 | tee -a ${BASENAME}_client_12_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	dir_name="joiner4/test.topic${F}"
	Check_directory $dir_name
	file_name="joiner4/test.topic${F}/data"
	Check_file $file_name
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner4 --dst joiner4 2>&1 | tee -a ${BASENAME}_client_13_log.txt
# To check if all index and meta files are created as expected.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	file_name="joiner4/test.topic${F}/data_meta"
	Check_file $file_name
	for (( J=0; J<=2; J++ )); do
		# we are NOT indexing sequenc number now
		if [ ${J} -ne 1 ]; then
			file_name="joiner4/test.topic${F}/index_${J}_data"
			Check_file $file_name
			file_name="joiner4/test.topic${F}/index_${J}_data_meta"
			Check_file $file_name
		fi
	done
done
# Start our Synapse server with database pointing to join/recover result.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner4
sleep 1
# Subscribe to the Synapse server and check all messages having correct sequence number and timestamp. As both subscribe_to and terminate_when_no_more_data
# are provided, and subscribe_to will be reached at first, therefore only 1081 messages will be received for each topic.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py SubLimit 1082 ${TOTAL_TOPIC} 2>&1 | tee -a ${BASENAME}_client_14_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client (joiner4) to exit with error..."
fi

Stop_synapse

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive5 --dst joiner5 2>&1 | tee -a ${BASENAME}_client_15_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	dir_name="joiner5/test.topic${F}"
	Check_directory $dir_name
	file_name="joiner5/test.topic${F}/data"
	Check_file $file_name
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner5 --dst joiner5 2>&1 | tee -a ${BASENAME}_client_16_log.txt
# To check if all index and meta files are created as expected.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	file_name="joiner5/test.topic${F}/data_meta"
	Check_file $file_name
	for (( J=0; J<=2; J++ )); do
		# we are NOT indexing sequenc number now
		if [ ${J} -ne 1 ]; then
			file_name="joiner5/test.topic${F}/index_${J}_data"
			Check_file $file_name
			file_name="joiner5/test.topic${F}/index_${J}_data_meta"
			Check_file $file_name
		fi
	done
done
# Start our Synapse server with database pointing to join/recover result.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner5
sleep 1
# Subscribe to the Synapse server and check all messages having correct sequence number and timestamp (As the subscirbe_to is in future, therefore all messages should be received.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 2>&1 | tee -a ${BASENAME}_client_17_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client (joiner5) to exit with error..."
fi

Stop_synapse
sleep 1

# clean out.
rm -fr ./archive1;
for (( F=2; F<=5; F++)); do
	rm -fr ./archive${F}
	rm -fr ./joiner${F}
done
rm -fr ./db
rm -f  ./archive_stdin_fifo

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
