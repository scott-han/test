#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
make ./amqp_0_9_1/server.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ${ODIR}/amqp_0_9_1/clients/Cpp/${BASENAME}.exe OUTPUT_DIR=$ODIR/  make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 
sleep 1

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe   

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Topics_Path Synapse_database.tmp > Client_log.txt 2>&1 || Bail "Client did not run OK"

Stop_synapse

Clean --Retain_exes
set +x
echo  TESTED OK: ${0}
