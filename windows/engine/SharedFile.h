#pragma once

// 多個程式共用的檔案（選字記憶檔）：每個載入輸入法的程式都會讀寫同一個檔案。
// - 寫入：先寫暫存檔並寫進磁碟，再取代原檔。讀的人只會看到完整的舊檔或新檔，寫到一半當掉也不會留下壞檔。
// - 同時寫入：用旁邊的鎖檔排隊（LockFileEx）。程式結束或當掉時，系統會自動解鎖。
// - 檔案版本：檔案編號、修改時間、大小。每次取代都是新的檔案、編號一定不同，不怕修改時間的精確度不夠。
// - MSIX 封裝程式（Windows Terminal、Claude 等）在 AppData 新建的資料夾，會被轉存到該程式的私有位置，
//   其他程式看不到；資料夾已經在真正的 AppData 裡時，裡面的新檔、改寫、取代都寫到真正的位置（2026-11-22 實測）。

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bangbang {

/// 檔案的版本。內容被取代、改寫或刪除後一定不同。
struct FileStamp {
    bool exists = false;
    std::uint32_t volume = 0;
    std::uint64_t fileIndex = 0;
    std::uint64_t writeTime = 0;
    std::uint64_t size = 0;
    friend bool operator==(const FileStamp&, const FileStamp&) = default;
};

/// 檔案目前的版本；檔案不存在時 exists 為 false。查不到（例如沒有權限）時回傳 nullopt。
std::optional<FileStamp> fileStamp(const std::filesystem::path& file);

enum class ReadResult { Ok, Missing, Failed };
/// 讀出整個檔案，以及讀到的那個檔案的版本。讀的時候不會擋住別的程式取代它。
ReadResult readSharedFile(const std::filesystem::path& file, std::string& data, FileStamp& stamp);

/// 先寫到同一個資料夾的暫存檔並寫進磁碟，再取代原檔。成功時回傳新檔的版本。
std::optional<FileStamp> replaceSharedFile(const std::filesystem::path& file, std::string_view data);

/// 刪掉 file 留下的暫存檔（寫到一半當掉的程式留下的）。只能在拿著鎖的時候呼叫：這時不會有別人正在寫暫存檔。
void removeStaleTempFiles(const std::filesystem::path& file);

/// 跨程式的鎖：同一個鎖檔同時只有一個程式拿得到。解構時解鎖。
class FileLock {
public:
    /// 最多等 timeout；等不到時回傳 nullopt。鎖檔不存在時會建立。
    static std::optional<FileLock> acquire(const std::filesystem::path& lockFile, std::chrono::milliseconds timeout);
    FileLock(FileLock&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock& operator=(FileLock&&) = delete;
    ~FileLock();

private:
    explicit FileLock(void* handle) : handle_(handle) {}
    void* handle_;
};

enum class FolderState {
    Ready,       // 資料夾存在，是所有程式都看得到的真正位置
    Missing,     // 不存在（create 為 false 時）
    Redirected,  // 被 MSIX 轉存到這個程式的私有位置，其他程式看不到
    Failed,      // 建立或檢查失敗
};
/// 檢查資料夾存在、而且沒有被轉存；create 為 true 時，不存在就建立。
/// 自己剛建立卻被轉存的資料夾會馬上刪掉，等不在封裝環境裡的程式建立真正的資料夾。
/// 資料夾本身是使用者建立的連結（例如連到同步資料夾）時，視為真正的位置。
FolderState checkSharedFolder(const std::filesystem::path& folder, bool create);

}  // namespace bangbang
