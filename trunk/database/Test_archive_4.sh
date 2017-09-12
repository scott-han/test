#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh
. `dirname ${0}`/Test_archive_common.sh
# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}
# This variable is how many message will be published while testing.
TOTAL_MESSAGE=3000
# This variable is to control how many topics we will have during testing.
TOTAL_TOPIC=5


if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
make amqp_0_9_1/server.exe || Bail
make amqp_0_9_1/clients/Cpp/Archive_maker.exe || Bail
EXPLICIT_PREREQUISITES=./amqp_0_9_1/clients/Cpp/Archive_maker.exe\ ./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.bat\ ${MYDIR}/${BASENAME}.ps1\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ CFLAGS=${CFLAGS}\ -DARCHIVE_MAKER_TEST make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

START_TIME="$(date +%s)"

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

mkdir -p database || Bail
cp ${MYDIR}/${BASENAME}.bat ./ || Bail
cp ${MYDIR}/${BASENAME}.ps1 ./ || Bail

Print_logs_in_background
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe
sleep 1
Create_archive_fifo_info

# Start archive_maker at first.
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive1 --subscribe_from "20160101T000000" --subscription test.* --timediff_tolerance 1 --check_timestamp_period 1 --timediff_alert echo --alarm_interval 60 --use_current_time 1 < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_0_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
sleep 5

#Publish a message whose timestamp is 30 days ago.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 30 1 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 5

current_time="$(date +%s)"

