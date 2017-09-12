$My_dir = split-path -parent $MyInvocation.MyCommand.Definition
. $My_dir/Test_common.ps1

$Basename = ([io.fileinfo]$MyInvocation.MyCommand.Definition).BaseName

try {

	Clear_synapse_db

	echo "Subscribing only phase running.";

	Start_synapse "$My_dir/server.exe"

	Start_client "c:/Python27/python.exe" "$My_dir/clients/python/$Basename.py --subscribe_only true"
	Start-Sleep -s 5 

	echo "Stopping python $(Get-Date)"
	$Client.Kill();

	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!"); 

	Wait_for_synapse_exit 
	Wait_for_client_exit

	echo "Sub/Pub phase running.";

	Start_synapse "$My_dir/server.exe"
	Start-Sleep -s 2 

	Start_client "c:/Python27/python.exe" "$My_dir/clients/python/$Basename.py --subscribe_only false"

	Async_sleep_on_active_synapse 10

	echo "Stopping synapse $(Get-Date)"
	$Synapse.StandardInput.WriteLine("!"); 

	Wait_for_synapse_exit 
	Wait_for_client_exit

	Clear_synapse_db

	echo "Script done running.";

} catch {
	Exit_with_error
}
