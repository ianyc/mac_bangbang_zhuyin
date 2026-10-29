#!/bin/zsh
# 用小麥注音詞庫 + 語料建語言模型（build/bunbun.lm），並跑選字正確率評測。
set -euo pipefail
source "$(dirname "$0")/common.sh"

for f in $CORPUS_FILES; do
    [[ -s $f ]] || { scripts/download-corpus.sh; break; }
done
swift build -c release --product bunbun-tool
mkdir -p build
.build/release/bunbun-tool build-lm --data data/mcbopomofo --weights data/reading-weights.txt \
    --out "$MODEL" --heldout build/heldout.txt $CORPUS_FILES
echo "== 日常用語測試"
.build/release/bunbun-tool eval --lm "$MODEL" Tests/eval/sentences.txt --show 10
echo "== 語料保留句測試"
.build/release/bunbun-tool eval --lm "$MODEL" build/heldout.txt --limit 8000 --show 0
