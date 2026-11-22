# 棒棒注音 Windows 版開發計畫

> 這份文件記錄 Windows 版「要怎麼做、做到哪裡算完成」。行為規格以 [DESIGN_AND_PORTING.md](DESIGN_AND_PORTING.md)（以下稱「設計文件」）第 3 節與 37 個單元測試為準，這裡不重複。
>
> - 建立：2026-11-20
> - 狀態：第 0～2 階段完成（2026-11-20）。第 3 階段程式完成、自動測試通過，等你在記事本驗收（2026-11-21）。

## 進度

| 階段 | 內容 | 狀態 |
|---|---|---|
| 0 | 環境與骨架 | 完成（2026-11-20）：x64／x86 都能編譯；GUID 已固定在 `windows/tip/Guids.h` |
| 1 | 引擎移植＋單元測試 | 完成（2026-11-20）：37 個移植測試＋21 個 Windows 新增測試，x64／x86、Debug／Release 全部通過 |
| 2 | 真模型＋bangbang-tool＋正確率對齊 | 完成（2026-11-20）：三組正確率與 Mac 版相同，見 3 節第 2 階段的「實測結果」 |
| 3 | 最小 TSF 輸入法（記事本能打字） | 完成（2026-11-21 驗收）：DLL、註冊腳本、testpad 完成；TSF 自動測試 x64／x86 各 20 項通過；記事本、設定、開始功能表搜尋、Claude、Chrome 都能用。開發版改部署到 `windows\out\deploy`（見實作紀錄的 MSIX 轉存問題） |
| 4 | 候選窗 | 完成（2026-11-22 驗收）：候選窗、位置、UI-less 完成；單元測試 x64／x86 各 60 項通過；testpad x64／x86 各 30 項通過（含 9 項候選與 UI-less）；記事本、Chrome、Claude 手動測試通過 |
| 5 | 中英切換（單按 Shift）＋工作列圖示 | |
| 6 | 選字記憶存檔（多程序）＋程式相容性 | |
| 7 | 安裝、解除安裝與更新 | |

---

## 1. 決策

| 項目 | 決定 | 說明 |
|---|---|---|
| 中英切換 | **只做「單按 Shift」** | 使用者決定（2026-11-20）。Caps Lock 維持一般的大寫鎖定；設計文件 8.5 的 Caps Lock 方案不做 |
| 程式碼位置 | 本 repo 的 `windows/` | 和 Mac 版共用 `data/`、`Tests/eval/`、`build/` 的模型檔；Mac 版程式不動 |
| CPU 架構 | x64＋x86 | 32 位元程式會載入 32 位元的 DLL。ARM64 不做 |
| 語言與工具 | C++20、CMake、MSVC（Visual Studio Community 2026） | TSF 輸入法是載入到每個程式裡的 COM DLL，不適合用 .NET；C++ 也最接近微軟的 SampleIME |
| 沿用 | 模型檔 `bangbang-v3.lm`、記憶檔 `user-model.json` 的格式、`data/` | 格式跨平台（設計文件 4.7、4.8） |
| 模型來源 | 先用從 Mac 複製來的 `build/bangbang-v3.lm` | 已驗證完整（90,170,496 bytes，與格式算出的大小一致）。在 Windows 上建模型列為之後再做 |
| 安裝方式 | 自用先用 PowerShell 腳本；要分享給朋友時再做安裝程式 | |
| 不做 | 設計文件第 6 節標 ⛔ 的項目；標 ⏸ 的項目不主動做 | 例如自訂詞、刪除單筆記憶、授權不明的語料 |

---

## 2. 架構

### 2.1 目錄

```
windows/
  CMakeLists.txt、CMakePresets.json   x64／x86 × Debug／Release
  engine/        選字引擎（C++，不依賴 TSF，可單獨測試）← 逐檔對應 Sources/BangbangEngine/
    Bopomofo       注音音節、大千排列、拼注音
    LanguageModel  模型檔（file mapping）、讀音查詢、雙連詞、隔詞搭配
    Decoder        整句選字、候選字清單
    Composition    組字區、游標、釘選、括號配對
    InputSession   按鍵狀態機、標點、符號候選
    UserModel      選字記憶（演算法同 Mac）＋檔案儲存（多程序安全）
  tip/           TSF 外殼 BangbangZhuyin.dll ← 取代 Sources/BangbangZhuyin/
  tool/          bangbang-tool.exe：convert、lookup、score、reading、eval、keys
  tests/         單元測試（37 個移植＋Windows 新增）、按鍵情境測試
  testpad/       測試用小程式（內含 RichEdit，x64／x86 各一），方便除錯，也用來測 32 位元
  scripts/       build.ps1、register.ps1（開發用）、install.ps1、uninstall.ps1
  third_party/   nlohmann/json（MIT，單一標頭檔）
```

