#! /bin/sh -x

. `dirname ${0}`/Test_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
make Svn_info.h

(trap 'Kill_Hack' SIGTERM ; make ./amqp_0_9_1/server.exe & wait $!) &
MAKE_SERVER_PIDS="${MAKE_SERVER_PIDS} ${!}"

(trap 'Kill_Hack' SIGTERM ; OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DLAZY_LOAD_LOG_1 make  $ODIR/amqp_0_9_1/server.exe & wait $!) &
MAKE_SERVER_PIDS="${MAKE_SERVER_PIDS} ${!}"

for pid in ${MAKE_SERVER_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "One of the built targets failed"
	fi
done
EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.Remade_Indicator\ $ODIR/amqp_0_9_1/server.Remade_Indicator\ ${MYDIR}/${BASENAME}.ps1\ ${MYDIR}/clients/python/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/amqp_0_9_1/`basename ${0} .sh`.okrun || Bail

exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

sleep 1

# Print_logs_in_background

# Trying to do without powershell (currently the logging redirection is a bit suspect -- it tends to have a delayed output... but also because the best option would be to use portable scripting, not ms-specific).
# powershell -Noninteractive -ExecutionPolicy ByPass -Command "${MYDIR}/${BASENAME}.ps1"  || Bail 
# here is the shell version (still not as complete)

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --reserve_memory_size 3G 

# Start client
for I in {1..5}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $!) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done

# Wait for clients to be done
for pid in ${CLIENT_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for clients to exit ok..."
	fi
done

Stop_synapse

sleep 1
Clean --Retain_exes

CLIENT_PIDS=""

# Repeat test with lazy loading on: as lazy load macro is important -- in this test we dont want to see any lazy loading (so will need to grep for it explicitly) :)
Start_synapse ./amqp_0_9_1/server.exe --reserve_memory_size 3G 
for I in {1..5}
do
	(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; nice -n 19 /c/Python27/python.exe ${MYDIR}/clients/python/${BASENAME}.py 2>&1 | tee ${BASENAME}_client_${I}_log.txt & wait $!) &
	CLIENT_PIDS="${CLIENT_PIDS} ${!}"
done
for pid in ${CLIENT_PIDS} ; do
	wait ${pid}
	if [ "$?" -ne "0" ]
	then
		Bail "Waiting for clients to exit ok..."
	fi
done
Stop_synapse
grep "LAZY_LOAD_LOG" synapse_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Lazy log logging activity should enabled in this server build."
fi
grep "POSTPON" synapse_log.txt
if [ "$?" -eq "0" ]
then
	Bail "Should not find any postone-related activity in the server log. Althougt this strictly depends on published timestamps! So may be just adjust the testing parameters"
fi

sleep 5

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
