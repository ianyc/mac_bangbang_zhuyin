<#
.SYNOPSIS
  檢查棒棒注音目前的安裝狀態（只讀不改，不需要系統管理員）：註冊的 DLL 與類別、Program Files 裡的檔案、
  「已安裝的應用程式」的項目、鍵盤清單、重新開機時要刪的檔案、選字記憶。安裝、更新、解除安裝之後用來確認。

.EXAMPLE
  windows\scripts\check-install.ps1
#>
$ErrorActionPreference = 'Stop'
$programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
$installRoot = Join-Path $programFiles 'BangbangZhuyin'
$clsid = '{37DA0F04-7924-465F-8DA3-C8EEE7D3BFD5}'
$profileGuid = '{0318F0A4-F04F-4C79-B66B-4CF72A7666ED}'
$secureMode = '{49D2F9CE-1F5E-11D7-A6D3-00065B84435C}'  # GUID_TFCAT_TIPCAP_SECUREMODE

function Open-LocalMachine([Microsoft.Win32.RegistryView] $view) {
    [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
}

Write-Host '== 註冊'
foreach ($view in [Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32) {
    $label = if ($view -eq [Microsoft.Win32.RegistryView]::Registry64) { 'x64' } else { 'x86' }
    $base = Open-LocalMachine $view
    try {
        $server = $base.OpenSubKey("Software\Classes\CLSID\$clsid\InprocServer32")
        $path = if ($server) { [string]$server.GetValue('') } else { $null }
        $categoryKey = $base.OpenSubKey("SOFTWARE\Microsoft\CTF\TIP\$clsid\Category\Item\$clsid")
        $categories = if ($categoryKey) { @($categoryKey.GetSubKeyNames() | ForEach-Object { $_.ToUpperInvariant() }) } else { @() }
        $profiles = $base.OpenSubKey("SOFTWARE\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000404\$profileGuid")
        if (-not $path) {
            Write-Host "  $label：沒有註冊"
            continue
        }
        $where = if ($path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { '安裝版' } else { '開發版或其他位置' }
        $exists = if (Test-Path -LiteralPath $path) { '' } else { '（檔案不存在！）' }
        Write-Host "  $label：$where $path$exists"
        Write-Host ("        類別 {0} 個，SECUREMODE：{1}；語言設定檔：{2}" -f $categories.Count, $(if ($categories -contains $secureMode) { '有' } else { '沒有' }), $(if ($profiles) { '有' } else { '沒有' }))
    } finally {
        $base.Close()
    }
}

Write-Host "== 安裝資料夾 $installRoot"
if (Test-Path -LiteralPath $installRoot) {
    Get-ChildItem -LiteralPath $installRoot -Recurse -File | ForEach-Object {
        $version = if ($_.Extension -eq '.dll') { " 版本 $($_.VersionInfo.ProductVersion)" } else { '' }
        Write-Host ("  {0,-34} {1,12:N0} bytes{2}" -f $_.FullName.Substring($installRoot.Length + 1), $_.Length, $version)
    }
} else {
    Write-Host '  （不存在）'
}

Write-Host '== 已安裝的應用程式'
# MSI 安裝的項目以產品代碼（{GUID}）為名；以前用 PowerShell 腳本安裝的項目叫 BangbangZhuyin。
$entries = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
    Where-Object { (Get-ItemProperty -LiteralPath $_.PSPath -Name DisplayName -ErrorAction SilentlyContinue).DisplayName -eq '棒棒注音' })
foreach ($entry in $entries) {
    $p = Get-ItemProperty -LiteralPath $entry.PSPath
    $kind = if ($entry.PSChildName -like '{*}') { "MSI，產品代碼 $($entry.PSChildName)" } else { '舊的 PowerShell 腳本安裝' }
    Write-Host "  $($p.DisplayName) $($p.DisplayVersion)（$kind），安裝日期 $($p.InstallDate)"
    Write-Host "  解除安裝：$($p.UninstallString)"
}
if (-not $entries.Count) { Write-Host '  （沒有項目）' }

Write-Host '== 重新開機時要刪除的檔案'
$pending = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations -ErrorAction SilentlyContinue).PendingFileRenameOperations
$ours = @($pending | Where-Object { $_ -match 'BangbangZhuyin' })
# MSI 更新或解除安裝時，還在使用的舊檔會移到 C:\Config.Msi\*.rbf，重新開機時刪除。檔名看不出是哪個 MSI 留下的。
$msiBackups = @($pending | Where-Object { $_ -match '\\Config\.Msi\\' })
$ours | ForEach-Object { Write-Host "  $_" }
if ($msiBackups.Count) { Write-Host "  Windows Installer 移開的使用中舊檔：$($msiBackups.Count) 個（在 C:\Config.Msi，不一定是棒棒注音的）" }
if ($ours.Count + $msiBackups.Count -eq 0) { Write-Host '  （沒有）' }

Write-Host '== 鍵盤清單（目前的使用者）'
$tipId = "0404:$clsid$profileGuid"
$found = $false
$languages = Get-WinUserLanguageList  # 整個清單是一個物件，要用索引逐一取出
for ($i = 0; $i -lt $languages.Count; $i++) {
    $language = $languages[$i]
    if ($language.InputMethodTips | Where-Object { $_ -ieq $tipId }) {
        Write-Host "  在「$($language.LocalizedName)」（$($language.LanguageTag)）的鍵盤清單裡"
        $found = $true
    }
}
if (-not $found) { Write-Host '  （不在鍵盤清單裡）' }

Write-Host '== 選字記憶'
$model = Join-Path $env:APPDATA 'BangbangZhuyin\user-model.json'
if (Test-Path -LiteralPath $model) {
    $item = Get-Item -LiteralPath $model
    Write-Host ("  {0}（{1:N0} bytes，{2}）" -f $model, $item.Length, $item.LastWriteTime)
} else {
    Write-Host "  （沒有 $model）"
}
