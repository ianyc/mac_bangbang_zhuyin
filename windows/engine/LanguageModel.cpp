#include "LanguageModel.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace bangbang {

namespace {

constexpr std::uint32_t kMagic = 0x4D4C4242;  // "BBLM"
constexpr std::uint32_t kVersion = 3;
constexpr std::size_t kHeaderCount = 10;
/// 檔頭裡的數量超過這個值就當成壞檔（也避免 32 位元下 count + 1 溢位）。
constexpr std::uint32_t kMaxCount = 0x10000000;

}  // namespace

/// 模型資料所在的記憶體：file mapping 對應的檔案，或自己配置的記憶體。物件釋放時一併釋放。
class LanguageModel::Storage {
public:
    virtual ~Storage() = default;
    const std::byte* base = nullptr;
    std::size_t size = 0;
};

namespace {

class MemoryStorage final : public LanguageModel::Storage {
public:
    explicit MemoryStorage(const std::vector<std::uint8_t>& bytes) : buffer_(bytes.size() / 8 + 1) {
        if (!bytes.empty()) std::memcpy(buffer_.data(), bytes.data(), bytes.size());
        base = reinterpret_cast<const std::byte*>(buffer_.data());
        size = bytes.size();
    }

private:
    std::vector<std::uint64_t> buffer_;  // 以 8 bytes 對齊
};

struct HandleHolder {
    HANDLE handle = nullptr;
    HandleHolder() = default;
    HandleHolder(const HandleHolder&) = delete;
    HandleHolder& operator=(const HandleHolder&) = delete;
    ~HandleHolder() {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};

struct ViewHolder {
    void* view = nullptr;
    ViewHolder() = default;
    ViewHolder(const ViewHolder&) = delete;
    ViewHolder& operator=(const ViewHolder&) = delete;
    ~ViewHolder() {
        if (view) UnmapViewOfFile(view);
    }
};

/// 唯讀對應整個模型檔。多個程式對應同一個檔案時，系統讓它們共用同一份實體記憶體。
class MappedStorage final : public LanguageModel::Storage {
public:
    explicit MappedStorage(const std::filesystem::path& path) {
        file_.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_.handle == INVALID_HANDLE_VALUE) throw ModelError(ModelError::Kind::CannotOpen, "無法開啟模型檔");
        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file_.handle, &fileSize) || fileSize.QuadPart <= 0 ||
            static_cast<unsigned long long>(fileSize.QuadPart) > SIZE_MAX) {
            throw ModelError(ModelError::Kind::CannotOpen, "模型檔大小不正確");
        }
        mapping_.handle = CreateFileMappingW(file_.handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_.handle) throw ModelError(ModelError::Kind::CannotOpen, "無法對應模型檔");
        view_.view = MapViewOfFile(mapping_.handle, FILE_MAP_READ, 0, 0, 0);
        if (!view_.view) throw ModelError(ModelError::Kind::CannotOpen, "無法對應模型檔到記憶體");
        base = static_cast<const std::byte*>(view_.view);
        size = static_cast<std::size_t>(fileSize.QuadPart);
    }

private:
    // 宣告順序決定釋放順序：先解除對應，再關閉 mapping 與檔案。
    HandleHolder file_;
    HandleHolder mapping_;
    ViewHolder view_;
};

/// 依序讀出各個表格；每個表格開頭都對齊 8 bytes。
struct SectionReader {
    const LanguageModel::Storage& storage;
    std::size_t offset = 0;

    template <class T>
    std::span<const T> next(std::size_t count) {
        offset = (offset + 7) & ~static_cast<std::size_t>(7);
        if (offset > storage.size || count > (storage.size - offset) / sizeof(T)) {
            throw ModelError(ModelError::Kind::Truncated, "模型檔不完整");
        }
        const T* pointer = reinterpret_cast<const T*>(storage.base + offset);
        offset += sizeof(T) * count;
        return {pointer, count};
    }
};

struct SectionWriter {
    std::vector<std::uint8_t> bytes;

