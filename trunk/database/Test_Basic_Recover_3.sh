#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
EXPLICIT_PREREQUISITES=$ODIR/database/basic_recover.exe\ $ODIR/amqp_0_9_1/server.exe\ $ODIR/amqp_0_9_1/clients/Cpp/${BASENAME}.exe OUTPUT_DIR=$ODIR/ make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# Print_logs_in_background

sleep 1

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Action Publish  > Client_Publishing_Log.txt 2>&1 || Bail "Publishing error"

Stop_synapse
mv synapse_log.txt Publishing_Stage_Synapse_Log.txt || Bail "Moving synapse log file to Publishing stage"

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Action Corrupt_And_Recover --Topics_Path Synapse_database.tmp > Client_Corrupting_Log.txt 2>&1 || Bail "Corrupt_And_Recover error"

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Action Subscribe  > Client_Subscribing_Log.txt 2>&1 || Bail "Subscription error"

Stop_synapse
mv synapse_log.txt Subscribing_Stage_Synapse_Log.txt || Bail "Moving synapse log file to subscribing stage"

sleep 1

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