#To ensure Latest_timestamp is updated in application.
grep -e "Latest timestamp for topic test.11topic1 is " ${BASENAME}_client_0_log.txt | while read -r line ; do
	BDEC=${line##* }
	BDEC=$(($BDEC/1000000))
	BDEC=$(($BDEC+720*60*60))
	if [ $current_time -le $BDEC ]; then
		Bail "Something is wrong as Archive_maker could not find the last archived timestamp for topic test.11topic1."
		break
	fi
done
# Check Archive_maker's log, it should include something like "The following topics are running late: {[test.11topic1, 30 days behind]"
total="$(grep -c "The following topics are running late: {\[test.11topic1, 30 days behind\]" ${BASENAME}_client_0_log.txt)"
if [ $total -lt 1 ]; then
	Bail "'The following topics are running late: {[test.11topic1, 30 days behind]' is not found as expected."
fi

# Wait 5 seconds which will allow timer reset checking flag to true.
sleep 5

# Publish another message whose timestamp is 29 days ago.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 29 2 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
#To ensure Latest_timestamp is updated in application.
current_time="$(date +%s)"
grep -e "Latest timestamp for topic test.11topic1 is " ${BASENAME}_client_0_log.txt | while read -r line ; do
	BDEC=${line##* }
	BDEC=$(($BDEC/1000000))
	BDEC=$(($BDEC+29*24*60*60))
	if [ $current_time -le $BDEC ]; then
		Bail "Something is wrong as Archive_maker could not find the last archived timestamp for topic test.11topic1."
		break
	fi
done
sleep 10
# Check Archive_maker's log, it should include something like "is reunning late, but it will NOT send alarm as the previous is just sent out"
total="$(grep -c "Archive_maker is running late, but it will NOT send alarm as the previous is just sent out" ${BASENAME}_client_0_log.txt)"
if [ $total -le 1 ]; then
	Bail "'Archive_maker is running late, but it will NOT send alarm as the previous is just sent out' is not found as expected."
fi
# Wait 60 seconds to ensure alarm send interval is passed.
sleep 60

# Publish another message 29 days ago.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 29 3 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 10

# Check Archive_maker's log, it should include something like "The following topics are running late: {[test.11topic1, 29 days begind]" and "return value is 0."
total="$(grep -c "The following topics are running late: {\[test.11topic1, 29 days behind\]" ${BASENAME}_client_0_log.txt)"
if [ $total -ne 2 ]; then
	Bail "'The following topics are running late: {[test.11topic1, 29 days behind]' is not found as expected."
fi

sleep 1
# Publish another message with current timestamp.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 0 4 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 10

# Check Archive_maker's log, it should include something like "Archive_maker is currently running less than 1 days behind.".
total="$(grep -c "Archive_maker is currently running less than 1 days behind" ${BASENAME}_client_0_log.txt)"
if [ $total -ne 2 ]; then
	Bail "'Archive_maker is currently running less than 1 days behind' is not found as expected."
fi


# Wait 60 seconds to ensure alarm sending interval is elapsed.
sleep 60

# Publish another message with current timestamp.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 0 5 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 10
# Check Archive_maker's log, it should include something like "Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification."
total="$(grep -c "Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification" ${BASENAME}_client_0_log.txt)"
if [ $total -lt 1 ]; then
	Bail "'Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification' is not found as expected."
fi

# Wait 60 seconds.
sleep 60

# Publish another message with current timestamp
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 0 7 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 10
# Check Archive_maker's log, it should include something like "Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification." (2 times)
total="$(grep -c "Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification" ${BASENAME}_client_0_log.txt)"
if [ $total -lt 2 ]; then
	Bail "'Archive_maker has caught up, but as it has already notified, therfore it will NOT send this notification' is not found as expected."
fi

sleep 1
# Publish another message whose timestamp is 4 days ago (to different topic)
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 12topic 4 1 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 10
# Check Archive_maker's log, it should include something like The following topics are running late: {[test.12topic1, 4 days beghind]"
total="$(grep -c "The following topics are running late: {\[test.12topic1, 4 days behind\]" ${BASENAME}_client_0_log.txt)"
if [ $total -ne 2 ]; then
	Bail "'The following topics are running late: {[test.12topic1, 4 days behind]' is not found as expected."
fi

# Publish another message with current timestamp
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 11topic 0 8 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 10

# Check Archive_maker's log, it should include something like "Archive_maker is currently running less than 1 days behind.".
total="$(grep -c " Archive_maker is running late, but it will NOT send alarm as the previous is just sent out" ${BASENAME}_client_0_log.txt)"
if [ $total -lt 4 ]; then
	Bail "' Archive_maker is running late, but it will NOT send alarm as the previous is just sent out' is not found as expected."
fi

sleep 1
# Publish another message whose timestamp is 4 days ago (to different topic)
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 1 1 12topic 3 2 2>&1 | tee ${BASENAME}_client_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 60
# Check Archive_maker's log, it should include something like "Archive_maker is currently running 3 days behind." although the catching up notification was just sending up.
total="$(grep -c "The following topics are running late: {\[test.12topic1, 3 days behind\]" ${BASENAME}_client_0_log.txt)"
if [ $total -ne 2 ]; then
	Bail "'The following topics are running late: {[test.12topic1, 3 days behind]' is not found as expected."
fi

Wait_archive_maker archive1 10
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --log_root ./log_root --synapse_at localhost:5672 --archive_root archive2 --subscribe_from "20160101T000000" --subscription test.* --timediff_tolerance 1 --check_timestamp_period 1 --use_current_time 1 --timediff_alert ${BASENAME}.bat < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_2_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
sleep 5
# manually check your inbox to ensure you've already got alarm email.

Wait_archive_maker archive2 10
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

dir_name="log_root"
temp="$(date +%s)" #Get utc time
today="$(date -u --date @${temp} +%Y%m%d)"
file_name="${dir_name}/${today}_log.txt"
Check_file $file_name
total="$(grep -c "return value is 0" ${file_name})"
if [ $total -lt 1 ]; then
	Bail "'return value is 0' is not found as expected."
fi

sleep 5
(trap 'Kill_Hack' SIGTERM ; set -o pipefail ; ${IVOCATION_DIR}/amqp_0_9_1/clients/Cpp/Archive_maker.exe --synapse_at localhost:5672 --archive_root archive2 --subscribe_from "20160101T000000" --subscription test.* --timediff_tolerance 1 --check_timestamp_period 1 --timediff_alert echo < ./archive_stdin_fifo 2>&1 | tee -a ${BASENAME}_client_3_log.txt & wait $!) &

ARCHIVE_MAKER_PID=${!}
exec 4> archive_stdin_fifo
sleep 5

Wait_archive_maker archive2 10
timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
wait ${ARCHIVE_MAKER_PID}

grep -e "Archive_maker is currently running less than 1 days behind" ${BASENAME}_client_3_log.txt
if [ "$?" -ne "0" ]; then
	Bail "'Archive_maker is currently running less than 1 days behind' is not found as expected."
fi

Stop_synapse

rm -fr ./archive1
rm -fr ./archive2
rm -fr ./db
rm -fr ./log_root
#rm -f  ./archive1.bin
rm -f  ./archive_stdin_fifo

cd $ODIR || Bail 
Clean --Retain_exes

set +x
echo  TESTED OK: ${0}
