# 棒棒注音（bangbang_zhuyin）

一套在 macOS 上使用的注音輸入法，操作方式比照 Windows 的**微軟新注音**，並且把重點放在**選字要準**。

macOS 內建的注音輸入法有幾個讓人不順手的地方：Shift + 字母會打出全形英文、要把游標移到字的後面才能選字、整句選字常常選錯。棒棒注音從選字引擎到輸入法外殼都是自己寫的，選字用的語言模型由大量台灣中文文章統計而來，全程在本機執行、不需要網路。

- 版本：0.2.0
- 系統需求：macOS 13 以上，Apple 晶片與 Intel 皆可
- 鍵盤排列：標準大千

---

## 功能總覽

### 選字準確

- **整句選字**：每按一個鍵，就對整串注音重新找出最合理的斷詞與用字。後面打的字可以修正前面的選擇，例如打完「ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ」會得到「越更新越難用」，而不是「月更新越南用」。
- **看上下文**：同時考慮「前一個詞」和「隔一個詞」的搭配，像「越…越…」「又…又…」這類前後呼應的句型也能選對。
- **符合實際打字習慣**：「不要」可以打本調 ㄅㄨˋ、「一下」打 ㄧ、「我覺得」打 ㄉㄜ˙。也就是說，打字不必照變調讀音。
- **不會亂選簡體字和罕用字**：簡體字、罕用異體字（例如「着」）仍查得到，但不會被自動選中。

### 操作比照微軟新注音

- **游標在字前面就能選字**：用 ← → 把游標移到要改的字前面，按 ↓ 叫出從這個字開始的詞與單字。游標在最後時，只列出最後一個字。
- **目前的選字排第一筆**，其他候選依上下文排序。
- **改字不影響前面**：手動選字後，前面的字固定不變，後面的字會依新的上下文重新調整。
- **按 Enter 才送出**：打字、空白、全形標點都會留在組字區，可以隨時回頭修改，按 Enter 才一起送出。
- **Shift + 字母輸出半形英文**，不會變成全形。
- **全形標點**：Shift + , . / ; ' 1 分別打出「，。？：；！」，' [ ] 打出「、「」」。
- **符號候選字**：在符號上按 ↓，可以換成全形／半形，或同一個鍵上的另一個符號。例如「：」可以換成「:」「；」「;」，空白可以換成全形空白。

### 會記住你的選擇

- 手動選過的字詞會記下來，下次在同樣的前後文直接選它。例如把「後選自」改成「候選字」一次之後，下次就會直接出現「候選字」。
- 記憶存在本機的 `~/Library/Application Support/BangbangZhuyin/user-model.json`，重新安裝或更新版本都會保留。

### 隱私

- 選字、記憶全部在本機完成，輸入法**不會連網**，也不會傳送任何打字內容。
- 語料只在建立語言模型時使用，不會打包進輸入法。

---

## 安裝

### 方法一：DMG 拖曳安裝（一般使用）

1. 打開 `BangbangZhuyin-<版本>.dmg`，把「棒棒注音」拖到旁邊的「Input Methods」資料夾（會要求輸入密碼）。
2. 登出再登入一次。
3. 到「系統設定 → 鍵盤 → 輸入方式 → 編輯… → ＋」，在「繁體中文」裡加入「棒棒注音」。
4. 請保留「ABC」輸入方式，用 Caps Lock 切換中英文。

DMG 會裝到 `/Library/Input Methods`，這台 Mac 的所有使用者共用。

- **被系統擋下來的話**：目前是本機簽章、沒有經過 Apple 公證。用 AirDrop、網路下載或訊息傳來的檔案會被 Gatekeeper 擋下，請在「終端機」執行下面這行，再登出、登入一次：

  ```bash
  xattr -dr com.apple.quarantine "/Library/Input Methods/BangbangZhuyin.app"
  ```

- **從 0.1 版升級**：0.1 版的英文名稱是 BunbunZhuyin。0.2 版改名後，系統會把它當成新的輸入法。請先在系統設定移除舊的「棒棒注音」，把 `/Library/Input Methods/BunbunZhuyin.app` 丟到垃圾桶，再安裝新版。舊的選字記憶會自動搬過來。

### 方法二：從原始碼安裝（開發用）

需要 Xcode。第一次建置時，會下載約 1.5 GB 的語料來建立語言模型。

```bash
scripts/install.sh
```