    template <class T>
    void section(const std::vector<T>& values) {
        while (bytes.size() % 8 != 0) bytes.push_back(0);
        if (values.empty()) return;
        const auto* p = reinterpret_cast<const std::uint8_t*>(values.data());
        bytes.insert(bytes.end(), p, p + values.size() * sizeof(T));
    }
};

/// 起點表必須遞增，而且最後一個值等於總數，否則查表會讀到範圍外。
void checkStarts(std::span<const std::uint32_t> starts, std::size_t total) {
    for (std::size_t i = 1; i < starts.size(); ++i) {
        if (starts[i] < starts[i - 1]) throw ModelError(ModelError::Kind::BadFormat, "模型檔的索引表不正確");
    }
    if (starts.empty() || starts.back() != total) throw ModelError(ModelError::Kind::BadFormat, "模型檔的索引表不正確");
}

std::vector<std::uint8_t> serialize(const std::vector<std::string>& words, const std::vector<float>& logUnigram,
                                    const std::vector<float>& logBackoff, const std::vector<std::uint32_t>& bigramStart,
                                    const std::vector<std::uint32_t>& bigramNext, const std::vector<float>& bigramLogProb,
                                    const std::map<ReadingKey, std::vector<LexiconEntry>>& lexicon,
                                    const std::map<std::uint64_t, float>& skipBoost) {
    std::vector<char> blob;
    std::vector<std::uint32_t> offsets{0};
    for (const auto& word : words) {
        blob.insert(blob.end(), word.begin(), word.end());
        offsets.push_back(static_cast<std::uint32_t>(blob.size()));
    }

    // 讀音依音節序列排序（std::map 本身就是這個順序）；每個讀音的候選詞依「單詞機率 + 讀音權重」由高到低，
    // 解碼時只取前幾名。
    std::vector<std::uint32_t> keyCodeStart{0};
    std::vector<std::uint16_t> keyCodes;
    std::vector<std::uint32_t> entryStart{0};
    std::vector<std::uint32_t> entryWord;
    std::vector<float> entryWeight;
    std::uint32_t maxPhraseLength = 1;
    for (const auto& [key, entries] : lexicon) {
        keyCodes.insert(keyCodes.end(), key.codes.begin(), key.codes.end());
        keyCodeStart.push_back(static_cast<std::uint32_t>(keyCodes.size()));
        maxPhraseLength = std::max(maxPhraseLength, static_cast<std::uint32_t>(key.codes.size()));
        std::vector<LexiconEntry> sorted = entries;
        std::stable_sort(sorted.begin(), sorted.end(), [&](const LexiconEntry& a, const LexiconEntry& b) {
            return logUnigram[a.word] + a.readingWeight > logUnigram[b.word] + b.readingWeight;
        });
        for (const auto& e : sorted) {
            entryWord.push_back(e.word);
            entryWeight.push_back(e.readingWeight);
        }
        entryStart.push_back(static_cast<std::uint32_t>(entryWord.size()));
    }

    std::vector<std::uint32_t> skipStart(words.size() + 1, 0);
    std::vector<std::uint32_t> skipNext;
    std::vector<float> skipValues;
    for (const auto& [key, value] : skipBoost) {
        skipStart[static_cast<std::size_t>(key >> 32) + 1] += 1;
        skipNext.push_back(static_cast<std::uint32_t>(key & 0xFFFFFFFF));
        skipValues.push_back(value);
    }
    for (std::size_t i = 1; i < skipStart.size(); ++i) skipStart[i] += skipStart[i - 1];

    SectionWriter w;
    w.section(std::vector<std::uint32_t>{kMagic, kVersion, static_cast<std::uint32_t>(words.size()),
                                         static_cast<std::uint32_t>(bigramNext.size()), static_cast<std::uint32_t>(lexicon.size()),
                                         static_cast<std::uint32_t>(keyCodes.size()), static_cast<std::uint32_t>(entryWord.size()),
                                         static_cast<std::uint32_t>(skipNext.size()), static_cast<std::uint32_t>(blob.size()),
                                         maxPhraseLength});
    w.section(offsets);
    w.section(blob);
    w.section(logUnigram);
    w.section(logBackoff);
    w.section(bigramStart);
    w.section(bigramNext);
    w.section(bigramLogProb);
    w.section(keyCodeStart);
    w.section(keyCodes);
    w.section(entryStart);
    w.section(entryWord);
    w.section(entryWeight);
    w.section(skipStart);
    w.section(skipNext);
    w.section(skipValues);
    return std::move(w.bytes);
}

}  // namespace

