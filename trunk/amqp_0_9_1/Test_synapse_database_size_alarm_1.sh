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

# one bug was discovered accidentally (occasionally the target_data_consumption limit was being written on a non-last range)... so will repeat the test multiple times to increase robustness somewhat...
for Repeat_test_count in {1..5}
do

cd $ODIR || Bail 


sleep 1

# Print_logs_in_background

# Trying to do without powershell (currently the logging redirection is a bit suspect -- it tends to have a delayed output... but also because the best option would be to use portable scripting, not ms-specific).
# powershell -Noninteractive -ExecutionPolicy ByPass -Command "${MYDIR}/${BASENAME}.ps1"  || Bail 
# here is the shell version (still not as complete)
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_alarm_threshold 1G --reserve_memory_size 3G
# Start client and publish
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --mode pre 2>&1 | tee ${BASENAME}_client_pre_log.txt 
if [ "$?" -ne "2" ]
then
	Bail "Waiting for saturating client to exit with error..."
fi

sleep 5
grep -i "Database_alarm_threshold.*breached" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not match database alarm threshold expectation..."
fi

# Start client and make sure cannot publish just one message...
timeout 35 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --mode one 2>&1 | tee ${BASENAME}_client_one_log.txt
if [ "$?" -ne "2" ]
then
	Bail "Waiting for one message client to exit with error..."
fi

ALARMS_SIZE=`grep -i "Database_alarm_threshold.*breached" synapse_log.txt | wc -l`
if [ "$ALARMS_SIZE" -ne "2" ]
then
	Bail "Expected two alarms triggers..."
fi

# Start client and sparsify the topic
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --mode post 2>&1 | tee ${BASENAME}_client_post_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Waiting for clients to exit ok..."
fi

grep -i "SPARSIFYING" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Grep could not match sparsification expectation..."
fi

Stop_synapse

sleep 1

cd $ODIR || Bail 
Clean --Retain_exes

done

Clean

set +x
echo  TESTED OK: ${0}
