# 棒棒注音（bunbun_zhuyin）

自用的 macOS 注音輸入法，目標是像 Windows 微軟新注音一樣好打，而且**選字要準**。

## 安裝

```bash
scripts/install.sh
```

第一次安裝後，到「系統設定 → 鍵盤 → 輸入方式 → 編輯… → ＋」，在「繁體中文」裡加入「棒棒注音」。
之後改了程式，再跑一次 `scripts/install.sh` 就會換成新版。

> 開發中的輸入法有可能出問題，請一直保留「ABC」輸入方式，隨時可以用 Caps Lock 切回去。

### 安裝到其他 Mac（DMG）

```bash
scripts/make-dmg.sh   # 產生 build/BunbunZhuyin-<版本>.dmg（Apple 晶片與 Intel 通用，macOS 13 以上）
```

在另一台 Mac 打開 DMG，把「棒棒注音」拖到旁邊的「Input Methods」資料夾，再照 DMG 裡的「安裝說明」登出、登入並加入輸入方式。

- DMG 會裝到 `/Library/Input Methods`（整台 Mac 共用）。開發用的這台已經用 `scripts/install.sh` 裝在 `~/Library/Input Methods`，不要再用 DMG 裝一份，否則會出現兩個棒棒注音。
- 目前是本機簽章、沒有經過 Apple 公證。用 AirDrop 或網路下載傳過去的檔案會被 Gatekeeper 擋下，要執行安裝說明裡的 `xattr` 指令解除；用隨身碟複製通常不會被標記。
- 選字記憶每台 Mac 各自獨立。

## 按鍵

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
| Shift + 字母 | 半形大寫英文 |
| Shift + , . / ; ' 1 | ，。？：；！（全形標點一律進入組字區，按 Enter 才送出） |
| ' [ ] / Shift + [ ] | 、「」『』 |
| Caps Lock | 系統的中英切換（ABC） |

手動選字後，選字位置**前面**的字就固定不變；後面的字會依新的上下文重新選。

選過的字詞會記在 `~/Library/Application Support/BunbunZhuyin/user-model.json`，下次在同樣的前後文會優先選它。

## 選字怎麼做的

1. **詞庫**：小麥注音的詞與讀音（`data/mcbopomofo/`，MIT 授權，快照版本見 `SOURCE.txt`）。
   - 補上一般人實際打的讀音：不要（ㄅㄨˋ）、一下（ㄧ）、我覺得（ㄉㄜ˙）。
   - 常用破音字的讀音比例自己調整：`data/reading-weights.txt`。
   - 簡體字、罕用異體字（非 Big5，以及 `data/discouraged-chars.txt` 列出的字，如「着」）仍可從候選字選，但不會被自動選中。
2. **語言模型**：用 PTT、中文維基與行政院即時新聞（約 2.2 億詞）統計
   - 雙連詞：前一個詞是什麼（解決「月更新越**南**用」→「越難用」）。
   - 隔一個詞的搭配：例如「越 ＿ 越」（解決「**月**更新越難用」→「越更新越難用」）。
3. **整句選字**：每次按鍵都對整串注音重新找出分數最高的斷詞組合。

### 正確率

| 測試 | 只用小麥詞頻 | 棒棒注音 |
|---|---|---|
| 日常用語 156 句（`Tests/eval/sentences.txt`）整句全對 | 80.1% | 99.4% |
| 語料保留未訓練的 8000 句，整句全對／逐字 | 62.3%／93.4% | 82.8%／97.3% |
| 行政院新聞（未訓練的那一半）8000 句，整句全對 | — | 82.0% |

評測時「他／她／它」「台／臺」視為相同。

## 開發

```bash
scripts/build-model.sh   # 重建語言模型並跑三組正確率評測（第一次會下載約 1.5 GB 語料到 corpus/）
scripts/build.sh         # 只建置 App
swift test               # 單元測試（按鍵行為、選字、模型檔）
```

`bunbun-tool` 的除錯指令：

```bash
.build/release/bunbun-tool convert --lm build/bunbun.lm ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ   # 注音轉中文
.build/release/bunbun-tool lookup  --lm build/bunbun.lm ㄉㄜ˙                              # 查某讀音的候選字與分數
.build/release/bunbun-tool score   --lm build/bunbun.lm 越 更新 越 難 用                   # 某種斷詞的分數明細
.build/release/bunbun-tool eval    --lm build/bunbun.lm Tests/eval/sentences.txt           # 跑測試句
.build/release/bunbun-tool keys    --lm build/bunbun.lm "5j/ jp6<down>2<enter>"           # 模擬按鍵（大千鍵位）
```

遇到選錯的句子，把它加進 `Tests/eval/sentences.txt`（需要時用「句子 | 注音」指定讀音），改完詞庫或參數後重跑 `scripts/build-model.sh` 確認沒有變差。

### 專案結構

```
Sources/BunbunEngine/   選字引擎（不依賴 AppKit）
  Bopomofo.swift        注音音節、大千排列、拼注音
  LanguageModel.swift   語言模型與模型檔格式
  Decoder.swift         整句選字（二階搜尋）、候選字清單
  Composition.swift     組字區：游標、插入刪除、手動選字
  InputSession.swift    按鍵狀態機、標點符號
  UserModel.swift       記住使用者的選字
Sources/BunbunZhuyin/   InputMethodKit 外殼：按鍵轉換、組字底線、候選字視窗
Sources/bunbun-tool/    建模型、評測、除錯、註冊輸入法
data/                   小麥注音詞庫快照、讀音比例
corpus/                 下載的語料（不納入 git）
build/                  模型與 App（不納入 git）
```

## 授權與資料來源

- 詞庫：[小麥注音 McBopomofo](https://github.com/openvanilla/McBopomofo)（MIT）
- 語料（只用來統計詞頻，不打包進輸入法）：[PTT-pretrain-zhtw](https://huggingface.co/datasets/yuhuanstudio/PTT-pretrain-zhtw)（Apache-2.0）、[wikipedia-zh-tw](https://huggingface.co/datasets/yuhuanstudio/wikipedia-zh-tw)（CC BY-SA 4.0）、[行政院即時新聞](https://www.ey.gov.tw/opendata/BF42D887A7B414E2/154)（政府資料開放授權條款）
