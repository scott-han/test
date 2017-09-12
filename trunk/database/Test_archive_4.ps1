 param(
	#$path is the root folder of the logs. The logs are like $path/YYYYMMDD_log.txt
 	[string]$path = "",
	[string] $content = "Alarm from Archive_maker ..."
 )
#To enable debug, please uncomment the following line.
#Set-PSDebug -Trace 1

#get last 50 logs from log file.
$date = Get-Date
$date = $date.ToUniversalTime()
$today = $date.ToString("yyyyMMdd")
$file = $today + "_log.txt"
$Tail = Get-Content $path/$file -Tail 50 | out-string

#Out-File -FilePath $path/alert.txt -InputObject $Tail

#if mail server requires authentication, please uncomment below code and fill with correct username and password.
#Then add -Credential $credential at the end of email sending cmdlet.

#$username = "[User name here]"
#$password = ConvertTo-SecureString "[Password here]" -AsPlainText -Force
#$credential = New-Object System.Management.Automation.PSCredential($username, $password)

#if the mail needs to be sent to multiple recipients, please create an email group or call this CmdLmt in the following way
#Send-MailMessage -To "to@dataprocessors.com.au", "another@dataprocessors.com.au" -From "from@dataprocessors.com.au" -Subject "[ALERT]Alert from Archive maker" -Body $Tail -Credential $credential -SmtpServer "smtp.dataprocessors.com.au" -priority High
Send-MailMessage -To "roger.luo@dataprocessors.com.au" -From "roger.luo@dataprocessors.com.au" -Subject "[ALERT]Alert from Archive maker" -Body ($content + "`r`n`r`nLast 50 lines of log are shown below: `r`n" + $Tail) -SmtpServer "smtp.dataprocessors.com.au" -priority High
