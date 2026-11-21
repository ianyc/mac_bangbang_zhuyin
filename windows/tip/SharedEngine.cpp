#include "SharedEngine.h"

#include <windows.h>
#include <shlobj.h>

#include <optional>
#include <vector>

#include "Globals.h"

namespace bangbang {

namespace {

std::vector<std::wstring> modelCandidates() {
    const std::wstring dir = moduleDirectory();
    return {dir + L"\\bangbang.lm", dir + L"\\..\\bangbang.lm"};
}

/// 是否在沙盒（AppContainer，例如 Microsoft Store 程式）裡執行。
bool isAppContainerProcess() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD value = 0, returned = 0;
    const bool ok = GetTokenInformation(token, TokenIsAppContainer, &value, sizeof(value), &returned) != FALSE;
    CloseHandle(token);
    return ok && value != 0;
}

std::optional<std::filesystem::path> userModelPath(bool secureMode) {
    if (secureMode) {
        debugLog(L"棒棒注音：安全模式，選字記憶只放在記憶體");
        return std::nullopt;
    }
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"BANGBANG_USER_MODEL", buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::filesystem::path(buffer);
    if (n == 0 && GetLastError() != ERROR_ENVVAR_NOT_FOUND) return std::nullopt;  // 設成空字串：不讀寫檔案
    // 沙盒讀不到使用者的記憶檔，寫入也只會寫到該程式自己的資料夾：先只記在記憶體裡（第 6 階段再設計共用方式）。
    if (isAppContainerProcess()) {
        debugLog(L"棒棒注音：在沙盒程式裡執行，選字記憶只放在記憶體");
        return std::nullopt;
    }
    PWSTR appData = nullptr;
    std::optional<std::filesystem::path> path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        path = std::filesystem::path(appData) / L"BangbangZhuyin" / L"user-model.json";
    }
    CoTaskMemFree(appData);
    return path;
}

}  // namespace

SharedEngine* sharedEngine(bool secureMode) {
    static std::once_flag once;
    static std::unique_ptr<SharedEngine> engine;
    std::call_once(once, [secureMode] {
        try {
            auto loaded = std::make_unique<SharedEngine>();
            for (const auto& candidate : modelCandidates()) {
                if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                try {
                    loaded->lm = LanguageModel::load(candidate);
                    loaded->modelPath = candidate;
                    break;
                } catch (const std::exception&) {
                    debugLog(L"棒棒注音：無法讀取模型檔 %s", candidate.c_str());
                }
            }
            if (!loaded->lm) {
                debugLog(L"棒棒注音：找不到模型檔 bangbang.lm（%s）", moduleDirectory().c_str());
                return;
            }
            loaded->userModel = std::make_unique<UserModel>(loaded->lm, userModelPath(secureMode));
            debugLog(L"棒棒注音：已載入模型 %s", loaded->modelPath.c_str());
            engine = std::move(loaded);
        } catch (...) {
            debugLog(L"棒棒注音：載入模型時發生錯誤");
        }
    });
    return engine.get();
}

}  // namespace bangbang
