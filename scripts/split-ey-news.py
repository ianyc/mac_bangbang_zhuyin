# 把行政院即時新聞（政府資料開放授權條款）切成訓練與測試兩半：
# 偶數篇轉成語料格式，奇數篇切成短句作為新聞類選字測試。
# 用法：python3 -I scripts/split-ey-news.py <ey154.json> <訓練輸出.json> <測試輸出.txt>
import json
import re
import sys

items = json.load(open(sys.argv[1], encoding="utf-8-sig"))
train = [{"text": x["標題"] + "\n" + x["內容"]} for i, x in enumerate(items) if i % 2 == 0]
json.dump(train, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False, indent=4)
clauses = []
for i, x in enumerate(items):
    if i % 2 == 0:
        continue
    text = x["內容"]
    for m in re.finditer(r"[一-鿿]+", text):
        before = text[: m.start()].rstrip(" ")
        # 接在數字或英文後面的不是句子開頭（例如「29日」的「日」）
        if before and re.match(r"[0-9A-Za-z０-９Ａ-Ｚａ-ｚ]", before[-1]):
            continue
        if 4 <= len(m.group()) <= 30:
            clauses.append(m.group())
open(sys.argv[3], "w", encoding="utf-8").write("\n".join(clauses))
print(f"新聞：訓練 {len(train)} 篇，測試 {len(clauses)} 句")
