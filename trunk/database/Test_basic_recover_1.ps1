
# param(
# 	[int]$total = 3000,
# 	[int]$corrupt_at = 2500,
# 	[int]$topics = 5
# )

$total = 30000
$Sparsification_corrupt_size = 5000
$corrupt_at = 17000
$topics = 7

$My_dir = split-path -parent $MyInvocation.MyCommand.Definition
. $My_dir/../amqp_0_9_1/Test_common.ps1

$Basename = ([io.fileinfo]$MyInvocation.MyCommand.Definition).BaseName

function Get_message_size_on_disk([ref]$Message_size)
{
	Write-Host "Getting message size on disk"
	$Stream = New-Object IO.FileStream "$Synapse_database_directory\test.topic1\data", 'Open', 'Read', 'Read'
	$Reader = New-Object IO.BinaryReader $Stream
	[System.UInt32]$Aligner = 7
	$Message_size.Value = ($Reader.ReadUInt32() + 7) -band (-bnot $Aligner)
	$Reader.Dispose()
}

try {

	Clear_synapse_db

	Start_synapse "$My_dir/../amqp_0_9_1/server.exe"

	Start_client "c:/Python27/python.exe" "$My_dir/$Basename.py Pub $total $topics"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		$host.SetShouldExit(-1);
		Throw "FAILED: Error in publisher";
	}
	Move-Item "$Invocation_directory/client_log.txt" "$Invocation_directory/publishing_client_log.txt"

	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!"); 

	Wait_for_synapse_exit

# Get size of message on disk...
	$Message_size = 0
	Get_message_size_on_disk([ref]$Message_size)
	Write-Host "Message size on disk is: $Message_size" 

	echo "Writing incorrect data"

	For ($i = 1; $i -le $topics; $i++) {
		$fs = New-Object IO.FileStream "$Synapse_database_directory\test.topic$i\data", 'Open', 'ReadWrite', 'ReadWrite'
		$fs.Position = $Message_size * $corrupt_at;
		$bw = New-Object IO.BinaryReader $fs
		# $data = New-Object byte[] 1
		$data = $bw.ReadBytes(1)
		echo "about to bitwise not"
		$data[0] = 0xff -band ( -bnot [int]$data[0] );
		echo "done bitwise"
		$fs.Position = $Message_size * $corrupt_at;
		echo "about to new writer"
		$bw = New-Object IO.BinaryWriter $fs;
		echo "done on new writer"
		$bw.Write($data)
		$bw.Dispose()
	}
	$Synapse_database_recoverydir = "$Invocation_directory/Synapse_database_recover.tmp"
	Clear_synapse_db "$Synapse_database_recoverydir"

	#Start basic_recover.exe to restore corruppted db.
	$sw = [Diagnostics.Stopwatch]::StartNew()
	
	Start_client "$My_dir/basic_recover.exe" "--src $Synapse_database_directory --dst $Synapse_database_recoverydir"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		$host.SetShouldExit(-1);
		Throw "FAILED: Error in basic_recover";
	}
	Move-Item "$Invocation_directory/client_log.txt" "$Invocation_directory/basic_recover_log.txt"

	$sw.Stop()
	Write-Host "Total recovery time: " + $sw.Elapsed
	For ($i = 1; $i -le $topics; $i++) {
		$data = [io.file]::ReadAllBytes("$Synapse_database_recoverydir\test.topic$i\data_meta")
		$bytes = [byte[]]($data[16], $data[17], $data[18], $data[19])
		$records = [BitConverter]::ToInt32($bytes, 0)
		if ($records -ne $corrupt_at) {
			Throw "Recovery result is incorrect."
		}
	}
	#Start Synapse server from the db recovery result directory.
	Start_synapse "$My_dir/../amqp_0_9_1/server.exe" "--database_root $Synapse_database_recoverydir"
	Start-Sleep -s 3
	#Start python subscription client to retrieve messages.
	Start_client "c:/Python27/python.exe" "$My_dir/$Basename.py Sub $corrupt_at $topics"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		$host.SetShouldExit(-1);
		Throw "FAILED: Error in subscribing client.";
	}
	Move-Item "$Invocation_directory/client_log.txt" "$Invocation_directory/subscribing_client_log.txt"
	
	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!");
	
	Wait_for_synapse_exit

	echo ">>>>>>>>Testing sparsification corruption in-place recovery"
	For ($i = 1; $i -le $topics; $i++) {
		$fs = New-Object IO.FileStream "$Synapse_database_recoverydir\test.topic$i\data", 'Open', 'ReadWrite', 'ReadWrite'
		$fs.Position = 0;
		$bw = New-Object IO.BinaryReader $fs
		$Array_size = $Message_size * $Sparsification_corrupt_size
		# $data = New-Object byte[] $Array_size
		$data = $bw.ReadBytes($Array_size)
		for ($j = 0; $j -ne $Array_size; $j++) {
			#$data[$j] = 0;
			$data[$j] = 0xff -band ( -bnot [int]$data[$j] );
		}
		$fs.Position = 0;
		$bw = New-Object IO.BinaryWriter $fs;
		$bw.Write($data)
		$bw.Dispose()
	}
	echo ">>>>>>>>starting in-place basic_recovery"
	Start_client "$My_dir/basic_recover.exe" "--concurrency_hint 3 --src $Synapse_database_recoverydir --dst $Synapse_database_recoverydir"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		$host.SetShouldExit(-1);
		Throw "FAILED: Error in basic_recover";
	}
	Move-Item "$Invocation_directory/client_log.txt" "$Invocation_directory/in_place_after_sparsification_basic_recovery_log.txt"
	echo ">>>>>>>>starting server"
	Start_synapse "$My_dir/../amqp_0_9_1/server.exe" "--database_root $Synapse_database_recoverydir"
	Start-Sleep -s 3
	Start_client "c:/Python27/python.exe" "$My_dir/$Basename.py Sub $corrupt_at $topics $Sparsification_corrupt_size"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		$host.SetShouldExit(-1);
		Throw "FAILED: Error in subscribe";
	}
	Move-Item "$Invocation_directory/client_log.txt" "$Invocation_directory/in_place_after_sparsification_subscribing_client_log.txt"
	$Synapse.StandardInput.WriteLine("!"); 
	Wait_for_synapse_exit

	echo ">>>>>>>>Testing in-place recovery with 'near eof' feature"
	$Copy_database = "$Invocation_directory/Near_eof_playground_database"
	Copy-Item "$Synapse_database_recoverydir" "$Copy_database" -recurse