這會建置並安裝到 `~/Library/Input Methods`，只有目前的使用者能用。之後改了程式，再跑一次就會換成新版。

開發用的 Mac 不要再用 DMG 裝一份，否則系統裡會出現兩個棒棒注音。

---

## 按鍵一覽

| 按鍵 | 行為 |
|---|---|
| 注音鍵（標準大千） | 拼注音；3 4 6 7 為聲調，空白鍵為一聲 |
| 空白鍵（沒有待拼注音時） | 在組字區加一個半形空白（按 Enter 才一起送出） |
| Enter | 送出組好的字 |
| ← → / Home / End | 移動游標，可回頭改字 |
| ↓ | 叫出候選字：游標在字**前面**時列出從這個字開始的詞（長詞在前）；游標在最後時只列出最後一個字。目前顯示的字詞一律排第一筆 |
| 候選窗中 1–9 / ↑↓ + Enter | 選字（選完游標會移到該詞後面） |
| 候選窗中 空白 / ← → | 換頁 |
| Esc | 清掉正在拼的注音；沒有注音時清掉整串 |
| Backspace / Delete | 刪除注音符號，或刪除游標前／後的字 |
| Shift + 字母 | 半形大寫英文 |
| Shift + , . / ; ' 1 | ，。？：；！（全形標點一律進入組字區，按 Enter 才送出） |
| ' [ ] / Shift + [ ] | 、「」『』 |
| 符號上按 ↓ | 換成該符號的全形／半形，或同一個鍵上的另一個符號 |
| 數字鍵盤 | 半形數字與運算符號 |
| Caps Lock | 系統的中英切換（ABC） |

---

## 選字是怎麼做的

棒棒注音用兩種資料來選字，兩者分工不同：

| | 詞庫 | 語料 |
|---|---|---|
| 是什麼 | 一本字典：有哪些詞、每個詞怎麼念 | 大量真實文章 |
| 決定什麼 | **打不打得出來** | **選哪一個** |
| 來源 | 小麥注音，約 16 萬個詞與讀音 | PTT、中文維基、行政院即時新聞，約 2.2 億詞 |

1. **詞庫**：小麥注音的詞與讀音快照（`data/mcbopomofo/`，版本見 `SOURCE.txt`），再加上棒棒注音自己的調整：
   - 補上一般人實際打的讀音（不要 ㄅㄨˋ、一下 ㄧ、我覺得 ㄉㄜ˙）。
   - 常用破音字單獨成字時的讀音比例：`data/reading-weights.txt`。
   - 台灣少用、不該被自動選中的字：`data/discouraged-chars.txt`。
2. **語言模型**：把語料用詞庫斷詞後，統計每個詞出現的頻率、「前一個詞是什麼」（雙連詞），以及「隔一個詞」的搭配。結果存成約 91 MB 的模型檔 `bangbang.lm`。這是傳統的統計語言模型，不是神經網路，在一般 Mac 的 CPU 上約 45 秒就能建好。
3. **整句搜尋**：打字時，對組字區裡所有可能的斷詞組合計算分數，挑出最高的一組，每次按鍵不到 1 毫秒。

### 正確率

| 測試 | 只用小麥詞頻 | 棒棒注音 |
|---|---|---|
| 日常用語 156 句（`Tests/eval/sentences.txt`），整句全對 | 80.1% | 99.4% |
| PTT／維基中未參與訓練的 8000 句，整句全對／逐字 | 62.9%／93.5% | 82.8%／97.3% |
| 行政院新聞中未參與訓練的 8000 句，整句全對／逐字 | 57.9%／93.2% | 82.1%／97.3% |

評測時「他／她／它」「台／臺」視為相同。

---

## 已知限制

- 還沒有「自訂詞」功能，詞庫裡沒有的新詞（例如新的人名、專有名詞）沒辦法整個詞一起打出來。
- 選字記憶只會增加，目前不能刪除單一筆記錯的記錄；要重來只能刪掉整個記憶檔。
- 還沒有偏好設定畫面，例如候選字大小、每頁數量、橫式或直式候選窗。
- 候選字視窗在部分 App（例如終端機類、Electron 類）的位置可能不準。
- 輸入法約佔 160 MB 記憶體。
- 快速連按 Caps Lock 時偶爾切換不過去，這是系統切換輸入方式的限制。
- 本機簽章、未經 Apple 公證，在其他 Mac 上可能需要手動解除 Gatekeeper 限制。

---

## 開發

