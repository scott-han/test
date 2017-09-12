#! /bin/sh -x

MYDIR_=`dirname ${0}`  
MYDIR=`cd $MYDIR_ && pwd -W`
IVOCATION_DIR=`pwd -W`
BASENAME=`basename ${0} .sh`

Reap()
{
	kill $(jobs -p)
	wait 
	echo 3>&-
}

Handler()
{
	echo "interrupted exit"
	Reap
}

Bail()
{
	touch ./stop_pf
	Remote_stop_synapse
	Reap
	set +x
	echo "FAILED: " ${0} ${*}
	exit 1
}

Clean() {
	Reap
	local Delete_exes="Yes"
	rm -frv *_log.txt
	while [ "$1" != "" ];
	do
		case $1 in
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
	fi
}

trap Handler SIGINT

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}
# This variable is how many message will be published while testing.
TOTAL_MESSAGE=1000000
# How many test clients per computer
MAX_CLIENTS=20
# Message size
#MESSAGE_SIZES="200 400 800 1600 3200 6400 12800 25600 51200 102400 204800"
MESSAGE_SIZES="110 200 400 800 1600"
SERVER=ult-synapse-dv2
PORT=4672
ALL_MACHINES="ULT-TDC-TEST1 ULT-TDC-UTIL1"
TOTAL_MACHINE=0
INTERVALS="2000 1000 500 200 100 50 20"
# To enable multiple publishers and multiple subscribers test
MPUBMSUB=1
# To enable multiple publishers test
MPUB=1
# To enable one publisher and one subscriber test
ONEPONES=1
# To enable one publisher test
ONEPUB=1
# To enable latency test with different publishing rate
VARRATE=1

Remote_start_synapse()
{
	( nohup ssh rogerl@${SERVER} "./start_synapse.sh < /dev/null > /dev/null 2>&1" < /dev/null > /dev/null 2>&1 ) &
}


Remote_stop_synapse() 
{
	( nohup ssh rogerl@${SERVER} "./stop_synapse.sh" < /dev/null > /dev/null 2>&1 ) &
	wait ${!}
}

Remote_execute()
{
	( nohup ssh rogerl@${1} "${2}" < /dev/null > /dev/null 2>&1 ) &
	wait "$!"
}

Generate_pub()
{
	echo '#! /bin/sh -x' > ./pub.sh
	echo "MAX_CLIENTS=${MAX_CLIENTS}" >> ./pub.sh
	echo "PORT=${PORT}" >> ./pub.sh
	echo "SERVER=${SERVER}" >> ./pub.sh
	echo "TOTAL_MESSAGE=${TOTAL_MESSAGE}" >> ./pub.sh
	echo "BASENAME=${BASENAME}" >> ./pub.sh
	echo 'Reap()' >> ./pub.sh
	echo '{' >> ./pub.sh
	echo '	kill $(jobs -p)' >> ./pub.sh
	echo '	wait' >> ./pub.sh
	echo '	echo 3>&-' >> ./pub.sh
	echo '}	' >> ./pub.sh
	echo 'Bail()' >> ./pub.sh
	echo '{' >> ./pub.sh
	echo 'Reap' >> ./pub.sh
	echo 'set +x' >> ./pub.sh
	echo "echo \"FAILED: \" ${0} ${*}" >> ./pub.sh
	echo 'exit 1' >> ./pub.sh
	echo '}' >> ./pub.sh
	echo 'CLIENT_PIDS=""' >> ./pub.sh
	echo 'for (( K=1; K<=${2}; K++ )); do' >> ./pub.sh
	echo '	( ./Synapse_performance_test_client.exe --synapse_at ${SERVER}:${PORT} --message_size ${1} --topic test.${4}topic${K} --total_messages $((${5}*${TOTAL_MESSAGE})) --duration ${6} > ${BASENAME}_${1}_${4}_${2}_${3}_pub_${K}_log.txt 2>&1 ) &' >> ./pub.sh
	echo '	CLIENT_PIDS="${CLIENT_PIDS} ${!}"' >> ./pub.sh
	echo 'done' >> ./pub.sh
	echo 'for pid in ${CLIENT_PIDS} ; do' >> ./pub.sh
	echo '	wait ${pid}' >> ./pub.sh
	echo '	if [ "$?" -ne "0" ]; then' >> ./pub.sh
	echo '		Bail "Wait for clients to exit ok ..."' >> ./pub.sh
	echo '	fi' >> ./pub.sh
	echo 'done	' >> ./pub.sh
	echo 'echo done' >> ./pub.sh
}

