#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
make database/basic_recover.exe || Bail
make amqp_0_9_1/server.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ./database/basic_recover.exe\ ${MYDIR}/${BASENAME}.ps1\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# Print_logs_in_background

powershell -Noninteractive -ExecutionPolicy ByPass -Command "${MYDIR}/${BASENAME}.ps1"  || Bail 

# todo more parametric and integrated with other scirpts behaviour (albeit soon powershell should be deprecated anyways).
EOFLOG=${ODIR}/in_place_and_near_eof_after_sparsification_subscribing_client_log.txt
grep "topic1.*NOT being recovered" ${EOFLOG} || Bail "topic1 should be recovered from start"
grep "topic2.*NOT being recovered" ${EOFLOG} || Bail "topic2 should be recovered from start"
grep "topic3.*NOT being recovered" ${EOFLOG} || Bail "topic3 should be recovered from start"
grep "topic4.*NOT being recovered" ${EOFLOG} || Bail "topic4 should be recovered from start"
grep "topic5.*NOT being recovered" ${EOFLOG} && Bail "topic5 should be recovered from close to eof"
grep "topic6.*NOT being recovered" ${EOFLOG} && Bail "topic6 should be recovered from close to eof"
grep "topic7.*NOT being recovered" ${EOFLOG} && Bail "topic7 should be recovered from close to eof"

cd $ODIR || Bail 
Clean

set +x
echo  TESTED OK: ${0}
