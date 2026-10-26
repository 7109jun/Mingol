# Writes the interesting lines of a log file as one workflow error annotation (readable through the
# check-run annotations API without downloading the job log).
param([string]$Path)
$lines = Get-Content $Path
$hits = $lines | Where-Object { $_ -match 'error|Error|fatal|FAILED|Could NOT|not found|Failed' } | Select-Object -First 40
$tail = $lines | Select-Object -Last 15
$text = (@($hits) + @('--- tail ---') + @($tail)) -join "`n"
$text = $text -replace '%', '%25' -replace "`r", '' -replace "`n", '%0A'
if ($text.Length -gt 30000) { $text = $text.Substring(0, 30000) }
Write-Output "::error title=$Path::$text"
