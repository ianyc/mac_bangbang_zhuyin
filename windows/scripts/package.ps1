<#
.SYNOPSIS
  做安裝檔：建置 Release 版，再用 WiX 做出 windows\out\dist\BangbangZhuyin-<版本>.msi。

.DESCRIPTION
  1. 建置 Release 版（x64 與 x86，並跑測試；-NoBuild 時略過）。
  2. 把要安裝的檔案放到 windows\out\dist\BangbangZhuyin-<版本>-files\：x64\、x86\ 的 DLL、模型、使用說明、授權。
  3. 用 WiX v6（windows\installer\BangbangZhuyin.wxs）做出 MSI，並用 Windows Installer 的 ICE 規則檢查。

  版本號來自 windows\CMakeLists.txt 的 project(VERSION)，也會編進 DLL（工作列圖示的右鍵選單、已安裝的應用程式）。
  每次建置的 MSI 都有新的產品代碼，所以同一個版本號重新建置後，也能直接裝在舊的上面（升級）。

  第一次要先安裝 WiX（需要 .NET SDK 6 以上）：
    dotnet tool install --global wix --version 6.0.2
    wix extension add -g WixToolset.UI.wixext/6.0.2

.EXAMPLE
  windows\scripts\package.ps1            # 先建置 Release 版，再做 MSI
.EXAMPLE
  windows\scripts\package.ps1 -NoBuild   # 用已經建置好的 Release 版
#>
param([switch] $NoBuild)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $windowsRoot

$wix = (Get-Command wix -ErrorAction SilentlyContinue).Source
if (-not $wix) { $wix = Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe' }
if (-not (Test-Path -LiteralPath $wix)) { throw '找不到 WiX（wix.exe）：請先執行 dotnet tool install --global wix --version 6.0.2，再執行 wix extension add -g WixToolset.UI.wixext/6.0.2' }

if (-not $NoBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Config Release }

$cmake = Get-Content -LiteralPath (Join-Path $windowsRoot 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(BangbangZhuyin VERSION (\d+\.\d+\.\d+)') { throw '在 windows\CMakeLists.txt 找不到 project(BangbangZhuyin VERSION x.y.z)' }
$version = $Matches[1]

# 2. 要安裝的檔案
$dist = Join-Path $windowsRoot 'out\dist'
$files = Join-Path $dist "BangbangZhuyin-$version-files"
# 只清空內容：資料夾本身可能是某個視窗的目前目錄，刪不掉。
if (Test-Path -LiteralPath $files) { Get-ChildItem -LiteralPath $files -Force | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force $files | Out-Null
foreach ($arch in 'x64', 'x86') {
    $dll = Join-Path $windowsRoot "out\build\$arch\tip\Release\BangbangZhuyin.dll"
    if (-not (Test-Path -LiteralPath $dll)) { throw "找不到 $dll，請先執行 windows\scripts\build.ps1 -Config Release" }
    $dllVersion = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
    if ($dllVersion -ne $version) { throw "$dll 的版本是 $dllVersion，和 CMakeLists.txt 的 $version 不同，請重新建置" }
    New-Item -ItemType Directory -Force (Join-Path $files $arch) | Out-Null
    Copy-Item -LiteralPath $dll -Destination (Join-Path $files "$arch\BangbangZhuyin.dll")
}
$model = Join-Path $repoRoot 'build\bangbang-v3.lm'
if (-not (Test-Path -LiteralPath $model)) { throw "找不到模型檔 $model" }
Copy-Item -LiteralPath $model -Destination (Join-Path $files 'bangbang.lm')
Copy-Item -LiteralPath (Join-Path $repoRoot 'data\mcbopomofo\LICENSE.txt') -Destination (Join-Path $files 'McBopomofo-LICENSE.txt')
# 使用說明存成有 BOM 的 UTF-8、CRLF 換行，記事本才讀得對。
$readme = [IO.File]::ReadAllText((Join-Path $windowsRoot 'package\README.txt')) -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText((Join-Path $files 'README.txt'), $readme, (New-Object Text.UTF8Encoding $true))

# 3. MSI
$setupDll = Join-Path $windowsRoot 'out\build\x64\installer\Release\BangbangSetup.dll'
if (-not (Test-Path -LiteralPath $setupDll)) { throw "找不到 $setupDll，請先執行 windows\scripts\build.ps1 -Config Release" }
$msi = Join-Path $dist "BangbangZhuyin-$version.msi"
& $wix build (Join-Path $windowsRoot 'installer\BangbangZhuyin.wxs') -arch x64 -culture zh-TW -ext WixToolset.UI.wixext -pdbtype none `
    -d "Version=$version" -d "SourceDir=$files" -d "SetupDll=$setupDll" -d "IconFile=$(Join-Path $windowsRoot 'tip\BangbangZhuyin.ico')" `
    -o $msi
if ($LASTEXITCODE -ne 0) { throw "WiX 建置 MSI 失敗（代碼 $LASTEXITCODE）" }
# ICE61（允許同一版本重新安裝）是預期中的警告。
& $wix msi validate $msi
if ($LASTEXITCODE -ne 0) { throw "MSI 沒有通過 ICE 檢查（代碼 $LASTEXITCODE）" }

$size = '{0:N1}' -f ((Get-Item -LiteralPath $msi).Length / 1MB)
Write-Host "已做好安裝檔：$msi（$size MB）" -ForegroundColor Green
