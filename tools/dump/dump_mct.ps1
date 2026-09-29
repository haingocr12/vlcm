<#
.SYNOPSIS
  Phần A của hướng dẫn: tìm MctHost.exe (con của AutoVLCMO.exe), chờ nó tự giải nén,
  dump bằng pe-sieve32 (hoặc hollows_hunter32), rồi kiểm tra bản dump bằng check_dump.py.

.EXAMPLE
  # cmd/PowerShell Administrator, pe-sieve32.exe nằm trong C:\tools
  .\dump_mct.ps1 -ToolsDir C:\tools
  .\dump_mct.ps1 -ToolsDir C:\tools -TargetPid 4321 -WaitSec 0
  .\dump_mct.ps1 -ToolsDir C:\tools -Instance client_1
  .\dump_mct.ps1 -ToolsDir C:\tools -HollowsHunter      # dump mọi MctHost cùng lúc
#>
param(
    [string]$ToolsDir = ".",          # thư mục chứa pe-sieve32.exe / hollows_hunter32.exe
    [int]$TargetPid = 0,                    # PID MctHost; 0 = tự tìm
    [string]$Instance = "",           # lọc theo "-instance:client_N" trong dòng lệnh
    [int]$WaitSec = 150,              # chờ cho MctHost giải nén + chạy ổn định (A1: 2-3 phút)
    [string]$OutDir = "dump_mct",
    [string]$Original = "",           # (tuỳ chọn) đường dẫn MctHost.exe gốc để so entropy
    [switch]$HollowsHunter
)
$ErrorActionPreference = "Stop"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Write-Warning "Nên chạy bằng Administrator, nếu không pe-sieve có thể không đọc được bộ nhớ." }

function Find-Tool($name) {
    $p = Join-Path $ToolsDir $name
    if (Test-Path $p) { return (Resolve-Path $p).Path }
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    throw "Không tìm thấy $name (đặt -ToolsDir hoặc thêm vào PATH)."
}

if ($HollowsHunter) {
    $hh = Find-Tool "hollows_hunter32.exe"
    if ($WaitSec -gt 0) { Write-Host "[*] Chờ $WaitSec giây cho MctHost giải nén..."; Start-Sleep -Seconds $WaitSec }
    & $hh /pname MctHost.exe /shellc /data 3 /dir $OutDir
    $dumpRoot = $OutDir
} else {
    if ($TargetPid -eq 0) {
        $auto = Get-CimInstance Win32_Process -Filter "Name='AutoVLCMO.exe'"
        $mct  = Get-CimInstance Win32_Process -Filter "Name='MctHost.exe'"
        if ($auto) { $mct = $mct | Where-Object { $auto.ProcessId -contains $_.ParentProcessId } }
        if ($Instance) { $mct = $mct | Where-Object { $_.CommandLine -like "*$Instance*" } }
        $mct = @($mct)
        if ($mct.Count -eq 0) { throw "Không thấy MctHost.exe nào (auto đã chạy acc chưa?)." }
        Write-Host "[*] Các MctHost tìm thấy:"
        $mct | ForEach-Object { Write-Host ("    PID {0,-6} cha {1,-6} {2}" -f $_.ProcessId, $_.ParentProcessId, $_.CommandLine) }
        if ($mct.Count -gt 1) { Write-Warning "Có nhiều MctHost, lấy cái đầu. Dùng -TargetPid hoặc -Instance để chọn." }
        $TargetPid = $mct[0].ProcessId
    }
    $proc = Get-Process -Id $TargetPid
    Write-Host "[*] Mục tiêu: PID $TargetPid, chạy từ $($proc.StartTime)"
    $age = ((Get-Date) - $proc.StartTime).TotalSeconds
    $left = [int]($WaitSec - $age)
    if ($left -gt 0) { Write-Host "[*] Chờ thêm $left giây cho MctHost giải nén..."; Start-Sleep -Seconds $left }

    $ps = Find-Tool "pe-sieve32.exe"
    Write-Host "[*] $ps /pid $TargetPid /dmode 3 /shellc /data 3 /dir $OutDir"
    & $ps /pid $TargetPid /dmode 3 /shellc /data 3 /dir $OutDir
    $dumpRoot = Join-Path $OutDir "process_$TargetPid"
}

if (-not (Test-Path $dumpRoot)) { throw "Không có thư mục dump $dumpRoot" }
Write-Host "[+] Dump nằm ở $dumpRoot"

$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) {
    $check = Join-Path $PSScriptRoot "check_dump.py"
    $args2 = @($check, $dumpRoot)
    if ($Original) { $args2 += @("--original", $Original) }
    & $py.Source @args2
} else {
    Write-Warning "Không có python trong PATH, bỏ qua bước kiểm tra (A3)."
}
Write-Host "[*] Nếu bản dump thiếu import: attach x32dbg -> Scylla (IAT Autosearch -> Get Imports -> Fix Dump)."
