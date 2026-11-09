#!/bin/zsh
# 用小麥注音詞庫 + 語料建語言模型（build/bangbang.lm），並跑選字正確率評測。
set -euo pipefail
source "$(dirname "$0")/common.sh"

for f in $CORPUS_FILES; do
    [[ -s $f ]] || { scripts/download-corpus.sh; break; }
done
swift build -c release --product bangbang-tool
mkdir -p build
.build/release/bangbang-tool build-lm --data data/mcbopomofo --weights data/reading-weights.txt \
    --discouraged data/discouraged-chars.txt \
    --out "$MODEL" --heldout build/heldout.txt $CORPUS_FILES
echo "== 日常用語測試"
.build/release/bangbang-tool eval --lm "$MODEL" Tests/eval/sentences.txt --show 10
echo "== 語料保留句測試"
.build/release/bangbang-tool eval --lm "$MODEL" build/heldout.txt --limit 8000 --show 0
echo "== 新聞測試"
.build/release/bangbang-tool eval --lm "$MODEL" build/news-test.txt --limit 8000 --show 0 2>/dev/null
