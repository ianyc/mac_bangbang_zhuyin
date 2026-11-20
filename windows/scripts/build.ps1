<#
.SYNOPSIS
  建置棒棒注音 Windows 版並執行單元測試。

.EXAMPLE
  windows\scripts\build.ps1                    # x64 與 x86 的 Debug，建置後跑測試
.EXAMPLE
  windows\scripts\build.ps1 -Config Release    # Release 版
.EXAMPLE
  windows\scripts\build.ps1 -Arch x64 -NoTest  # 只建置 x64，不跑測試
#>
param(
    [ValidateSet('x64', 'x86', 'all')] [string] $Arch = 'all',
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [switch] $NoTest
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot

# 用 Visual Studio 附帶的 CMake，不需要另外安裝或設定 PATH。
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw '找不到 Visual Studio Installer（vswhere.exe）。' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw '找不到含「使用 C++ 的桌面開發」的 Visual Studio。' }
$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $cmakeBin 'cmake.exe'
$ctest = Join-Path $cmakeBin 'ctest.exe'

function Invoke-Native([string] $exe, [string[]] $arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "執行失敗（代碼 $LASTEXITCODE）：$(Split-Path -Leaf $exe) $($arguments -join ' ')" }
}

$archs = if ($Arch -eq 'all') { @('x64', 'x86') } else { @($Arch) }
Push-Location $windowsRoot
try {
    foreach ($a in $archs) {
        $preset = "$a-$($Config.ToLowerInvariant())"
        Write-Host "== $preset：設定" -ForegroundColor Cyan
        Invoke-Native $cmake @('--preset', $a)
        Write-Host "== $preset：建置" -ForegroundColor Cyan
        Invoke-Native $cmake @('--build', '--preset', $preset)
        if (-not $NoTest) {
            Write-Host "== $preset：測試" -ForegroundColor Cyan
            Invoke-Native $ctest @('--preset', $preset)
        }
    }
    Write-Host '完成。' -ForegroundColor Green
} finally {
    Pop-Location
}
