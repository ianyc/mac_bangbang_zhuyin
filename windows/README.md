# 棒棒注音 Windows 版（開發中）

macOS 版「棒棒注音」的 Windows 11 版本：TSF 輸入法，選字引擎與 Mac 版邏輯相同。

- 開發計畫與進度：[docs/WINDOWS_PLAN.md](../docs/WINDOWS_PLAN.md)
- 行為規格與引擎原理：[docs/DESIGN_AND_PORTING.md](../docs/DESIGN_AND_PORTING.md)

## 建置

需要 Visual Studio 2026，安裝時勾選「使用 C++ 的桌面開發」，會一併裝好 Windows 11 SDK 和 CMake。第一次設定時，CMake 會下載 GoogleTest。

```powershell
windows\scripts\build.ps1                    # x64 與 x86 的 Debug，建置後跑測試
windows\scripts\build.ps1 -Config Release    # Release 版
windows\scripts\build.ps1 -Arch x64 -NoTest  # 只建置 x64，不跑測試
```

也可以用 Visual Studio 的「開啟資料夾」打開 `windows/`，它會讀取 `CMakePresets.json`。

建置產物在 `windows/out/build/<x64|x86>/`，不納入 git。

## 開發工具 bangbang-tool

指令與 Mac 版相同。沒有指定 `--lm` 時，使用 repo 的 `build/bangbang-v3.lm`。

```powershell
$t = 'windows\out\build\x64\tool\Release\bangbang-tool.exe'
& $t convert ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ          # 注音轉中文
& $t lookup ㄉㄜ˙                                  # 某讀音的候選字與分數
& $t score 越 更新 越 難 用                         # 某種斷詞的分數明細
& $t reading 越更新越難用                           # 中文轉注音
& $t eval Tests\eval\sentences.txt                 # 跑測試句（加 --timing 顯示選字時間）
& $t keys "5j/ jp6<down><pick:聞><enter>"           # 模擬按鍵（大千鍵位）
```

在 PowerShell 裡看中文輸出前，先執行 `[Console]::OutputEncoding = [Text.Encoding]::UTF8`。

三組正確率評測（應與 Mac 版相同：99.4%、82.8%、82.1%）：

```powershell
& $t eval Tests\eval\sentences.txt --show 10
& $t eval build\heldout.txt --limit 8000 --show 0
& $t eval build\news-test.txt --limit 8000 --show 0
```

## 目錄

```
engine/        選字引擎（不依賴 TSF，可單獨測試）← 對應 Sources/BangbangEngine/
tip/           TSF 外殼 BangbangZhuyin.dll ← 對應 Sources/BangbangZhuyin/
tool/          bangbang-tool.exe：評測、除錯、模擬按鍵 ← 對應 Sources/bangbang-tool/
tests/         單元測試（GoogleTest）
scripts/       建置、註冊、安裝腳本
third_party/   nlohmann/json 3.12.0（MIT）
```

模型檔 `build/bangbang-v3.lm` 和評測句沿用 repo 根目錄的檔案，和 Mac 版共用。