### 2.2 引擎（engine/）

- **以 Mac 版為準逐檔移植**：類別切分、函式名稱與參數都不變，包括單字 40／詞 12 的搜尋寬度、每節點 6 種狀態、組字上限 20 格、每頁 9 個、記憶上限 3 次、勝出邊際 4、單詞加分 0.8。
- **文字**：引擎內部用 UTF-8，組字區一格就是一個 Unicode 碼位。外殼要交給 TSF 時才轉成 UTF-16，同時把「第幾格」換算成 UTF-16 的位置。
  - 原因：詞庫有 1,023 個 BMP 以外的字，還有「𨑨迌」「𰻞𰻞麵」兩個詞，這些字在 UTF-16 裡各佔 2 個單位。
  - 詞庫裡沒有異體字選擇器，所以用碼位計數和 Swift 的字元計數結果一樣。
- **結果要和 Mac 版完全一致**：分數一律用 32 位元 float 計算，排序用 `std::stable_sort`，「取最大值」一律取第一個，走訪順序照 Mac 版。
- **介面**：`InputSession::handle(KeyEvent) → SessionOutput` 不變。另外加一個不改變狀態的 `wouldHandle(KeyEvent)`，給 TSF 的 OnTestKeyDown 用，並用測試保證它和 `handle` 回傳的 handled 一致。
- **模型檔**：用 `CreateFileMapping`／`MapViewOfFile` 唯讀對應，所有程式共用同一份實體記憶體。測試則用記憶體內建立的模型，同 Mac 版的 `serialize`。
- **選字記憶**：演算法、檔案格式（JSON 第 2 版、舊格式自動轉換）都和 Mac 版相同，Mac 的記憶檔可以直接複製過來用。儲存方式改成多程序安全，見 2.4。

### 2.3 TSF 外殼（tip/）

對應 Mac 版的做法：

- **註冊**（取代 Info.plist）：
  - 註冊 COM：InprocServer32，ThreadingModel = Apartment。
  - 用 `ITfInputProcessorProfileMgr::RegisterProfile` 註冊語言設定檔：LANGID 0x0404、名稱「棒棒注音」、圖示放在 DLL 資源裡。
  - 用 `ITfCategoryMgr` 註冊類別，同 SampleIME 的 8 個：TIP_KEYBOARD、DISPLAYATTRIBUTEPROVIDER、TIPCAP_UIELEMENTENABLED、TIPCAP_SECUREMODE、TIPCAP_COMLESS、TIPCAP_INPUTMODECOMPARTMENT、TIPCAP_IMMERSIVESUPPORT、TIPCAP_SYSTRAYSUPPORT。
  - 安裝時用 `InstallLayoutOrTip("0x0404:{CLSID}{設定檔 GUID}")` 加進使用者的鍵盤清單。依微軟規範，不直接寫登錄。
- **接按鍵**（取代 `handle(event)`）：`ITfKeyEventSink` 的 OnTestKeyDown 用 `wouldHandle` 判斷，OnKeyDown 呼叫 `handle`。Ctrl／Alt／Win 組合鍵一律交給應用程式。
- **組字底線**（取代 `setMarkedText`）：在 edit session 裡 `StartComposition`，設定文字與游標位置。
  - 顯示屬性由 `ITfDisplayAttributeProvider` 提供：整段單底線，正在選字的那段粗底線。
  - `bAttr` 設成 TF_ATTR_INPUT／TF_ATTR_TARGET_CONVERTED，讓透過 IMM32 的舊程式也能正確顯示。
- **送出**（取代 `insertText`）：把文字放進組字範圍，再 `EndComposition`。
- **畫面更新**：
  - 按鍵處理的 edit session 用 `TF_ES_ASYNCDONTCARE` 加 `TF_ES_READWRITE`。SampleIME 註明 Word 不能用同步模式。
  - edit session 執行時，先送出累積的待送文字，再把引擎「目前的」組字狀態整個畫上去。這樣即使是非同步執行，畫面也不會亂。
