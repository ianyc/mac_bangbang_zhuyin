<#
.SYNOPSIS
  安裝或更新棒棒注音（Windows 版）。在安裝包的資料夾裡雙擊 install.cmd，或執行：
  powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1

.DESCRIPTION
  1. 以系統管理員身分（會跳出一次 UAC）：
     - 把 DLL、模型與說明複製到 C:\Program Files\BangbangZhuyin。
       已經被程式載入、正在使用的舊檔不能覆蓋：先改名成 *.old-時間，重新開機時自動刪除。
       已經開著的程式繼續用舊版，重新啟動後才用新版。
     - 用 64 位元與 32 位元的 regsvr32 註冊（COM 與 TSF 輸入法設定檔）。
     - 登錄到「設定 → 應用程式 → 已安裝的應用程式」，可以從那裡解除安裝。
  2. 以目前的使用者：把棒棒注音加進「中文（繁體，台灣）」的鍵盤清單。

  選字記憶在 %APPDATA%\BangbangZhuyin，安裝與更新都不會動到。
  -AdminStep 是腳本自己以系統管理員身分重新執行時用的，不需要自己加。
#>
param([switch] $AdminStep)

$ErrorActionPreference = 'Stop'
$packageRoot = $PSScriptRoot
# 32 位元的 PowerShell 裡 %ProgramFiles% 是 Program Files (x86)；%ProgramW6432% 一定是 64 位元的 Program Files。
$programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
$installRoot = Join-Path $programFiles 'BangbangZhuyin'
# 與 windows\tip\Guids.h 相同，不能更改。
$clsid = '{37DA0F04-7924-465F-8DA3-C8EEE7D3BFD5}'
$profileGuid = '{0318F0A4-F04F-4C79-B66B-4CF72A7666ED}'
$tip = "0x0404:$clsid$profileGuid"
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\BangbangZhuyin'
# 安裝包裡要複製到安裝資料夾的檔案。
$files = @('x64\BangbangZhuyin.dll', 'x86\BangbangZhuyin.dll', 'bangbang.lm', 'McBopomofo-LICENSE.txt', 'README.txt', 'uninstall.ps1')

# 結束代碼（系統管理員那一步）：0 完成；10 完成，但有舊檔還在使用中（要重新啟動程式）；
# 1／2 x64／x86 註冊失敗；3 複製失敗；6 安裝包不完整。
$codeInUse = 10

Add-Type -Namespace Bangbang -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool MoveFileEx(string existing, string replacement, int flags);
[DllImport("input.dll", CharSet = CharSet.Unicode)]
public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
'@

