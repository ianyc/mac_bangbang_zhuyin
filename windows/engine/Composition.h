#pragma once

// 對應 Sources/BangbangEngine/Composition.swift：組字區（游標、插入刪除、手動選字、釘選、括號配對）。

#include <optional>
#include <string>
#include <vector>

#include "Decoder.h"

namespace bangbang {

/// 組字區：使用者已經打好、還沒送出的那一串字，包含游標與手動選字。
class Composition {
public:
    explicit Composition(Decoder decoder);

    Decoder decoder;

    const std::vector<Token>& tokens() const { return tokens_; }
    /// 游標在第幾格之前（0...tokens.size()）。
    int cursor() const { return cursor_; }
    const std::vector<Pin>& pins() const { return pins_; }
    const std::vector<WalkNode>& walk() const { return walk_; }

    bool isEmpty() const { return tokens_.empty(); }
    int count() const { return static_cast<int>(tokens_.size()); }
    std::string text() const;
    /// 游標前面的文字（每一格剛好對應一個字）。
    std::string textBeforeCursor() const;
    std::string textAfterCursor() const;

    void insert(Token token);
    bool deleteBackward();
    bool deleteForward();
    bool moveCursor(int position);
    void clear();

    std::vector<Candidate> candidates() const;
    /// 手動選字：把這個詞釘在該位置，再重新計算整句。
    /// 選字位置前面的字全部固定不變；後面的字可依新的上下文重新選。
    void select(const Candidate& candidate);
    /// 把某一格的符號換成另一個符號（例如「：」換成「；」）。
    /// 換的是括號時，配對的另一半也一起換，例如把『』的前半改成「，後半自動變成」。
    void replaceLiteral(int index, const std::string& text);
    /// 選字結果裡，結束在 position 的那個詞（作為下一個詞的上下文）。
    WordId wordEnding(int position) const;
    /// 組字區太長時，把最前面的詞送出，回傳送出的文字。
    std::string commitLeading(int maxTokens);
    void rewalk();

private:
    void remove(int index);
    std::optional<int> matchingBracket(const std::string& bracket, int index) const;
    void freezeText(int position);

    std::vector<Token> tokens_;
    int cursor_ = 0;
    std::vector<Pin> pins_;
    std::vector<WalkNode> walk_;
};

}  // namespace bangbang
