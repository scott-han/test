#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

TOTAL_MESSAGE=3000
TOTAL_TOPIC=5

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
make Svn_info.h

(trap 'Kill_Hack' SIGTERM ; make ./amqp_0_9_1/server.exe & wait $!) &
MAKE_PIDS="${MAKE_PIDS} ${!}"

(trap 'Kill_Hack' SIGTERM ; make amqp_0_9_1/clients/Cpp/Topic_mirror.exe & wait $!) &
MAKE_PIDS="${MAKE_PIDS} ${!}"

for pid in ${MAKE_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "One of the built targets failed"
	fi
done
EXPLICIT_PREREQUISITES=./amqp_0_9_1/clients/Cpp/Topic_mirror.exe\ ./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# When calling this function, 
# the first parameter is the id of this instance (must greater than 3, as 3 is used by the default synapse server);
# the second parameter is the full path of the Synapse binary
# the third parameter is the port number to listen on.
Start_a_synapse()
{
	local id=${1}
	shift
	local Synapse_Path="${1}"
	if [ ! -x ${Synapse_Path} ]; then
		Bail "Synapse executable does not exist"
	fi
	shift

	local port=${1}
	shift
	if [ ! -p "server_stdin_fifo${id}" ]; then
		mkfifo server_stdin_fifo${id}
	fi

	(trap 'Kill_Hack' SIGTERM ; ${Synapse_Path} --IO_Services_Size 2 --listen_on localhost:${port} --database_root Synapse_database.tmp $* < ./server_stdin_fifo${id} > synapse${id}_log.txt 2>&1 & wait $!) &
	A_SYNAPSE_PID=${!}
	eval "exec ${id}> server_stdin_fifo${id}"
	#	WAIT_PIDS="${WAIT_PIDS} ${!}"


	# Noting for posterity, this method alone wont work because the server may well start up but the socket-listening part of functionality may not start at the same time (exactly) as the process start/load... so subsequent connections from clients may still barf...
	for I in {1..15}
	do
		if ps -p ${A_SYNAPSE_PID} > /dev/null
		then
			echo "$(date) Synapse${id} starting ok"
			break
		else
			sleep 1
		fi
	done

	# so also trying to actually connect via tcp socket (on its own however this method also may not work as system may explicitly refuse connections 'real-quick' so the loop may iterate faster than 15 secs -- if 'nc' is connecting before the process is proprely loaded in RAM by the background job).
	for I in {1..30}
	do
		nc -z -w 1 localhost ${port} && break
	done
	nc -z -w 1 localhost ${port} || Bail "Could not start server${id} in time"

	echo "$(date) Synapse${id} started ok"
}
# When calling this function, 
# the first parameter is the id of this instance (You passed to Start_a_synapse when calling it, must greater than 3, as 3 is used by the default synapse server)
# the second parameter is the SYNAPSE_ID you get after calling Start_a_synapse (A_SYNAPSE_ID)
Stop_a_synapse() {
	timeout 35 echo ! >&${1} || Bail "Trying to write exit char (!) to server"
	( trap 'Kill_Hack' SIGTERM ; sleep 700 & wait $! ; kill ${2} ; ) &
	TIMEOUT_PID=$!
	wait ${2} || Bail "Waiting for server to exit ok..."
	Synapse_Exit_Code=$?
	if [ $Synapse_Exit_Code -ne 0 ]; then
		Bail "Synapse${1} did not exit OK"
	fi
	kill ${TIMEOUT_PID}
	wait ${TIMEOUT_PID}
}

Create_mirror_fifo()
{
	if [ ! -p "mirror_stdin_fifo" ]; then
		mkfifo mirror_stdin_fifo
	fi
}

Kill_Hard() {
	pssuspend64.exe -accepteula python.exe > /dev/null 2>&1
	kill -KILL $(jobs -p)
	pssuspend64.exe -accepteula -r python.exe > /dev/null 2>&1
}

# Test 1, start two synapses, one is to receive data, the other one is to receive mirrored topics, before publisher is completed, shutdown the second synapse, and restart it (Topic_mirror should exit without any issue, restart Topic_mirror as well), 
# the second server should get all messages even though it was stopped in the middle.
LOOPS="wp whatever"
for wp in ${LOOPS} ; do
	extra=""
	if [ "${wp}" = "wp" ]; then
		extra="--not_using_source_seq_and_timestamp --instance_name mirror_1"
	fi
#:<< COMMENT
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 1 0 ${wp} 2>&1 | tee ${BASENAME}_client_1_1_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Publish client exits with error."
	fi
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}

	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_1_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}

	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_1_3_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}

	sleep 3
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	wait ${SYNAPSE_ID_2}
	wait ${MIRROR_TOPIC_PID}
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_1_4_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 6000 ${TOTAL_TOPIC} 1 6672 ${wp} 2>&1 | tee ${BASENAME}_client_1_5_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi	

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672




	# Test 2, start two synapses, before publisher is completed, stop Topic_mirror, and then restart it, the second server should get all messages.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_2_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	Create_mirror_fifo
	# Start Topic_mirror 
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_2_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown Topic_mirror
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	# Start it again.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_2_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}

	exec 5> mirror_stdin_fifo
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 6672 ${wp} 2>&1 | tee ${BASENAME}_client_2_4_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}	

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672





	# Test 3, start two synapses, before publisher is completed, kill Topic_mirror and then restart it, the second server should get all messages.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	# Publish a lot of messages to ensure Topic_mirror is busy while killing it.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 20000 ${TOTAL_TOPIC} 1 0 ${wp} 2>&1 | tee ${BASENAME}_client_3_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 2
	# Start Topic_mirror 
	(trap 'Kill_Hard' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_3_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown Topic_mirror
	kill ${MIRROR_TOPIC_PID}
	wait ${MIRROR_TOPIC_PID}
	# Start it again.
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_3_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 20000 ${TOTAL_TOPIC} 1 6672 ${wp} 2>&1 | tee ${BASENAME}_client_3_4_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}	

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672




	# Test 4, start two synapses, before publisher is completed, stop the target synapse, Topic_mirror should exit without any issue.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_4_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	# Start Topic_mirror 
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_4_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown the second synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	wait ${MIRROR_TOPIC_PID}

	# Start it again.
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_4_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 6672 ${wp} 2>&1 | tee ${BASENAME}_client_4_4_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi	

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
#COMMENT


	# Test 5,  start two synapses, before publisher is completed, stop the source synapse, Topic_mirror should exit without any issue.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_5_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	# Start Topic_mirror 
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_5_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown the second synapse
	Stop_synapse
	wait ${MIRROR_TOPIC_PID}
	wait ${PUBLISH_CLIENT_PID}

	# Start it again.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo > ${BASENAME}_client_5_3_log.txt 2>&1 & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Give Topic_mirror 10 seconds to collect all topics that server has.

	# Publish messages again and add 5 more topics this time
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; sleep 6; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub $((${TOTAL_MESSAGE}*2)) $((${TOTAL_TOPIC}*2)) 6001 0 ${wp} 2>&1 | tee ${BASENAME}_client_5_4_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	exec 5> mirror_stdin_fifo
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub $((${TOTAL_MESSAGE}*3)) ${TOTAL_TOPIC} 3001 6672 ${wp} rt to 2>&1 | tee ${BASENAME}_client_5_5_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi	
	# Now subscribe again (to ensure history data is in same order, especially when using the seq and timestamp from source server)
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub $((${TOTAL_MESSAGE}*3)) ${TOTAL_TOPIC} 3001 6672 ${wp} norealtime to 2>&1 | tee ${BASENAME}_client_5_6_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "History data does not match."
	fi

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672


