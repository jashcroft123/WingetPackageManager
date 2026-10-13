param([int]$Trials=10)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ReleaseUi {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr p);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder text,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 public static IntPtr Root(int pid){IntPtr root=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var cls=new StringBuilder(64);GetClassName(h,cls,64);if(id==pid&&cls.ToString()=="WingetSwift"){root=h;return false;}return true;},IntPtr.Zero);return root;}
 public static string Text(IntPtr h){var text=new StringBuilder(512);GetWindowText(h,text,512);return text.ToString();}
 public static void Send(IntPtr h,uint m,int w){IntPtr result;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,5000,out result)==IntPtr.Zero)throw new Exception("UI message timed out");}
}
'@
$rows=@()
foreach($arch in @('x64','x86')){
 $relative=if($arch -eq 'x64'){'dist\compact-x64\WingetSwift.exe'}else{'dist\compact\WingetSwift.exe'}
 $exe=Join-Path $PSScriptRoot $relative
 for($trial=0;$trial -le $Trials;$trial++){
  $start=[Diagnostics.ProcessStartInfo]::new($exe)
  $start.WorkingDirectory=$PSScriptRoot;$start.UseShellExecute=$false;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  $root=[IntPtr]::Zero;$watch=[Diagnostics.Stopwatch]::StartNew();$process=[Diagnostics.Process]::Start($start)
  try{
   if(!$process.WaitForInputIdle(10000)){throw 'Window did not become responsive'}
   $root=[IntPtr]::Zero;for($i=0;$i -lt 250;$i++){$root=[ReleaseUi]::Root($process.Id);if($root -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 20};if($root -eq [IntPtr]::Zero){throw 'Root window missing'};$watch.Stop()
   if($trial -eq 0){
    [ReleaseUi]::Send($root,0x111,102);[ReleaseUi]::Send($root,0x111,105)
    $feedback='';for($i=0;$i -lt 100;$i++){$feedback=[ReleaseUi]::Text([ReleaseUi]::GetDlgItem($root,2));if($feedback.Contains('Checking for updates')){break};Start-Sleep -Milliseconds 20}
    if(!$feedback.Contains('Checking for updates')){throw "Update feedback missing: $feedback"}
    foreach($id in @(100,101,102,103,105)){if(![ReleaseUi]::IsWindowEnabled([ReleaseUi]::GetDlgItem($root,$id))){throw "Browsing control $id disabled"}}
    [ReleaseUi]::Send($root,0x111,100)
    if(![ReleaseUi]::Text([ReleaseUi]::GetDlgItem($root,10)).Contains('Discover')){throw 'Navigation failed while checking updates'}
    Write-Output "PASS $arch release: update feedback and navigation during background work"
   }else{
    $process.Refresh();$rows += [pscustomobject]@{Architecture=$arch;Trial=$trial;ExeBytes=(Get-Item $exe).Length;LaunchToInputIdleMs=[Math]::Round($watch.Elapsed.TotalMilliseconds,2);PrivateBytes=$process.PrivateMemorySize64}
   }
  }finally{
   if(!$process.HasExited){[void][ReleaseUi]::PostMessage($root,0x10,[IntPtr]::Zero,[IntPtr]::Zero);if(!$process.WaitForExit(10000)){$process.Kill();throw 'Read-only close failed'}}
   $process.Dispose()
  }
 }
}
$rows | Export-Csv (Join-Path $PSScriptRoot 'release-launch-results.csv') -NoTypeInformation
foreach($group in ($rows | Group-Object Architecture)){$sorted=@($group.Group.LaunchToInputIdleMs | Sort-Object);Write-Output "$($group.Name): $($group.Group[0].ExeBytes) bytes, median window-ready $([Math]::Round(($sorted[[int][Math]::Floor(($sorted.Count-1)/2)]+$sorted[[int][Math]::Floor($sorted.Count/2)])/2,2)) ms"}
