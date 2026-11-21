#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "LanguageModel.h"
#include "UserModel.h"

namespace bangbang {

/// 同一個程式裡所有輸入視窗共用的語言模型與選字記憶。
struct SharedEngine {
    std::shared_ptr<const LanguageModel> lm;
    std::unique_ptr<UserModel> userModel;
    /// 保護選字記憶：同一個程式可能有好幾個 UI 執行緒，各自有一個 TextService。
    std::mutex mutex;
    std::wstring modelPath;
};

/// 第一次呼叫時載入模型與選字記憶。找不到或讀不了模型時回傳 nullptr，這時輸入法不處理任何按鍵。
///
/// 模型檔的位置：DLL 旁邊的 bangbang.lm，或上一層資料夾的 bangbang.lm（安裝版是 x64\、x86\ 共用上一層的模型）。
/// 選字記憶：%APPDATA%\BangbangZhuyin\user-model.json。測試時可以用環境變數 BANGBANG_USER_MODEL 指定其他檔案；
/// 設成空字串表示只記在記憶體裡、不讀寫檔案。
///
/// secureMode：TSF 以安全模式啟用（登入畫面這類以系統身分執行的地方）時，不讀寫任何使用者的檔案，
/// 選字記憶只放在記憶體。同一個程式裡以第一次呼叫的值為準。
SharedEngine* sharedEngine(bool secureMode);

}  // namespace bangbang
