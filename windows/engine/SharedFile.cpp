#include "SharedFile.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace bangbang {

namespace {

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
/// 記憶檔不可能這麼大：超過就當作壞檔（內容讀成空的），不把奇怪的大檔案讀進記憶體。
constexpr std::uint64_t kMaxFileSize = 64ull << 20;
constexpr DWORD kChunk = 1u << 20;

class Handle {
public:
    explicit Handle(HANDLE h) : h_(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { close(); }
    bool valid() const { return h_ != INVALID_HANDLE_VALUE && h_ != nullptr; }
    HANDLE get() const { return h_; }
    void close() {
        if (valid()) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE h_;
};

bool isNotFound(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

std::optional<FileStamp> stampOf(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info)) return std::nullopt;
    FileStamp s;
    s.exists = true;
    s.volume = info.dwVolumeSerialNumber;
    s.fileIndex = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    s.writeTime = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    s.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return s;
}

/// 檔案或資料夾實際所在的位置：會解析連結，也會顯示 MSIX 轉存後的私有位置（…\Packages\<程式>\LocalCache\…）。
std::optional<std::wstring> finalPath(const std::filesystem::path& path) {
    Handle h(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!h.valid()) return std::nullopt;
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD n = GetFinalPathNameByHandleW(h.get(), buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0) return std::nullopt;
        if (n < buffer.size()) {
            buffer.resize(n);
            return buffer;
        }
        buffer.resize(n);  // 空間不夠：n 是需要的大小（含結尾的 0）
    }
    return std::nullopt;
}

bool samePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

/// POSIX 式的取代：原檔正被別的程式開著（允許刪除的方式）也能取代，它們繼續讀到舊的內容。
/// MoveFileEx 在這種情況會失敗（2026-11-22 實測），寫的人得一直重試。
bool renameOver(HANDLE source, const std::filesystem::path& target) {
    const std::wstring name = target.wstring();
    std::vector<std::byte> buffer(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, name.c_str(), info->FileNameLength);
    return SetFileInformationByHandle(source, FileRenameInfoEx, info, static_cast<DWORD>(buffer.size())) != FALSE;
}

bool writeAll(HANDLE h, std::string_view data) {
    std::size_t done = 0;
    while (done < data.size()) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, kChunk));
        if (!WriteFile(h, data.data() + done, chunk, &n, nullptr) || n == 0) return false;
        done += n;
    }
    return true;
}

}  // namespace

std::optional<FileStamp> fileStamp(const std::filesystem::path& file) {
    // 只要求讀取屬性：不受別人開檔的共用模式限制，也不會擋住別人取代檔案。
    Handle h(CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid()) {
        if (isNotFound(GetLastError())) return FileStamp{};
        return std::nullopt;
    }
    return stampOf(h.get());
}

ReadResult readSharedFile(const std::filesystem::path& file, std::string& data, FileStamp& stamp) {
    data.clear();
    stamp = {};
    Handle h(CreateFileW(file.c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!h.valid()) return isNotFound(GetLastError()) ? ReadResult::Missing : ReadResult::Failed;
    const auto s = stampOf(h.get());
    if (!s) return ReadResult::Failed;
    stamp = *s;
    if (s->size > kMaxFileSize) return ReadResult::Ok;
    data.resize(static_cast<std::size_t>(s->size));
    std::size_t done = 0;
    while (done < data.size()) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, kChunk));
        if (!ReadFile(h.get(), data.data() + done, chunk, &n, nullptr)) return ReadResult::Failed;
        if (n == 0) break;
        done += n;
    }
    data.resize(done);
    return ReadResult::Ok;
}

std::optional<FileStamp> replaceSharedFile(const std::filesystem::path& file, std::string_view data) {
    static std::atomic<unsigned> counter{0};
    std::filesystem::path temp = file;
    temp += L"." + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter) + L".tmp";
    {
        Handle h(CreateFileW(temp.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!h.valid()) return std::nullopt;
        // 先寫進磁碟再取代：停電時才不會留下有檔名、內容卻是空的記憶檔。
        if (!writeAll(h.get(), data) || !FlushFileBuffers(h.get())) {
            h.close();
            DeleteFileW(temp.c_str());
            return std::nullopt;
        }
        if (renameOver(h.get(), file)) {
            h.close();
            return fileStamp(file);
        }
    }
    // 檔案系統不支援 POSIX 式的取代，或原檔被不允許刪除的方式開著（防毒、舊版輸入法）：
    // 改用 MoveFileEx，暫時失敗就稍等再試，最多約 0.1 秒。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD error = GetLastError();
        const bool transient = error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
        if (!transient || std::chrono::steady_clock::now() >= deadline) {
            DeleteFileW(temp.c_str());
            return std::nullopt;
        }
        Sleep(1);
    }
    return fileStamp(file);
}

void removeStaleTempFiles(const std::filesystem::path& file) {
    std::filesystem::path pattern = file;
    pattern += L".*.tmp";
    WIN32_FIND_DATAW found{};
    const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        DeleteFileW((file.parent_path() / found.cFileName).c_str());
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

std::optional<FileLock> FileLock::acquire(const std::filesystem::path& lockFile, std::chrono::milliseconds timeout) {
    const HANDLE h = CreateFileW(lockFile.c_str(), GENERIC_READ | GENERIC_WRITE, kShareAll, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        OVERLAPPED overlapped{};
        if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped)) return FileLock(h);
        if (GetLastError() != ERROR_LOCK_VIOLATION || std::chrono::steady_clock::now() >= deadline) {
            CloseHandle(h);
            return std::nullopt;
        }
        Sleep(1);
    }
}

FileLock::~FileLock() {
    if (!handle_) return;
    OVERLAPPED overlapped{};
    UnlockFileEx(handle_, 0, 1, 0, &overlapped);
    CloseHandle(handle_);
}

FolderState checkSharedFolder(const std::filesystem::path& folder, bool create) {
    bool created = false;
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!isNotFound(GetLastError())) return FolderState::Failed;
        if (!create) return FolderState::Missing;
        if (CreateDirectoryW(folder.c_str(), nullptr)) {
            created = true;
        } else if (GetLastError() != ERROR_ALREADY_EXISTS) {  // 別的程式剛好同時建立時是 ALREADY_EXISTS
            return FolderState::Failed;
        }
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return FolderState::Failed;
    } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return FolderState::Ready;  // 使用者自己建立的連結
    }
    // 被轉存的資料夾，實際位置會在封裝程式的私有資料夾裡，和「上一層的實際位置＋資料夾名稱」不同。
    const auto actual = finalPath(folder);
    const auto parent = finalPath(folder.parent_path());
    if (!actual || !parent) return FolderState::Failed;
    std::wstring expected = *parent;
    if (!expected.empty() && expected.back() != L'\\') expected += L'\\';
    expected += folder.filename().wstring();
    if (samePath(*actual, expected)) return FolderState::Ready;
    if (created) RemoveDirectoryW(actual->c_str());
    return FolderState::Redirected;
}

}  // namespace bangbang
