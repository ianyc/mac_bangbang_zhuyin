#!/bin/zsh
# 檢查棒棒注音的安裝狀態（只讀不改）：裝在哪、有幾份、版本、系統輸入方式、執行中的程式、選字記憶。
# 比照 Windows 版的 windows/scripts/check-install.ps1。
# 用法：scripts/check-install.sh [--logs]
#   --logs  另外列出最近一小時輸入法的錯誤訊息（要讀系統記錄，比較慢）
set -u
source "$(dirname "$0")/common.sh"
setopt null_glob

TOOL=.build/release/bangbang-tool
SUPPORT="$HOME/Library/Application Support"
warnings=0
warn() { print "  ⚠️  $1"; warnings=$((warnings + 1)); }

# ---------------------------------------------------------------- 安裝位置
print "== 安裝位置"
apps=()
for dir in "$HOME/Library/Input Methods" "/Library/Input Methods"; do
    for app in "$dir"/BangbangZhuyin.app "$dir"/BunbunZhuyin.app; do
        [[ -d $app ]] || continue
        apps+=("$app")
        plist="$app/Contents/Info.plist"
        exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print CFBundleExecutable' "$plist" 2>/dev/null)"
        model="$app/Contents/Resources/bangbang.lm"
        print "  $app"
        version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$plist" 2>/dev/null)
        bundle=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$plist" 2>/dev/null)
        # 用 ctime（放到這個位置的時間）：Finder 拖曳安裝會保留原本的修改時間
        installed=$(stat -f '%Sc' -t '%Y-%m-%d %H:%M' "$exe" 2>/dev/null)
        signature=$(codesign -dv "$app" 2>&1 | sed -n 's/^Signature=//p')
        print "    版本 $version（$bundle），安裝時間 $installed，架構 $(lipo -archs "$exe" 2>/dev/null)，簽章 $signature"
        if [[ -f $model ]]; then
            # 檔頭第 5～8 個 byte 是格式版本（小端序）
            format=$(xxd -s 4 -l 1 -p "$model")
            note=""
            [[ -f $MODEL ]] && { cmp -s "$MODEL" "$model" && note="，和 build/ 的模型相同" || note="，和 build/ 的模型不同"; }
            print "    模型檔 $(( $(stat -f '%z' "$model") / 1048576 )) MB，格式第 $((16#$format)) 版$note"
        else
            warn "找不到模型檔 $model：輸入法會沒有作用"
        fi
        [[ ${app:t} == BunbunZhuyin.app ]] && warn "這是改名前的舊版，請移除（見 README 的「從 0.1 版升級」）"
    done
done
case ${#apps} in
    0) warn "沒有安裝。用 scripts/install.sh 或 DMG 安裝" ;;
    1) ;;
    *) warn "裝了 ${#apps} 份：系統不一定啟動哪一份，同時啟動時還會搶連線、打不了字。請只留一份" ;;
esac

# ---------------------------------------------------------------- 系統輸入方式
print "\n== 系統輸入方式"
if [[ ! -x $TOOL ]]; then
    print "  （第一次使用，先建置 bangbang-tool…）"
    swift build -c release --product bangbang-tool > /dev/null 2>&1
fi
if [[ -x $TOOL ]]; then
    $TOOL tis | sed 's/^/  /'
    $TOOL tis | grep -q "已啟用：是" || warn "棒棒注音沒有加進輸入方式：到「系統設定 → 鍵盤 → 輸入方式」加入"
else
    warn "無法建置 bangbang-tool，略過這一項"
fi

# ---------------------------------------------------------------- 執行中的程式
print "\n== 執行中的程式"
# ps 的 etime 格式是 [[天-]時:]分:秒，換算成秒
seconds_since() {
    local t=$1 d=0 h=0 m=0 s=0
    [[ $t == *-* ]] && { d=${t%%-*}; t=${t#*-}; }
    local parts=(${(s.:.)t})
    case ${#parts} in
        3) h=$parts[1]; m=$parts[2]; s=$parts[3] ;;
        2) m=$parts[1]; s=$parts[2] ;;
    esac
    print $(( ((10#$d * 24 + 10#$h) * 60 + 10#$m) * 60 + 10#$s ))
}
pids=($(pgrep -x BangbangZhuyin) $(pgrep -x BunbunZhuyin))
if (( ${#pids} == 0 )); then
    print "  沒有在執行（切換到棒棒注音時，系統才會啟動它）"
fi
for pid in $pids; do
    exe=$(ps -o comm= -p $pid)
    elapsed=$(ps -o etime= -p $pid | tr -d ' ')
    print "  pid $pid，已執行 $elapsed：$exe"
    started=$(( $(date +%s) - $(seconds_since $elapsed) ))
    if [[ -f $exe ]] && (( $(stat -f '%c' "$exe") > started )); then
        warn "程式啟動之後才裝了新版，執行中的還是舊版：切到別的輸入法再切回來，或登出再登入"
    fi
done
(( ${#pids} > 1 )) && warn "有 ${#pids} 份同時在執行：只有一份連得上系統，可能打不了字。登出再登入最保險"

# ---------------------------------------------------------------- 選字記憶
print "\n== 選字記憶"
memory="$SUPPORT/BangbangZhuyin/user-model.json"
if [[ -f $memory ]]; then
    print "  $memory"
    print "    $(stat -f '%z' "$memory") bytes，最後修改 $(stat -f '%Sm' -t '%Y-%m-%d %H:%M' "$memory")"
    python3 -I - "$memory" <<'EOF' 2>/dev/null || warn "記憶檔讀不懂（壞掉了）：下次學習時會改名成 .corrupt 保留，再寫新的"
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
print(f"    格式第 {d.get('version', 1)} 版，上下文記錄 {len(d.get('contextual', {}))} 筆、單詞記錄 {len(d.get('unigram', {}))} 筆")
EOF
else
    print "  還沒有記憶檔（第一次手動選字後才會建立）"
fi
for f in "$SUPPORT/BangbangZhuyin"/*.corrupt; do
    print "  保留的壞檔：$f（$(stat -f '%Sm' -t '%Y-%m-%d %H:%M' "$f")）"
done
[[ -d "$SUPPORT/BunbunZhuyin" ]] && print "  改名前的舊記憶（備份，可刪）：$SUPPORT/BunbunZhuyin"

# ---------------------------------------------------------------- 最近的錯誤
if [[ ${1:-} == --logs ]]; then
    print "\n== 最近一小時的錯誤訊息"
    errors=$(log show --last 1h --style compact \
        --predicate '(process == "BangbangZhuyin" OR process == "BunbunZhuyin") AND (eventMessage CONTAINS[c] "Failed to register" OR eventMessage CONTAINS "Zhuyin:")' 2>/dev/null \
        | grep -v '^Timestamp' | cut -c1-200)
    if [[ -n $errors ]]; then
        print -r -- "$errors" | sed 's/^/  /'
        print -r -- "$errors" | grep -qi "Failed to register" && warn "有連線註冊失敗：通常是兩份同時啟動，登出再登入就好"
    else
        print "  沒有"
    fi
fi

print ""
(( warnings == 0 )) && print "看起來一切正常。" || print "有 $warnings 個要注意的地方（標 ⚠️ 的）。"
