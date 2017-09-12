$My_dir = split-path -parent $MyInvocation.MyCommand.Definition
. $My_dir/Test_common.ps1

$Basename = ([io.fileinfo]$MyInvocation.MyCommand.Definition).BaseName

try {

	Clear_synapse_db

	Start_synapse "$My_dir/server.exe"


	Start_client "php.exe" "$Basename.php" "$My_dir/clients/php"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		Throw "FAILED: Error in publisher";
	}

	Async_sleep_on_active_synapse 35

	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!"); 

	Wait_for_synapse_exit

	Clear_synapse_db

} catch {
	Exit_with_error
}
