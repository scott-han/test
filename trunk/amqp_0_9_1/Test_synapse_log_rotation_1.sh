#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ ${MYDIR}/clients/python/${BASENAME}.py\ ${MYDIR}/${BASENAME}.ps1 OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DTEST_SYNAPSE_LOG_ROTATION_1 make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

Print_logs_in_background

powershell -Noninteractive -ExecutionPolicy ByPass -Command "${MYDIR}/${BASENAME}.ps1" || Bail "Powershell script failed."

sleep 1

cd $ODIR || Bail 

FOUND_LOGS=`find ./amqp_0_9_1/Synapse_log_output -name "*.txt" | wc -l`
if [[ ${FOUND_LOGS} =~ ^[0-9]+$ ]] ; then
if [ "${FOUND_LOGS}" -gt "2" ]; then
	echo "Found server ${FOUND_LOGS} logs."	
else
	Bail "Server should produce more logs."
fi
else
	Bail "Could not find any logs produced by server."
fi

Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
