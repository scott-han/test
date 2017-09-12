#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ $ODIR/amqp_0_9_1/clients/Cpp/${BASENAME}.exe OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# Print_logs_in_background

sleep 1

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Action Publish || Bail "testing end of steram mode"

Stop_synapse

for I in {1..3}
do

	Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

	./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Action Subscribe || Bail "testing end of steram mode"

	Stop_synapse

done

sleep 1

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
