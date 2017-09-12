# Set-PSDebug -Trace 1

function Exit_with_error {
	Try {
		Write-Error ">>>>>>>ERROR: $_"
		if ($global:Synapse -and !$global:Synapse.HasExited) 
		{
			$global:Synapse.Kill()
		}
		if ($global:Client -and !$global:Client.HasExited) 
		{
			$global:Client.Kill()
		}
	} Catch {
	}
	Exit 1
}

Trap
{
	Exit_with_error
}

$Invocation_directory = $(get-location).Path

$Synapse_database_directory = "$Invocation_directory/Synapse_database.tmp"

$Action = { 
	if(-not [string]::IsNullOrEmpty($EventArgs.data)) {
		"$($EventArgs.data)" | Out-File -FilePath "$($event.MessageData)" -Encoding ASCII -Append
	}
}

$global:Synapse
$global:Client

function Start_synapse($Synapse_path, $Extra_arguments = "")
{
	echo "Starting synapse $Synapse_path $(Get-Date)"
	$Synapse_start_info = New-Object System.Diagnostics.ProcessStartInfo;
	$Synapse_start_info.FileName = "$Synapse_path"; 
	$Synapse_start_info.Arguments = "--listen_on localhost:5672 --database_root $Synapse_database_directory --reserve_memory_size 3G $Extra_arguments"; 
	$Synapse_start_info.UseShellExecute = $false; 
	$Synapse_start_info.RedirectStandardInput = $true; 
	$Synapse_start_info.RedirectStandardError = $true; 

	#$global:Synapse = [System.Diagnostics.Process]::Start($Synapse_start_info);
	$global:Synapse = New-Object System.Diagnostics.Process;
	$global:Synapse.StartInfo = $Synapse_start_info;
	Register-ObjectEvent -InputObject $global:Synapse -EventName ErrorDataReceived -SourceIdentifier Synapse_cerr -Action $Action -MessageData "$Invocation_directory/synapse_log.txt" | Out-Null
	Register-ObjectEvent -InputObject $global:Synapse -EventName Exited -SourceIdentifier Synapse_exit | Out-Null
	$global:Synapse.Start() | Out-Null
	$global:Synapse.BeginErrorReadLine();
	for ($i = 0; $i -ne 10; $i++) {
		echo "Attempt ($i out of 10) to validate starting Synapse on $(Get-Date)"
		Start-Sleep -m 500
		try {
			$Socket = New-Object System.Net.Sockets.TcpClient("localhost", 5672);
			if ($Socket.Connected) {
				$Socket.Close();
				echo "Attempt to validate starting Synapse on $(Get-Date) OK"
				return;
			}
		} catch {
		}
	}
	echo "Attempt to validate starting Synapse on $(Get-Date) failed..."
}

function Wait_for_synapse_exit
{
	echo "Waiting for synapse $(Get-Date)"
	Wait-Event -SourceIdentifier Synapse_exit -Timeout 50 | Out-Null
	$global:Synapse.WaitForExit(35000)
	if (!$global:Synapse.HasExited -or $global:Synapse.ExitCode -ne 0) {
		Throw "Error in Wait_for_synapse_exit";
	}
	Remove-Event -SourceIdentifier Synapse_exit
	unregister-event -SourceIdentifier Synapse_cerr
	unregister-event -SourceIdentifier Synapse_exit
}

function Async_sleep_on_active_synapse($Timeout)
{
	Wait-Event -SourceIdentifier Synapse_exit -Timeout $Timeout 
	if ($global:Synapse.HasExited) {
		Throw "Error in synapse, expecting active synapse.";
	}
}

function Start_client($Client_path, $Client_args, $Working_directory) 
{
	echo "Starting client $Client_path $(Get-Date)"
	$Client_start_info = New-Object System.Diagnostics.ProcessStartInfo;
	$Client_start_info.FileName = "$Client_path"; 
	$Client_start_info.Arguments = "$Client_args"; 
	$Client_start_info.UseShellExecute = $false; 
	$Client_start_info.RedirectStandardOutput = $true; 
	$Client_start_info.RedirectStandardError = $true; 

	if ($Working_directory) {
		$Client_start_info.WorkingDirectory = "$Working_directory"; 
		# $Php_start_info.EnvironmentVariables.Add("THRIFT_PHP_LIB_PATH", "c:/msys/home/leonz/work/tdc/federated_serialisation/trunk/thrift/dist/lib/php/lib"); 
	}

	$global:Client = New-Object System.Diagnostics.Process;
	$global:Client.StartInfo = $Client_start_info;
	Register-ObjectEvent -InputObject $global:Client -EventName OutputDataReceived -SourceIdentifier Client_cout -Action $Action -MessageData "$Invocation_directory/client_log.txt" | Out-Null
	Register-ObjectEvent -InputObject $global:Client -EventName ErrorDataReceived -SourceIdentifier Client_cerr -Action $Action -MessageData "$Invocation_directory/client_log.txt" | Out-Null
	Register-ObjectEvent -InputObject $global:Client -EventName Exited -SourceIdentifier Client_exit | Out-Null
	$global:Client.Start() | Out-Null
	$global:Client.BeginOutputReadLine();
	$global:Client.BeginErrorReadLine();
}

function Wait_for_client_exit 
{
	echo "Waiting for client $(Get-Date)"
	Wait-Event -SourceIdentifier Client_exit | Out-Null
	$global:Client.WaitForExit();
	Remove-Event -SourceIdentifier Client_exit
	unregister-event -SourceIdentifier Client_cout
	unregister-event -SourceIdentifier Client_cerr
	unregister-event -SourceIdentifier Client_exit
}

function Clear_synapse_db($Database_directory = "")
{
	if ([string]::IsNullOrEmpty($Database_directory)) {
		$Database_directory = $Synapse_database_directory;
	}
	# Remove-Item -ErrorAction silentlycontinue -Recurse -Force $Database_directory
  echo "Clearing synapse database $Database_directory on $(Get-Date)"
	if (Test-Path $Database_directory) {
		for ($i = 0; $i -ne 10; $i++) {
			try {
				Remove-Item -ErrorAction Stop -Recurse -Force $Database_directory
				return
			} catch {
				echo "Clearing synapse database $Database_directory attempt $i failed on $(Get-Date)"
				Start-Sleep -s 1
			}
		}
		Throw "Clearing synapse database $Database_directory failed after all attempts on $(Get-Date)"
	} else {
		echo "Clearing synapse database $Database_directory OK because does not exist..."
	}
}
