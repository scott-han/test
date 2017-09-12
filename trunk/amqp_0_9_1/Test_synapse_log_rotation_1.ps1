$My_dir = split-path -parent $MyInvocation.MyCommand.Definition
. $My_dir/Test_common.ps1

$Basename = ([io.fileinfo]$MyInvocation.MyCommand.Definition).BaseName

try {

	Clear_synapse_db

	echo "Log production stage.";

	Start_synapse "$Invocation_directory/amqp_0_9_1/server.exe" "--log_root $Invocation_directory/amqp_0_9_1/Synapse_log_output"

	Start_client "c:/Python27/python.exe" "$My_dir/clients/python/$Basename.py"
	Start-Sleep -s 5 

	echo "Waiting for client python $(Get-Date)"
	Wait_for_client_exit

	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!"); 
	Wait_for_synapse_exit 

	Clear_synapse_db

	echo "Script done running.";

} catch {
	Exit_with_error
}
