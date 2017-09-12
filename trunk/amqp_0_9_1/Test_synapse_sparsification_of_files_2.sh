#! /bin/sh -x

# More of a fuzzy loading of the server with various sources (concurrent/async/etc) of sparsificatino parameters (publishers vs subcsribers; target_data_consumption_limit vs sparsify_upto_timestamp)

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
make ./amqp_0_9_1/server.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

# Print_logs_in_background

Clean --Retain_exes
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --reserve_memory_size 3G
# Start clients concurrent behaviour between sparsification, publishing and subscription
for I in {1..3}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Sparsify --Topic_name ${BASENAME}.${I} 2>&1 | tee ${BASENAME}_sparsifying_client_${I}_log.txt & wait $!) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done
for I in {1..3}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Publish --Topic_name ${BASENAME}.${I} 2>&1 | tee ${BASENAME}_publishing_client_${I}_log.txt & wait $!) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done
for I in {1..3}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Subscribe --Topic_name ${BASENAME}.${I} 2>&1 | tee ${BASENAME}_subscribing_client_${I}_log.txt & wait $!) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done

# Wait for clients to be done
for pid in ${CLIENT_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for clients to exit ok pass A ..."
	fi
done

Stop_synapse
Clean --Retain_exes
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --reserve_memory_size 3G

sleep 1

# All on the same thread/client per topic (esp. sparsify and subcrsibe on the same tcp socket)
CLIENT_PIDS=""
for I in {1..3}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Publish --Subscribe --Sparsify --Topic_name ${BASENAME}.${I} 2>&1 | tee ${BASENAME}_all_client_${I}_log.txt & wait $!) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done
# Wait for clients to be done
for pid in ${CLIENT_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for clients to exit ok pass B ..."
	fi
done

Stop_synapse

sleep 1

cd $ODIR || Bail 
Clean

set +x
echo  TESTED OK: ${0}