Generate_sub()
{
	echo '#! /bin/sh -x' > ./sub.sh
	echo "MAX_CLIENTS=${MAX_CLIENTS}" >> ./sub.sh
	echo "PORT=${PORT}" >> ./sub.sh
	echo "SERVER=${SERVER}" >> ./sub.sh
	echo "TOTAL_MESSAGE=${TOTAL_MESSAGE}" >> ./sub.sh
	echo "BASENAME=${BASENAME}" >> ./sub.sh
	echo 'Reap()' >> ./sub.sh
	echo '{' >> ./sub.sh
	echo '	kill $(jobs -p)' >> ./sub.sh
	echo '	wait' >> ./sub.sh
	echo '	echo 3>&-' >> ./sub.sh
	echo '}	' >> ./sub.sh
	echo 'Bail()' >> ./sub.sh
	echo '{' >> ./sub.sh
	echo 'Reap' >> ./sub.sh
	echo 'set +x' >> ./sub.sh
	echo "echo \"FAILED: \" ${0} ${*}" >> ./sub.sh
	echo 'exit 1' >> ./sub.sh
	echo '}' >> ./sub.sh
	echo 'CLIENT_PIDS=""' >> ./sub.sh
	echo 'for (( K=1; K<=${3}; K++ )); do' >> ./sub.sh
	echo '	( ./Synapse_performance_test_client.exe --synapse_at ${SERVER}:${PORT} --sub --topic test.${4}topic${K} --total_messages $((${5}*${TOTAL_MESSAGE})) > ${BASENAME}_${1}_${4}_${2}_${3}_sub_${K}_log.txt 2>&1 ) &' >> ./sub.sh
	echo '	CLIENT_PIDS="${CLIENT_PIDS} ${!}"' >> ./sub.sh
	echo 'done' >> ./sub.sh
	echo 'for pid in ${CLIENT_PIDS} ; do' >> ./sub.sh
	echo '	wait ${pid}' >> ./sub.sh
	echo '	if [ "$?" -ne "0" ]; then' >> ./sub.sh
	echo '		Bail "Wait for clients to exit ok ..."' >> ./sub.sh
	echo '	fi' >> ./sub.sh
	echo 'done	' >> ./sub.sh
}

Generate_start_synapse()
{
	echo '#! /bin/sh -x' > ./start_synapse.sh
	echo "PORT=${PORT}" >> ./start_synapse.sh
	echo 'Reap()' >> ./start_synapse.sh
	echo '{' >> ./start_synapse.sh
	echo '	kill $(jobs -p)' >> ./start_synapse.sh
	echo '	wait' >> ./start_synapse.sh
	echo '	echo 3>&-' >> ./start_synapse.sh
	echo '}	' >> ./start_synapse.sh
	echo 'Bail()' >> ./start_synapse.sh
	echo '{' >> ./start_synapse.sh
	echo 'Reap' >> ./start_synapse.sh
	echo 'set +x' >> ./start_synapse.sh
	echo "echo \"FAILED: \" ${0} ${*}" >> ./start_synapse.sh
	echo 'exit 1' >> ./start_synapse.sh
	echo '}' >> ./start_synapse.sh
	echo 'rm ./stop_synapse_now' >> ./start_synapse.sh
	echo 'if [ ! -p "server_stdin_fifo" ]; then' >> ./start_synapse.sh
	echo '	mkfifo server_stdin_fifo' >> ./start_synapse.sh
	echo 'fi' >> ./start_synapse.sh
	echo '( ./server.exe --listen_on 0.0.0.0:${PORT} --database_root d:\perf_db --IO_Service_Concurrency_Reserve_Multiplier 3 --IO_Services_Size 16 < ./server_stdin_fifo > synapse_log.txt 2>&1 ) &' >> ./start_synapse.sh
	echo 'SYNAPSE_PID=${!}' >> ./start_synapse.sh
	echo 'echo ${SYNAPSE_PID} > ./synapse_id' >> ./start_synapse.sh
	echo 'exec 3> server_stdin_fifo' >> ./start_synapse.sh
	echo 'for I in {1..15}' >> ./start_synapse.sh
	echo 'do' >> ./start_synapse.sh
	echo '	if ps -p ${SYNAPSE_PID} > /dev/null' >> ./start_synapse.sh
	echo '	then' >> ./start_synapse.sh
	echo '		echo "Synapse starting ok"' >> ./start_synapse.sh
	echo '		break' >> ./start_synapse.sh
	echo '	else' >> ./start_synapse.sh
	echo '		sleep 1' >> ./start_synapse.sh
	echo '	fi' >> ./start_synapse.sh
	echo 'done' >> ./start_synapse.sh
	echo 'for I in {1..15}' >> ./start_synapse.sh
	echo 'do' >> ./start_synapse.sh
	echo '	nc -z -w 1 localhost ${PORT} && break' >> ./start_synapse.sh
	echo 'done' >> ./start_synapse.sh
	echo 'nc -z -w 1 localhost ${PORT} || Bail "Could not start server in time"' >> ./start_synapse.sh
	echo 'echo "Synapse started ok"' >> ./start_synapse.sh
	echo 'while [ ! -f ./stop_synapse_now ]; do' >> ./start_synapse.sh
	echo 'sleep 2' >> ./start_synapse.sh
	echo 'done' >> ./start_synapse.sh
	echo 'timeout 35 echo ! >&3 || Bail "Trying to write exit char (!) to server"' >> ./start_synapse.sh
	echo 'wait ${SYNAPSE_PID} || Bail "Waiting for server to exit ok..."' >> ./start_synapse.sh
	echo 'exec 3>&-' >> ./start_synapse.sh
	
}

