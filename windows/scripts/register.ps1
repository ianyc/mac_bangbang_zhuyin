<#
.SYNOPSIS
  開發用：把建置好的棒棒注音部署到 windows\out\deploy，並向系統註冊。
  （安裝到 Program Files：用 windows\scripts\package.ps1 做安裝包，再執行安裝包裡的 install.cmd。）

.DESCRIPTION
  1. 複製 x64 與 x86 的 BangbangZhuyin.dll、模型檔與授權到 windows\out\deploy（不納入 git）。
     DLL 被使用中的程式載入時不能覆蓋，會先把舊檔改名再複製：已經開著的程式繼續用舊版，新開的程式用新版。
  2. 第一次（或 DLL 位置、註冊的類別改變時）以系統管理員身分執行 regsvr32，註冊 COM 與 TSF 語言設定檔：會跳出一次 UAC。
     之後重新建置再執行，只會更新檔案，不需要再按 UAC。
  3. 把棒棒注音加進目前使用者「中文（繁體，台灣）」的鍵盤清單。

  開發版不放在 %LOCALAPPDATA%：從 MSIX 封裝程式（例如 Claude 桌面版的終端機或 Claude Code）裡執行時，
  新建在 AppData 的檔案會被轉存到那個程式的私有資料夾（%LOCALAPPDATA%\Packages\<程式>\LocalCache），
  其他程式在註冊的路徑上找不到 DLL，只好改用微軟注音。腳本會開放唯讀權限給 Microsoft Store 程式（沙盒）。

  開發版和安裝版（Program Files）註冊的是同一個輸入法，後註冊的會取代先註冊的：
  執行這個腳本會改回開發版；執行安裝包的 install.cmd 會改成安裝版。

.EXAMPLE
  windows\scripts\register.ps1                  # 部署 Debug 版並註冊
.EXAMPLE
  windows\scripts\register.ps1 -Config Release  # 部署 Release 版
.EXAMPLE
  windows\scripts\register.ps1 -Unregister      # 從鍵盤清單移除，並取消註冊開發版
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [switch] $Unregister,
    [switch] $ForceRegister
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $windowsRoot
$devRoot = Join-Path $windowsRoot 'out\deploy'
# 32 位元的 PowerShell 裡 %ProgramFiles% 是 Program Files (x86)；%ProgramW6432% 一定是 64 位元的 Program Files。
$programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
$installRoot = Join-Path $programFiles 'BangbangZhuyin'
# 與 windows\tip\Guids.h 相同，不能更改。
$clsid = '{37DA0F04-7924-465F-8DA3-C8EEE7D3BFD5}'
$profileGuid = '{0318F0A4-F04F-4C79-B66B-4CF72A7666ED}'
$tip = "0x0404:$clsid$profileGuid"
$dlls = @{ x64 = Join-Path $devRoot 'x64\BangbangZhuyin.dll'; x86 = Join-Path $devRoot 'x86\BangbangZhuyin.dll' }
# 開發版註冊的類別，與 windows\tip\Register.cpp 的 kCategories 相同：TIP_KEYBOARD、DISPLAYATTRIBUTEPROVIDER、
# TIPCAP_SYSTRAYSUPPORT、TIPCAP_COMLESS、TIPCAP_IMMERSIVESUPPORT、TIPCAP_UIELEMENTENABLED、TIPCAP_INPUTMODECOMPARTMENT。
# （SECUREMODE 只有安裝在 Program Files 時才註冊。）
$expectedCategories = @(
    '{34745C63-B2F0-4784-8B67-5E12C8701A31}', '{046B8C80-1647-40F7-9B21-B93B81AABC1B}',
    '{25504FB4-7BAB-4BC1-9C69-CF81890F0EF5}', '{364215D9-75BC-11D7-A6EF-00065B84435C}',
    '{13A016DF-560B-46CD-947A-4C3AF1E0E35D}', '{49D2F9CF-1F5E-11D7-A6D3-00065B84435C}',
    '{CCF05DD7-4A87-11D7-A6E2-00065B84435C}')

