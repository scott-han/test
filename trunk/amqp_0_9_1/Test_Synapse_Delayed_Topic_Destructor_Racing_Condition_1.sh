#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ ${ODIR}/amqp_0_9_1/clients/Cpp/${BASENAME}.exe OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DDATA_PROCESSORS_TEST_SYNAPSE_DELAYED_TOPIC_DESTRUCTOR_RACING_CONDITION_1=888\ -DDATA_PROCESSORS_TEST_SYNAPSE_DELAYED_TOPIC_DESTRUCTOR_RACING_CONDITION_1_INVOKE_BUG\ -DDATA_PROCESSORS_SYNAPSE_TEST_ALLOW_SILENT_CRASH make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# This run is to invoke the error -- ensures that the timings of client tester is hitting the right combination of events/occurances on the server side.
Start_synapse ./amqp_0_9_1/server.exe

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe > Client_log.txt 2>&1 || Bail "Client did not run OK"

# timeout mechanism for 'wait' below
(trap 'Kill_Hack' SIGTERM ; sleep 55 & wait $! ; touch ./Synapse_Should_Be_Done_By_Now_Flag ; kill ${SYNAPSE_PID} ; ) &
TIMEOUT_PID=$!

wait ${SYNAPSE_PID}
Synapse_Exit_Code=$?
kill ${TIMEOUT_PID}

# Synapse should have not exited ok
if [ $Synapse_Exit_Code -eq 0 ]; then
	Bail "Synapse was not expected to exit OK"
fi

# Synapse error code should not have been caused by the timeout of the 'wait' mechanism
if [ -f ./Synapse_Should_Be_Done_By_Now_Flag ]; then
	rm  -f ./Synapse_Should_Be_Done_By_Now_Flag
	Bail "Synapse was not expected to be still running."
fi

find . -type d -name "*database*.tmp" -exec rm -frv {} \;
rm ./amqp_0_9_1/server.exe
rm ./amqp_0_9_1/.server.dd
rm ./amqp_0_9_1/server_pch.h.gch
rm ./amqp_0_9_1/.server_pch.h.dd
mv synapse_log.txt Failed_Run_Synapse_Log.txt
mv Client_Log.txt Failed_Run_Client_Log.txt

# This is now about running server without error and making sure that all is good (i.e. on OK pass).

cd ${IVOCATION_DIR} || Bail
OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DDATA_PROCESSORS_TEST_SYNAPSE_DELAYED_TOPIC_DESTRUCTOR_RACING_CONDITION_1=888 make ${ODIR}/amqp_0_9_1/server.exe || Bail
cd $ODIR || Bail 
sleep 1

Start_synapse ./amqp_0_9_1/server.exe   

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe > Client_log.txt 2>&1 || Bail "Client did not run OK"

Stop_synapse

Clean --Retain_exes
set +x
echo  TESTED OK: ${0}
