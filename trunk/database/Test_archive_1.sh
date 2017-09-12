#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh
. `dirname ${0}`/Test_archive_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}
# This variable is how many message will be published while testing.
TOTAL_MESSAGE=3000
# This variable is to control how many topics we will have during testing.
TOTAL_TOPIC=30

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
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 1451606400000000 2>&1 | tee ${BASENAME}_client_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Publish client did not exit with error..."
fi
Create_archive_fifo_info

for (( II=1; II<=2; II++ )); do
	# Start Archive_maker and subscuribe to all topics.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000000" --subscription test.* < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_${II}_log.txt & wait $!) &
	ARCHIVE_MAKER_PID=${!}
	exec 4> archive_stdin_fifo

	if [ ${II} -gt 1 ]; then
		for(( K=1; K<=$((${II})); K++ )); do
			Wait_and_stop_archive_maker ./archive${II}
			# All others followed Archive_maker running, we don't need to provide savepoint_filepath option anymore.
			# Here other instances of Archive_maker is subscribed from different start time, and the early started one is to subscribe
			# to a late start point, just to test Archive_joiner can still merge them togegher without any issue. So after all these
			# Archive_maker instances are completed, the files should like this.
			# Topic_YYYYMMDD_segment_0, subscribed from 2016-01-01 00:00:00.
			# Topic_YYYYMMDD_segment_1, subscribed from The day after tomorrow.
			# Topic_YYYYMMDD_segment_2, subscribed from tomorrow.
			(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000$((9-${K}))00" --subscription test.* < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_${II}_log.txt & wait $!) &
			ARCHIVE_MAKER_PID=${!}
			exec 4> archive_stdin_fifo
		done
	fi

	Wait_and_stop_archive_maker ./archive${II}
	#
	#To check if we've already got some output files from Archive_maker.
	#As the messages are published once per minute, therefore we can only check
	#if there are some message on today's date.
	#
	today="20160101"
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
		file_name="./archive${II}/${today}/test.topic${F}/${today}_segment_0_${hash}"
		Check_file $file_name
	done

	# To check if segment_n is genereated as expected.
	if [ ${II} -ge 2 ]; then
		today="20160101"
		for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
			hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
			file_name="./archive${II}/${today}/test.topic${F}/${today}_segment_${II}_${hash}"
			Check_file $file_name
		done
	fi

	# Stop snapse server and clean environment.
	Stop_synapse
	sleep 1

	# Start Archive_joiner and join all segment files left from Archive_maker.
	${IVOCATION_DIR}/database/Archive_joiner.exe --src archive${II} --dst joiner${II} 2>&1 | tee -a ${BASENAME}_client_${II}_log.txt
	
	# To check if all topic folders are created and data file is generated under topic folder.
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		dir_name="joiner${II}/test.topic${F}"
		Check_directory $dir_name
		file_name="joiner${II}/test.topic${F}/data"
		Check_file $file_name
	done

	# Start basic_recover on all files left from Archive_joiner
	${IVOCATION_DIR}/database/basic_recover.exe --src joiner${II} --dst joiner${II} 2>&1 | tee -a ${BASENAME}_client_${II}_log.txt
	
	# To check if all index and meta files are created as expected.
	for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
		file_name="joiner${II}/test.topic${F}/data_meta"
		Check_file $file_name
		for (( J=0; J<=2; J++ )); do
			# we are NOT indexing sequenc number now
			if [ ${J} -ne 1 ]; then
				file_name="joiner${II}/test.topic${F}/index_${J}_data"
				Check_file $file_name
				file_name="joiner${II}/test.topic${F}/index_${J}_data_meta"
				Check_file $file_name
			fi
		done
	done
	
	# One more check, ensure files on the day after tomorrow are processed as segment_0, segment_1 and then segment_2 as this version of
	# Archive_maker get the latest archived timestamp from archived file, and it always use the latest archived timestamp as
	# the subscribe from.
	if [ ${II} -eq 2 ]; then
		today="20160101"
		for ((F=1; F<=${TOTAL_TOPIC}; F++)); do
			found="processing): archive${II}\\${today}\test.topic${F}\${today}_segment_"
			file_seg=0
			grep -F "$found" ${BASENAME}_client_${II}_log.txt | while read -r line ; do
				actual_seg=${line: -1}
				case $file_seg in
					0)
					if [ $actual_seg -ne 0 ]; then
						Bail "File segment is not processed as expected."
						break
					fi
					;;
					1)
					if [ $actual_seg -ne 1 ]; then
						Bail "File segment is not processed as expected."
						break
					fi
					;;
					2)
					if [ $actual_seg -ne 2 ]; then
						Bail "File segment is not processed as expected."
						break
					fi
					;;
				esac
				file_seg=$(($file_seg+1))
			done
		done
		echo "File segments for all topics are processed as expected order."
	fi

	# Start synapse server again, but this time the database_root is pointing to the join/recover result.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner${II}

	sleep 1
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 2>&1 | tee -a ${BASENAME}_client_${II}_log.txt 
	if [ "$?" -ne "0" ]
	then
		Bail "Subscribing client did not exit with error..."
	fi

	if [ ${II} -lt 2 ]; then
		#Stop this synapse server as it is started from database ./joiner[I]
		Sleep 5
		Stop_synapse
		
		Sleep 4
		#Start snapse at default database (./db)
		Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
		Sleep 1
	fi
