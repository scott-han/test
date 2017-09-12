#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

Print_logs_in_background

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

for I in {1..70}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_${I}_log.txt & wait $!) &
	PUBLISHER_PIDS="${PUBLISHER_PIDS} ${!}"
done

for pid in ${PUBLISHER_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail
	fi
done

sleep 5

if grep -qi "out of order" synapse_log.txt
then
	Bail
fi

if grep -qi "assert" synapse_log.txt
then
	Bail
fi

# nuke synapse server
Reap

sleep 1

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