#:<< COMMENT
	# Test 6, same with test 1, but this time messages should be forwarded to two synapses.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 1 0 ${wp} 2>&1 | tee ${BASENAME}_client_6_1_log.txt
	if [ "$?" -ne "0" ]; then
		Bail "Publish client exits with error."
	fi
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Start_a_synapse 6 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 7672 --database_root 7672
	SYNAPSE_ID_3=${A_SYNAPSE_PID}

	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_6_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}

	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_6_3_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}

	sleep 2

	Stop_a_synapse 4 ${SYNAPSE_ID_2}

	wait ${SYNAPSE_ID_2}
	wait ${MIRROR_TOPIC_PID}

	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_6_4_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}

	exec 5> mirror_stdin_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 6000 ${TOTAL_TOPIC} 1 6672 ${wp} 2>&1 | tee ${BASENAME}_client_6_5_log.txt & wait $! ) &
	SUB_ID1=${!}
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 6000 ${TOTAL_TOPIC} 1 7672 ${wp} 2>&1 | tee ${BASENAME}_client_6_6_log.txt & wait $! ) &
	SUB_ID2=${!}
	wait ${SUB_ID1}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client2 exits with error."
	fi
	wait ${SUB_ID2}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client1 exits with error."
	fi	
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi
	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	Stop_a_synapse 6 ${SYNAPSE_ID_3}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
	rm -fr ./7672

	# Test 7, same as test 2, but this time, messages should be forwarded to two synapses.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Start_a_synapse 6  ${IVOCATION_DIR}/amqp_0_9_1/server.exe 7672 --database_root 7672
	SYNAPSE_ID_3=${A_SYNAPSE_PID}
	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_7_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	Create_mirror_fifo
	# Start Topic_mirror 
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_7_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 4
	# Shutdown Topic_mirror
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	# Start it again.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_7_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 6672 ${wp} 2>&1 | tee ${BASENAME}_client_7_4_log.txt & wait $! ) &
	SUB_ID1=${!}
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 7672 ${wp} 2>&1 | tee ${BASENAME}_client_7_5_log.txt & wait $! ) &
	SUB_ID2=${!}
	wait ${SUB_ID1}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client2 exits with error."
	fi
	wait ${SUB_ID2}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client1 exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi	

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	Stop_a_synapse 6 ${SYNAPSE_ID_3}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
	rm -fr ./7672

	# Test 8, same as test 3, but this time, message should be forwarded to two synapse.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Start_a_synapse 6 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 7672 --database_root 7672
	SYNAPSE_ID_3=${A_SYNAPSE_PID}
	# Publish a lot of messages to ensure Topic_mirror is busy while killing it.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 20000 ${TOTAL_TOPIC} 1 0 ${wp} 2>&1 | tee ${BASENAME}_client_8_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 2

	# Start Topic_mirror 
	(trap 'Kill_Hard' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_8_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Sleep some seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Kill Topic_mirror
	kill ${MIRROR_TOPIC_PID}
	wait ${MIRROR_TOPIC_PID}
	# Start it again.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_8_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}

	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 20000 ${TOTAL_TOPIC} 1 6672 ${wp} 2>&1 | tee ${BASENAME}_client_8_4_log.txt & wait $! ) &
	SUB_ID1=${!}
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 20000 ${TOTAL_TOPIC} 1 7672 ${wp} 2>&1 | tee ${BASENAME}_client_8_5_log.txt & wait $! ) &
	SUB_ID2=${!}
	wait ${SUB_ID1}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client2 exits with error."
	fi
	wait ${SUB_ID2}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client1 exits with error."
	fi
	wait ${PUBLISH_CLIENT_PID}

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	Stop_a_synapse 6 ${SYNAPSE_ID_3}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
	rm -fr ./7672

	# Test 9, same as test 4, but this time messages are forwarded to two synapses.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Start_a_synapse 6 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 7672 --database_root 7672
	SYNAPSE_ID_3=${A_SYNAPSE_PID}
	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_9_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	# Start Topic_mirror 
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_9_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown the second synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	wait ${MIRROR_TOPIC_PID}

	# Start it again.
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_9_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo

	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 6672 ${wp} 2>&1 | tee ${BASENAME}_client_9_4_log.txt & wait $! ) &
	SUB_ID1=${!}
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 7672 ${wp} 2>&1 | tee ${BASENAME}_client_9_4_log.txt & wait $! ) &
	SUB_ID2=${!}
	wait ${SUB_ID1}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client2 exits with error."
	fi
	wait ${SUB_ID2}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client1 exits with error."
	fi	
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi	

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	Stop_a_synapse 6 ${SYNAPSE_ID_3}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
	rm -fr ./7672

	# Test 10, same as test 5, but this time messages are forwarded to two synapses.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}
	Start_a_synapse 6 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 7672 --database_root 7672
	SYNAPSE_ID_3=${A_SYNAPSE_PID}
	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_10_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	# Start Topic_mirror 
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} 2>&1 | tee -a ${BASENAME}_client_10_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown the second synapse
	Stop_synapse
	wait ${MIRROR_TOPIC_PID}
	wait ${PUBLISH_CLIENT_PID}

	# Start it again.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_10_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}

	# Publish messages again and add 5 more topics this time
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; sleep 6; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub $((${TOTAL_MESSAGE}*2)) $((${TOTAL_TOPIC}*2)) 6001 0 ${wp} 2>&1 | tee ${BASENAME}_client_10_4_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1

	exec 5> mirror_stdin_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub $((${TOTAL_MESSAGE}*3)) ${TOTAL_TOPIC} 3001 6672 ${wp} rt to 2>&1 | tee ${BASENAME}_client_10_5_log.txt & wait $! ) &
	SUB_ID1=${!}
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub $((${TOTAL_MESSAGE}*3)) ${TOTAL_TOPIC} 3001 7672 ${wp} rt to 2>&1 | tee ${BASENAME}_client_10_6_log.txt & wait $! ) &
	SUB_ID2=${!}
	wait ${SUB_ID1}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client2 exits with error."
	fi
	wait ${SUB_ID2}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client1 exits with error."
	fi	
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	Stop_a_synapse 6 ${SYNAPSE_ID_3}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
	rm -fr ./7672




	# Test 11, start 2 synapses, one as the source, one as target, and forward some topics from sourc to target, then start the second target synapse, and restart Topic_mirror to mirror all topics, topics should be mirrored to both servers without any issue.
	Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
	Start_a_synapse 4 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 6672 --database_root 6672
	SYNAPSE_ID_2=${A_SYNAPSE_PID}

	# Publish a message every 30 milli-seconds to ensure we can stop Topic_mirror before publisher is completed.
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${TOTAL_TOPIC} 3001 30 ${wp} 2>&1 | tee ${BASENAME}_client_11_1_log.txt & wait $! ) &
	PUBLISH_CLIENT_PID=${!}
	sleep 1
	# Start Topic_mirror 
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_11_2_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	# Sleep 10 seconds to ensure some messages are already mirrored to other server.
	sleep 5
	# Shutdown Topic_mirror
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	Start_a_synapse 6 ${IVOCATION_DIR}/amqp_0_9_1/server.exe 7672 --database_root 7672
	SYNAPSE_ID_3=${A_SYNAPSE_PID}
	Create_mirror_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_mirror.exe --synapse_at localhost:5672 --subscription test.* --mirror_to localhost:6672 --mirror_to localhost:7672 --time_out 1 ${extra} < ./mirror_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_11_3_log.txt & wait $! ) &
	MIRROR_TOPIC_PID=${!}
	exec 5> mirror_stdin_fifo
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 6672 ${wp} 2>&1 | tee ${BASENAME}_client_11_4_log.txt & wait $! ) &
	SUB_ID1=${!}
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ;/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 3000 ${TOTAL_TOPIC} 3001 7672 ${wp} 2>&1 | tee ${BASENAME}_client_11_5_log.txt & wait $! ) &
	SUB_ID2=${!}
	wait ${SUB_ID1}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client2 exits with error."
	fi
	wait ${SUB_ID2}
	if [ "$?" -ne "0" ]; then
		Bail "Subscriber client1 exits with error."
	fi	
	wait ${PUBLISH_CLIENT_PID}
	echo ! >&5 || Bail "Trying to stop Topic_mirror (!) to Topic_mirror.exe"
	wait ${MIRROR_TOPIC_PID}
	if [ "$?" -ne "1" ]; then
		Bail "Topic_mirror exits with error."
	fi

	Stop_synapse
	Stop_a_synapse 4 ${SYNAPSE_ID_2}
	Stop_a_synapse 6 ${SYNAPSE_ID_3}
	rm -fr ./Synapse_database.tmp
	rm -fr ./6672
	rm -fr ./7672
#COMMENT

done


rm ./mirror_stdin_fifo
rm ./server_stdin_fif*

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
