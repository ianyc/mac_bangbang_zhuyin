<#
.SYNOPSIS
  解除安裝棒棒注音（Windows 版）。可以從「設定 → 應用程式 → 已安裝的應用程式」解除安裝，
  或在安裝包的資料夾裡雙擊 uninstall.cmd。

.DESCRIPTION
  1. 以目前的使用者：從鍵盤清單移除棒棒注音。
  2. 以系統管理員身分（會跳出一次 UAC）：
     - 用 64 位元與 32 位元的 regsvr32 取消註冊。
     - 刪除 C:\Program Files\BangbangZhuyin。正在被程式使用的檔案刪不掉，會在重新開機時自動刪除。
     - 從「已安裝的應用程式」移除。

  選字記憶（%APPDATA%\BangbangZhuyin）會保留，以後重新安裝會繼續使用；不需要的話可以自己刪掉那個資料夾。
  -AdminStep 是腳本自己以系統管理員身分重新執行時用的，不需要自己加。
#>
param([switch] $AdminStep)

$ErrorActionPreference = 'Stop'
$programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
$installRoot = Join-Path $programFiles 'BangbangZhuyin'
# 與 windows\tip\Guids.h 相同，不能更改。
$clsid = '{37DA0F04-7924-465F-8DA3-C8EEE7D3BFD5}'
$profileGuid = '{0318F0A4-F04F-4C79-B66B-4CF72A7666ED}'
$tip = "0x0404:$clsid$profileGuid"
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\BangbangZhuyin'

# 結束代碼（系統管理員那一步）：0 完成；10 完成，但有檔案還在使用中（重新開機時刪除）；1／2 x64／x86 取消註冊失敗。
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

function Uninstall-AsAdmin {
    $code = 0
    $targets = @(
        @{ Exe = Join-Path $env:SystemRoot 'System32\regsvr32.exe'; Dll = Join-Path $installRoot 'x64\BangbangZhuyin.dll'; Code = 1 },
        @{ Exe = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'; Dll = Join-Path $installRoot 'x86\BangbangZhuyin.dll'; Code = 2 }
    )
    foreach ($target in $targets) {
        if (-not (Test-Path -LiteralPath $target.Dll)) { continue }
        $process = Start-Process -FilePath $target.Exe -ArgumentList @('/s', '/u', "`"$($target.Dll)`"") -Wait -PassThru
        if ($process.ExitCode -ne 0 -and $code -eq 0) { $code = $target.Code }
    }

    # 刪除安裝資料夾（資料夾名稱一定是 BangbangZhuyin，避免以系統管理員身分刪錯地方）。
    # MoveFileEx 的第二個參數要傳真正的 null（[NullString]::Value）：PowerShell 的 $null 會變成空字串，排程就會失敗。
    $pending = $false
    if ((Split-Path -Leaf $installRoot) -eq 'BangbangZhuyin' -and (Test-Path -LiteralPath $installRoot)) {
        # 先刪檔案。正在使用的先改名，再排到重新開機時刪除（MOVEFILE_DELAY_UNTIL_REBOOT）：
        # 原本的檔名空出來，之後沒重新開機就重新安裝時，新裝的檔案才不會在重新開機時被刪掉。
        foreach ($file in Get-ChildItem -LiteralPath $installRoot -Recurse -File) {
            try {
                Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop
            } catch {
                $target = $file.FullName
                if ($file.Name -notlike '*.old-*') {
                    $renamed = "$($file.FullName).old-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
                    try {
                        Move-Item -LiteralPath $file.FullName -Destination $renamed -ErrorAction Stop
                        $target = $renamed
                    } catch { }
                }
                [void][Bangbang.Native]::MoveFileEx($target, [NullString]::Value, 4)
                $pending = $true
            }
        }
        # 再由深到淺刪資料夾；還有檔案的排到重新開機時刪除（到時候如果又裝了新版，資料夾不是空的，就不會被刪）。
        $folders = @(Get-ChildItem -LiteralPath $installRoot -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending) +
                   @(Get-Item -LiteralPath $installRoot)
        foreach ($folder in $folders) {
            try {
                Remove-Item -LiteralPath $folder.FullName -Force -ErrorAction Stop
            } catch {
                [void][Bangbang.Native]::MoveFileEx($folder.FullName, [NullString]::Value, 4)
                $pending = $true
            }
        }
    }
    if (Test-Path $uninstallKey) { Remove-Item -Path $uninstallKey -Recurse -Force }
    if ($code -eq 0 -and $pending) { $code = $codeInUse }
    return $code
}

if ($AdminStep) {
    exit (Uninstall-AsAdmin)
}

Write-Host '解除安裝棒棒注音'
# 先從目前使用者的鍵盤清單移除（ILOT_UNINSTALL）。
[void][Bangbang.Native]::InstallLayoutOrTip($tip, 1)

if (Test-Admin) {
    $code = Uninstall-AsAdmin
} else {
    Write-Host '需要系統管理員權限，會跳出 UAC，請按「是」…'
    try {
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-AdminStep')
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList $arguments
        $code = $process.ExitCode
    } catch {
        Write-Host '已取消（沒有取得系統管理員權限）。棒棒注音已經從鍵盤清單移除，但檔案還在；要完整解除安裝請再執行一次。' -ForegroundColor Yellow
        exit 1
    }
}

Write-Host ''
switch ($code) {
    0 { Write-Host '已解除安裝棒棒注音。' -ForegroundColor Green }
    $codeInUse {
        Write-Host '已解除安裝棒棒注音。' -ForegroundColor Green
        Write-Host '有些檔案還被開著的程式使用中，會在重新開機時自動刪除。'
    }
    default { Write-Host "取消註冊時發生問題（代碼 $code），請重新開機後再執行一次。" -ForegroundColor Red }
}
Write-Host "選字記憶保留在 $env:APPDATA\BangbangZhuyin（重新安裝會繼續使用；不需要的話可以自己刪掉這個資料夾）。"
Write-Host '已經開著的程式要重新啟動，才會完全卸下棒棒注音。'
