#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ ${ODIR}/amqp_0_9_1/clients/Cpp/${BASENAME}.exe OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DDATA_PROCESSORS_TEST_SYNAPSE_MULTIPLE_MODES_OF_PUBLISHING_1 make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 
sleep 1

Start_synapse ./amqp_0_9_1/server.exe   

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe  > Client_log.txt 2>&1 || Bail "Client did not run OK"

Stop_synapse

Grep_For_Text() {
	grep -i "${1}" synapse_log.txt
	if [ "$?" -ne "0" ]
	then
		Bail "Grep could not match: ${1}"
	fi
}

Grep_For_Text "Resign_Publisher seen resignation with pending new request being ready"
Grep_For_Text "Resign_Publisher seen resignation from active/live peer"

Clean --Retain_exes
set +x
echo  TESTED OK: ${0}