ReadingKey ReadingKey::of(const std::vector<Syllable>& syllables) {
    std::vector<std::uint16_t> codes;
    codes.reserve(syllables.size());
    for (const auto& s : syllables) codes.push_back(s.code());
    return ReadingKey(std::move(codes));
}

std::vector<Syllable> ReadingKey::syllables() const {
    std::vector<Syllable> result;
    result.reserve(codes.size());
    for (const auto c : codes) result.push_back(Syllable::fromCode(c));
    return result;
}

std::size_t ReadingKeyHash::operator()(std::span<const std::uint16_t> codes) const noexcept {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (const auto c : codes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h ^ (h >> 32));
}

LanguageModel::LanguageModel(std::unique_ptr<Storage> storage) : storage_(std::move(storage)) {
    SectionReader r{*storage_};
    const auto header = r.next<std::uint32_t>(kHeaderCount);
    if (header[0] != kMagic || header[1] != kVersion) throw ModelError(ModelError::Kind::BadFormat, "不是第 3 版的棒棒注音模型檔");
    for (std::size_t i = 2; i < kHeaderCount; ++i) {
        if (header[i] > kMaxCount) throw ModelError(ModelError::Kind::BadFormat, "模型檔的檔頭不正確");
    }
    const std::size_t wordCount = header[2], bigramCount = header[3], keyCount = header[4];
    const std::size_t codeCount = header[5], entryCount = header[6], skipCount = header[7];
    const std::size_t blobCount = header[8];
    if (wordCount == 0 || header[9] == 0 || header[9] > 64) throw ModelError(ModelError::Kind::BadFormat, "模型檔的檔頭不正確");
    maxPhraseLength_ = static_cast<int>(header[9]);

    const auto offsets = r.next<std::uint32_t>(wordCount + 1);
    const auto blob = r.next<char>(blobCount);
    logUnigram_ = r.next<float>(wordCount);
    logBackoff_ = r.next<float>(wordCount);
    bigramStart_ = r.next<std::uint32_t>(wordCount + 1);
    bigramNext_ = r.next<std::uint32_t>(bigramCount);
    bigramLogProb_ = r.next<float>(bigramCount);
    keyCodeStart_ = r.next<std::uint32_t>(keyCount + 1);
    keyCodes_ = r.next<std::uint16_t>(codeCount);
    entryStart_ = r.next<std::uint32_t>(keyCount + 1);
    entryWord_ = r.next<std::uint32_t>(entryCount);
    entryWeight_ = r.next<float>(entryCount);
    skipStart_ = r.next<std::uint32_t>(wordCount + 1);
    skipNext_ = r.next<std::uint32_t>(skipCount);
    skipValues_ = r.next<float>(skipCount);

    // 便宜的一致性檢查：壞掉的模型檔要在載入時就拒絕，不能等到查表時讀到範圍外、讓宿主程式當掉。
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        if (offsets[i] < offsets[i - 1]) throw ModelError(ModelError::Kind::BadFormat, "模型檔的詞表不正確");
    }
    if (offsets.back() > blobCount) throw ModelError(ModelError::Kind::BadFormat, "模型檔的詞表不正確");
    checkStarts(bigramStart_, bigramCount);
    checkStarts(keyCodeStart_, codeCount);
    checkStarts(entryStart_, entryCount);
    checkStarts(skipStart_, skipCount);
    for (const auto w : entryWord_) {
        if (w >= wordCount) throw ModelError(ModelError::Kind::BadFormat, "模型檔的讀音表不正確");
    }
    words_ = WordTable(offsets.data(), wordCount, blob.data());
}