### 常用指令

```bash
scripts/install.sh         # 建置並安裝到 ~/Library/Input Methods
scripts/build.sh           # 只建置 App（UNIVERSAL=1 時編成 Apple 晶片與 Intel 通用版）
scripts/make-dmg.sh        # 產生 build/BangbangZhuyin-<版本>.dmg
scripts/build-model.sh     # 重建語言模型，並跑三組正確率評測
scripts/download-corpus.sh # 下載語料到 corpus/（build-model.sh 會自動呼叫）
swift test                 # 單元測試：按鍵行為、選字、符號、模型檔
```

發布新版本時，先修改 `Resources/Info.plist` 的 `CFBundleShortVersionString`（顯示的版本號）與 `CFBundleVersion`（內部編號），再執行 `scripts/make-dmg.sh`。

### 除錯工具 `bangbang-tool`

```bash
T=.build/release/bangbang-tool
$T convert --lm build/bangbang.lm ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ   # 注音轉中文
$T lookup  --lm build/bangbang.lm ㄉㄜ˙                              # 查某讀音的候選字與分數
$T score   --lm build/bangbang.lm 越 更新 越 難 用                   # 某種斷詞的分數明細
$T eval    --lm build/bangbang.lm Tests/eval/sentences.txt           # 跑測試句
$T keys    --lm build/bangbang.lm "5j/ jp6<down>2<enter>"           # 模擬按鍵（大千鍵位）
```

遇到選錯的句子，把它加進 `Tests/eval/sentences.txt`（需要時用「句子 | 注音」指定讀音），改完詞庫或參數後重跑 `scripts/build-model.sh`，確認正確率沒有變差。

### 專案結構

```
Sources/BangbangEngine/      選字引擎（不依賴 AppKit，可單獨測試）
  Bopomofo.swift             注音音節、大千排列、拼注音
  LanguageModel.swift        語言模型與模型檔格式
  Decoder.swift              整句選字（二階搜尋）、候選字清單
  Composition.swift          組字區：游標、插入刪除、手動選字
  InputSession.swift         按鍵狀態機、標點與符號候選
  UserModel.swift            記住使用者的選字
Sources/BangbangZhuyin/      InputMethodKit 外殼：按鍵轉換、組字底線、候選字視窗
Sources/bangbang-tool/       建模型、評測、除錯、註冊輸入法
Tests/BangbangEngineTests/   單元測試
Tests/eval/sentences.txt     日常用語選字測試句
Resources/                   Info.plist、多語系名稱、DMG 安裝說明
data/                        小麥注音詞庫快照、讀音比例、少用字清單
scripts/                     建置、安裝、打包、下載語料
corpus/                      下載的語料（不納入 git）
build/                       模型、App、DMG（不納入 git）
```

---

## 版本紀錄

### 0.2.0（2026-11-01）

- 英文名稱由 BunbunZhuyin 改為 BangbangZhuyin，舊的選字記憶會自動搬移。
- 在符號上按 ↓，可以換成全形／半形或同一個鍵上的其他符號，也可以把空白換成全形空白。
- DMG 打包修正偶爾出現的「裝置已經沒有空間」錯誤。

### 0.1.0（2026-10-29）

- 第一版：標準大千、整句選字（雙連詞＋隔詞搭配）、候選字視窗、記住使用者選字。
- 依實測回饋：空白鍵留在組字區、游標在最後只列出最後一個字、選字後前面的字不變。
- 目前的選字排在候選第一筆、全形標點一律進入組字區。
- 加入行政院即時新聞語料，並降低「着」等少用異體字的優先順序。
- DMG 拖曳安裝，Apple 晶片與 Intel 通用。

---

## 授權與資料來源

- 詞庫：[小麥注音 McBopomofo](https://github.com/openvanilla/McBopomofo)（MIT），授權聲明隨輸入法附上（`McBopomofo-LICENSE.txt`）。
- 語料（只用來統計詞頻，不打包進輸入法）：
  - [PTT-pretrain-zhtw](https://huggingface.co/datasets/yuhuanstudio/PTT-pretrain-zhtw)（Apache-2.0）
  - [wikipedia-zh-tw](https://huggingface.co/datasets/yuhuanstudio/wikipedia-zh-tw)（CC BY-SA 4.0）
  - [行政院即時新聞](https://www.ey.gov.tw/opendata/BF42D887A7B414E2/154)（政府資料開放授權條款）
