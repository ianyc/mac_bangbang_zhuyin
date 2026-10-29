#!/bin/zsh
# 下載建語言模型用的繁體中文語料（約 1.5 GB，只用來統計詞頻，不會打包進輸入法）。
#   PTT 文章：https://huggingface.co/datasets/yuhuanstudio/PTT-pretrain-zhtw （Apache-2.0）
#   中文維基：https://huggingface.co/datasets/yuhuanstudio/wikipedia-zh-tw （CC BY-SA 4.0）
#   行政院即時新聞：https://www.ey.gov.tw/opendata/BF42D887A7B414E2/154 （政府資料開放授權條款；
#     只保留近期約 500 篇，內容會隨時間更新）
set -euo pipefail
source "$(dirname "$0")/common.sh"

mkdir -p corpus/ptt corpus/wiki corpus/news build
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
fetch "https://www.ey.gov.tw/NewOpenData/json/154" corpus/news/ey154.json
python3 -I scripts/split-ey-news.py corpus/news/ey154.json corpus/news/ey-train.json build/news-test.txt
