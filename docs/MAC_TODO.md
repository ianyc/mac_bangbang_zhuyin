# Mac 版待辦：比照 Windows 版的兩項調整

> 這份是給 Mac 版的 Claude Code 讀的工作說明（2026-11-23 建立）。
> Windows 版已經改好，使用者也實測過；Mac 版照同樣的規格改。Windows 版的程式在 `windows/` 資料夾，可以當作參考實作。
> 改完之後請刪掉這份檔案，或在最後面寫上完成日期。

## 背景

使用者在 Windows 上比照 Windows 11 的微軟注音，調整了幾個地方，其中兩項決定 Mac 版也一起改：

1. 標點符號的按法（全形標點改用 Control，Shift＋標點變成半形）
2. 拼注音時按 Backspace，刪掉最靠近游標的注音

Windows 版其他的調整是 Windows 專屬的，**Mac 版不用改**：

- 候選窗改成微軟注音的樣子
- 組字區上限 30 格
- 單按 Shift 切換中英時不送出組字區
- 工作列圖示

---

## 1. 標點符號改成微軟注音的按法

### 規格

**全形標點**：按著 Control（⌃）打。打出後和現在的全形標點一樣先進組字區，按 Enter 才送出；按 ↓ 有符號候選。

| 按鍵 | 打出 | 按 ↓ 的候選（依序） |
|---|---|---|
| ⌃ + , | ， | ， 、 , ＜ < 〈 《 |
| ⌃ + . | 。 | 。 . ． ＞ > 〉 》 |
| ⌃ + ; | ； | ； ： : ; |
| ⌃ + ' | 、 | 、 ' ； ＂ " “ ” ‘ ’ |
| ⌃ + [ | 【 | 【 「 『 [ { ［ ｛ 〔 |
| ⌃ + ] | 】 | 】 」 』 ] } ］ ｝ 〕 |

使用者指定的只有：「，」的第二個候選是「、」；「【」之後依序是「「」「『」；「】」之後依序是「」」「』」。其他順序沒有要求。

**半形符號**：沒有候選字。沒有組字時直接送出；組字中按的話，和 Shift＋字母一樣放進組字區，跟著句子一起送出。

| 按鍵 | 打出 | 原本（Mac 版現在） |
|---|---|---|
| Shift + , | < | ， |
| Shift + . | > | 。 |
| Shift + / | ? | ？ |
| Shift + ; | : | ： |
| Shift + ' | " | ； |
| Shift + 1 | ! | ！ |
| Shift + 9 | ( | （ |
| Shift + 0 | ) | ） |
| Shift + ` | ~ | ～ |
| ' | ' | 、 |
| [ | [ | 「 |
| ] | ] | 」 |

**不變**：` = \ 是半形；Shift + 2～8 是 @ # $ % ^ & *；Shift + - 是 _；Shift + = 是 +；Shift + \ 是 |；Shift + [ ] 是全形的『』。

### 要改的地方

- `Sources/BangbangEngine/InputSession.swift`
  - `Punctuation.unshifted`、`Punctuation.shifted`：換成上表的新對應。
  - 新增 `Punctuation.control`：`,` → ，、`.` → 。、`;` → ；、`'` → 、、`[` → 【、`]` → 】。
  - `Punctuation.groups`：
    - 「，」那一組改成 `["，", "、", ",", "＜", "<", "〈", "《"]`。
    - 左括號那一組改成 `["【", "「", "『", "[", "{", "［", "｛", "〔"]`。
    - 右括號那一組改成 `["】", "」", "』", "]", "}", "］", "｝", "〕"]`。
  - `Punctuation.alternatives(for:)`：先找「以這個符號開頭」的那一組，沒有才找第一個含有它的組。因為「、」現在也出現在「，」那一組，但「、」要用它自己那一組。
  - `KeyEvent` 要能表示 Control，例如加一個 `case control(Character)`。
    - `handle` 收到時，查 `Punctuation.control`，查得到就 `insertLiteral`，查不到就交還（handled: false）。
    - 判斷會不會處理某個鍵的地方（如果有，像 Windows 版的 `wouldHandle`）也要同步。
- `Sources/BangbangZhuyin/InputController.swift` 的 `handle(_:client:)`
  - 現在只要有 Command／Control／Option 就 `return false`。
  - 改成：只按著 Control（沒有 Command、Option、Shift）＋上面六個鍵時，交給 session 處理；其他組合鍵照舊交還給 App。
  - 開著 Caps Lock（直接打英文）時，現在一律 `return false`。Windows 版是不分中英模式都打得出全形標點。Mac 版有兩種做法，請先問使用者選哪一種：
    - Caps Lock 時按 ⌃＋標點，直接送出全形標點（`insertText`，不進組字區、沒有候選）。
    - 比照 Windows 版，先進組字區。
- `Sources/BangbangZhuyin/KeyMapping.swift`：`keyEvent(from:)` 用 keyCode 判斷是哪個實體按鍵。按著 Control 時 `event.characters` 可能是控制字元，所以一樣要用 keyCode 判斷。

### 要注意的地方（請和使用者確認）

