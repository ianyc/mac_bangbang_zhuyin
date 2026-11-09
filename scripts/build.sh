#!/bin/zsh
# 編譯並組成 build/BangbangZhuyin.app（本機簽章）。
# UNIVERSAL=1 時編成同時支援 Apple 晶片與 Intel 的版本（打包 DMG 給其他 Mac 用）。
set -euo pipefail
source "$(dirname "$0")/common.sh"

swift build -c release
BIN=.build/release
if [[ "${UNIVERSAL:-0}" == 1 ]]; then
    swift build -c release --arch arm64 --arch x86_64 --product $APP_NAME
    BIN=$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)
fi
[[ -s $MODEL ]] || scripts/build-model.sh
[[ -s build/icon.tiff ]] || swift scripts/make-icon.swift build/icon.tiff

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/$APP_NAME" "$APP/Contents/MacOS/"
cp Resources/Info.plist "$APP/Contents/"
cp -R Resources/*.lproj "$APP/Contents/Resources/"
cp build/icon.tiff "$MODEL" "$APP/Contents/Resources/"
cp data/mcbopomofo/LICENSE.txt "$APP/Contents/Resources/McBopomofo-LICENSE.txt"
codesign --force --sign - "$APP"
echo "已建置：$APP"
