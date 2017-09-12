#! /bin/sh -x

. `dirname ${0}`/../amqp_0_9_1/Test_common.sh
. `dirname ${0}`/Test_archive_common.sh

# note, currently ought to be an msys compatible path (as is being called potentially from automate msys chain of commands)
ODIR=${TESTING_DIRECTORY}${BASENAME}

if [ -z "${EXPLICIT_PREREQUISITES}" ]; then
# normally would put it in the EXPLICIT_PREREQUISITES line (so that gets made specifically for this test),
# but we currently can reuse the binaries from other potential targets because the build is generic enough.
make amqp_0_9_1/server.exe || Bail
make database/basic_recover.exe || Bail
EXPLICIT_PREREQUISITES=./database/basic_recover.exe\ ./amqp_0_9_1/server.exe\ ${MYDIR}/${BASENAME}.py OUTPUT_DIR=$ODIR/ make ${ODIR}/database/`basename ${0} .sh`.okrun || Bail
exit
fi

if [ -d "$ODIR" ]; then
	echo Warining: $ODIR already exists
fi

cd $ODIR || Bail 

# First test, nothing special
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 2>&1 | tee ${BASENAME}_truncate_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Sleep 60
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 2>&1 | tee ${BASENAME}_sub_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
# Stop synapse server
Stop_synapse
Sleep 1
rm -fr ./db
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING index" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING index) in Synapse's log."
fi
cp synapse_log.txt synapse_1_log.txt

# Second test, try to truncate topic before the first message
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_2_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 137000000 2>&1 | tee ${BASENAME}_truncate_2_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
cp synapse_log.txt synapse_2_log.txt

# 3rd test, shutdown server and restart server, to ensure there is no any problem 
Stop_synapse
Sleep 1
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -eq "0" ]; then
	Bail "Grep should not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -eq "0" ]; then
	Bail "Grep should not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
cp synapse_log.txt synapse_3_log.txt

Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db

# Ensure no message is truncated.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 1470000000 2>&1 | tee ${BASENAME}_sub_3_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
# Stop synapse server
Stop_synapse
Sleep 1
rm -fr ./db
cp synapse_log.txt synapse_3_1_log.txt

# 4th test, try to truncate topic after the last message
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_4_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1570000000 2>&1 | tee ${BASENAME}_truncate_4_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Sleep 60
# Ensure the last message is not truncated.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 1470239999 2>&1 | tee ${BASENAME}_sub_4_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
# Stop synapse server
Stop_synapse
Sleep 1
rm -fr ./db
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -eq "0" ]; then
	Bail "Grep should not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -eq "0" ]; then
	Bail "Grep should not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
cp synapse_log.txt synapse_4_log.txt

# 5th test, try to truncate topic multiple times
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_5_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470120000 2>&1 | tee ${BASENAME}_truncate_5_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470130000 2>&1 | tee ${BASENAME}_truncate_5_2_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470140000 2>&1 | tee ${BASENAME}_truncate_5_3_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470150000 2>&1 | tee ${BASENAME}_truncate_5_4_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Sleep 60
# Ensure the last message is not truncated.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 1470150000 2>&1 | tee ${BASENAME}_sub_5_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
# Stop synapse server
Stop_synapse
Sleep 1
rm -fr ./db
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING index" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING index) in Synapse's log."
fi
cp synapse_log.txt synapse_5_log.txt

# 6th test, try to truncate topic multiple times in reverse order
# Start Snapse server with the default database root ./db and publish some data
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_6_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470150000 2>&1 | tee ${BASENAME}_truncate_6_1_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470140000 2>&1 | tee ${BASENAME}_truncate_6_2_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470130000 2>&1 | tee ${BASENAME}_truncate_6_3_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 1470120000 2>&1 | tee ${BASENAME}_truncate_6_4_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Sleep 60
# Ensure the last message is not truncated.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 1470150000 2>&1 | tee ${BASENAME}_sub_6_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
# Stop synapse server
Stop_synapse
Sleep 1
rm -fr ./db
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING index" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING index) in Synapse's log."
fi
cp synapse_log.txt synapse_6_log.txt

# 7th test, send topic truncation before topic is created, the server should not crash.
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 2>&1 | tee ${BASENAME}_truncate_7_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_7_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

Stop_synapse
sleep 1

grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
cp synapse_log.txt synapse_7_log.txt
rm -fr ./db

# 8th test, shutdown server before the topic truncation is completed
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_8_log.txt
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 2>&1 | tee ${BASENAME}_truncate_8_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Stop_synapse
sleep 1
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING index" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING index) in Synapse's log."
fi
# Restart server to ensure everything is ok
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 2>&1 | tee ${BASENAME}_sub_8_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Stop_synapse
sleep 1
rm -fr ./db

# 9th test, basic_recovery on copied sparsify file
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db
sleep 1
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Pub 2>&1 | tee ${BASENAME}_pub_9_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi

/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Trun 2>&1 | tee ${BASENAME}_truncate_9_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Stop_synapse
sleep 1
grep -i "SPARSIFYING file!!! from" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (SPARSIFYING file!!! from) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING file!!!" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING file!!!) in Synapse's log."
fi
grep -i "DOEN SPARSIFYING index" synapse_log.txt
if [ "$?" -ne "0" ]; then
	Bail "Grep could not find expected text (DOEN SPARSIFYING index) in Synapse's log."
fi

cp -r ./db ./db_copy
${IVOCATION_DIR}/database/basic_recover.exe --src db_copy --dst db_copy --sparsify_only 2>&1 | tee -a ${BASENAME}_recover_9_log.txt
grep -i "oops" ${BASENAME}_recover_9_log.txt
if [ "$?" -eq "0" ]; then
	Bail "basic_recover failed to restore truncated but not set the file as sparsified file."
fi
Start_synapse ${IVOCATION_DIR}/amqp_0_9_1/server.exe --database_root db_copy
# Ensure the last message is not truncated.
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py Sub 2>&1 | tee ${BASENAME}_sub_9_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for subscription client to exit with error..."
fi
/c/Python27/python.exe ${MYDIR}/${BASENAME}.py SubAll 2>&1 | tee ${BASENAME}_suball_9_log.txt 
if [ "$?" -ne "0" ]
then
	Bail "Waiting for publish client to exit with error..."
fi
Stop_synapse
sleep 1
rm -fr ./db
rm -fr ./db_copy


cd $ODIR || Bail 
Clean

set +x
echo  TESTED OK: ${0}
