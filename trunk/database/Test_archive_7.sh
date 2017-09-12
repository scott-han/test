#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh
. `dirname ${0}`/Test_archive_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}
# This variable is how many message will be published while testing.
TOTAL_MESSAGE=12000
# This variable is to control how many topics we will have during testing.
TOTAL_TOPIC=3

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
make database/basic_recover.exe || Bail
make amqp_0_9_1/server.exe || Bail
make database/Archive_joiner.exe || Bail
# Custom build to simulate Archive_maker get topic truncation timestamp at first.
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/clients/Cpp/Archive_maker.exe\ ./database/Archive_joiner.exe\ ./database/basic_recover.exe\ ./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DFORCE_TRUNCATION_WHILE_SUBSCRIPTION make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# Print_logs_in_background
timestamp="$(date +%s)"
today="$(date -u --date @${timestamp} +%Y%m%d)"
timestamp="$(date +%s --date=${today})" 
mkdir archivist
Create_archive_fifo_info

ddiffd=2
ddifft=4

# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
for (( K=1; K<=10; K++ )); do
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py ${TOTAL_MESSAGE} ${TOTAL_TOPIC} ${timestamp} ${K} 2>&1 | tee ${BASENAME}_pub_client_first_${K}_log.txt 
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for publish client to exit with error..."
	fi
done
# Start Archive_maker and subscuribe to all topics.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ./amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive --subscribe_from "20160101T000000" --subscription "#" --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic ${ddifft} --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo

Wait_archive_maker ./archive 20
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}	

grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive\\[0-9]\{8\} are removed, as they are older than' ${BASENAME}_sub_client_log.txt
if [ "$?" -eq "0" ]
then
	Bail "Grep should not find text(All files from topic test.topic[1-3] in folder archive\YYYYMMDD are removed, as they are older than)..."
fi	

#
#To check if we've already got some output files from Archive_maker.
#As the messages are published once per minute, therefore we can only check
#if there are some message on today's date.
#
temp=$(($timestamp-15*86400))
for (( K=1; K<=10; K++ )); do
	today="$(date -u --date @${temp} +%Y%m%d)"
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
		file_name="./archive/${today}/test.topic${F}/${today}_segment_0_${hash}"
		Check_file $file_name
	done
	temp=$(($temp+86410))
done

# To remove older files in archivist folder.
rm -fr ./archivist/archive
# Copy file to archivist folder again.
cp -r archive archivist
# Generate hash for all files.
for ff in $( find archivist/archive -type f -name "*segment*" ); do
	hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
	echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
done		

