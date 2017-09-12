#! /bin/sh -x

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

Print_logs_in_background

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py  --Subscribe 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for clients to exit ok..."
fi

Stop_synapse

sleep 1

grep -i "Topic name(=.\+) contains invalid character" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Subscribing pass -- Grep could not match expectation of server detecting topic name ending with a space..."
fi

mv synapse_log.txt synapse_log_during_subscribing_part_of_test.txt || Bail "renaming intermediate synapse log"
Clear --Retain_exes
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe 
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py  --Publish 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for clients to exit ok..."
fi

Stop_synapse

sleep 1

grep -i "Topic name(=.\+) contains invalid character" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Publishing pass -- Grep could not match expectation of server detecting topic name ending with a space..."
fi

cd $ODIR || Bail 
Clean

set +x
echo  TESTED OK: ${0}
