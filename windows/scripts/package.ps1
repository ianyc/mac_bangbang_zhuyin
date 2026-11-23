<#
.SYNOPSIS
  做安裝包：把 Release 版的 DLL、模型、安裝與解除安裝腳本、使用說明放進
  windows\out\dist\BangbangZhuyin-<版本>-windows\，並壓成同名的 zip（可以帶到別台電腦安裝）。

.DESCRIPTION
  版本號來自 windows\CMakeLists.txt 的 project(VERSION)，也會編進 DLL（工作列圖示的右鍵選單、已安裝的應用程式）。
  安裝包裡的檔案來自 windows\package：.ps1 與 README.txt 存成有 BOM 的 UTF-8（Windows PowerShell 5.1 與記事本才讀得對），
  .cmd 只有英數字（命令提示字元用主控台的字碼頁讀）。

.EXAMPLE
  windows\scripts\package.ps1            # 先建置 Release 版（x64 與 x86，並跑測試），再做安裝包
.EXAMPLE
  windows\scripts\package.ps1 -NoBuild   # 用已經建置好的 Release 版
#>
param([switch] $NoBuild)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $windowsRoot

if (-not $NoBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Config Release }

$cmake = Get-Content -LiteralPath (Join-Path $windowsRoot 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(BangbangZhuyin VERSION (\d+\.\d+\.\d+)') { throw '在 windows\CMakeLists.txt 找不到 project(BangbangZhuyin VERSION x.y.z)' }
$version = $Matches[1]

$name = "BangbangZhuyin-$version-windows"
$dist = Join-Path $windowsRoot 'out\dist'
$folder = Join-Path $dist $name
# 只清空內容：資料夾本身可能是某個視窗的目前目錄（例如 install.cmd 跑完停在「按任意鍵」的視窗），刪不掉。
if (Test-Path -LiteralPath $folder) { Get-ChildItem -LiteralPath $folder -Force | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force $folder | Out-Null

foreach ($arch in 'x64', 'x86') {
    $dll = Join-Path $windowsRoot "out\build\$arch\tip\Release\BangbangZhuyin.dll"
    if (-not (Test-Path -LiteralPath $dll)) { throw "找不到 $dll，請先執行 windows\scripts\build.ps1 -Config Release" }
    $dllVersion = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
    if ($dllVersion -ne $version) { throw "$dll 的版本是 $dllVersion，和 CMakeLists.txt 的 $version 不同，請重新建置" }
    New-Item -ItemType Directory -Force (Join-Path $folder $arch) | Out-Null
    Copy-Item -LiteralPath $dll -Destination (Join-Path $folder "$arch\BangbangZhuyin.dll")
}
$model = Join-Path $repoRoot 'build\bangbang-v3.lm'
if (-not (Test-Path -LiteralPath $model)) { throw "找不到模型檔 $model" }
Copy-Item -LiteralPath $model -Destination (Join-Path $folder 'bangbang.lm')
Copy-Item -LiteralPath (Join-Path $repoRoot 'data\mcbopomofo\LICENSE.txt') -Destination (Join-Path $folder 'McBopomofo-LICENSE.txt')

foreach ($file in 'install.ps1', 'uninstall.ps1', 'README.txt', 'install.cmd', 'uninstall.cmd') {
    $text = [IO.File]::ReadAllText((Join-Path $windowsRoot "package\$file")) -replace "`r?`n", "`r`n"
    $bom = -not $file.EndsWith('.cmd')
    [IO.File]::WriteAllText((Join-Path $folder $file), $text, (New-Object Text.UTF8Encoding $bom))
}

$zip = Join-Path $dist "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
$size = '{0:N1}' -f ((Get-Item -LiteralPath $zip).Length / 1MB)
Write-Host "已做好安裝包：$folder" -ForegroundColor Green
Write-Host "壓縮檔：$zip（$size MB）"
