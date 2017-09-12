#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DTEST_SYNAPSE_INDEX_MAX_KEY_UPDATE_REQUESTS_THROTTLING_WATERMARK_1=1 make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

cd $ODIR || Bail 
sleep 1
Print_logs_in_background

Start_synapse ./amqp_0_9_1/server.exe   

for I in {1..11}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $!) &
	PUBLISHER_PIDS="${PUBLISHER_PIDS} ${!}"
done
for pid in ${PUBLISHER_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for all clients to exit OK"
	fi
done

sleep 1

Stop_synapse

THROTTLING_ON_SIZE=`grep -i "publisher throttling ON due to index notification" synapse_log.txt | wc -l`
if [ "$THROTTLING_ON_SIZE" -lt "10" ]
then
	Bail "Expected more throttling ON occurances"
fi

THROTTLING_OFF_SIZE=`grep -i "publisher throttling OFF due to index notification" synapse_log.txt | wc -l`
if [ "$THROTTLING_OFF_SIZE" -lt "10" ]
then
	Bail "Expected more throttling OFF occurances"
fi

sleep 1

Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
