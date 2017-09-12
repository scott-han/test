#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# Example of parallel building of servers...
make Svn_info.h
(trap 'Kill_Hack' SIGTERM ; OUTPUT_DIR=$ODIR/A/ CFLAGS=${CFLAGS}\ -DDATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_1=257000000u make -j 3 $ODIR/A/amqp_0_9_1/server.exe  & wait $!) &
MAKE_SERVER_PIDS="${MAKE_SERVER_PIDS} ${!}"
(trap 'Kill_Hack' SIGTERM ; OUTPUT_DIR=$ODIR/B/ CFLAGS=${CFLAGS}\ -DDATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_TOPIC_STREAM_1=536870912u make -j 3 ${ODIR}/B/amqp_0_9_1/server.exe  & wait $!) &
MAKE_SERVER_PIDS="${MAKE_SERVER_PIDS} ${!}"
(trap 'Kill_Hack' SIGTERM ; OUTPUT_DIR=$ODIR/C/ CFLAGS=${CFLAGS}\ -DDATA_PROCESSORS_TEST_SYNAPSE_MEMORY_SIZE_ALARM_CHANNEL_1=536870912u make -j 3 ${ODIR}/C/amqp_0_9_1/server.exe  & wait $!) &
MAKE_SERVER_PIDS="${MAKE_SERVER_PIDS} ${!}"
(trap 'Kill_Hack' SIGTERM ; OUTPUT_DIR=$ODIR/D/ make -j 3 ${ODIR}/D/amqp_0_9_1/server.exe  & wait $!) &
MAKE_SERVER_PIDS="${MAKE_SERVER_PIDS} ${!}"
for pid in ${MAKE_SERVER_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "One of the built targets failed"
	fi
done
EXPLICIT_PREREQUISITES=${ODIR}/A/amqp_0_9_1/server.Remade_Indicator\ ${ODIR}/B/amqp_0_9_1/server.Remade_Indicator\ ${ODIR}/C/amqp_0_9_1/server.Remade_Indicator\ ${ODIR}/D/amqp_0_9_1/server.Remade_Indicator\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

cd $ODIR || Bail 
sleep 1

##########################################
# Memory alarm triggered from topic
##########################################
echo "Testing Memory alarm triggered from topic"
#Print_logs_in_background

Start_synapse ./A/amqp_0_9_1/server.exe --memory_alarm_threshold 2G  
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "1" ]
then
	Bail "Waiting for saturating client to exit with error..."
fi

sleep 5
grep -i "Memory_alarm_threshold.* topic .*ctor.*breached" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not match database alarm threshold expectation..."
fi

Stop_synapse

sleep 1
Clean --Retain_exes

##########################################
# Memory alarm triggered from topic_stream
##########################################
echo "Testing Memory alarm triggered from topic_stream"

Start_synapse ./B/amqp_0_9_1/server.exe --memory_alarm_threshold 2G 
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "1" ]
then
	Bail "Waiting for saturating client to exit with error..."
fi

sleep 5
grep -i "Memory_alarm_threshold.* topic_stream .*ctor.*breached" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not match database alarm threshold expectation..."
fi

Stop_synapse

sleep 1
Clean --Retain_exes

##########################################
# Memory alarm triggered from channel
##########################################
echo "Testing Memory alarm triggered from channel"

Start_synapse ./C/amqp_0_9_1/server.exe --memory_alarm_threshold 2G 
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_log.txt 
if [ "$?" -ne "1" ]
then
	Bail "Waiting for saturating client to exit with error..."
fi

sleep 5
grep -i "Memory_alarm_threshold.* channel .*ctor.*breached" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not match database alarm threshold expectation..."
fi

Stop_synapse

sleep 1
Clean --Retain_exes

##########################################
# Generic activity
##########################################
echo "Testing Generic activity"

Start_synapse ./D/amqp_0_9_1/server.exe --memory_alarm_threshold 2G 

for I in {1..20}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $!) &
	NUKE_PIDS="${NUKE_PIDS} ${!}"
done

sleep 25
grep -i "refusing" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not match database alarm threshold expectation..."
fi

kill ${NUKE_PIDS}
wait ${NUKE_PIDS}

date
echo "Sleeping before running an expected OK pass"
sleep 50

echo "Activating an expected OK pass"
date

/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Messages_to_publish 10 2>&1 | tee ${BASENAME}_client_expecting_OK_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for saturating client to exit OK..."
fi

Stop_synapse

sleep 1
Clean --Retain_exes

##########################################
set +x
echo  TESTED OK: ${0}
