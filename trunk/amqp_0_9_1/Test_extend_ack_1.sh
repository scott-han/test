#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

TOTAL_MESSAGE=120000
TOTAL_TOPIC=50
MAX_PUBS=5

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
(trap 'Kill_Hack' SIGTERM ; make ./amqp_0_9_1/server.exe & wait $!)
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
PUBLISH_PID=""
num=$((${TOTAL_TOPIC}/${MAX_PUBS}))
for (( I=0; I<${MAX_PUBS}; I++ )); do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub ${TOTAL_MESSAGE} ${num} $(($I * ${num})) 2>&1 | tee ${BASENAME}_pub_$(($I+1))_log.txt & wait $! ) &
	PUBLISH_PID="$PUBLISH_PID ${!}"
done
HARVEST_PID=""
for (( I=0; I<${MAX_PUBS}; I++ )); do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Ext $(($I+1)) $(($I%2)) 2>&1 | tee ${BASENAME}_ext_$((${I}+1))_log.txt & wait $! ) &
	HARVEST_PID="$HARVEST_PID ${!}"
done
SUBSCRIBE_PID=""
for (( I=0; I<${MAX_PUBS}; I++ )); do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; /c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub ${TOTAL_MESSAGE} ${num} $(($I * ${num})) 2>&1 | tee ${BASENAME}_sub_$(($I+1))_log.txt & wait $! ) &
	SUBSCRIBE_PID="$SUBSCRIBE_PID ${!}"
done
for PID in ${PUBLISH_PID} ; do
	wait $PID
	if [ "$?" -ne "0" ]; then
		Bail "One of publishers returns error"
	fi
done
for PID in ${SUBSCRIBE_PID} ; do
	wait ${PID}
	if [ "$?" -ne "0" ]; then
		Bail "One of subscribers returns error"
	fi
done
for PID in ${HARVEST_PID} ; do
	ps -p $PID
	a=$?
	if [ $a -ne 0 ]; then
		Bail "One of extend ack test client terminated unexpectedly."
	fi
done
Stop_synapse
for PID in ${HARVEST_PID} ; do
	wait ${PID}
done
cd $ODIR || Bail 
bad=$(grep -c "bad buffering" ./synapse_log.txt)
if [ $bad -gt 0 ]; then
	Bail "Found 'bad buffering' in server log"
fi
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
