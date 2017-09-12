#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ ${MYDIR}/clients/php/synapse_client.php\ ${MYDIR}/clients/php/${BASENAME}.php OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/${BASENAME}.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

rm -fr db

Print_logs_in_background

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

( set -o pipefail ; php ${MYDIR}/clients/php/${BASENAME}.php 2>&1 | tee ${BASENAME}_log.txt) || Bail
sleep 1

sleep 7
# todo -- dangerous!
#cd ${IVOCATION_DIR}
#rm -fr $ODIR

cd $ODIR || Bail 
Clean --Retain_exes
rm -fr db 

set +x
echo  TESTED OK: ${0}