Generate_stop_synapse()
{
	echo '#! /bin/sh -x' > ./stop_synapse.sh
	echo 'Reap()' >> ./stop_synapse.sh
	echo '{' >> ./stop_synapse.sh
	echo '	kill $(jobs -p)' >> ./stop_synapse.sh
	echo '	wait' >> ./stop_synapse.sh
	echo '	echo 3>&-' >> ./stop_synapse.sh
	echo '}	' >> ./stop_synapse.sh
	echo 'Bail()' >> ./stop_synapse.sh
	echo '{' >> ./stop_synapse.sh
	echo 'Reap' >> ./stop_synapse.sh
	echo 'set +x' >> ./stop_synapse.sh
	echo "echo \"FAILED: \" ${0} ${*}" >> ./stop_synapse.sh
	echo 'exit 1' >> ./stop_synapse.sh
	echo '}' >> ./stop_synapse.sh
	echo 'SYNAPSE_PID=$(< ./synapse_id)' >> ./stop_synapse.sh
	echo 'touch ./stop_synapse_now' >> ./stop_synapse.sh
	echo '( sleep 30 ; kill ${SYNAPSE_PID} ; ) &' >> ./stop_synapse.sh
	echo 'TIMEOUT_PID=$!' >> ./stop_synapse.sh
	echo 'while ps -p ${SYNAPSE_PID} > /dev/null' >> ./stop_synapse.sh
	echo 'do' >> ./stop_synapse.sh
	echo 'sleep 1' >> ./stop_synapse.sh
	echo 'done' >> ./stop_synapse.sh
	echo 'kill ${TIMEOUT_PID}' >> ./stop_synapse.sh
	echo 'rm ./server_stdin_fifo' >> ./stop_synapse.sh
}

