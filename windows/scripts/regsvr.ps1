<#
.SYNOPSIS
  由 register.ps1 以系統管理員身分呼叫：用 64 位元與 32 位元的 regsvr32 註冊（或取消註冊）開發版的棒棒注音。
  一般不需要直接執行。安裝版（Program Files）的註冊由安裝包的 install.ps1／uninstall.ps1 處理。

.DESCRIPTION
  結束代碼：0 成功；1 x64 失敗；2 x86 失敗。
#>
param(
    [string] $Dll64,
    [string] $Dll86,
    [switch] $Unregister
)

$exitCode = 0
$targets = @(
    @{ Exe = Join-Path $env:SystemRoot 'System32\regsvr32.exe'; Dll = $Dll64; Code = 1 },
    @{ Exe = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'; Dll = $Dll86; Code = 2 }
)
foreach ($target in $targets) {
    if (-not $target.Dll -or -not (Test-Path -LiteralPath $target.Dll)) { continue }
    $arguments = @('/s')
    if ($Unregister) { $arguments += '/u' }
    $arguments += "`"$($target.Dll)`""
    $process = Start-Process -FilePath $target.Exe -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { $exitCode = $target.Code }
}
exit $exitCode
