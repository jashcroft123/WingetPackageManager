param([int]$Trials = 10,[switch]$IncludeCompact,[string]$OutputPrefix = 'benchmark')
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PerfNative {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr CreateEvent(IntPtr a,bool manual,bool initial,string name);
 [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc,IntPtr param);
 public delegate bool EnumProc(IntPtr h,IntPtr param);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 public static void CloseWindow(int processId) { EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==processId)PostMessage(h,0x10,IntPtr.Zero,IntPtr.Zero);return true;},IntPtr.Zero); }
}
'@
$root = (Get-Location).Path
$apps = @(
 @{ Name='Classic'; Path=(Join-Path $root 'dist\WingetSwiftClassic.exe') },
 @{ Name='Slint'; Path=(Join-Path $root 'dist\modern\WingetSwiftModern.exe') }
)
if($IncludeCompact){$apps += @{Name='Compact';Path=(Join-Path $root 'dist\diagnostics\WingetSwift.exe')}}
$rows = [Collections.Generic.List[object]]::new()
function Measure-App($app, $trial) {
 $start = [Diagnostics.ProcessStartInfo]::new($app.Path, '--perf-hold')
 $start.UseShellExecute=$false; $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden; $start.WorkingDirectory=$root
 $watch=[Diagnostics.Stopwatch]::StartNew();$process=[Diagnostics.Process]::Start($start)
 $ready=[PerfNative]::CreateEvent([IntPtr]::Zero,$true,$false,"Local\WingetSwiftReady$($process.Id)")
 try {
  if([PerfNative]::WaitForSingleObject($ready,15000) -ne 0){throw "$($app.Name) did not signal rendered readiness"}
  $launchMs=$watch.Elapsed.TotalMilliseconds
  $state=Get-Content (Join-Path $root "perf-state-$($process.Id).txt")
  $windowHandle=[IntPtr]::new([long]$state[0])
  Start-Sleep -Milliseconds 750
  $process.Refresh();$cpuBefore=$process.TotalProcessorTime.TotalMilliseconds;$idle=[Diagnostics.Stopwatch]::StartNew()
  Start-Sleep -Milliseconds 2000
  $process.Refresh();$cpuMs=$process.TotalProcessorTime.TotalMilliseconds-$cpuBefore;$idleMs=$idle.Elapsed.TotalMilliseconds
  $result=[pscustomobject]@{App=$app.Name;Trial=$trial;LaunchMs=[Math]::Round($launchMs,3);PrivateMiB=[Math]::Round($process.PrivateMemorySize64/1MB,3);WorkingSetMiB=[Math]::Round($process.WorkingSet64/1MB,3);IdleCpuMs=[Math]::Round($cpuMs,3);IdleSingleCorePercent=[Math]::Round(100*$cpuMs/$idleMs,4);ExeBytes=(Get-Item $app.Path).Length}
  [void][PerfNative]::PostMessage($windowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
  if(!$process.WaitForExit(5000)){throw "$($app.Name) did not close normally"}
  return $result
 } finally { [void][PerfNative]::CloseHandle($ready);if(!$process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose() }
}
# One unrecorded warm-up each; measured runs alternate order to reduce order bias.
foreach($app in $apps){$null=Measure-App $app 0}
for($trial=1;$trial -le $Trials;$trial++) {
 $order=$apps.Clone();if(!($trial%2)){[Array]::Reverse($order)}
 foreach($app in $order){$row=Measure-App $app $trial;$rows.Add($row);Write-Output "$($row.App) $trial : $($row.LaunchMs) ms; $($row.PrivateMiB) MiB private"}
}
$rows | Export-Csv "$OutputPrefix-results.csv" -NoTypeInformation
function Median($values){$sorted=@($values|Sort-Object);$n=$sorted.Count;if($n%2){return $sorted[[int][Math]::Floor($n/2)]};return ($sorted[$n/2-1]+$sorted[$n/2])/2}
$summary=foreach($app in $apps){$group=@($rows|Where-Object App -eq $app.Name);[pscustomobject]@{App=$app.Name;Trials=$Trials;LaunchMedianMs=(Median $group.LaunchMs);LaunchMinMs=($group.LaunchMs|Measure-Object -Minimum).Minimum;LaunchMaxMs=($group.LaunchMs|Measure-Object -Maximum).Maximum;PrivateMedianMiB=(Median $group.PrivateMiB);WorkingSetMedianMiB=(Median $group.WorkingSetMiB);IdleCpuMedianPercent=(Median $group.IdleSingleCorePercent);IdleCpuMeanPercent=($group.IdleSingleCorePercent|Measure-Object -Average).Average;IdleCpuMaxPercent=($group.IdleSingleCorePercent|Measure-Object -Maximum).Maximum;ExeBytes=$group[0].ExeBytes}}
[pscustomobject]@{MeasuredAt=(Get-Date -Format o);OS=[Environment]::OSVersion.VersionString;CPU=(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name);LogicalProcessors=[Environment]::ProcessorCount;Method='Warm filesystem cache; hidden 1100x760 logical client windows at system DPI; same 98-row installed-package fixture; process start to native window creation, fixture parsing/model population and forced offscreen render; 750ms settling then 2s idle CPU sample; no WinGet/network during measurement; alternating order; one excluded warmup each';Results=$summary} | ConvertTo-Json -Depth 6 | Set-Content "$OutputPrefix-summary.json" -Encoding utf8
$summary | Format-Table -AutoSize
