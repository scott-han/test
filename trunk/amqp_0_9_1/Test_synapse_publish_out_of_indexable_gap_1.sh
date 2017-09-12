#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
make ./amqp_0_9_1/server.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.ps1\ ${MYDIR}/clients/php/${BASENAME}.php OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

Print_logs_in_background

powershell -Noninteractive -ExecutionPolicy ByPass -Command "${MYDIR}/${BASENAME}.ps1"  || Bail 

sleep 1

cd $ODIR || Bail 
Clean

set +x
echo  TESTED OK: ${0}