# corrupt meta_data file for topic 1
	$fs = New-Object IO.FileStream "$Synapse_database_recoverydir\test.topic1\data_meta", 'Open', 'ReadWrite', 'ReadWrite'
	$fs.Position = 12
	$bw = New-Object IO.BinaryReader $fs
	$data = $bw.ReadBytes(1)
	$data[0] = 0xff -band ( -bnot [int]$data[0] );
	$fs.Position = 12;
	$bw = New-Object IO.BinaryWriter $fs
	$bw.Write($data)
	$bw.Dispose()
# corrupt index meta_data file for topic 2
	$fs = New-Object IO.FileStream "$Synapse_database_recoverydir\test.topic2\index_2_data_meta", 'Open', 'ReadWrite', 'ReadWrite'
	$fs.Position = 12
	$bw = New-Object IO.BinaryReader $fs
	$data = $bw.ReadBytes(1)
	$data[0] = 0xff -band ( -bnot [int]$data[0] );
	$fs.Position = 12
	$bw = New-Object IO.BinaryWriter $fs
	$bw.Write($data)
	$bw.Dispose()
# delete meta_data file for topic 3
	Remove-Item "$Synapse_database_recoverydir\test.topic3\data_meta"
# delete index meta_data file for topic 4
	Remove-Item "$Synapse_database_recoverydir\test.topic4\index_0_data_meta"
	Start_client "$My_dir/basic_recover.exe" "--concurrency_hint 9 --src $Synapse_database_recoverydir --dst $Synapse_database_recoverydir --recover_from_eof 2s"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		$host.SetShouldExit(-1);
		Throw "FAILED: Error in basic_recover";
	}
	Move-Item "$Invocation_directory/client_log.txt" "$Invocation_directory/in_place_and_near_eof_after_sparsification_subscribing_client_log.txt"
	$Database_a = Get-ChildItem -Recurse -path $Synapse_database_recoverydir
	$Database_b = Get-ChildItem -Recurse -path "$Copy_database"
	$Comparison_result = Compare-Object -ReferenceObject $Database_a -DifferenceObject $Database_b -passThru
	if ($Comparison_result) {
		Throw "FAILED: something differs!";
	}

} catch [Exception] {
	echo $_.Exception|format-list -force
	Exit_with_error
}
