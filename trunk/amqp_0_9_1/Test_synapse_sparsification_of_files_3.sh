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

set -o pipefail 

/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Publish 2>&1 | tee ${BASENAME}_publishing_client_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Waiting for client to exit ok during publishing round."
fi
sleep 5

Database_size_before_sparsification=`du -sk ./Synapse_database.tmp | cut -f1`

/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Sparsify 2>&1 | tee ${BASENAME}_sparsifying_client_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Waiting for client to exit ok during sparsifying round."
fi
sleep 50

Database_size_after_sparsification=`du -sk ./Synapse_database.tmp | cut -f1`

if [ $( echo "${Database_size_before_sparsification} * 0.7 < ${Database_size_after_sparsification}" | bc ) -eq 1 ]; then
	Bail "Should be greater sparsification of the disk."
fi

Stop_synapse

cd $ODIR || Bail 
Clean

set +x
echo  TESTED OK: ${0}