done
# Stop synapse server
Stop_synapse
Sleep 5
TOTAL_TOPIC=3
# Start test archived file management.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root archived_file
# Publish 12000 messages to 3 topics, start from 20160101T000000 (GMT)
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 12000 ${TOTAL_TOPIC} 1451606400000000 1000000 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive3 --subscribe_from "20160101T000000" --terminate_when_no_more_data --subscription test.* 2>&1 | tee -a ${BASENAME}_client_3_${I}_log.txt 

# Start Archive_joiner and join all segment files left from Archive_maker.
${IVOCATION_DIR}/database/Archive_joiner.exe --src archive3 --dst joiner3 2>&1 | tee -a ${BASENAME}_client_3_joiner_log.txt

# To check if all topic folders are created and data file is generated under topic folder.
for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
	dir_name="joiner3/test.topic${F}"
	Check_directory $dir_name
	file_name="joiner3/test.topic${F}/data"
	Check_file $file_name
done

# Start basic_recover on all files left from Archive_joiner
${IVOCATION_DIR}/database/basic_recover.exe --src joiner3 --dst joiner3 2>&1 | tee -a ${BASENAME}_client_3_recover_log.txt

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

# To prepare the Archivist folder, copy archived files to there and generate sha256 files.
mkdir 2016
mkdir 2016/201601
cp -r archive3 2016/201601
for ff in $( find 2016/201601/archive3 -type f -name "201601*" ); do
	hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
	echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
done

# To create an invalid sha256 file (Archive_maker will treat this as incorrect copy of the file)
for ff in $( find 2016/201601/archive3/20160102/test.topic1 -type f -name "*.sha256" ); do
	echo "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f" > ${ff}
done
# To set the start point for removing files and truncating topics.
ddiff=7

# Start another instance, this time with command to remove and truncate topic etc.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive3 --subscribe_from "20160109T000000" --subscription test.* --days_to_delete_archived_file ${ddiff} --days_to_truncate_topic $(($ddiff+10)) --timediff_alert echo --timediff_tolerance 1 --archivist_path YYYY/YYYYMM/archive3/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_3_final_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_archive_maker ./archive3 30
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

# To check if files in folder 20160101 are removed and related log is found.
grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive3\\20160101 are removed, as they are older than' ${BASENAME}_client_3_final_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected text(All files from topic test.topic[1-3] in folder archive3\20160101 are removed, as they are older than)..."
fi

# To check if 20160101 is removed as expected, but all others are not removed.
# test.topic1 should be removed in 20160101
if [ -d "archive3/20160101/test.topic1" ]; then
	Bail "Topic1 is not removed in folder 20160101"
fi
# But test.topic2 and test.topic3 should not be removed as test.topic1 is failed in 20160102
dir_name="archive3/20160101/test.topic2"
Check_directory $dir_name
dir_name="archive3/20160101/test.topic3"
Check_directory $dir_name
for (( T=1; T <= ${TOTAL_TOPIC}; T++ )); do
	dir_name="archive3/20160102/test.topic${T}"
	Check_directory $dir_name;
	dir_name="archive3/20160106/test.topic${T}"
	Check_directory $dir_name;
	dir_name="archive3/20160107/test.topic${T}"
	Check_directory $dir_name;
	dir_name="archive3/20160108/test.topic${T}"
	Check_directory $dir_name;	
