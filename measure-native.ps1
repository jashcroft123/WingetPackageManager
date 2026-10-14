param([int]$Trials=10,[string]$Exe='dist\native\WingetSwift.exe')
# Run only on a machine where Application Control permits this build.
# This never changes antivirus or code-integrity settings.
$ErrorActionPreference='Stop'
if($Trials -lt 1 -or $Trials -gt 100){throw 'Trials must be between 1 and 100'}
$exePath=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot $Exe)).Path
$results=@()
for($trial=0;$trial -le $Trials;$trial++){
 $process=$null
 $watch=[Diagnostics.Stopwatch]::StartNew()
 try {
  $process=Start-Process -FilePath $exePath -WorkingDirectory $PSScriptRoot -PassThru -WindowStyle Hidden
  if(!$process.WaitForInputIdle(10000)){throw 'App did not become input-idle within 10 seconds'}
  $watch.Stop()
  if($process.HasExited){throw 'App exited before measurement'}
  $process.Refresh()
  if($trial -gt 0){$results+=[pscustomobject]@{Trial=$trial;ExeBytes=(Get-Item -LiteralPath $exePath).Length;LaunchToInputIdleMs=[Math]::Round($watch.Elapsed.TotalMilliseconds,2);PrivateBytes=$process.PrivateMemorySize64}}
 } finally {
  if($process -and !$process.HasExited){[void]$process.CloseMainWindow();if(!$process.WaitForExit(10000)){throw 'App did not close cleanly after its read-only check'}}
  if($process){$process.Dispose()}
 }
}
$results | Export-Csv (Join-Path $PSScriptRoot 'native-launch-results.csv') -NoTypeInformation
$sorted=@($results.LaunchToInputIdleMs | Sort-Object)
$middle=[int][Math]::Floor($sorted.Count/2)
$median=if($sorted.Count%2){$sorted[$middle]}else{($sorted[$middle-1]+$sorted[$middle])/2}
Write-Output "Native EXE: $((Get-Item -LiteralPath $exePath).Length) bytes; median launch-to-input-idle: $median ms"
