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

for T in {1..5}; do
	CLIENT_PID=""
	for N in {1..20}; do
		(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py  --Publish 2>&1 | tee ${BASENAME}_client_${T}.${N}_log.txt & wait $!) &
		CLIENT_PID="${CLIENT_PID} ${!}"
	done
	wait ${CLIENT_PID}
	nc -z -w 1 localhost 5672 || Bail "Could not connect server."
done

Stop_synapse
sleep 1

grep "ERROR, cant have multiple publishers to the same topic (Last known publisher" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Publishing pass -- Grep could not match expectation of server detecting multiple publishers on the same topic..."
fi

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
