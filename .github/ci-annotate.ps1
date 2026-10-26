# Writes the interesting lines of a log file as one workflow error annotation (readable through the
# check-run annotations API without downloading the job log). Lines are de-duplicated and shortened.
param([string]$Path)
$lines = Get-Content $Path
$hits = $lines |
  Where-Object { $_ -match '(error|warning) [A-Z]+[0-9]+|CMake Error|fatal|FAILED|Could NOT|Failed|CHECK|REQUIRE|FAIL|\.cpp:[0-9]+:|\*\*\*' -and $_ -notmatch 'C2220' } |
  ForEach-Object { ($_ -replace '\s*\[[^\]]*\]\s*$', '') -replace 'D:\\a\\Mingol\\Mingol\\', '' } |
  Select-Object -Unique | Select-Object -First 45
$tail = $lines | Select-Object -Last 8
$text = (@($hits) + @('--- tail ---') + @($tail)) -join "`n"
$text = $text -replace '%', '%25' -replace "`r", '' -replace "`n", '%0A'
if ($text.Length -gt 30000) { $text = $text.Substring(0, 30000) }
Write-Output "::error title=$Path::$text"
