#!/bin/zsh
# 打包成 DMG：打開後把「棒棒注音」拖到「Input Methods」資料夾即可安裝（給其他 Mac 用）。
# 安裝位置是 /Library/Input Methods（這台 Mac 的所有使用者共用）。
set -euo pipefail
source "$(dirname "$0")/common.sh"

UNIVERSAL=1 scripts/build.sh
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" Resources/Info.plist)
STAGE=build/dmg
DMG="build/BunbunZhuyin-$VERSION.dmg"
rm -rf $STAGE "$DMG"
mkdir -p $STAGE
cp -R "$APP" $STAGE/
ln -s "/Library/Input Methods" "$STAGE/Input Methods"
cp Resources/dmg-install.txt "$STAGE/安裝說明.txt"
hdiutil create -volname "棒棒注音 $VERSION" -srcfolder $STAGE -ov -format UDZO "$DMG" > /dev/null
rm -rf $STAGE
echo "已產生：$ROOT/$DMG（$(du -h "$DMG" | cut -f1)）"
