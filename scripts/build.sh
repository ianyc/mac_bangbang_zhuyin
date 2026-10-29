#!/bin/zsh
# 編譯並組成 build/BunbunZhuyin.app（本機簽章，只給自己用）。
set -euo pipefail
source "$(dirname "$0")/common.sh"

swift build -c release
[[ -s $MODEL ]] || scripts/build-model.sh
[[ -s build/icon.tiff ]] || swift scripts/make-icon.swift build/icon.tiff

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp .build/release/$APP_NAME "$APP/Contents/MacOS/"
cp Resources/Info.plist "$APP/Contents/"
cp -R Resources/*.lproj "$APP/Contents/Resources/"
cp build/icon.tiff "$MODEL" "$APP/Contents/Resources/"
cp data/mcbopomofo/LICENSE.txt "$APP/Contents/Resources/McBopomofo-LICENSE.txt"
codesign --force --sign - "$APP"
echo "已建置：$APP"