function Test-Admin {
    $principal = New-Object Security.Principal.WindowsPrincipal ([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# 重新開機時刪除（MOVEFILE_DELAY_UNTIL_REBOOT）。第二個參數要傳真正的 null：
# PowerShell 把 $null 傳給 .NET 的字串參數時會變成空字串，MoveFileEx 就會失敗（2026-11-23 實測）。
function Remove-OnReboot([string] $path) {
    [void][Bangbang.Native]::MoveFileEx($path, [NullString]::Value, 4)
}

# 複製；目的地正在使用時先改名再複製。回傳是否遇到正在使用的檔案。
function Copy-Replacing([string] $source, [string] $destination) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    if (Test-Path -LiteralPath $destination) {
        try {
            Copy-Item -LiteralPath $source -Destination $destination -Force -ErrorAction Stop
            return $false
        } catch {
            $old = "$destination.old-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
            Move-Item -LiteralPath $destination -Destination $old -ErrorAction Stop
            Remove-OnReboot $old
            Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
            return $true
        }
    }
    Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
    return $false
}

function Install-AsAdmin {
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $file))) { return 6 }
    }
    $inUse = $false
    try {
        New-Item -ItemType Directory -Force $installRoot | Out-Null
        # 上次更新時留下的舊檔：沒人用了就刪掉，還在用的排到重新開機時刪除（再排一次也沒關係）。
        # 用 foreach 變數：在 catch 裡 $_ 是錯誤，不是檔案。
        foreach ($old in Get-ChildItem -LiteralPath $installRoot -Recurse -Filter '*.old-*') {
            try { Remove-Item -LiteralPath $old.FullName -Force -ErrorAction Stop } catch { Remove-OnReboot $old.FullName }
        }
        foreach ($file in $files) {
            $source = Join-Path $packageRoot $file
            $destination = Join-Path $installRoot $file
            # 模型檔有 90 MB，而且每個程式都開著：內容沒變就不複製。要比內容：從 zip 解壓縮出來的檔案修改時間會變。
            if ($file -eq 'bangbang.lm' -and (Test-Path -LiteralPath $destination)) {
                $a = Get-Item -LiteralPath $source; $b = Get-Item -LiteralPath $destination
                if ($a.Length -eq $b.Length -and (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $destination).Hash) { continue }
            }
            if (Copy-Replacing $source $destination) { $inUse = $true }
        }
        # 從網路下載的安裝包會帶著「來自網際網路」的標記，裝好的檔案不需要。
        Get-ChildItem -LiteralPath $installRoot -Recurse -File | Unblock-File
    } catch {
        return 3
    }

    $targets = @(
        @{ Exe = Join-Path $env:SystemRoot 'System32\regsvr32.exe'; Dll = Join-Path $installRoot 'x64\BangbangZhuyin.dll'; Code = 1 },
        @{ Exe = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'; Dll = Join-Path $installRoot 'x86\BangbangZhuyin.dll'; Code = 2 }
    )
    foreach ($target in $targets) {
        $process = Start-Process -FilePath $target.Exe -ArgumentList @('/s', "`"$($target.Dll)`"") -Wait -PassThru
        if ($process.ExitCode -ne 0) { return $target.Code }
    }

    # 「已安裝的應用程式」裡的項目：解除安裝按鈕執行安裝資料夾裡的 uninstall.ps1。
    $dll = Join-Path $installRoot 'x64\BangbangZhuyin.dll'
    $sizeKB = [int]((Get-ChildItem -LiteralPath $installRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1KB)
    New-Item -Path $uninstallKey -Force | Out-Null
    $values = @{
        DisplayName = '棒棒注音'
        DisplayVersion = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
        Publisher = '棒棒注音'
        InstallLocation = $installRoot
        DisplayIcon = "$dll,0"
        UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $installRoot 'uninstall.ps1')`""
        InstallDate = (Get-Date -Format 'yyyyMMdd')
    }
    foreach ($name in $values.Keys) { Set-ItemProperty -Path $uninstallKey -Name $name -Value $values[$name] }
    foreach ($name in 'NoModify', 'NoRepair') { New-ItemProperty -Path $uninstallKey -Name $name -Value 1 -PropertyType DWord -Force | Out-Null }
    New-ItemProperty -Path $uninstallKey -Name 'EstimatedSize' -Value $sizeKB -PropertyType DWord -Force | Out-Null

    if ($inUse) { return $codeInUse }
    return 0
}

if ($AdminStep) {
    exit (Install-AsAdmin)
}

$version = (Get-Item -LiteralPath (Join-Path $packageRoot 'x64\BangbangZhuyin.dll') -ErrorAction SilentlyContinue).VersionInfo.ProductVersion
Write-Host "安裝棒棒注音 $version 到 $installRoot"
if (Test-Admin) {
    $code = Install-AsAdmin
} else {
    Write-Host '需要系統管理員權限，會跳出 UAC，請按「是」…'
    try {
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-AdminStep')
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList $arguments
        $code = $process.ExitCode
    } catch {
        Write-Host '已取消安裝（沒有取得系統管理員權限）。' -ForegroundColor Yellow
        exit 1
    }
}
switch ($code) {
    { $_ -eq 0 -or $_ -eq $codeInUse } { break }
    6 { Write-Host '安裝包不完整：請把整個 zip 解壓縮之後，在解壓縮出來的資料夾裡執行 install.cmd。' -ForegroundColor Red; exit $code }
    3 { Write-Host "無法複製檔案到 $installRoot。" -ForegroundColor Red; exit $code }
    default { Write-Host "向系統註冊失敗（代碼 $code）。" -ForegroundColor Red; exit $code }
}

# 加進目前使用者的鍵盤清單（要以使用者自己的身分做，不是系統管理員）。
if (-not [Bangbang.Native]::InstallLayoutOrTip($tip, 0)) {
    Write-Warning '無法自動加進鍵盤清單，請到「設定 → 時間與語言 → 語言與地區 → 中文（繁體，台灣）→ 語言選項 → 新增鍵盤」手動加入「棒棒注音」。'
}

Write-Host ''
if ($code -eq $codeInUse) {
    Write-Host "已更新成 $version。" -ForegroundColor Green
    Write-Host '已經開著的程式還在用舊版：重新啟動這些程式（或登出再登入）之後才會用新版。舊檔會在重新開機時自動刪除。'
} else {
    Write-Host "已安裝棒棒注音 $version。" -ForegroundColor Green
    Write-Host '按 Win + 空白鍵切換到「棒棒注音」。已經開著的程式要重新啟動才會用到。'
}
Write-Host "選字記憶在 $env:APPDATA\BangbangZhuyin。"
