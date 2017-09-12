#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
make Svn_info.h || Bail "make Svn_info.h"
EXPLICIT_PREREQUISITES=$ODIR/amqp_0_9_1/server.exe\ $ODIR/amqp_0_9_1/clients/Cpp/${BASENAME}.exe\ $ODIR/database/basic_recover.exe OUTPUT_DIR=$ODIR/ make -j 5 ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

# Print_logs_in_background

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Stage Publish > Client_Publishing_Log.txt 2>&1 || Bail "Publishing initial data"

Stop_synapse
mv synapse_log.txt Publishing_Stage_Synapse_Log.txt || Bail "Moving synapse log file to Publishing stage"

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Stage Corrupt  Synapse_database.tmp/test.cpp.1/data > Client_Corrupting_Log.txt 2>&1 || Bail "Intentionally erroring publishing data"

./database/basic_recover.exe --inhibit_delta_poisoning false --src Synapse_database.tmp --dst Synapse_database.tmp > Basic_Recover_Log.txt 2>&1 || Bail "Patching up with delta of erroneous data"

Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G

./amqp_0_9_1/clients/Cpp/${BASENAME}.exe --Stage Subscribe  > Client_Subscribing_Log.txt 2>&1 || Bail "Subscribing to with-delta-poison data"

/c/Python27/python.exe ${IVOCATION_DIR}/amqp_0_9_1/clients/python/${BASENAME}.py > Python_Client_Subscribing_Log.txt 2>&1 || Bail "Python client subscribing to with-delta-poison data"


Stop_synapse
mv synapse_log.txt Subscribing_Stage_Synapse_Log.txt || Bail "Moving synapse log file to subscribing stage"

sleep 5

# nuke synapse server
Reap

sleep 1

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