for (( K=1; K<=5; K++ )); do
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py ${TOTAL_MESSAGE} ${TOTAL_TOPIC} ${timestamp} $((10+$K)) 2>&1 | tee ${BASENAME}_pub_client_second_${K}_log.txt 
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for publish client to exit with error..."
	fi
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ./amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive --subscribe_from "20160101T000000" --subscription "#" --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic ${ddifft} --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_${K}_log.txt & wait $!) &
	ARCHIVE_MAKER_PID=${!}
	exec 4> archive_stdin_fifo
	Wait_archive_maker ./archive 20
	timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
	wait ${ARCHIVE_MAKER_PID}		
			
	# Check file is created
	temp=$(($timestamp-(6-$K)*86400))
	today="$(date -u --date @${temp} +%Y%m%d)"
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
		file_name="./archive/${today}/test.topic${F}/${today}_segment_0_${hash}"
		Check_file $file_name
	done
	
	# To remove older files in archivist folder.
	rm -fr ./archivist/archive
	# Copy file to archivist folder again.
	cp -r archive archivist
	# Generate hash for all files.
	for ff in $( find archivist/archive -type f -name "*segment*" ); do
		hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
		echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
	done		
	# Check

	delete=$(($temp-($ddiffd+2)*86400))
	del_date="$(date -u --date @${delete} +%Y%m%d)"
	dir_name=archive/${del_date}
	if [ -d ${dir_name} ]; then
		Bail "Files in folder ${del_date} are not removed as expected."
	fi

		
	grep -i 'Start archived file management, Days_to_delete_archived_files = 2, Days_to_truncate_topic = 4' ${BASENAME}_sub_client_${K}_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Grep could not find text(Start archived file management, Days_to_delete_archived_files = 2, Days_to_truncate_topic = 4) ..."
	fi			
	grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive\\[0-9]\{8\} are removed, as they are older than' ${BASENAME}_sub_client_${K}_log.txt
	if [ "$?" -ne "0" ]
	then
		Bail "Grep could not find text(All files from topic test.topic[1-3] in folder archive\YYYYMMDD are removed, as they are older than)..."
	fi
	for (( T=1; T<=${TOTAL_TOPIC}; T++ )); do
		grep -i "Truncate topic test.topic${T} at" ${BASENAME}_sub_client_${K}_log.txt
		if [ "$?" -ne "0" ]; then
			Bail "Truncate topic test.topic${T} is not found."
		fi
		grep -i "Successfully truncate topic test.topic${T} at"  ${BASENAME}_sub_client_${K}_log.txt
		if [ "$?" -ne "0" ]; then
			grep -i "Subscription with truncated parameter topic=test.topic${T}"  ${BASENAME}_sub_client_${K}_log.txt
			if [ "$?" -ne "0" ]; then
				Bail "Truncation topic command is not sent to server."
			fi
		fi
	done
	grep -e 'All files for topic test.topic[1-3]\{1\} in folder archive\\[0-9]\{8\} are checked, but they will not be removed as the condition is not met' ${BASENAME}_sub_client_${K}_log.txt
	if [ "$?" -eq "0" ]
	then
		Bail "Grep should not find text(All files for topic test.topic[1-3] in folder archive\\YYYYMMDD are checked, but they will not be removed as the condition is not met)..."
	fi			
	grep -e "Subscription with truncated parameter topic=test.topic[1-3]" ${BASENAME}_sub_client_${K}_log.txt | while read -r line ; do
		BDEC=${line##* }
		BDEC=$(($BDEC/1000000))
		BDEC=$(($BDEC+96*60*60)) #4 days.
		if [ $temp -le $BDEC ]; then
			Bail "Topic test.topic[1-3] is truncated with wrong timestamp."
			break
		fi
	done
	grep -e "Successfully truncate topic test.topic test.topic[1-3] at" ${BASENAME}_sub_client_${K}_log.txt | while read -r line ; do
		BDEC=${line##* }
		BDEC=$(($BDEC/1000000))
		BDEC=$(($BDEC+96*60*60)) #4 days.
		if [ $temp -le $BDEC ]; then
			Bail "Topic test.topic[1-3] is truncated with wrong timestamp."
			break
		fi
	done					
done

# Stop synapse server

Stop_synapse
Sleep 1

grep -i "Atomic_subscription_cork(=0) Tcp_no_delay(=false) Sparsify_upto_timestamp(=" synapse_log.txt
if [ "$?" -ne "0" ]; then
	grep -i "on_basic_consume with Sparsify_only Sparsify_upto_timestamp(=" synapse_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Grep could not find expected (on_basic_consume with Sparsify_only Sparsify_upto_timestamp(=) in Synapse's log"
	fi
fi
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi

# All data files are not removed as we may need to check them manually, but if you want to remove them, please uncomment below 3 lines.
rm -fr ./archive
rm -fr ./db

# To test archive_joiner insert 8 bytes zero into the output file if there is no any overlap between two segment files.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
timestamp="$(date +%s)"
today="$(date -u --date @${timestamp} +%Y%m%d)"
timestamp="$(date +%s --date=${today})" 
sleep 1
for (( K=1; K<=10; K++ )); do
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py 5 5 ${timestamp} ${K} 2>&1 | tee ${BASENAME}_pub_client_first_${K}_log.txt 
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for publish client to exit with error..."
	fi
done

./amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive --subscribe_from "20160101T000000" --subscription "#" --terminate_when_no_more_data 2>&1 | tee -a ${BASENAME}_archiver_log.txt

temp=$(($timestamp-13*86400))
for (( K=3; K<=8; K++ )); do
	today="$(date -u --date @${temp} +%Y%m%d)"
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
		file_name="./archive/${today}/test.topic${F}/${today}_segment_0_${hash}"
		Check_file $file_name
	done
	if [ "${K}" -ne "6" ]; then
		rm -fr ./archive/${today}/
	fi
	temp=$(($temp+86410))
done

${IVOCATION_DIR}/database/Archive_joiner.exe --src archive --dst joined 2>&1 | tee -a ${BASENAME}_joiner_1_log.txt

grep -i "There is no overlap after file" ${BASENAME}_joiner_1_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected (There is no overlap after file) in ${BASENAME}_joiner_1_log.txt"
fi

${IVOCATION_DIR}/database/basic_recover.exe --src joined --dst recovered --inhibit_delta_poisoning false 2>&1 | tee -a ${BASENAME}_recover_1_log.txt

total="$(grep -c "Delta Poisons: 2" ${BASENAME}_recover_1_log.txt)"
if [ "$total" -ne "5" ]; then
	Bail "Grep could not find expected (Delta Poisons: 2) in ${BASENAME}_recover_1_log.txt" 
fi

total="$(grep -c "Messages: 27" ${BASENAME}_recover_1_log.txt)"
if [ "$total" -ne "5" ]; then
	Bail "Grep could not find expected (Messages: 27) in ${BASENAME}_recover_1_log.txt" 
fi

Stop_synapse
Sleep 1

rm -fr ./archive
rm -fr ./db
rm -fr ./joined
rm -fr ./recovered

rm -f archive_stdin_fifo
rm -fr ./archivist
cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
