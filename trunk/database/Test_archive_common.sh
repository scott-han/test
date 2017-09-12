
#GET_LAST_MODIFIED_TIME
#The function is to get the last modified time of all files under a folder recursively.
#The function also roundup the file modification time to integer.
#GET_LAST_MODIFIED_TIME
Get_last_modified_time()
{
	last_time="$(find $1 -type f -printf '%T@ %p\n' | sort -n | tail -1 | cut -f1 -d" ")"
	if [ ! -z $last_time ]
	then
		last_time="$(date --date @${last_time} +%s)"
	fi
}

Get_folder_size()
{
	# as per https://blogs.msdn.microsoft.com/oldnewthing/20111226-00/?p=8813
	# merely opening and closing file handle may feed the file-size info into the directory entry... 
	# so not using  -sk but rather forcing du to walk through every file (just in case total one simply calles overall dir info)
	current_size="$(du -k $1 | tail -n 1 | cut -f1 -d$'\t')"
}

#WAIT_ARCHIVE_MAKER
#This function is to wait Archive_maker completing its task.
#I've attempted to check the last modified time of file synapse_log.txt, but that doesn't work under some circumstances.
#I've also tried to check last modified time of all files under archive folder, it doesn't work well.
#Therefore we will have to check the total size of all files in archive folder
#WAIT_ARCHIVE_MAKER
Wait_archive_maker()
{
	span=17
	if [ ! -z $2 ]; then
		span=$2
	fi
	Get_folder_size $1
	last_time="$(date +%s)"
	while true
	do 
		sleep 5
		last_size=$current_size
		Get_folder_size $1
		current_time="$(date +%s)"
		if [ "$current_size" = "$last_size" ] && [ $(($current_time-$last_time)) -ge $span ]
		then 
			break
		else
			if [ "$current_size" != "$last_size" ]; then
				last_size=$current_size
				last_time=$current_time
			fi
		fi
	done
}

#
# To check if a file exists
#
Check_file()
{
	if [ -f $1 ]; then
		echo "File $1 is found"
	else
		Bail "File $1 is not found as expected."
	fi
}

#
# To check if a folder exists
#
Check_directory()
{
	if [ -d $1 ]; then
		echo "Directory $1 is found"
	else
		Bail "Directory $1 is not found as expected."
	fi
}
#
# To wait archive maker completion all its tasks, and then shutdown it gracefully.
#
Wait_and_stop_archive_maker()
{
	Wait_archive_maker $1
	timeout 5 echo ! >&4 || Bail "Trying to write exit char (!) to Archive_maker"
	wait ${ARCHIVE_MAKER_PID}
}
#
# To create a pipe which will be used to communicate with Archive_maker.
#
Create_archive_fifo_info()
{
	if [ ! -p "archive_stdin_fifo" ]; then
		mkfifo archive_stdin_fifo
	fi
}
#
# To get decimal from the hex string that is found in our savepoint file.
# 
Get_as_decimal()
{
	AA=$1
	ret=0
	scale=1
	for (( i=0; i<8; i++)); do 
		pos="$(($i*2))"; 
		temp="$((16#${AA:$pos:2}))"; 
		temp="$(($temp*$scale))"; 
		ret="$(($ret+$temp))"; 
		scale="$(($scale*256))" #Times 256 as we process it every two digits.
	done
	DECIMAL=$ret
}