Generate_powershell_script()
{
	echo 'param([String]$Output="e:\result.txt", [String]$Server="localhost")' > ./perf.ps1
	echo 'if (Test-Path ".\stop_pf")' >> ./perf.ps1
	echo '{' >> ./perf.ps1
	echo '	Remove-Item ".\stop_pf" | Out-Null' >> ./perf.ps1
	echo '}' >> ./perf.ps1
	echo 'DO {' >> ./perf.ps1
	echo '$CPU=(Get-Counter -ComputerName $Server "\Process(server)\% Processor Time").CounterSamples.CookedValue' >> ./perf.ps1
	echo '$Mem=(Get-Counter -ComputerName $Server "\Process(server)\Working Set").CounterSamples.CookedValue' >> ./perf.ps1
	echo '$Disk=(Get-Counter -computername $Server "\Process(server)\IO Data Bytes/sec").CounterSamples.CookedValue' >> ./perf.ps1
	echo '$Network=(Get-WmiObject -ComputerName $Server -Class Win32_PerfFormattedData_Tcpip_NetworkInterface | measure BytesTotalPersec -Sum).Sum' >> ./perf.ps1
	echo '$Now=[int64](([datetime]::UtcNow)-(get-date "1/1/1970")).TotalSeconds' >> ./perf.ps1
	echo '"$CPU, $Mem, $Disk, $Network, $Now" | Out-File -encoding ASCII $Output -Append' >> ./perf.ps1
	#echo 'Start-Sleep -s 1' >> ./perf.ps1
	echo '}While (-Not (Test-Path ".\stop_pf"))' >> ./perf.ps1
	
	echo 'param([String]$Output="e:\result.txt", [String]$Server="ULT-SYNAPSE-DV2")' > ./query.ps1
	echo '$Cores=(Get-WMIObject -Class Win32_Processor -Computer $Server | Measure-Object -Property numberoflogicalprocessors -Sum | Select-Object Sum).Sum' >> ./query.ps1
	echo '$Temp=(Get-CimInstance Win32_OperatingSystem -Computer $Server | Select-Object Caption, Version, OSArchitecture)' >> ./query.ps1
	echo '$OS=$Temp.Caption' >> ./query.ps1
	echo '$OSVer=$Temp.Version' >> ./query.ps1
	echo '$OSBits=$Temp.OSArchitecture' >> ./query.ps1
	echo '$Mem=(Get-CimInstance Win32_PhysicalMemory -Computer $Server | Measure-Object -Property capacity -Sum | Select-Object Sum).Sum' >> ./query.ps1
	echo '$CPU=(Get-WMIObject -Computer $Server -Class Win32_Processor | Select-Object Name, MaxClockSpeed)' >> ./query.ps1
	echo '$CPUName=$CPU.Name[0]' >> ./query.ps1
	echo '$CPUSpeed=$CPU.MaxClockSpeed[0]' >> ./query.ps1
	echo '$SOCKET=$CPU.length' >> ./query.ps1
	echo '"$Server,$OS,$OSVer,$OSBits,$Mem,$CPUName,$Cores,$CPUSpeed,$SOCKET" | Out-File -encoding ASCII $Output -Append' >> ./query.ps1
}

Check_server()
{
	for I in {1..15}
	do
		nc -z -w 1 ${SERVER} ${PORT} && break
		sleep 1
	done
	nc -z -w 1 ${SERVER} ${PORT} || Bail "Could not start server in time"
}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
	# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
	# but we currently can reuse the binaries from other potential targets because the build is generic enough.
	CFLAGS=${CFLAGS}\ -DLOG_ON make amqp_0_9_1/server.exe
	# Custom build to simulate Archive_maker get topic truncation timestamp at first.
	EXPLICIT_PREREQUISITES=./amqp_0_9_1/server.exe\ ./database/${BASENAME}.py\ ./database/${BASENAME}.Rmd OUTPUT_DIR=$ODIR/ make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

mkdir -p database || Bail
mkdir -p amqp_0_9_1 || Bail
cd ${IVOCATION_DIR} || Bail

CFLAGS=${CFLAGS}\ -DLOG_ON make  $ODIR/amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe || Bail
cd $ODIR || Bail
cp ${MYDIR}/${BASENAME}.py $ODIR/
cp ${MYDIR}/${BASENAME}.Rmd $ODIR/
cp ${MYDIR}/../amqp_0_9_1/server.exe ${ODIR}/amqp_0_9_1/
Generate_start_synapse
Generate_stop_synapse
touch ./stop_pf
Remote_stop_synapse
Remote_execute ${SERVER} "rm -fr db"
Remote_execute ${SERVER} "rm ./Synapse_*.txt"

scp ./amqp_0_9_1/server.exe rogerl@${SERVER}:~/
scp ./start_synapse.sh rogerl@${SERVER}:~/
scp ./stop_synapse.sh rogerl@${SERVER}:~/
scp ./perf.ps1 rogerl@${SERVER}:~/
Generate_pub
Generate_sub
MACHINE0=""
for C in ${ALL_MACHINES} ; do
    TOTAL_MACHINE=$(($TOTAL_MACHINE+1))
	if [ "${TOTAL_MACHINE}" -eq "1" ]; then
		MACHINE0="${C}"
	fi
	scp ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe rogerl@${C}:~/
	scp ./pub.sh rogerl@${C}:~/
	scp ./sub.sh rogerl@${C}:~/