- 在「終端機」裡，⌃[ 等於 Esc，用 vim 的人常用。輸入法啟用時，這個鍵會被吃掉變成「【」。
- ⌃, ⌃. 等鍵在某些 App 有自己的快捷鍵，也會被蓋過。Windows 版使用者接受了這個取捨。

### 測試（`Tests/BangbangEngineTests/BangbangEngineTests.swift`）

這幾個測試是照舊按法寫的，要改：

- `testFullWidthPunctuationEntersComposition`：Shift+, 改成 ⌃+,，並把 `'`、`[` 改成 ⌃+'、⌃+[。預期送出「，。、【」之類（照新按法調整）。
- `testSymbolCandidates`：改用 ⌃+; 打出「；」，候選是 `["；", "：", ":", ";"]`。
- `testSymbolCandidatesInsideSentence`：句中的全形逗號改用 ⌃+,。

`testBracketPairChangesTogether` 用的是 Shift+[ 的『』，按法沒變，不用改。

可以照抄的 Windows 版測試，在 `windows/tests/WindowsTests.cpp` 的 `WindowsPunctuationTests` 四項：

- 半形符號直接送出
- ⌃＋標點進組字區
- 候選順序
- 組字中打半形符號

Windows 版的實作可以參考：

- `windows/engine/Punctuation.cpp`：`Style::Windows` 的表、`groups`、`alternatives`。
- `windows/engine/InputSession.cpp`：`handleCharacter` 的 ctrl 分支、`characterWouldBeHandled`。

---

## 2. 拼注音時的 Backspace：刪最靠近游標的注音

### 規格

拼字區有注音時按 Backspace，刪掉**顯示在最後面**（最靠近游標）的那個符號，順序是韻母 → 介音 → 聲母，不再是「最後輸入的那個」。

使用者回報的例子：

1. 先打 ㄏㄨ，在打聲調之前再打 ㄈ。ㄈ 會取代 ㄏ，拼字區變成 ㄈㄨ。
2. 這時按 Backspace：要刪掉 ㄨ，剩下 ㄈ。現在 Mac 版刪的是 ㄈ（最後輸入的），剩下 ㄨ。

### 要改的地方

- `Sources/BangbangEngine/Bopomofo.swift` 的 `BopomofoComposer`：
  - 拿掉記錄輸入順序的 `order`。
  - `backspace()` 改成：有韻母就清韻母，否則有介音就清介音，否則清聲母。
- 參考實作：`windows/engine/Bopomofo.cpp` 的 `BopomofoComposer::backspace`。

### 測試

- `testComposerReplacesSlotAndBackspaces`：ㄅ、ㄚ、ㄇ（取代 ㄅ）之後是「ㄇㄚ」。按 Backspace 後預期改成「ㄇ」（原本是「ㄚ」），再按一次就變成空的。
- 可以照抄 `windows/tests/WindowsTests.cpp` 的 `BackspaceTests.DeletesComponentNearestTheCursor`：打 `cjz`（ㄏ ㄨ ㄈ），Backspace 後是「ㄈ」。

---

## 改完之後

- 更新 `docs/DESIGN_AND_PORTING.md`：
  - 3.2 節：Backspace 改成「刪掉最靠近游標的那個符號」。
  - 3.5 節：換成新的標點表，並補上 ⌃＋標點。
- 跑 `swift test`，並請使用者實際打字確認：上面兩張表的每個按鍵，以及 ㄏㄨ＋ㄈ 的 Backspace。
- 告訴使用者 Windows 版可以跟著整理，下次在 Windows 上處理：
  - Windows 引擎為了讓移植的測試照 Mac 舊規格跑，保留了 `Punctuation::Style::Mac`。
  - `windows/tests/EngineTests.cpp` 裡的移植測試也還是舊預期。
  - Mac 版改完後，這些都可以拿掉或改成新規格，讓兩邊的測試再對齊。

---

## 完成紀錄（Mac 版，2026-11-26）

兩項都已在 Mac 版 0.3.1 完成。和上面規格不同的地方：

- **全形標點的修飾鍵用 ⌥（Option），不是 ⌃**：使用者指定，避開終端機裡 ⌃[ 等於 Esc，以及 App 常用的 ⌃ 快捷鍵。
  - 引擎的按鍵事件是 `KeyEvent.option(Character)`，對應表是 `Punctuation.option`。其餘的表、候選分組、`alternatives` 的找法都照本文件。
  - 外殼只在「只按著 ⌥（沒有 Shift、⌘、⌃）＋這六個鍵」時交給引擎，其他 ⌥ 組合鍵（例如 ⌥← 跳一個字）照舊交還給 App。
  - 代價：棒棒注音啟用時，⌥＋這六個鍵原本的特殊符號（≤ ≥ … æ “ ‘）打不出來。
- **Caps Lock 鎖定大寫時按 ⌥＋標點**：使用者選了「直接送出全形標點」（`insertText`，不進組字區、沒有候選）。

驗證：

- 單元測試：改了 4 個照舊按法寫的測試；照抄 Windows 版新增 5 個（`WindowsPunctuationTests` 4 項、`BackspaceTests` 1 項）。共 42 個，全部通過；三組正確率不變（99.4%／82.8%／82.1%）。
- 用 `bangbang-tool keys` 搭配真模型，逐一確認上面兩張表的每個按鍵、候選順序，以及 ㄏㄨ＋ㄈ 的 Backspace。`keys` 新增 `<opt:,>`（⌥＋鍵）、`<shift:,>`（Shift＋鍵）的寫法。
- 設計文件 3.2、3.5、3.6、3.8 節與第 6 節的決策紀錄、README 都已更新。

**Windows 版之後可以整理**（下次在 Windows 上處理）：

- Mac 版的標點現在和 Windows 配置相同，只差修飾鍵（Mac ⌥、Windows Ctrl）。`Punctuation::Style::Mac` 可以拿掉，引擎預設改用新規格。
- `windows/tests/EngineTests.cpp` 裡從 Mac 移植的測試還是舊預期，可以照 Mac 版這次的修改對齊。Mac 版改的 4 個測試：`testComposerReplacesSlotAndBackspaces`、`testFullWidthPunctuationEntersComposition`、`testSymbolCandidates`、`testSymbolCandidatesInsideSentence`。
