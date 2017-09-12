MYDIR_=`dirname ${0}`  
MYDIR=`cd $MYDIR_ && pwd -W`
IVOCATION_DIR=`pwd -W`
BASENAME=`basename ${0} .sh`

set -o pipefail

export PS4='+(\t ${BASH_SOURCE}:${LINENO}): ${FUNCNAME[0]:+${FUNCNAME[0]}(): }'

Kill_Hack() {
	pssuspend64.exe -accepteula python.exe > /dev/null 2>&1
	kill $(jobs -p)
	pssuspend64.exe -accepteula -r python.exe > /dev/null 2>&1
}

Reap()
{
	Kill_Hack
	wait 
	echo 3>&-
}

Handler()
{
	echo "$(date) interrupted exit"
	Reap
}

Bail()
{
	if [ -z "${Data_Processors_Test_Common_Do_Not_Reap_On_Bail}" ]; then
		Reap
	fi
	set +x
	echo "----------------------------------------"
	echo ">>> BAIL <<< $(date) FAILED: " ${0} ${*}
	echo "----------------------------------------"
	exit 1
}

trap Handler SIGINT

Print_logs_in_background()
{
	( while true ; do pwd ; sleep 5 ; date +"%Y-%b-%d %T.%6N" ; find . -name "*_log.txt" -exec tail {} \; 2> /dev/null ; done ) &
#	WAIT_PIDS="${WAIT_PIDS} ${!}"
}

Start_synapse()
{
	echo "${1:?When calling Start_synapse() you now need to set first arg to path pointing to synapse executable}" > /dev/null
	Synapse_Path="${1}"
	if [ ! -x ${Synapse_Path} ]; then
		Bail "Synapse executable does not exist"
	fi
	Synapse_Directory=`dirname $1`
	Synapse_Binary=`basename $1`
	Synapse_Binary_Without_Extension=${Synapse_Binary%.*}
	shift
	if [ ! -p "server_stdin_fifo" ]; then
		mkfifo server_stdin_fifo
	fi
	if [ -n "$BUILD_COVERAGE" ] ; then 
		Synapse_Coverage_Commands_Path="${Synapse_Directory}/${Synapse_Binary_Without_Extension}.Coverage_Commands"
		EXEC_WRAPPER="${Synapse_Coverage_Commands_Path} --Wrap_Test_Run "
	fi
	(trap 'Kill_Hack' SIGTERM ; ${EXEC_WRAPPER} ${Synapse_Path} --IO_Services_Size 2 --listen_on localhost:5672 --database_root Synapse_database.tmp $* < ./server_stdin_fifo > synapse_log.txt 2>&1 & wait $!) &
	SYNAPSE_PID=${!}
	exec 3> server_stdin_fifo
	#	WAIT_PIDS="${WAIT_PIDS} ${!}"


	# Noting for posterity, this method alone wont work because the server may well start up but the socket-listening part of functionality may not start at the same time (exactly) as the process start/load... so subsequent connections from clients may still barf...
	for I in {1..15}
	do
		if ps -p ${SYNAPSE_PID} > /dev/null
		then
			echo "$(date) Synapse starting ok"
			break
		else
			sleep 1
		fi
	done

	# so also trying to actually connect via tcp socket (on its own however this method also may not work as system may explicitly refuse connections 'real-quick' so the loop may iterate faster than 15 secs -- if 'nc' is connecting before the process is proprely loaded in RAM by the background job).
	for I in {1..30}
	do
		nc -z -w 1 localhost 5672 && break
	done
	nc -z -w 1 localhost 5672 || Bail "Could not start server in time"

	echo "$(date) Synapse started ok"
}

Clean() {
	Reap
	local Delete_exes="Yes"
	find . -iname "*_log.txt" -exec rm {} \;
	while [ "$1" != "" ];
	do
		case $1 in
	#		--Synapse_database ) shift
	#		Synapse_database="$1" ;;
			--Retain_exes ) 
			Delete_exes="No" ;;
		esac
		shift
	done
	find . -type d -name "*database*.tmp" -exec rm -frv {} \;
	# delete exe files 
	if [ ${Delete_exes} = "Yes" ]; then
		rm -fv server_stdin_fifo
		find . -name "*.exe" -exec rm -fv {} \;
		find . -name "*.h.gch" -exec rm -fv {} \;
		find . -name "*.gcno" -exec rm -fv {} \;
		find . -name "*.gcda" -exec rm -fv {} \;
	fi
}

Stop_synapse() {
	timeout 35 echo ! >&3 || Bail "Trying to write exit char (!) to server"
	( trap 'Kill_Hack' SIGTERM ; sleep 700 & wait $! ; kill ${SYNAPSE_PID} ; ) &
	TIMEOUT_PID=$!
	wait ${SYNAPSE_PID} || Bail "Waiting for server to exit ok..."
	Synapse_Exit_Code=$?
	if [ $Synapse_Exit_Code -ne 0 ]; then
		Bail "Synapse did not exit OK"
	fi
	kill ${TIMEOUT_PID}
	wait ${TIMEOUT_PID}
	if [ -n "$BUILD_COVERAGE" ] ; then 
		if [ ! -x ${Synapse_Coverage_Commands_Path} ]; then
			Bail "Synapse_Coverage_Commands_Path does not exist"
		fi
		${Synapse_Coverage_Commands_Path} --After_Test_Run
	fi
}

if [ -z "${TESTING_DIRECTORY}" ] ; then 
	Bail "Variable TESTING_DIRECTORY must be defined in the environment of the running shell."
fi

export CFLAGS=-DLOG_ON 
