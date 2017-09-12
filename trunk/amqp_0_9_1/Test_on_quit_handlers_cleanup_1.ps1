$My_dir = split-path -parent $MyInvocation.MyCommand.Definition
. $My_dir/Test_common.ps1

try {

	$Basename = ([io.fileinfo]$MyInvocation.MyCommand.Definition).BaseName

	Clear_synapse_db

	Start_synapse "$My_dir/server.exe"

	Start_client "c:/Python27/python.exe" "$My_dir/clients/python/$Basename.py"
	Wait_for_client_exit
	if ($Client.ExitCode -ne 0) {
		Throw "FAILED: Error in publisher";
	}

	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!"); 

	Wait_for_synapse_exit

	Clear_synapse_db

} catch {
	Exit_with_error
}
