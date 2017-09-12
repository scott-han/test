@ECHO OFF
powershell -NoProfile -ExecutionPolicy Bypass -Command " Get-EventLog -List | Format-Table -Property Log -HideTableHeaders | Out-String -Stream | Where-Object { $_ -ne '' } | ForEach { echo 'Logfile: '$_ ; Get-EventLog -LogName $_.trim() -Source Synapse -ErrorAction SilentlyContinue | Format-Table -Auto -Wrap TimeGenerated,Source,Message } "
PAUSE