done

Generate_powershell_script
rm ./computer.txt
ssh rogerl@${SERVER} "rm computer.txt"
scp ./query.ps1 rogerl@${SERVER}:~/
ssh rogerl@${SERVER} "powershell -NoProfile -ExecutionPolicy Bypass -Command ./query.ps1 -Output computer.txt -Server ${SERVER}"
scp rogerl@${SERVER}:~/computer.txt ./
for C in ${ALL_MACHINES} ; do
	ssh rogerl@${C} "rm *.txt"
	scp ./query.ps1 rogerl@${C}:~/
	ssh rogerl@${C} "powershell -NoProfile -ExecutionPolicy Bypass -Command ./query.ps1 -Output ${C}.txt -Server ${C}"
	scp rogerl@${C}:~/${C}.txt ./
	cat ${C}.txt >> computer.txt
done
for N in {1..1}; do
	rm ./Synapse*.txt
	rm ./latency*.txt
	rm ./mr*.txt
	rm perf_*.txt
	ssh rogerl@${SERVER} "rm ./perf_*.txt"

	echo -e "Start Time\t$(date +%c)" > ./test_date.txt

	for S in ${MESSAGE_SIZES} ; do
		# multiple pub andd multiple sub
		Remote_execute ${SERVER} "rm -fr /d/perf_db"
		if [ "${MPUBMSUB}" -eq "1" ] ; then
			Remote_start_synapse
			sleep 1
			Check_server
			CLIENT_PIDS=""
			( ssh rogerl@${SERVER} "( powershell -NoProfile -ExecutionPolicy Bypass -Command ./perf.ps1 -Output perf_${S}_${MAX_CLIENTS}pub${MAX_CLIENTS}sub.txt -Server localhost ) " < /dev/null > /dev/null 2>&1 ) &
			PERF_PID=$!
			
			for M in ${ALL_MACHINES}; do
				MACHINE0=${M}
				( nohup ssh rogerl@${M} "./sub.sh ${S} ${MAX_CLIENTS} ${MAX_CLIENTS} ${M} 10" < /dev/null > /dev/null 2>&1 ) &
				CLIENT_PIDS="${CLIENT_PIDS} ${!}"
				( nohup ssh rogerl@${M} "./pub.sh ${S} ${MAX_CLIENTS} ${MAX_CLIENTS} ${M} 10 -1" < /dev/null > /dev/null 2>&1 ) &
				CLIENT_PIDS="${CLIENT_PIDS} ${!}"
			done
			LATENCY_CLIENT_PIDS=""
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --sub --tcp_no_delay --synapse_at ${SERVER}:${PORT} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}pub${MAX_CLIENTS}sub.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --has_waypoint --tcp_no_delay --synapse_at ${SERVER}:${PORT} --message_size ${S} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}_mpubmsub_pub_log.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			for pid in ${LATENCY_CLIENT_PIDS} ; do
				wait ${pid}
				if [ "$?" -ne "0" ]
				then
					Bail "Waiting for remote execution to exit ok..."
				fi
			done
			sleep 20
			ssh rogerl@${SERVER} "touch ./stop_pf"
			wait ${PERF_PID}
			Remote_stop_synapse
			sleep 1
			for pid in ${CLIENT_PIDS}; do
				wait ${pid}
			done
			Remote_execute ${SERVER} "yes | cp Synapse_log.txt Synapse_${S}_${MAX_CLIENTS}pub${MAX_CLIENTS}sub.txt"
		fi
		
		# multiple publisher
		if [ "${MPUB}" -eq "1" ] ; then
			Remote_execute ${SERVER} "rm -fr /d/perf_db"

			Remote_start_synapse
			sleep 1
			Check_server
			CLIENT_PIDS=""
			( ssh rogerl@${SERVER} "( powershell -NoProfile -ExecutionPolicy Bypass -Command ./perf.ps1 -Output perf_${S}_${MAX_CLIENTS}pub.txt -Server ${SERVER} ) " < /dev/null > /dev/null 2>&1 ) &
			PERF_PID=$!
			for M in ${ALL_MACHINES}; do
				( nohup ssh rogerl@${M} "./pub.sh ${S} ${MAX_CLIENTS} 0 ${M} 10 -1" < /dev/null > /dev/null 2>&1 ) &
				CLIENT_PIDS="${CLIENT_PIDS} ${!}"
			done
			LATENCY_CLIENT_PIDS=""
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --sub --tcp_no_delay --synapse_at ${SERVER}:${PORT} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}pub.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --has_waypoint --tcp_no_delay --synapse_at ${SERVER}:${PORT} --message_size ${S} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}_mpub_pub_log.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			for pid in ${LATENCY_CLIENT_PIDS=""} ; do
				wait ${pid}
				if [ "$?" -ne "0" ]
				then
					Bail "Waiting for remote execution to exit ok..."
				fi
			done	
			sleep 20
			ssh rogerl@${SERVER} "touch ./stop_pf"
			wait ${PERF_PID}
			Remote_stop_synapse
			sleep 1
			for pid in ${CLIENT_PIDS} ; do
				wait ${pid}
			done
			Remote_execute ${SERVER} "yes | cp Synapse_log.txt Synapse_${S}_${MAX_CLIENTS}pub.txt"
		fi
		
		# One publisher only
		if [ "${ONEPUB}" -eq "1" ] ; then
			Remote_execute ${SERVER} "rm -fr /d/perf_db"

			Remote_start_synapse
			sleep 1
			Check_server
			CLIENT_PIDS=""
			( ssh rogerl@${SERVER} "( powershell -NoProfile -ExecutionPolicy Bypass -Command ./perf.ps1 -Output perf_${S}_1pub.txt -Server ${SERVER} ) " < /dev/null > /dev/null 2>&1 ) &
			PERF_PID=$!
			( nohup ssh rogerl@${MACHINE0} "./pub.sh ${S} 1 0 ${MACHINE0} 12 -1" < /dev/null > /dev/null 2>&1 ) &
			CLIENT_PIDS="${CLIENT_PIDS} ${!}"
			LATENCY_CLIENT_PIDS=""
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --sub --tcp_no_delay --synapse_at ${SERVER}:${PORT} --topic test.latency1 --total_messages 1000 >latency_${S}_1pub.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --has_waypoint --tcp_no_delay --synapse_at ${SERVER}:${PORT} --message_size ${S} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}_1pub_pub_log.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			for pid in ${LATENCY_CLIENT_PIDS} ; do
				wait ${pid}
				if [ "$?" -ne "0" ]
				then
					Bail "Waiting for remote execution to exit ok..."
				fi
			done
			Sleep 20
			ssh rogerl@${SERVER} "touch ./stop_pf"
			wait ${PERF_PID}
			Remote_stop_synapse
			sleep 1
			for pid in ${CLIENT_PIDS} ; do
				wait ${pid}
			done		
			Remote_execute ${SERVER} "yes | cp Synapse_log.txt Synapse_${S}_1pub.txt"
		fi
		
		# 1 pub 1 sub
		if [ "${ONEPONES}" -eq "1" ] ; then
			Remote_execute ${SERVER} "rm -fr /d/perf_db"
			
			Remote_start_synapse
			sleep 1
			Check_server
			CLIENT_PIDS=""
			( ssh rogerl@${SERVER} "( powershell -NoProfile -ExecutionPolicy Bypass -Command ./perf.ps1 -Output perf_${S}_1pub1sub.txt -Server ${SERVER} ) " < /dev/null > /dev/null 2>&1 ) &
			PERF_PID=$!
			( nohup ssh rogerl@${MACHINE0} "./sub.sh ${S} 1 1 ${MACHINE0} 10" < /dev/null > /dev/null 2>&1 ) &
			CLIENT_PIDS="${CLIENT_PIDS} ${!}"
			( nohup ssh rogerl@${MACHINE0} "./pub.sh ${S} 1 1 ${MACHINE0} 10 -1" < /dev/null > /dev/null 2>&1 ) &
			CLIENT_PIDS="${CLIENT_PIDS} ${!}"
			LATENCY_CLIENT_PIDS=""
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --sub --tcp_no_delay --synapse_at ${SERVER}:${PORT} --topic test.latency1 --total_messages 1000 >latency_${S}_1pub1sub.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
			( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --has_waypoint --tcp_no_delay --synapse_at ${SERVER}:${PORT} --message_size ${S} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}_1pub1sub_pub_log.txt 2>&1 ) &
			LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"

			for pid in ${LATENCY_CLIENT_PIDS} ; do
				wait ${pid}
				if [ "$?" -ne "0" ]
				then
					Bail "Waiting for clients to exit ok..."
				fi
			done
			sleep 20
			ssh rogerl@${SERVER} "touch ./stop_pf"
			wait ${PERF_PID}
			Remote_stop_synapse
			sleep 1
			for pid in ${CLIENT_PIDS} ; do
				wait ${pid}
			done		

			Remote_execute ${SERVER} "yes | cp Synapse_log.txt Synapse_${S}_1pub1sub.txt"
		fi
		
		# This is to test how the latency is changed while publishing messages in different rate.
		if [ "${VARRATE}" -eq "1" ] ; then
			Remote_execute ${SERVER} "rm -fr /d/perf_db"
			# Anoter latency test
			for SPAN in ${INTERVALS} ; do
				Remote_start_synapse
				sleep 1
				Check_server
				CLIENT_PIDS=""
				( ssh rogerl@${SERVER} "( powershell -NoProfile -ExecutionPolicy Bypass -Command ./perf.ps1 -Output perf_${S}_${MAX_CLIENTS}_${SPAN}pub.txt -Server ${SERVER} ) " < /dev/null > /dev/null 2>&1 ) &
				PERF_PID=$!
				for M in ${ALL_MACHINES}; do
					( nohup ssh rogerl@${M} "./pub.sh ${S} ${MAX_CLIENTS} 0 ${M} 1 ${SPAN}" < /dev/null > /dev/null 2>&1 ) &
					CLIENT_PIDS="${CLIENT_PIDS} ${!}"
				done
				( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --sub --tcp_no_delay --synapse_at ${SERVER}:${PORT} --topic test.latency1 --total_messages 1000 >mrlatency_${S}_${MAX_CLIENTS}_${SPAN}_pub.txt 2>&1 ) &
				LATENCY_CLIENT_PIDS=""
				LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
				( ./amqp_0_9_1/clients/Cpp/Synapse_performance_test_client.exe --has_waypoint --tcp_no_delay --synapse_at ${SERVER}:${PORT} --message_size ${S} --topic test.latency1 --total_messages 1000 >latency_${S}_${MAX_CLIENTS}_${SPAN}_mpub_pub_log.txt 2>&1 ) &
				LATENCY_CLIENT_PIDS="${LATENCY_CLIENT_PIDS} ${!}"
				for pid in ${LATENCY_CLIENT_PIDS} ; do
					wait ${pid}
					if [ "$?" -ne "0" ]
					then
						Bail "Waiting for remote execution to exit ok..."
					fi
				done	
				sleep 10
				ssh rogerl@${SERVER} "touch ./stop_pf"
				wait ${PERF_PID}
				Remote_stop_synapse
				sleep 1
				for pid in ${CLIENT_PIDS} ; do
					wait ${pid}
				done		
				Remote_execute ${SERVER} "rm -fr /d/perf_db"
				scp rogerl@${SERVER}:~/Synapse_log.txt ./mrSynapse_${S}_${SPAN}_mpub.txt
			done
		fi
	done

	scp rogerl@${SERVER}:~/Synapse_*.txt ./
	scp rogerl@${SERVER}:~/perf_*.txt ./

	# analyze log files and generate report
	rm ./*.csv

	/c/python27/Python.exe ./${BASENAME}.py ${MAX_CLIENTS} ${MESSAGE_SIZES}
	echo -e "End Time\t$(date +%c)" >> ./test_date.txt
	echo -e "Max Clients\t${MAX_CLIENTS}" >> ./test_date.txt
	echo -e "Total Messages\t${TOTAL_MESSAGE}" >> ./test_date.txt
	R -e "rmarkdown::render('${BASENAME}.Rmd')"
	if [ "$?" -ne "0" ]; then
		Bail "R -e execution failed."
	fi
	cp ./${BASENAME}.html ./Performance_report_${N}.html
	rm ./${BASENAME}.html
	rm ./database/${BASENAME}.okrun
	cd $ODIR || Bail 
	Clean --Retain_exes
done

mkdir report
cp *.html ./report/
rm ./*.csv
rm ./*.txt
rm ./*.html
rm ./*.sh
rm ./*.ps1
rm ./stop_pf

set +x
echo  TESTED OK: ${0}