- **組字被應用程式中斷**：收到 `ITfCompositionSink::OnCompositionTerminated` 時重設引擎。
- **切換應用程式或輸入法時送出組字區**：在 `ITfThreadMgrEventSink::OnSetFocus` 與停用（Deactivate）時處理。
- **候選窗**（取代 `NSPanel`）：自己畫的頂層視窗。
  - 視窗樣式：WS_POPUP，加上 WS_EX_TOPMOST、WS_EX_TOOLWINDOW、WS_EX_NOACTIVATE；WM_MOUSEACTIVATE 回傳 MA_NOACTIVATE，不搶焦點。
  - 擁有者視窗用 `ITfContextView::GetWnd`，失敗時改用 GetFocus。
  - 用 DirectWrite 繪製，字型為微軟正黑體 20pt，跟隨系統深淺色，Windows 11 圓角。
  - 依所在螢幕的 DPI 調整大小，但不更改宿主程式的 DPI 設定（微軟規範）。
  - 顯示、隱藏、內容改變時送出 `EVENT_OBJECT_IME_SHOW`／`HIDE`／`CHANGE`。
- **候選窗位置**（取代 `attributes(forCharacterIndex:)`）：
  - 用 `ITfContextView::GetTextExt` 取位置，並用 `ITfTextLayoutSink` 等版面就緒；拿不到時退回系統游標位置。
  - 放在游標那一行下方，放不下就改放上方，不超出螢幕工作區。
- **輸入法選單**（Mac 版只顯示版本號）：改成工作列的「中／英」圖示，用 `ITfLangBarItemButton` 加 `GUID_LBI_INPUTMODE`。
  - 左鍵點一下切換中英，右鍵選單顯示版本號。
  - 圖示依微軟規範做成 DLL 資源裡的 ICO，尺寸 16 到 48，黑白兩色。
- **嗶聲**（取代 `NSSound.beep`）：`MessageBeep(MB_OK)`。

其他要點：

- **密碼欄等不能輸入的地方**：`GUID_COMPARTMENT_KEYBOARD_DISABLED` 或 `GUID_COMPARTMENT_EMPTYCONTEXT` 成立時，按鍵全部交還。SampleIME 這段的判斷有錯，不照抄。
- **絕對不能讓宿主程式當掉**：
  - COM 邊界攔下所有例外。
  - 模型載入失敗時，按鍵全部交還，就像 Mac 版的 session 是 nil。
  - DllMain 不做重活，模型在第一次啟用時才載入。
- **執行階段靜態連結（/MT）**：不需要另外安裝 VC++ 可轉散發套件，帶到朋友的電腦上也能直接用。

### 2.4 選字記憶的儲存（多程序）

- **位置**：`%APPDATA%\BangbangZhuyin\user-model.json`，對應 Mac 的 `~/Library/Application Support/BangbangZhuyin/`。
- **多程序同時使用**：每個程式都會載入同一個 DLL、各有一份引擎狀態，可能同時寫入記憶檔。
  - 寫入：鎖檔（`LockFileEx`）→ 重新讀檔並合併 → 寫暫存檔 → 取代原檔 → 解鎖。不會互相覆蓋，也不會寫壞。
  - 讀取：開始組字前檢查檔案的修改時間，被別的程式更新過就重新載入。A 程式學到的，B 程式下一次打字就用得到。
  - 載入要輕：每個程式都會載入一次記憶，所以不建整份詞表的文字索引。
- **封裝程式（MSIX）的 AppData 虛擬化**（2026-11-21 發現並實測確認）：Claude、Windows 11 記事本都是封裝成 MSIX 的完整信任程式。這類程式（和它們開出來的子程式）在 AppData 新建的檔案，會轉存到 `%LOCALAPPDATA%\Packages\<程式>\LocalCache` 底下的私有位置，其他程式看不到；第 3 階段的開發版部署就是因此只有 Claude 看得到（見第 3 階段的實作紀錄）。所以在這些程式裡學到的選字，寫不進共用的記憶檔，記憶會分散在各程式的私有資料夾。可能的做法：記憶檔改放在不受虛擬化影響的位置（例如不在 AppData 的使用者資料夾），或交給一個不在封裝環境裡的小程式（例如 out-of-proc COM 伺服器）代寫。
- **Microsoft Store 程式（AppContainer）**：
  - 模型放在 Program Files，預設就讀得到。
  - 記憶檔在使用者資料夾，沙盒裡可能讀寫不到，所以讀寫失敗一律安靜略過，不能當掉。
  - 要不要開放唯讀權限、讓 Store 程式也套用記憶，第 6 階段實測後再決定。

### 2.5 中英切換（單按 Shift）

