#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
make ./amqp_0_9_1/server.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

# Print_logs_in_background

for Overall_test_reruns in {1..20}
do

	MODULO=`echo "${Overall_test_reruns} % 3" | bc`
	if [ ${MODULO} -eq 0 ]
	then
		Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --reserve_memory_size 8G --nice_performance_degradation 20
	elif [ ${MODULO} -eq 1 ]
	then
		Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --reserve_memory_size 8G 
	elif [ ${MODULO} -eq 2 ]
	then
		Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --nice_performance_degradation 20
	fi

	TOPICS_SIZE=`ls Synapse_database.tmp | wc -l`
	if [ "TOPICS_SIZE" -ne "0" ]
	then
		Bail "No topics should be present during startup."
	fi

	# read aoeuaoeuaoeu

	CLIENT_PIDS=""

	for I in {1..3}
	do
		(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $!) &
		CLIENT_PIDS="${CLIENT_PIDS} ${!}"
	done

	# Wait for clients to be done
	for pid in ${CLIENT_PIDS} ; do
		wait ${pid}
	done

	echo "Stoping synapse. All clients exited OK."
	Stop_synapse

	TOPICS_SIZE=`ls Synapse_database.tmp | wc -l`
	if [ "TOPICS_SIZE" -lt "700" ]
	then
		Bail "More topics should have been generated."
	fi

	sleep 1

	cd $ODIR || Bail 
	Clean --Retain_exes

done

Clean

set +x
echo  TESTED OK: ${0}
