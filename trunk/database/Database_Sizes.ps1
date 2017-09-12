param (
	[Parameter(Mandatory=$true)]
	[string]$Database_Root
)

"sep=,"

$Database_Root_Info = (Get-ChildItem $Database_Root -recurse | Where-Object {$_.PSIsContainer -eq $True} | Sort-Object)
foreach ($I in $Database_Root_Info) {
	$Topic_Info = (Get-ChildItem $I.FullName | Measure-Object -property length -sum)
	$Total_Size += $Topic_Info.sum
	"""" + $I.FullName + """ , " + "{0}" -f ($Topic_Info.sum / 1MB)
}

"""Cumulative"" , " + "{0}" -f ($Total_Size / 1MB)

