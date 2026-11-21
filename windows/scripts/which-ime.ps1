<#
.SYNOPSIS
  列出哪些程式載入了棒棒注音、哪些載入了微軟注音，用來確認實際在用的是哪一個輸入法。

.DESCRIPTION
  - 程式要在輸入框裡打過字，才會載入輸入法。
  - 輸入法載入後會留在程式裡；在同一個程式裡切換過兩種輸入法，就會兩個都列出來。
    想確認某個程式用的是哪一個，先把那個程式關掉重開，只切到一種輸入法打字，再執行這個腳本。

.EXAMPLE
  windows\scripts\which-ime.ps1
#>
$names = @{ 'BangbangZhuyin.dll' = '棒棒注音'; 'IMTCTIP.DLL' = '微軟注音' }
$rows = foreach ($process in Get-Process) {
    try {
        $loaded = @($process.Modules | Where-Object { $names.ContainsKey($_.ModuleName) } | ForEach-Object { $names[$_.ModuleName] } | Select-Object -Unique)
    } catch {
        continue  # 權限不足（例如系統管理員身分執行的程式）
    }
    if ($loaded.Count -gt 0) {
        [pscustomobject]@{ '程式' = $process.ProcessName; 'PID' = $process.Id; '輸入法' = $loaded -join '、'; '視窗標題' = $process.MainWindowTitle }
    }
}
if ($rows) {
    $rows | Sort-Object '程式' | Format-Table -AutoSize
} else {
    Write-Host '目前沒有程式載入棒棒注音或微軟注音（程式要在輸入框裡打過字才會載入）。'
}
