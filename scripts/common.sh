# 各腳本共用的設定。
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# 系統的開發工具若指向 Command Line Tools，改用 Xcode（測試需要 XCTest）。
if [[ -d /Applications/Xcode.app ]] && [[ "$(xcode-select -p 2>/dev/null)" == *CommandLineTools* ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

APP_NAME=BunbunZhuyin
APP="$ROOT/build/$APP_NAME.app"
INSTALL_DIR="$HOME/Library/Input Methods"
MODEL="$ROOT/build/bunbun.lm"
CORPUS_FILES=(corpus/ptt/ppt_pretrain.json corpus/wiki/wiki_0.json corpus/wiki/wiki_1.json corpus/wiki/wiki_2.json corpus/wiki/wiki_3.json)
