#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# to save on time, build some dependencies in parallel
make Svn_info.h
(trap 'Kill_Hack' SIGTERM ; make -j 3 ./amqp_0_9_1/clients/Cpp/Publishable_Gap_Controller.exe & wait $!) &
MAKE_PIDS="${MAKE_PIDS} ${!}"
(trap 'Kill_Hack' SIGTERM ; make -j 3 ./amqp_0_9_1/server.exe & wait $!) &
MAKE_PIDS="${MAKE_PIDS} ${!}"
for pid in ${MAKE_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Making one of the targets"
	fi
done
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ./amqp_0_9_1/clients/Cpp/Publishable_Gap_Controller.exe\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail
exit
fi

cd $ODIR || Bail 

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --memory_alarm_threshold 2G 

# First run should be ok
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Days 1 2>&1 | tee -a ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "First run should be OK"
fi
echo "$(date) First run OK" >> ${BASENAME}_client_log.txt

# Second run should fail
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Days 300 2>&1 | tee -a ${BASENAME}_client_log.txt 
if [ "$?" -eq "0" ]
then
	Bail "Second run should Fail"
fi
echo "$(date) Second run confirmed as failed " >> ${BASENAME}_client_log.txt

grep "timestamp is too far into the future with respect to the allowed maximum indexable gap" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Expecting to see relevant 'timestamp too far into the future' message in server log..."
fi

# Run publishing gap controller

if [ ! -p "Client_Input_FIFO" ]; then
	mkfifo Client_Input_FIFO
fi
if [ ! -p "Client_Output_FIFO" ]; then
	mkfifo Client_Output_FIFO
fi

function Local_Bail {
	echo 4>&-
	Bail $*
}

${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Publishable_Gap_Controller.exe --Topic_Name a --Gap 300 < ./Client_Input_FIFO > ./Client_Output_FIFO 2>&1 &
PUBLISHABLE_GAP_CONTROLLER_PID=${!}

exec 4> Client_Input_FIFO

echo "Starting Publishable_Gap_Controller_Log" > ./Publishable_Gap_Controller_Log.txt
printf "Yes\nYes!\n" >&4
while read -r Line || [[ -n "$Line" ]]; do
	echo $Line >> ./Publishable_Gap_Controller_Log.txt
	echo $Line | grep "enter.\+pin:\s\+\([0-9]\+\)"
	if [ "$?" -eq "0" ]
	then
		echo `echo $Line | sed "s/enter.\+pin:\s\+\([0-9]\+\)/\1/"` >&4
	fi
	Line=""
done < ./Client_Output_FIFO

wait ${PUBLISHABLE_GAP_CONTROLLER_PID} || Local_Bail "Waiting for Publishable_Gap_Controller to exit ok..."

# 3rd run should be ok
/c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py --Days 300 2>&1 | tee -a ${BASENAME}_client_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "3rd run should be OK"
fi
echo "$(date) 3rd run OK" >> ${BASENAME}_client_log.txt

Stop_synapse

Clean --Retain_exes

##########################################
set +x
echo  TESTED OK: ${0}
