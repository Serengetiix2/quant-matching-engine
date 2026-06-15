import json, time, datetime, urllib.request

ITEMS = ["ENCHANTED_LAPIS_LAZULI", "ENCHANTED_DIAMOND", "ENCHANTED_GOLD", "INK_SACK:3"]
OUT = "bazaar_snapshots.jsonl"
INTERVAL = 300  # seconds between polls (5 min); API updates ~every 20s, 5 min is plenty

def snapshot():
    with urllib.request.urlopen("https://api.hypixel.net/v2/skyblock/bazaar", timeout=15) as r:
        data = json.load(r)
    if not data.get("success"):
        return
    row = {"ts": data["lastUpdated"], "fetched": datetime.datetime.utcnow().isoformat(), "items": {}}
    for item in ITEMS:
        p = data["products"].get(item)
        if p:
            row["items"][item] = {"buy_summary": p["buy_summary"],
                                  "sell_summary": p["sell_summary"],
                                  "quick_status": p["quick_status"]}
    with open(OUT, "a") as f:
        f.write(json.dumps(row) + "\n")

while True:
    try:
        snapshot()
        print("snapshot at", datetime.datetime.now().strftime("%H:%M:%S"))
    except Exception as e:
        print("skip:", e)
    time.sleep(INTERVAL)