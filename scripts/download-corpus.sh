#!/bin/zsh
# 下載建語言模型用的繁體中文語料（約 1.5 GB，只用來統計詞頻，不會打包進輸入法）。
#   PTT 文章：https://huggingface.co/datasets/yuhuanstudio/PTT-pretrain-zhtw （Apache-2.0）
#   中文維基：https://huggingface.co/datasets/yuhuanstudio/wikipedia-zh-tw （CC BY-SA 4.0）
set -euo pipefail
source "$(dirname "$0")/common.sh"

mkdir -p corpus/ptt corpus/wiki
fetch() {
    local url=$1 out=$2
    if [[ -s $out ]]; then
        echo "已存在：$out"
    else
        echo "下載 $out"
        curl -L --fail --retry 3 -o "$out.part" "$url" && mv "$out.part" "$out"
    fi
}
fetch "https://huggingface.co/datasets/yuhuanstudio/PTT-pretrain-zhtw/resolve/main/ppt_pretrain.json" corpus/ptt/ppt_pretrain.json
for i in 0 1 2 3; do
    fetch "https://huggingface.co/datasets/yuhuanstudio/wikipedia-zh-tw/resolve/main/2023_05_not_clean/wiki_pretrain_dataset_$i.json" "corpus/wiki/wiki_$i.json"
done
