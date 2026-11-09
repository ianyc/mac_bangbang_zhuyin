#!/bin/zsh
# 建置並安裝到 ~/Library/Input Methods，取代正在執行的舊版。
set -euo pipefail
source "$(dirname "$0")/common.sh"

scripts/build.sh
mkdir -p "$INSTALL_DIR"
# 結束舊版程式；下次切到棒棒注音時，系統會自動啟動新版。
pkill -x $APP_NAME || true
rm -rf "$INSTALL_DIR/$APP_NAME.app"
cp -R "$APP" "$INSTALL_DIR/"
.build/release/bangbang-tool register "$INSTALL_DIR/$APP_NAME.app"
echo "已安裝到 $INSTALL_DIR/$APP_NAME.app"
echo "第一次安裝：到「系統設定 → 鍵盤 → 輸入方式 → 編輯… → ＋」，在「繁體中文」裡加入「棒棒注音」。"