- **切換條件**：Shift 按下再放開，中間沒有按其他鍵，才切換。切換到英文之前，先送出組字區。
- **英文模式**：按鍵全部交給應用程式，Caps Lock 與 Shift＋字母都照系統原本的行為。
- **實作**：
  - 用 preserved key：VK_SHIFT 加 TF_MOD_ON_KEYUP。
  - 官方文件沒有保證「中間按了別的鍵就不會觸發」，所以要像 SampleIME 一樣自己追蹤。
  - 按住 Shift 用滑鼠點選（選取文字）也不能切換，實作時要處理。
  - 中英狀態存在 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`。這樣系統的輸入法開關熱鍵、或應用程式自己關掉輸入法時，狀態都一致，工作列圖示也會跟著變。
- **待確認**（第 5 階段開始前問使用者）：中英狀態要所有程式共用一個，還是每個程式各自記住？

### 2.6 安裝位置

| 項目 | 位置 |
|---|---|
| 輸入法 DLL | `C:\Program Files\BangbangZhuyin\x64\BangbangZhuyin.dll`、`C:\Program Files\BangbangZhuyin\x86\BangbangZhuyin.dll` |
| 模型檔、授權 | `C:\Program Files\BangbangZhuyin\bangbang.lm`、`McBopomofo-LICENSE.txt` |
| 選字記憶 | `%APPDATA%\BangbangZhuyin\user-model.json`（解除安裝時保留） |

- Program Files 預設允許 Store 程式讀取，DLL 和模型放在這裡就不用另外調整權限。這也是微軟 IME 規範的建議。
- 開發時直接從建置資料夾註冊 DLL，但只有一般桌面程式能用；Store 程式的相容性用安裝版測。

---

## 3. 開發階段

每個階段結束時都要跑完該階段的驗證，你驗收之後再 commit。工作量標示為小、中、大。

### 第 0 階段：環境與骨架（小）

- 安裝 Visual Studio Community 2026：使用 C++ 的桌面開發、Windows 11 SDK、CMake。
- 建立 `windows/` 骨架、CMake presets（x64／x86 × Debug／Release）、測試框架（GoogleTest）、`.gitignore`。
- 產生並固定所有 GUID：CLSID、語言設定檔、顯示屬性、Shift 切換鍵。之後不再更改。
- **完成標準**：x64、x86 都能編譯，一個空的測試能通過。

### 第 1 階段：引擎移植＋單元測試（大）

- 逐檔移植 6 個引擎檔案。
- 37 個單元測試照原本的名稱與斷言移植。
- 新增測試：
  - `wouldHandle` 與 `handle` 一致。
  - 含 BMP 以外的字元時，UTF-16 位置換算正確。
  - 記憶檔與 Mac 版互相讀得懂。
- **完成標準**：x64、x86 的測試全部通過。

### 第 2 階段：真模型＋bangbang-tool＋正確率對齊（中）

- 用 file mapping 讀取 `build/bangbang-v3.lm`。
- 做 `bangbang-tool.exe`：convert、lookup、score、reading、eval、keys，指令格式同 Mac 版。
- 把設計文件 8.9 裡屬於引擎的項目寫成 `keys` 情境測試，用真模型自動跑。
- **完成標準**：
  - eval 三組結果：日常用語 156 句 99.4%；保留句 8000 句 82.8%／97.3%；新聞 8000 句 82.1%／97.3%。
  - 「越更新越難用」選字正確。
  - 速度不比 Mac 版慢（8000 句約 1.3 秒）。
- **選做**（需要你在 Mac 上跑兩行指令）：用 `eval --show 100000` 輸出 Mac 版所有的失敗句，和 Windows 版逐句比對，確認完全一致。
- **實測結果**（2026-11-20，Intel Core i5-12400，x64 Release）：

  | 測試 | Mac 版 | Windows 版 |
  |---|---|---|
  | 日常用語 156 句 | 99.4% | 99.4%（唯一錯的同樣是「慢慢地」→「慢慢的」） |
  | 保留句 8000 句，整句／逐字 | 82.8%／97.3% | 82.8%／97.25% |
  | 新聞 8000 句，整句／逐字 | 82.1%／97.3% | 82.1%／97.28% |

  - x86 版正確率完全相同。
  - 速度：保留句 8000 句 2.1 秒，其中整句選字每句 0.24 毫秒（新聞句較長，0.37 毫秒）。Mac 版的 1.3 秒是在 Mac 的硬體上量的，不能直接比較；每次按鍵的選字時間遠低於感覺得到的程度。
  - 為了速度做的調整（結果不變）：搜尋狀態放在連續記憶體、雙連詞與隔詞搭配的查表改成 inline、Release 版開啟連結時最佳化。

### 第 3 階段：最小 TSF 輸入法（大）

- 做出 DLL：COM 與 TSF 註冊、啟用／停用、接按鍵、組字底線、送出、焦點切換時送出。
- 開發用工具：建置腳本、以系統管理員身分註冊／取消註冊的腳本、替換使用中 DLL 的方法、testpad。
- **完成標準**（在記事本測試）：
  - 「中文（繁體，台灣）」的鍵盤清單裡看得到棒棒注音。
  - 能拼注音、整句選字，組字區有底線。
  - 游標移動、Backspace、Delete、Esc、Enter 送出、空白鍵進組字區、全形標點、Shift＋字母輸出半形英文，都符合規格。
  - Ctrl 組合鍵照常運作。
  - 反覆切換輸入法、切換視窗都不會當掉。
- **實作紀錄**（2026-11-21）：
  - 開發版部署在 `windows\out\deploy`（不納入 git），用 `windows\scripts\register.ps1` 部署與註冊（第一次、或位置與類別改變時要按 UAC），並開放唯讀權限給沙盒程式（ALL APPLICATION PACKAGES、ALL RESTRICTED APPLICATION PACKAGES）。`register.ps1 -Install` 改裝到 `Program Files\BangbangZhuyin`（和正式安裝相同的位置）。
  - 註冊的 TSF 類別：TIP_KEYBOARD、DISPLAYATTRIBUTEPROVIDER、TIPCAP_SYSTRAYSUPPORT、TIPCAP_COMLESS、TIPCAP_IMMERSIVESUPPORT、TIPCAP_UIELEMENTENABLED（宣告支援 UI-less，第 4 階段的候選窗必須照這個規則做）、TIPCAP_INPUTMODECOMPARTMENT（啟用時把 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 設成開啟、`INPUTMODE_CONVERSION` 設成中文；第 5 階段的中英切換要更新它們）。
  - TIPCAP_SECUREMODE 只有 DLL 放在 Program Files 或 Windows 資料夾時才註冊（`register.ps1 -Install`）：安全模式會在登入畫面這類以系統身分執行的地方載入輸入法，DLL 放在使用者改得動的資料夾就等於提權漏洞。安全模式下（`TF_TMAE_SECUREMODE`）選字記憶只放在記憶體。
  - IMMERSIVESUPPORT 是 Store 程式（AppContainer，例如開始功能表搜尋）載入輸入法的條件。沙盒程式裡的選字記憶先只放在記憶體。
  - 檢查哪個程式實際載入了哪個輸入法：`windows\scripts\which-ime.ps1`。
  - **新版記事本一直改用微軟注音，真正的原因是 MSIX 的 AppData 轉存**（2026-11-21 查明）：
    - Claude 桌面版是 MSIX 封裝程式，Claude Code 執行的指令與從它開出來的程式（testpad、regsvr32）都在 Claude 的封裝環境裡。開發版原本部署在 `%LOCALAPPDATA%\BangbangZhuyin\dev`，在封裝環境裡新建的 AppData 檔案會轉存到 `%LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\BangbangZhuyin\dev`，只有 Claude 自己看得到。
    - 所以只有 Claude 與 testpad 載入得到棒棒注音；記事本、設定、開始功能表搜尋等程式照註冊的路徑找不到 DLL，TSF 改用同語言的微軟注音（打出「月更新越南用」、出現微軟的選字框），設定的鍵盤清單也不列出棒棒注音（所以微軟注音的「移除」不能按）。
    - 使用者在 Claude 外面執行 `register.ps1 -Install`（Program Files）和 `register.ps1`（寫到真正的 AppData）之後，記事本、設定、開始功能表搜尋都正常，關掉「使用舊版的微軟注音」也正常。證據：從 Claude 裡寫進 `%LOCALAPPDATA%\BangbangZhuyin` 的測試檔出現在 Claude 的 LocalCache；同一個 DLL 路徑，記事本載入的是新版（映像大小 4,304,896），Claude 裡看到的是 12:31 的舊版。
    - 因此開發版改部署到 `windows\out\deploy`（D 槽，不會轉存）。從 Claude 裡寫 HKCU 也會轉存，例如替棒棒注音加的 `Tsf3Override` 旗標系統根本沒看到（已刪除）。
    - 從 Claude 裡用 `Start-Process -Verb RunAs` 開的系統管理員程序也在同一個封裝環境裡（2026-11-22 用記號檔確認），所以以前 regsvr32 註冊得到私有副本。清掉第 3 階段留下的 Program Files 安裝與 AppData 舊檔時，真正的 AppData 資料夾要在私有副本刪掉之後才看得到、刪得掉。
    - 第 6 階段要處理同一個問題：選字記憶檔在 `%APPDATA%`，輸入法在 Claude、記事本這些封裝程式裡寫入時，會各自轉存到它們的私有資料夾，記憶會分散。
    - 調查中補上的 UIELEMENTENABLED、INPUTMODECOMPARTMENT、SECUREMODE 都不是記事本問題的原因，但 SampleIME 都有註冊、之後的階段也用得到，保留。
  - 調查時確認、之後除錯可能用得到的事實：
    - 新版記事本（11.2607，WinAppSDK 2.7）自帶的 `riched20.dll` 透過 `Windows.UI.Text.Core`（CoreText，類別 `CTSF30Base`）接文字輸入；棒棒注音照樣以傳統 TIP 的方式載入它的行程。
    - 微軟新式輸入法有私有類別 `{3AF314A2-D79F-4B1B-9992-15086D339B05}`（中文另有 `{74769EE9-4A66-4F9D-90D6-BF8B7C3EB461}`）。「使用舊版的微軟輸入法」存在 `HKCU\Software\Microsoft\Input\TSF\Tsf3Override\{CLSID}` 的 `NoTsf3Override*`；微軟注音的舊版設定檔在 HKLM 是 `Enable = 0`、`ProfileFlags = 2`（`TF_RP_HIDDENINSETTINGUI`）。
    - msctf 的 TraceLogging provider `{4F6A3C95-B86C-59F7-D8ED-D5B0B6A683D6}`（要系統管理員才能錄）會記錄每個程式的 `InputSessionStarted`／`InputSessionFinalized`：用的是哪個 TIP、按了幾個鍵、其中幾個交給輸入法。DLL 載入失敗時事件裡不會寫原因。
    - 輸入法的除錯訊息（`OutputDebugString`）可以用 DBWIN 方式收集；啟用時會印出 `ActivateEx` 與 `GetActiveFlags` 的旗標。testpad、Chrome 都是 `0x0`、`0x80000000`（一般模式）。
  - 轉換模式 compartment：只在 `ActivateEx` 裡設定時，事後讀到的是 `VT_EMPTY`（開關是 1）；改成在 `ITfActiveLanguageProfileNotifySink::OnActivated` 再設定一次（同 SampleIME）後，testpad 讀到「開啟、中文」，兩處的 `SetValue` 都回傳 S_OK。有一次從 `Start-Process` 啟動的 testpad 仍讀到 `VT_EMPTY`，第 5 階段加上 compartment 事件通知時再追。
  - testpad 的按鍵測試需要測試視窗在前景。使用者剛操作過電腦時，Windows 的前景鎖定不讓背景程式搶焦點，20 項會全部標成「略過」；這時改由使用者在自己的終端機執行 testpad（從前景程式啟動的程式可以取得前景）。
  - 按鍵依掃描碼（實體位置）對應；Ctrl／Alt 組合鍵交還；Caps Lock 鎖定時英文直接交還，組字中按 Caps Lock 先送出組字區（同 Mac 版）。
  - `testpad --selftest`：在 RichEdit 上啟用棒棒注音，用 ITfKeystrokeMgr 模擬按鍵，逐鍵比對「吃／不吃」並比對文件文字與引擎結果。一般 EDIT 控制項走 IMM32 相容層，按鍵不經過 ITfKeystrokeMgr，無法這樣模擬，改用手動測試。
    - 測試視窗不在前景時（例如測試中點了別的視窗），RichEdit 沒有 TSF 焦點，按鍵不會交給輸入法；這種情況標成「略過」、結束代碼 3，不算失敗。
    - 另外檢查啟用後的輸入模式 compartment（開關、轉換模式）。`--probe-compartments` 會先印出執行緒與全域 compartment 的診斷資訊。
  - 候選窗還沒有（第 4 階段）：按 ↓ 時只會看到選字那段變成粗底線，選字要等第 4 階段。

### 第 4 階段：候選窗（中）

- 依 2.3 的做法實作候選窗，候選窗的操作照設計文件 3.4。
- 第 3 階段已註冊 UIELEMENTENABLED，所以候選窗要支援 UI-less：顯示前先用 `ITfUIElementMgr::BeginUIElement` 詢問，程式表示要自己畫（`pbShow = FALSE`）時就不顯示，改由程式透過 `ITfCandidateListUIElement` 取得候選。
- **完成標準**：
  - 設計文件 8.9 裡和候選有關的項目全部通過：打「中文輸入法」後游標在最後按 ↓ 只出現「法」、游標在「輸」前面按 ↓ 列出「輸入法」、目前選字排第一、把「中文」的「文」改成「聞」時「中」不變。
  - 符號候選與括號配對正常。
  - 候選窗位置正確、不搶焦點，深淺色與多螢幕 DPI 都正確。
  - 在記事本和 Edge 都測過。
- **實作紀錄**（2026-11-21）：
  - 引擎已經處理所有候選鍵（↓ 開啟、1–9、↑↓、空白／→／PageDown、←／PageUp、Enter、Esc），外殼只負責顯示。另外加了 Windows 版專用的 `highlightCandidate`／`chooseCandidate`，給 UI-less 操作與滑鼠點選用（單元測試確認結果和按鍵操作相同）。
  - `CandidateRenderer`：Direct2D＋DirectWrite 量測與繪製，比例照 Mac 版（列高 30、邊距 6、編號欄 22、候選字 20、編號 13、頁碼 11 DIP），字型微軟正黑體 UI。淺色／深色依「應用程式模式」，反白用系統強調色（字的黑白依亮度決定），高對比模式用系統色。testpad 的 `--render-candidates` 用同一份程式把樣本畫成 PNG 檢查外觀。
  - `CandidateWindow`：WS_POPUP＋TOPMOST／TOOLWINDOW／NOACTIVATE，WM_MOUSEACTIVATE 回 MA_NOACTIVATE；在宿主的 UI 執行緒建立、沿用該執行緒的 DPI 設定，依視窗（或每螢幕 DPI 程式所在螢幕）的 DPI 放大；Windows 11 圓角；顯示、隱藏、改變時送出 EVENT_OBJECT_IME_*。可以用滑鼠點選（Mac 版沒有，Windows 使用者習慣）。視窗類別在 DLL 被 FreeLibrary 卸載時取消註冊。d2d1／dwrite／dwmapi／shcore 延遲載入，沒開過候選窗的程式不會載入它們。
  - 位置：在 edit session 裡對正在選字的那段文字呼叫 `ITfContextView::GetTextExt`，放在下方，放不下改放上方，不超出工作區；拿不到時退回系統游標，再退回滑鼠位置。候選窗開著時註冊 `ITfTextLayoutSink`，版面改變（例如 Chrome 晚一步排好版）就重新定位。
  - UI-less：每次開候選先 `BeginUIElement`；程式回答自己畫（或以 `TF_TMAE_UIELEMENTENABLEDONLY` 啟用）就不顯示候選窗。`CandidateListUI` 實作 `ITfCandidateListUIElementBehavior`（數量、字串、反白、分頁、SetSelection／Finalize／Abort），內容變更時依差異回報 `TF_CLUIE_*` 並 `UpdateUIElement`，關閉時 `EndUIElement`。
  - testpad 新增 9 項候選測試：候選窗顯示、位置（不蓋住游標那一行）、不搶焦點、UI 元件內容和引擎逐項相同，以及扮演 UI-less 程式（`ITfUIElementSink` 回答自己畫）用 SetSelection／Finalize／Abort 操作。
  - 手動測試（2026-11-22）：記事本的候選項目全部正常。Chrome 與 Claude 一開始跳不出候選窗，原因是它們還掛著第 3 階段的舊 DLL（Chrome 關掉視窗後仍有背景程序）；完全關掉重開後正常。Chromium 的 `ITfUIElementSink` 會回答「請輸入法自己畫」。

### 第 5 階段：中英切換＋工作列圖示（小～中）

- 依 2.5 實作。開始前先確認 2.5 的待確認事項。
- 第 3 階段留下的已知問題：輸入法啟用時把 `INPUTMODE_CONVERSION` 設成中文（`SetValue` 回傳 S_OK），但 testpad 視窗在前景時，事後讀到的常是 `VT_EMPTY`；不在前景時則保持中文（2026-11-21 實測）。這一階段要加上 compartment 事件通知（`ITfCompartmentEventSink`），找出是誰清掉的、何時要重新設定，並把 testpad 的輸入模式檢查改回「轉換模式必須是中文」才算通過。
- **完成標準**：
  - 單按 Shift 會切換；Shift＋字母、按住 Shift 點選都不會切換。
  - 切換前會先送出組字區。
  - 工作列顯示「中」或「英」，點一下可以切換。
  - 英文模式完全不干擾按鍵。
  - 密碼欄不會出現組字。

### 第 6 階段：選字記憶存檔＋程式相容性（中）

- 依 2.4 實作多程序安全的存檔與重新載入。
- 逐一測試：記事本、Edge、Chrome、Office（有安裝的話）、Store 程式（開始功能表搜尋、設定）、Windows Terminal、VS Code 等 Electron 程式、32 位元程式（用 x86 版 testpad）。
- **完成標準**：
  - 在「我愛」後面選「妳」一次，下次預設「妳」；再選「你」一次，下次預設「你」。
  - 兩個程式同時學習，記憶不會互相覆蓋或損壞；A 程式學到的，B 程式用得到。
  - Mac 的記憶檔複製過來可以直接用。
  - 上面每個程式都能正常輸入，候選窗位置正確。

### 第 7 階段：安裝、解除安裝與更新（中）

- `install.ps1`：複製到 Program Files，註冊 x64 與 x86 的 DLL，加進鍵盤清單。
- `uninstall.ps1`：取消註冊、移除檔案，保留選字記憶。
- 更新流程：DLL 被使用中的程式鎖住時，先改名再放上新版，並提示登出或重新開機。
- 補上 Windows 版的 README，並把 Windows 的決策補進設計文件第 6 節。
- **完成標準**：從乾淨的狀態安裝、使用、更新、解除安裝都正常；設計文件 8.9 的驗收清單全部打勾。

### 之後再做（可選）

- 給朋友用的安裝程式（例如 Inno Setup 或 WiX）與程式碼簽章。
- UI-less 模式：全螢幕遊戲裡由遊戲自己畫候選字。
- 觸控鍵盤（候選窗不能被觸控鍵盤蓋住）、無障礙（UI Automation）。
- 在 Windows 上建模型：移植 build-lm 與語料下載。
- ARM64。

---

## 4. 驗證方式

| 層次 | 怎麼驗證 | 何時 |
|---|---|---|
| 引擎行為 | 37 個單元測試＋Windows 新增的測試 | 每次修改 |
| 選字正確率 | `eval` 三組評測，和設計文件 5.1 比對 | 第 2 階段起，改到引擎就跑 |
| 按鍵情境 | `bangbang-tool keys` 情境測試（用真模型） | 第 2 階段起 |
| 實際應用程式 | 各階段的完成標準＋設計文件 8.9 的清單 | 第 3 階段起，每個階段 |
| 除錯 | Visual Studio 附加到記事本等程式；Debug 版輸出記錄，用 DebugView 看 | 隨時 |

---

## 5. 風險與對策

| 風險 | 對策 |
|---|---|
| DLL 出錯會讓宿主程式當掉 | COM 邊界攔下例外；模型載入失敗時按鍵全部交還；每個階段都在多個程式測 |
| 各程式的 TSF 行為不一致（Word 不能同步編輯；Chrome、Terminal、Electron 回報的位置） | 用非同步 edit session；拿不到位置時退回游標位置；第 3 階段起就在多個程式測 |
| 結果和 Mac 版有細微差異 | float、穩定排序；三組評測加上逐句比對 |
| Store 程式讀不到記憶檔 | 讀寫失敗安靜略過；第 6 階段決定是否開放唯讀權限 |
| 更新時 DLL 被使用中的程式鎖住 | 先改名再複製；提示登出 |
| 32 位元程式裡 90 MB 的檔案對應失敗 | 失敗時按鍵全部交還並記錄；第 6 階段實測 |
| 這台電腦是 Insider 版 Windows（build 26300） | 遇到奇怪的問題時，到正式版 Windows 上再確認 |
| 未簽章的 DLL 被防毒軟體或 SmartScreen 警告 | 自用可以接受；分享前再考慮簽章 |

---

## 6. 工作方式

- 預設在 `windows` 分支開發，每個階段驗收後 commit，commit 訊息用中文；里程碑完成後再合回 master。
- Mac 版的程式不動。如果要改引擎的行為，兩邊一起改，並更新設計文件。
- 每個階段完成時，更新本文件開頭的進度表。

---

## 7. 參考資料

- 設計文件：[DESIGN_AND_PORTING.md](DESIGN_AND_PORTING.md)，包含行為規格、引擎原理、模型檔格式。
- 微軟 IME 規範：<https://learn.microsoft.com/windows/apps/design/input/input-method-editor-requirements>（2026-10-02 更新）。重點：
  - 必須用 TSF，用 IMM32 做的輸入法已經被系統封鎖。
  - 要在 Store 程式裡使用，必須註冊 `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT`。
  - 工作列的模式圖示用 `GUID_LBI_INPUTMODE`。
  - 唯讀資料放在 Program Files。
  - 不要宣告 DPI 設定，但要支援每台螢幕各自的 DPI。
- `InstallLayoutOrTip`（input.dll）：<https://learn.microsoft.com/windows/win32/tsf/installlayoutortip>。
- 微軟 SampleIME（MIT 授權）：`microsoft/Windows-classic-samples` 的 `Samples/IME/cpp/SampleIME`。它以 Windows 8.1 為目標，只參考架構；如果改寫引用其中的程式碼，要附上授權聲明。
- PIME、新酷音 Windows 版：只參考做法，不複製程式碼。
