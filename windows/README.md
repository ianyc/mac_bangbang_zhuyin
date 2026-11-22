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

## 開發版註冊（在自己的電腦上試用）

```powershell
windows\scripts\build.ps1                    # 先建置
windows\scripts\register.ps1                 # 部署到 windows\out\deploy 並註冊（第一次要按 UAC）
windows\scripts\register.ps1 -Install        # 改安裝到 Program Files\BangbangZhuyin 並註冊（每次都要按 UAC）
windows\scripts\register.ps1 -Unregister     # 從鍵盤清單移除、取消註冊，並刪除 Program Files 裡的安裝
```

- 之後改了程式，重新建置再執行 `register.ps1` 就好：只會更新檔案，不用再按 UAC（註冊的位置或類別改變時才會再跳出）。已經開著的程式要重新啟動才會用到新版；Chrome、Edge 關掉視窗後常常還有背景程序，要整個結束（或重新開機）。檔案總管、開始功能表搜尋要登出或重新開機。
- 用 Win + 空白鍵切換到「棒棒注音」。
- 開發版放在 `windows\out\deploy`（不納入 git；刪掉 `out` 資料夾後要重新執行 `register.ps1`）。腳本會開放唯讀權限給 Microsoft Store 程式（沙盒）。
- 不放在 `%LOCALAPPDATA%`：從 MSIX 封裝程式（例如 Claude 桌面版的終端機）裡執行時，新建在 AppData 的檔案會被轉存到那個程式的私有資料夾（`%LOCALAPPDATA%\Packages\<程式>\LocalCache`），其他程式找不到 DLL，只好改用微軟注音。
- `-Install` 的位置和正式安裝相同。只有放在這種一般使用者改不了的位置，DLL 才會註冊 SECUREMODE 類別（允許在登入畫面這類以系統身分執行的地方載入）；放在使用者資料夾時註冊它，等於讓任何程式都能換掉 DLL 來取得系統權限。之後不加 `-Install` 執行，會改回註冊開發版。

## 選字記憶

- 記在 `%APPDATA%\BangbangZhuyin\user-model.json`，格式和 Mac 版相同：Mac 的記憶檔可以直接複製過來。開著的程式不用重開，下次組字時就會重新載入。
- 所有程式共用：一個程式學到的，其他程式下次組字就用得到。刪掉這個檔案就是清除記憶。
- 旁邊的 `user-model.json.lock` 是多個程式同時寫入時排隊用的，可以不用理它。
- Microsoft Store 的沙盒程式（開始功能表搜尋、計算機等）讀不到記憶檔：在那裡學到的選字，只在程式開著時有效。

## 測試程式 testpad

```powershell
windows\out\build\x64\testpad\Debug\testpad.exe              # 手動測試視窗：上面是 RichEdit（TSF），下面是 EDIT（IMM32 相容層）
windows\out\build\x86\testpad\Debug\testpad.exe              # 32 位元版，測 32 位元程式的情況
windows\out\build\x64\testpad\Debug\testpad.exe --selftest   # 自動測試：模擬按鍵，比對輸入法打出的字與引擎結果
windows\out\build\x64\testpad\Debug\testpad.exe --render-candidates <資料夾>   # 把候選窗樣本畫成 PNG，檢查外觀
windows\out\build\x64\testpad\Debug\testpad.exe --time-candidates window      # 量候選窗第一次、第二次顯示花的時間
windows\out\build\x64\testpad\Debug\testpad.exe --time-candidates steps       # 把第一次顯示拆成各步驟量時間（加 software 改用軟體繪製）
windows\out\build\x64\testpad\Debug\testpad.exe --time-candidates window capture <PNG 檔>   # 另外擷取螢幕上的候選窗
```

- 自動測試只在 testpad 自己的視窗裡啟用棒棒注音；需要先執行 `register.ps1`。也會檢查候選窗（是否顯示、位置、不搶焦點、UI 元件內容）、UI-less 模式、中英切換與跨程式的選字記憶。
- 測試時輸入法用暫存的記憶檔，不會動到你的選字記憶，測完自動刪掉。
- `--time-candidates` 要每次在新的程式裡執行，量到的才是「新開的程式第一次選字」；它用的是和輸入法同一份候選窗程式。
- 中英狀態是所有程式共用的：測試中會暫時切換它（其他程式也會跟著變），測完還原成測試前的狀態。
- 按鍵要送到輸入法，測試視窗必須在前景。剛操作過電腦時，Windows 不讓背景程式搶焦點，這時按鍵測試會全部標成「略過」：請在自己的終端機執行，跑的時候不要動鍵盤滑鼠。

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
