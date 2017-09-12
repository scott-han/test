#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ $ODIR/amqp_0_9_1/clients/Cpp/${BASENAME}.exe OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DTEST_SYNAPSE_INDEX_MAX_KEY_UPDATE_REQUESTS_THROTTLING_WATERMARK_1=5\ -DTEST_SYNAPSE_INDEX_THROTTLE_ON_PUBLISHED_GAP_SIZE_1=1000000\ -DTEST_SYNAPSE_INDEX_THROTTLE_ON_PUBLISHED_GAP_THROTTLING_WAIT_1=100 make -j 5 ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe > Client_Log.txt 2>&1 || Bail "Client"

Stop_synapse

THROTTLING_ON_SIZE=`grep -i "publisher throttling ON due to index notification" synapse_log.txt | wc -l`
if [ "$THROTTLING_ON_SIZE" -lt "300" ]
then
	Bail "Expected more throttling ON occurances"
fi

THROTTLING_OFF_SIZE=`grep -i "publisher throttling OFF due to index notification" synapse_log.txt | wc -l`
if [ "$THROTTLING_OFF_SIZE" -lt "300" ]
then
	Bail "Expected more throttling OFF occurances"
fi

THROTTLING_WRITES_SIZE=`grep -i "WARNING Index.\+throttling write-out activated" synapse_log.txt | wc -l`
if [ "$THROTTLING_WRITES_SIZE" -lt "800" ]
then
	Bail "Expected more throttling writeout occurances"
fi

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}