if (-not ('Bangbang.InputSettings' -as [type])) {
    Add-Type -Namespace Bangbang -Name InputSettings -MemberDefinition @'
[DllImport("input.dll", CharSet = CharSet.Unicode)]
public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
'@
}

# 以系統管理員身分執行 regsvr.ps1，回傳它的結束代碼。
function Invoke-Elevated([string[]] $arguments) {
    $helper = Join-Path $PSScriptRoot 'regsvr.ps1'
    $argumentList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$helper`"") + $arguments
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList $argumentList
    return $process.ExitCode
}

function Get-RegisteredPath([Microsoft.Win32.RegistryView] $view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $key = $base.OpenSubKey("Software\Classes\CLSID\$clsid\InprocServer32")
        if ($null -eq $key) { return $null }
        try { return [string]$key.GetValue('') } finally { $key.Close() }
    } finally {
        $base.Close()
    }
}

function Get-RegisteredCategories([Microsoft.Win32.RegistryView] $view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $key = $base.OpenSubKey("SOFTWARE\Microsoft\CTF\TIP\$clsid\Category\Item\$clsid")
        if ($null -eq $key) { return @() }
        try { return @($key.GetSubKeyNames() | ForEach-Object { $_.ToUpperInvariant() }) } finally { $key.Close() }
    } finally {
        $base.Close()
    }
}

function Test-CategoriesMatch([Microsoft.Win32.RegistryView] $view) {
    $expected = ($expectedCategories | ForEach-Object { $_.ToUpperInvariant() } | Sort-Object) -join ','
    $actual = (Get-RegisteredCategories $view | Sort-Object) -join ','
    return $expected -eq $actual
}

function Copy-Replacing([string] $source, [string] $destination) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    if (Test-Path -LiteralPath $destination) {
        try {
            Copy-Item -LiteralPath $source -Destination $destination -Force
            return
        } catch {
            # 使用中的檔案不能覆蓋，但可以改名：舊程式繼續用舊檔，新開的程式用新檔。
            $old = "$(Split-Path -Leaf $destination).old-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
            Rename-Item -LiteralPath $destination -NewName $old
        }
    }
    Copy-Item -LiteralPath $source -Destination $destination
}

function Remove-OldCopies([string] $directory) {
    Get-ChildItem -LiteralPath $directory -Filter '*.old-*' -ErrorAction SilentlyContinue | ForEach-Object {
        try { Remove-Item -LiteralPath $_.FullName -Force } catch { }  # 還在使用中的，下次再刪
    }
}

if ($Unregister) {
    $registered = @{
        x64 = Get-RegisteredPath ([Microsoft.Win32.RegistryView]::Registry64)
        x86 = Get-RegisteredPath ([Microsoft.Win32.RegistryView]::Registry32)
    }
    # 目前註冊的是安裝版：交給安裝版自己的解除安裝（它也會移除「已安裝的應用程式」裡的項目與 Program Files 的檔案）。
    if (($registered.x64 -and $registered.x64.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) -or
        ($registered.x86 -and $registered.x86.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase))) {
        Write-Host "目前註冊的是安裝版（$installRoot）：請從「設定 → 應用程式 → 已安裝的應用程式」解除安裝棒棒注音，或執行安裝包的 uninstall.cmd。" -ForegroundColor Yellow
        return
    }
    [void][Bangbang.InputSettings]::InstallLayoutOrTip($tip, 1)  # ILOT_UNINSTALL：從鍵盤清單移除
    $arguments = @('-Unregister')
    foreach ($arch in 'x64', 'x86') {
        $path = if ($registered[$arch]) { $registered[$arch] } elseif (Test-Path $dlls[$arch]) { $dlls[$arch] } else { $null }
        if ($path) { $arguments += @("-Dll$($arch -replace 'x', '')", "`"$path`"") }
    }
    if ($arguments.Count -gt 1) {
        Write-Host '取消註冊（會跳出 UAC，請按「是」）…'
        $code = Invoke-Elevated $arguments
        if ($code -ne 0) { throw "取消註冊失敗（代碼 $code）" }
    }
    Write-Host '已取消註冊開發版。已經開著的程式要重新啟動，才會完全卸下棒棒注音。' -ForegroundColor Green
    return
}

