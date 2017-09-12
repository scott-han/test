$My_dir = split-path -parent $MyInvocation.MyCommand.Definition
. $My_dir/Test_common.ps1

$Basename = ([io.fileinfo]$MyInvocation.MyCommand.Definition).BaseName

try {

	for ($i = 0; $i -ne 100; $i++) {
		echo "Loop iteration $i $(Get-Date)"

		Clear_synapse_db

		Start_synapse "$My_dir/server.exe"

		Start_client "c:/Python27/python.exe" "$My_dir/clients/python/$Basename.py"
		Start-Sleep -s 10 

		echo "Stopping python $(Get-Date)"
		$Client.Kill();

		echo "Stopping synapse $(Get-Date)"
		$Synapse.StandardInput.WriteLine("!"); 

		Wait_for_synapse_exit
		Wait_for_client_exit

	}

	Clear_synapse_db

	echo "Script done running.";

} catch {
	Exit_with_error
}
