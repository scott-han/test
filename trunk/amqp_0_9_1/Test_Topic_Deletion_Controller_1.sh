#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# to save on time, build some dependencies in parallel
make Svn_info.h
(trap 'Kill_Hack' SIGTERM ; make -j 3 ./amqp_0_9_1/clients/Cpp/Topic_Deletion_Controller.exe & wait $!) &
MAKE_PIDS="${MAKE_PIDS} ${!}"
(trap 'Kill_Hack' SIGTERM ; make -j 3 ./amqp_0_9_1/server.exe & wait $!) &
MAKE_PIDS="${MAKE_PIDS} ${!}"
for pid in ${MAKE_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Making one of the targets"
	fi
done
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ./amqp_0_9_1/clients/Cpp/Topic_Deletion_Controller.exe\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

cd $ODIR || Bail 

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --memory_alarm_threshold 2G 

# Publish 3 topics
CLIENT_PIDS=""
for I in {1..3}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Topic_Name ${I} 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $!) &
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
for I in {1..3}
do
	if [ ! -e "Synapse_database.tmp/${I}/data" ]; then
		Bail "Topic ${I} data file should exist by now"
	fi
done

# Run topic deletion controllers

if [ ! -p "Client_Input_FIFO" ]; then
	mkfifo Client_Input_FIFO
fi
if [ ! -p "Client_Output_FIFO" ]; then
	mkfifo Client_Output_FIFO
fi

function Local_Bail {
	echo 4>&-
	Bail $*
}

function Run_Controller {
	# Await for topic to exist on file
	FOUND_ATTEMPT=0
	while [ "${FOUND_ATTEMPT}" -lt "70"  ]
	do
		if [ -d "Synapse_database.tmp/${1}" ]; then
			break
		else
			FOUND_DTOR=$((FOUND_DTOR + 1))
			sleep 3
		fi
	done
	if [ "${FOUND_ATTEMPT}" -eq "70"  ]
	then
		Local_Bail "topic ${1} data should exist by now"
	fi
	# Mark for deletion
	${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Topic_Deletion_Controller.exe --Topic_Name ${1}  < ./Client_Input_FIFO > ./Client_Output_FIFO 2>&1 &
	local TOPIC_DELETION_CONTROLLER_PID=${!}
	exec 4> Client_Input_FIFO
	echo "Starting Topic_Deletion_Controller_Log_${1}" > ./Topic_Deletion_Controller_Log_${1}.txt
	printf "Yes\nYes!\n" >&4
	while read -r Line || [[ -n "$Line" ]]; do
		echo $Line >> ./Topic_Deletion_Controller_Log_${1}.txt
		echo $Line | grep "enter.\+pin:\s\+\([0-9]\+\)"
		if [ "$?" -eq "0" ]
		then
			echo `echo $Line | sed "s/enter.\+pin:\s\+\([0-9]\+\)/\1/"` >&4
		fi
		Line=""
	done < ./Client_Output_FIFO
	wait ${TOPIC_DELETION_CONTROLLER_PID} || Local_Bail "Waiting for Topic_Deletion_Controller to exit ok for the topic ${1}"
	echo 4>&-
}

# Await for topic to be nuked from filesystem.
function Await_Topic_Deletion {
	FOUND_ATTEMPT=0
	while [ "${FOUND_ATTEMPT}" -lt "70"  ]
	do
		if [ ! -d "Synapse_database.tmp/${1}" ]; then
			break
		else
			FOUND_DTOR=$((FOUND_DTOR + 1))
			sleep 3
		fi
	done
	if [ "${FOUND_ATTEMPT}" -eq "70"  ]
	then
		Local_Bail "topic ${1} data should not exist by now"
	fi
}

# Nuke topic when it is likely to still be in RAM
Run_Controller 1
Await_Topic_Deletion 1

# Nuke another topic when it (topic struct itself, not the meta_topic) is already dtored...
FOUND_ATTEMPT=0
while [ "${FOUND_ATTEMPT}" -lt "70"  ]
do
	if grep topic\ dtor:.*path:.*2\$ synapse_log.txt
	then
		break
	else
		FOUND_DTOR=$((FOUND_DTOR + 1))
		sleep 3
	fi
done
if [ "${FOUND_ATTEMPT}" -eq "70"  ]
then
	Local_Bail "Could not detect topic dtor for topic 2 before  issuing deletion test"
fi
Run_Controller 2
Await_Topic_Deletion 2

# Publish nuke topic whist quitting server...
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Topic_Name 4 --Perpetual 2>&1 | tee ${BASENAME}_client_4_log.txt & wait $!) &
ONGOING_PUBLISHER=${!}

sleep 3
Run_Controller 4

Stop_synapse

wait ${ONGOING_PUBLISHER}

if [ ! -d "Synapse_database.tmp/3" ]; then
	Local_Bail "Topic 3 data file should still exist"
fi
if [ -d "Synapse_database.tmp/4" ]; then
	Local_Bail "Topic 4 data file should not exist by now"
fi

Clean --Retain_exes

##########################################
set +x
echo  TESTED OK: ${0}