# 1. 找到建置好的檔案
$sources = @{}
foreach ($arch in 'x64', 'x86') {
    $sources[$arch] = Join-Path $windowsRoot "out\build\$arch\tip\$Config\BangbangZhuyin.dll"
    if (-not (Test-Path $sources[$arch])) { throw "找不到 $($sources[$arch])，請先執行 windows\scripts\build.ps1 -Config $Config" }
}
$model = Join-Path $repoRoot 'build\bangbang-v3.lm'
if (-not (Test-Path $model)) { throw "找不到模型檔 $model" }
$license = Join-Path $repoRoot 'data\mcbopomofo\LICENSE.txt'

# 2. 部署到開發資料夾
foreach ($arch in 'x64', 'x86') {
    Copy-Replacing $sources[$arch] $dlls[$arch]
    Remove-OldCopies (Split-Path -Parent $dlls[$arch])
}
$deployedModel = Join-Path $devRoot 'bangbang.lm'
$modelChanged = -not (Test-Path $deployedModel)
if (-not $modelChanged) {
    $a = Get-Item $model; $b = Get-Item $deployedModel
    $modelChanged = $a.Length -ne $b.Length -or $a.LastWriteTimeUtc -ne $b.LastWriteTimeUtc
}
if ($modelChanged) { Copy-Replacing $model $deployedModel }
Remove-OldCopies $devRoot
Copy-Item -LiteralPath $license -Destination (Join-Path $devRoot 'McBopomofo-LICENSE.txt') -Force
# 讓 Microsoft Store 程式（沙盒）也讀得到 DLL 與模型：開放唯讀權限給「所有應用程式套件」與「所有受限應用程式套件」。
# 這是微軟 IME 規範要求的做法；安裝到 Program Files 時預設就有這個權限。
& icacls.exe $devRoot /grant '*S-1-15-2-1:(OI)(CI)(RX)' '*S-1-15-2-2:(OI)(CI)(RX)' /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Warning "無法設定 $devRoot 的讀取權限（icacls 代碼 $LASTEXITCODE），Store 程式裡可能無法使用棒棒注音。" }
Write-Host "已部署 $Config 版到 $devRoot"

# 3. 註冊（需要系統管理員）。已經用同一個位置、同一組類別註冊過就略過。
$registered = (Get-RegisteredPath ([Microsoft.Win32.RegistryView]::Registry64)) -eq $dlls.x64 -and
              (Get-RegisteredPath ([Microsoft.Win32.RegistryView]::Registry32)) -eq $dlls.x86 -and
              (Test-CategoriesMatch ([Microsoft.Win32.RegistryView]::Registry64)) -and
              (Test-CategoriesMatch ([Microsoft.Win32.RegistryView]::Registry32))
if ($ForceRegister -or -not $registered) {
    Write-Host '向系統註冊棒棒注音（會跳出 UAC，請按「是」）…'
    $code = Invoke-Elevated @('-Dll64', "`"$($dlls.x64)`"", '-Dll86', "`"$($dlls.x86)`"")
    if ($code -ne 0) { throw "regsvr32 失敗（代碼 $code）" }
    Write-Host '已註冊 x64 與 x86。'
}

# 4. 加進鍵盤清單
if (-not [Bangbang.InputSettings]::InstallLayoutOrTip($tip, 0)) {
    Write-Warning '無法自動加進鍵盤清單，請到「設定 → 時間與語言 → 語言與地區 → 中文（繁體，台灣）→ 語言選項 → 新增鍵盤」手動加入。'
}
Write-Host '完成。用 Win + 空白鍵切換到「棒棒注音」。已經開著的程式要重新啟動，才會用到新版。' -ForegroundColor Green
