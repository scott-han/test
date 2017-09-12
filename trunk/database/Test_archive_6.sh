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
make amqp_0_9_1/clients/Cpp/Archive_maker.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/clients/Cpp/Archive_maker.exe\ ./database/Archive_joiner.exe\ ./database/basic_recover.exe\ ./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
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
for (( II=1; II<=3; II++ )); do
	case ${II} in
		1) ddiffd=50; ddifft=45 ;;
		2) ddiffd=9; ddifft=7 ;;
		3) ddiffd=6; ddifft=7 ;;
	esac
	# Start Snapse server with the default database root ./db${II} and publish some data
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db${II}
	sleep 1
	for (( K=1; K<=10; K++ )); do
		/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} ${timestamp} ${K} 2>&1 | tee ${BASENAME}_pub_client_${II}_first_${K}_log.txt 
		if [ "$?" -ne "0" ]
		then
			Bail "Waiting for publish client to exit with error..."
		fi
	done
	# Start Archive_maker and subscuribe to all topics.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000000" --subscription "#" --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic ${ddifft} --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive${II}/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_${II}_log.txt & wait $!) &
	ARCHIVE_MAKER_PID=${!}
	exec 4> archive_stdin_fifo

	Wait_archive_maker ./archive${II} 20
	timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
	wait ${ARCHIVE_MAKER_PID}	
	
	grep -e 'All files in folder archive${II}\\[0-9]\{6\} are removed, as they are older than' ${BASENAME}_sub_client_${II}_log.txt
	if [ "$?" -eq "0" ]
	then
		Bail "Grep should not find text(All files in folder archive${II}\YYYYMMDD are removed, as they are older than)..."
	fi	
	
	#
	#To check if we've already got some output files from Archive_maker.
	#As the messages are published once per minute, therefore we can only check
	#if there are some message on today's date.
	#
	temp=$(($timestamp-30*86400))
	for (( K=1; K<=10; K++ )); do
		today="$(date -u --date @${temp} +%Y%m%d)"
		for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
			hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
			file_name="./archive${II}/${today}/test.topic${F}/${today}_segment_0_${hash}"
			Check_file $file_name
		done
		temp=$(($temp+86410))
	done
	
	# To remove older files in archivist folder.
	rm -fr ./archivist/archive${II}
	# Copy file to archivist folder again.
	cp -r archive${II} archivist
	# Generate hash for all files.
	for ff in $( find archivist/archive${II} -type f -name "*segment*" ); do
		hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
		echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
	done		
	
	for (( K=1; K<=8; K++ )); do
		/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} ${timestamp} $((10+$K)) 2>&1 | tee ${BASENAME}_pub_client_${II}_second_${K}_log.txt 
		if [ "$?" -ne "0" ]
		then
			Bail "Waiting for publish client to exit with error..."
		fi
		(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000000" --subscription "#" --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic ${ddifft} --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive${II}/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_${II}_${K}_log.txt & wait $!) &
		ARCHIVE_MAKER_PID=${!}
		exec 4> archive_stdin_fifo
		Wait_archive_maker ./archive${II} 20
		timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
		wait ${ARCHIVE_MAKER_PID}		
				
		# Check file is created
		temp=$(($timestamp-(21-$K)*86400))
		today="$(date -u --date @${temp} +%Y%m%d)"
		for (( F=1; F<=${TOTAL_TOPIC}; F++ )); do
			hash=$(printf '%s' "localhost:5672|test.topic${F}" | md5sum | cut -d ' ' -f 1)
			file_name="./archive${II}/${today}/test.topic${F}/${today}_segment_0_${hash}"
			Check_file $file_name
		done
		
		# To remove older files in archivist folder.
		rm -fr ./archivist/archive${II}
		# Copy file to archivist folder again.
		cp -r archive${II} archivist
		# Generate hash for all files.
		for ff in $( find archivist/archive${II} -type f -name "*segment*" ); do
			hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
			echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
		done		
		# Check
		if [ ${II} -gt 1 ]; then
			delete=$(($temp-($ddiffd+2)*86400))
			del_date="$(date -u --date @${delete} +%Y%m%d)"
			dir_name=archive${II}/${del_date}
			if [ -d ${dir_name} ]; then
				Bail "Files in folder ${del_date} are not removed as expected."
			fi
		fi
		case ${II} in
			1) 
			grep -i 'Start archived file management, Days_to_delete_archived_files = 50, Days_to_truncate_topic = 45' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -ne "0" ]; then
				Bail "Grep could not find text(Start archived file management, Days_to_delete_archived_files = 50, Days_to_truncate_topic = 45) ..."
			fi
			grep -e 'All files in folder archive1\\[0-9]\{8\} are removed, as they are older than' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -eq "0" ]
			then
				Bail "Grep should not find text(All files in folder archive${II}\YYYYMMDD are removed, as they are older than)..."
			fi
			grep -e 'All files in folder archive1\\[0-9]\{8\} are checked, but they will not be removed as the condition is not met' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -eq "0" ]
			then
				Bail "Grep should not find text(All files in folder archive{II}\\YYYYMMDD are checked, but they will not be removed as the condition is not met)..."
			fi
			;;
			2) 
			grep -i 'Start archived file management, Days_to_delete_archived_files = 9, Days_to_truncate_topic = 7' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -ne "0" ]; then
				Bail "Grep could not find text(Start archived file management, Days_to_delete_archived_files = 9, Days_to_truncate_topic = 7) ..."
			fi			
			grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive2\\[0-9]\{8\} are removed, as they are older than' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -ne "0" ]
			then
				Bail "Grep could not find text(All files from topic test.topic[1-3] in folder archive${II}\YYYYMMDD are removed, as they are older than)..."
			fi
			for (( T=1; T<=${TOTAL_TOPIC}; T++ )); do
				grep -i "Truncate topic test.topic${T} at" ${BASENAME}_sub_client_${II}_${K}_log.txt
				if [ "$?" -ne "0" ]; then
					Bail "Truncate topic test.topic${T} is not found."
				fi
			done			
			;;
			3)
			grep -i 'Start archived file management, Days_to_delete_archived_files = 6, Days_to_truncate_topic = 7' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -ne "0" ]; then
				Bail "Grep could not find text(Start archived file management, Days_to_delete_archived_files = 6, Days_to_truncate_topic = 7) ..."
			fi			
			grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive3\\[0-9]\{8\} are removed, as they are older than' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -ne "0" ]
			then
				Bail "Grep could not find text(All files from topic test.topic[1-3] in folder archive${II}\YYYYMMDD are removed, as they are older than)..."
			fi
			for (( T=1; T<=${TOTAL_TOPIC}; T++ )); do
				grep -i "Truncate topic test.topic${T} at" ${BASENAME}_sub_client_${II}_${K}_log.txt
				if [ "$?" -ne "0" ]; then
					Bail "Truncate topic test.topic${T} is not found."
				fi
			done
			grep -e 'All files for topic test.topic[1-3]\{1\} in folder archive3\\[0-9]\{8\} are checked, but they will not be removed as the condition is not met' ${BASENAME}_sub_client_${II}_${K}_log.txt
			if [ "$?" -eq "0" ]
			then
				Bail "Grep should not find text(All files for topic test.topic[1-3] in folder archive{II}\\YYYYMMDD are checked, but they will not be removed as the condition is not met)..."
			fi			
			;;
		esac
	done
	# No data in the last 10 days, but 3 days more data then.
	for (( K=1; K<=3; K++ )); do
		/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} 2 ${timestamp} $((27+$K)) 2>&1 | tee ${BASENAME}_pub_client_${II}_final_log.txt 
		if [ "$?" -ne "0" ]
		then
			Bail "Waiting for publish client to exit with error..."
		fi
	done
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000000" --subscription "#" --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic ${ddifft} --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive${II}/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_${II}_30_log.txt & wait $!) &
	ARCHIVE_MAKER_PID=${!}
	exec 4> archive_stdin_fifo
	Wait_archive_maker ./archive${II} 20
	timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
	wait ${ARCHIVE_MAKER_PID}
	
	if [ ${II} -gt 1 ]; then
		for (( T=1; T<=3; T++ )); do
			grep -i "Successfully truncate topic test.topic${T} at" ${BASENAME}_sub_client_${II}_30_log.txt
			if [ "$?" -ne "0" ]; then
				grep -i "Subscription with truncated parameter topic=test.topic${T}" ${BASENAME}_sub_client_${II}_30_log.txt
				if [ "$?" -ne "0" ]; then
					Bail "Truncation topic command is not sent to server."
				fi
			fi
		done

		BDEC1="$(grep -e "Successfully truncate topic test.topic1" ${BASENAME}_sub_client_${II}_30_log.txt | while read -r line ; do
			BDEC1=${line##* }
			BDEC1=$(($BDEC1/1000000))
			echo ${BDEC1}
		done)"		
		if [ -z $BDEC1 ]; then
			BDEC1="$(grep -e "Subscription with truncated parameter topic=test.topic1" ${BASENAME}_sub_client_${II}_30_log.txt | while read -r line ; do
				BDEC1=${line##* }
				BDEC1=$(($BDEC1/1000000))
				echo ${BDEC1}
			done)"				
		fi
		BDEC3="$(grep -e "Successfully truncate topic test.topic3" ${BASENAME}_sub_client_${II}_30_log.txt | while read -r line ; do
			BDEC3=${line##* }
			BDEC3=$(($BDEC3/1000000))
			echo ${BDEC3}
		done)"
		if [ -z $BDEC3 ]; then
			BDEC3="$(grep -e "Subscription with truncated parameter topic=test.topic3" ${BASENAME}_sub_client_${II}_30_log.txt | while read -r line ; do
				BDEC3=${line##* }
				BDEC3=$(($BDEC3/1000000))
				echo ${BDEC3}
			done)"
		fi
		if [ ${BDEC1} -lt ${BDEC3} ]; then
			Bail "Truncation time of test.topic1 cannot less than that of test.topic3"
		fi
	fi
	if [ ${II} -eq 2 ]; then
		# To remove older files in archivist folder.
		rm -fr ./archivist/archive${II}
		# Copy file to archivist folder again.
		cp -r archive${II} archivist
		# Generate hash for all files.
		for ff in $( find archivist/archive${II} -type f -name "*segment*" ); do
			hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
			echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
		done
		(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000000" --subscription "#" --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic 1 --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive${II}/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_${II}_31_log.txt & wait $!) &
		ARCHIVE_MAKER_PID=${!}
		exec 4> archive_stdin_fifo
		Wait_archive_maker ./archive${II} 20
		timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
		wait ${ARCHIVE_MAKER_PID}	

		topic1="$(grep -c 'All files from topic test.topic1 in folder archive2' ${BASENAME}_sub_client_${II}_31_log.txt)"
		topic3="$(grep -c 'All files from topic test.topic3 in folder archive2' ${BASENAME}_sub_client_${II}_31_log.txt)"
		if [ -z $topic3 ]; then
			if [ ${topic1} -le ${topic3} ]; then
				Bail "Total removed files for topic1 should more than that for topic3."
			fi
		fi
		# Do it again, this time, we only archive 1 topic.
		# To remove older files in archivist folder.
		rm -fr ./archivist/archive${II}
		# Copy file to archivist folder again.
		cp -r archive${II} archivist
		# Generate hash for all files.
		for ff in $( find archivist/archive${II} -type f -name "*segment*" ); do
			hash=$(sha256sum ${ff} | cut -d ' ' -f 1)
			echo ${hash} | fold -w2 | paste -sd' ' > ${ff}.sha256
		done
		(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive${II} --subscribe_from "20160101T000000" --subscription test.topic1 --days_to_delete_archived_file ${ddiffd} --days_to_truncate_topic 1 --timediff_alert echo --timediff_tolerance 1 --archivist_path archivist/archive${II}/YYYYMMDD < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_sub_client_${II}_32_log.txt & wait $!) &
		ARCHIVE_MAKER_PID=${!}
		exec 4> archive_stdin_fifo
		Wait_archive_maker ./archive${II} 20
		timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
		wait ${ARCHIVE_MAKER_PID}	
		grep -e 'All files for topic test.topic[1-3]\{1\} in folder archive2\\[0-9]\{8\} are checked, but they will not be removed as the condition is not met' ${BASENAME}_sub_client_${II}_32_log.txt
		if [ "$?" -ne "0" ]
		then
			Bail "Grep could not find expected text(All files for topic test.topic[1-3] in folder archive2\\YYYYMMDD are checked, but they will not be removed as the condition is not met)..."
		fi
		grep -e 'All files from topic test.topic[1-3]\{1\} in folder archive2\\[0-9]\{8\} are removed, as they are older than' ${BASENAME}_sub_client_${II}_32_log.txt
		if [ "$?" -eq "0" ]
		then
			Bail "Grep should not find text(All files from topic test.topic[1-3] in folder archive${II}\YYYYMMDD are removed, as they are older than)..."
		fi		
	fi
	
	if [ ${II} -eq 1 ]; then
		# only try to join and recover archived file when no file is removed.
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
		

		# Start synapse server again, but this time the database_root is pointing to the join/recover result.
		Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root joiner${II}

		sleep 1
		/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub $((21*$TOTAL_MESSAGE)) 2 2>&1 | tee -a ${BASENAME}_python_sub_client_${II}_log.txt 
		if [ "$?" -ne "0" ]
		then
			Bail "Waiting for subscribe client to exit with error..."
		fi
		/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub $((18*$TOTAL_MESSAGE)) 3 2>&1 | tee -a ${BASENAME}_python_sub_client_${II}_log.txt 
		if [ "$?" -ne "0" ]
		then
			Bail "Waiting for subscribe client to exit with error..."
		fi	
	fi

	if [ ${II} -lt 3 ]; then
		#Stop this synapse server as it is started from database ./joiner[I]
		Stop_synapse
		Sleep 1
	fi
done
# Stop synapse server

Stop_synapse
Sleep 1

# All data files are not removed as we may need to check them manually, but if you want to remove them, please uncomment below 3 lines.
for (( I=1; I<=3; I++ )); do
	rm -fr ./archive${I}
	rm -fr ./joiner${I}
	rm -fr ./db${I}
done
rm -f archive_stdin_fifo
rm -fr ./archivist

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