LanguageModel::~LanguageModel() = default;

std::shared_ptr<const LanguageModel> LanguageModel::load(const std::filesystem::path& path) {
    return std::shared_ptr<const LanguageModel>(new LanguageModel(std::make_unique<MappedStorage>(path)));
}

std::shared_ptr<const LanguageModel> LanguageModel::build(const std::vector<std::string>& words,
                                                          const std::vector<float>& logUnigram,
                                                          const std::vector<float>& logBackoff,
                                                          const std::vector<std::uint32_t>& bigramStart,
                                                          const std::vector<std::uint32_t>& bigramNext,
                                                          const std::vector<float>& bigramLogProb,
                                                          const std::map<ReadingKey, std::vector<LexiconEntry>>& lexicon,
                                                          const std::map<std::uint64_t, float>& skipBoost) {
    const auto bytes = serialize(words, logUnigram, logBackoff, bigramStart, bigramNext, bigramLogProb, lexicon, skipBoost);
    return std::shared_ptr<const LanguageModel>(new LanguageModel(std::make_unique<MemoryStorage>(bytes)));
}

void LanguageModel::write(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(storage_->base), static_cast<std::streamsize>(storage_->size));
    if (!out) throw ModelError(ModelError::Kind::CannotOpen, "無法寫入模型檔");
}

LanguageModel::Lookup LanguageModel::lookup(std::span<const std::uint16_t> codes) const {
    const std::size_t count = keyCodeStart_.size() - 1;
    std::size_t lo = 0, hi = count;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) >> 1;
        if (compareKey(mid, codes) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    // lo 是第一個 ≥ codes 的讀音；完全相同時就是它，更長的同字首讀音緊接在後。
    Lookup result{LexiconEntries(entryWord_.data(), entryWeight_.data(), 0, 0), false};
    std::size_t next = lo;
    if (lo < count && compareKey(lo, codes) == 0) {
        result.entries = LexiconEntries(entryWord_.data(), entryWeight_.data(), entryStart_[lo], entryStart_[lo + 1]);
        next = lo + 1;
    }
    result.hasLonger = next < count && keyHasPrefix(next, codes);
    return result;
}

bool LanguageModel::hasReading(const Syllable& syllable) const {
    const std::uint16_t code = syllable.code();
    return !lookup(std::span<const std::uint16_t>(&code, 1)).entries.empty();
}

std::map<ReadingKey, std::vector<LexiconEntry>> LanguageModel::lexicon() const {
    std::map<ReadingKey, std::vector<LexiconEntry>> result;
    for (std::size_t k = 0; k + 1 < keyCodeStart_.size(); ++k) {
        ReadingKey key(std::vector<std::uint16_t>(keyCodes_.begin() + keyCodeStart_[k], keyCodes_.begin() + keyCodeStart_[k + 1]));
        const LexiconEntries entries(entryWord_.data(), entryWeight_.data(), entryStart_[k], entryStart_[k + 1]);
        result[std::move(key)] = std::vector<LexiconEntry>(entries.begin(), entries.end());
    }
    return result;
}

int LanguageModel::compareKey(std::size_t k, std::span<const std::uint16_t> codes) const {
    const std::size_t start = keyCodeStart_[k], end = keyCodeStart_[k + 1];
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (start + i >= end) return -1;
        const std::uint16_t mine = keyCodes_[start + i];
        if (mine != codes[i]) return mine < codes[i] ? -1 : 1;
    }
    return end - start == codes.size() ? 0 : 1;
}

bool LanguageModel::keyHasPrefix(std::size_t k, std::span<const std::uint16_t> codes) const {
    const std::size_t start = keyCodeStart_[k], end = keyCodeStart_[k + 1];
    if (end - start <= codes.size()) return false;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (keyCodes_[start + i] != codes[i]) return false;
    }
    return true;
}

}  // namespace bangbang