done
# To check if "Calculated hash" error is found in log.
grep -i 'After calling system(echo \"Calculated hash' ${BASENAME}_client_3_final_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected text(After calling system(echo \"Calculated hash)..."
fi

# To remove older files in archivist folder.
rm -fr ./2016/201601/archive3
# Copy file to archivist folder again.
cp -r archive3 2016/201601
# Generate hash for all files.
for ff in $( find 2016/201601/archive3 -type f -name "201601*" ); do
	hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
	echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
done

# Subscribe again with different parameters (days_to_truncate_topic is less than days_to_delete_archived_file)
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive3 --subscribe_from "20160109T000000" --subscription test.* --days_to_delete_archived_file $(($ddiff-2)) --days_to_truncate_topic $(($ddiff-4)) --timediff_alert echo --timediff_tolerance 1 --archivist_path YYYY/YYYYMM/archive3/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_3_extra_log.txt & wait $!) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_archive_maker ./archive3 30
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}
# To check if all files are removed as expected.
grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive3\\20160104 are removed, as they are older than' ${BASENAME}_client_3_extra_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected text(All files from topic test.topic[1-3] folder archive3\20160104 are removed, as they are older than)..."
fi

grep -e 'All files for topic test.topic[1-3]\{1\} in folder archive3\\20160106 are checked, but they will not be removed as the condition is not met' ${BASENAME}_client_3_extra_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not find expected text(All files for topic test.topic[1-3] in folder archive3\\20160106 are checked, but they will not be removed as the condition is not met)..."
fi

for (( T=1; T <= ${TOTAL_TOPIC}; T++ )); do
	if [ -d "archive3/20160103/test.topic${T}" ]; then
		Bail "Topic ${T} is not removed in folder 20160103"
	fi
	if [ -d "archive3/20160102/test.topic${T}" ]; then
		Bail "Topic ${T} is not removed in folder 20160102"
	fi
done

for (( T=1; T <= ${TOTAL_TOPIC}; T ++ )); do
	grep -i "Truncate topic test.topic${T} at" ${BASENAME}_client_3_extra_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Truncate topic test.topic${T} is not found."
	fi
done

# 20160107 should not be removed.
dir_name="archive3/20160107"
Check_directory $dir_name
if [ -d "archive3/20160104" ]; then
	Bail "Folder 20160104 should be removed."
fi

# Subscribe again with different parameters, simulate Archive_maker is started next day (so days_to_delete_archived_file and days_to_truncate_topic are all decreased by 1)
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive3 --subscribe_from "20160109T000000" --subscription test.* --days_to_delete_archived_file $(($ddiff-3)) --days_to_truncate_topic $(($ddiff-5)) --timediff_alert echo --timediff_tolerance 1 --archivist_path YYYY/YYYYMM/archive3/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_3_another_log.txt & wait $! ) &
ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
Wait_archive_maker ./archive3 30
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

# should find 20160107 is removed now.
grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive3\\20160105 are removed, as they are older than' ${BASENAME}_client_3_another_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep should find text(All files from topic test.topic[1-3] in folder archive3\\20160105 are removed, as they are older than)..."
fi
# Folder 20160104 should be removed this time.
if [ -d "archive3/20160105" ]; then
	Bail "Folder 20160105 should be removed."
fi

# 20160109 should be checked this time.
grep -e 'All files for topic test.topic[1-3]\{1\} in folder archive3\\20160107 are checked, but they will not be removed as the condition is not met' ${BASENAME}_client_3_another_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep should find text(All files for topic test.topic[1-3] in folder archive3\\20160107 are checked, but they will not be removed as the condition is not met)..."
fi

# Stop synapse server (on original database)
Stop_synapse
Sleep 1
# To verify data is identical after archiving, joining and recovering.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner3
Sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 12000 ${TOTAL_TOPIC} 1000000 2>&1 | tee -a ${BASENAME}_client_3_sub_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscribe client to exit with error..."
fi

Stop_synapse
Sleep 1

# All data files are not removed as we may need to check them manually, but if you want to remove them, please uncomment below 3 lines.
for (( I=1; I<=3; I++ )); do
	rm -fr ./archive${I}
	rm -fr ./joiner${I}
done
rm -f archive_stdin_fifo
rm -fr ./db
rm -fr ./2016
rm -fr ./archived_file
cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
