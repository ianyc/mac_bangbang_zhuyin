<#
.SYNOPSIS
  由 register.ps1 以系統管理員身分呼叫：用 64 位元與 32 位元的 regsvr32 註冊（或取消註冊）棒棒注音。
  一般不需要直接執行。

.DESCRIPTION
  -InstallRoot：註冊前先把建置好的檔案複製到這個資料夾（Program Files\BangbangZhuyin）：
    x64\BangbangZhuyin.dll、x86\BangbangZhuyin.dll、bangbang.lm、McBopomofo-LICENSE.txt。
  -RemoveRoot：取消註冊後刪除這個資料夾；還在使用中的檔案刪不掉，留到下次。
  為了避免以系統管理員身分刪錯資料夾，這兩個資料夾的名稱都必須是 BangbangZhuyin。

  結束代碼：0 成功；1 x64 註冊失敗；2 x86 註冊失敗；3 複製失敗；4 有檔案還在使用中、沒有刪掉；5 資料夾名稱不對。
#>
param(
    [string] $Dll64,
    [string] $Dll86,
    [switch] $Unregister,
    [string] $InstallRoot,
    [string] $SourceX64,
    [string] $SourceX86,
    [string] $Model,
    [string] $License,
    [string] $RemoveRoot
)

function Copy-Replacing([string] $source, [string] $destination) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    if (Test-Path -LiteralPath $destination) {
        try {
            Copy-Item -LiteralPath $source -Destination $destination -Force -ErrorAction Stop
            return
        } catch {
            # 使用中的檔案不能覆蓋，但可以改名：已經開著的程式繼續用舊檔，新開的程式用新檔。
            $old = "$(Split-Path -Leaf $destination).old-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
            Rename-Item -LiteralPath $destination -NewName $old -ErrorAction Stop
        }
    }
    Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
}

foreach ($root in @($InstallRoot, $RemoveRoot)) {
    if ($root -and (Split-Path -Leaf $root) -ne 'BangbangZhuyin') { exit 5 }
}

if ($InstallRoot) {
    try {
        Copy-Replacing $SourceX64 (Join-Path $InstallRoot 'x64\BangbangZhuyin.dll')
        Copy-Replacing $SourceX86 (Join-Path $InstallRoot 'x86\BangbangZhuyin.dll')
        $deployedModel = Join-Path $InstallRoot 'bangbang.lm'
        $a = Get-Item -LiteralPath $Model -ErrorAction Stop
        $b = Get-Item -LiteralPath $deployedModel -ErrorAction SilentlyContinue
        if (-not $b -or $a.Length -ne $b.Length -or $a.LastWriteTimeUtc -ne $b.LastWriteTimeUtc) { Copy-Replacing $Model $deployedModel }
        Copy-Item -LiteralPath $License -Destination (Join-Path $InstallRoot 'McBopomofo-LICENSE.txt') -Force -ErrorAction Stop
        Get-ChildItem -LiteralPath $InstallRoot -Recurse -Filter '*.old-*' | ForEach-Object {
            try { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction Stop } catch { }  # 還在使用中的，下次再刪
        }
    } catch {
        exit 3
    }
}

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

if ($RemoveRoot -and (Test-Path -LiteralPath $RemoveRoot)) {
    Remove-Item -LiteralPath $RemoveRoot -Recurse -Force -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $RemoveRoot) -and $exitCode -eq 0) { $exitCode = 4 }
}
exit $exitCode
