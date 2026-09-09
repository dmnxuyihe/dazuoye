"""Serve a packaged, reproducible historical experiment; never train in requests."""

import json
from copy import deepcopy
from functools import lru_cache
from pathlib import Path


BUSINESS_STATIONS = (
    ("1075", "宝安湾站", "1031"),
    ("1268", "前海枢纽站", "316"),
    ("1606", "福田中心站", "1113"),
    ("2068", "罗湖口岸站", "1121"),
    ("2406", "龙岗能源站", "982"),
    ("1362", "西乡站", "647"),
    ("2043", "坂田站", "728"),
    ("2555", "坪山站", "849"),
    ("1041", "空港新城站", "585"),
    ("1599", "龙华站", "724"),
    ("2426", "大运站", "858"),
    ("1176", "沙井站", "568"),
)


@lru_cache(maxsize=1)
def load_forecast():
    path = Path(__file__).with_name("data") / "load_forecast.json"
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def console_scopes():
    return [
        {"id": "business", "label": "业务站点合计（12站）"},
        *[{"id": f"station-{sid}", "label": name} for sid, name, _ in BUSINESS_STATIONS],
        {"id": "all", "label": "UrbanEV 深圳全网"},
    ]


def _sum_series(results, field):
    series = [results[zone][field] for _, _, zone in BUSINESS_STATIONS]
    return [round(sum(values), 3) for values in zip(*series)]


def _business_result(results):
    regional = [results[zone] for _, _, zone in BUSINESS_STATIONS]
    prediction = _sum_series(results, "prediction")
    threshold = round(sum(item["threshold"] for item in regional), 3)
    metrics = []
    for index in range(len(regional[0].get("metrics", []))):
        rows = [item["metrics"][index] for item in regional]
        metrics.append({
            "model": rows[0]["model"] + "（区域平均）",
            "mae": round(sum(row["mae"] for row in rows) / len(rows), 3),
            "rmse": round(sum(row["rmse"] for row in rows) / len(rows), 3),
            "wape": round(sum(row["wape"] for row in rows) / len(rows), 3),
            "validation_mae": round(sum(row["validation_mae"] for row in rows) / len(rows), 3),
            "selected": False,
        })
    return {
        "label": "12个业务站点对应区域合计",
        "scope_note": "汇总业务数据库采用的12个UrbanEV站点对应区域；不是275个区域的全网值。",
        "model": "12个区域最优模型汇总",
        "metrics": metrics,
        "history": _sum_series(results, "history"),
        "prediction": prediction,
        "lower": _sum_series(results, "lower"),
        "upper": _sum_series(results, "upper"),
        "threshold": threshold,
        "high_hours": sum(value > threshold for value in prediction),
        "test_coverage": round(sum(item["test_coverage"] for item in regional) / len(regional), 2),
    }


def console_forecast(artifact, scope):
    results = artifact["results"]
    if scope == "business":
        return _business_result(results)
    if scope == "all":
        result = deepcopy(results.get("all"))
        if result is not None:
            result["label"] = "UrbanEV 深圳全网"
            result["scope_note"] = "使用UrbanEV数据集中全部275个区域形成的深圳整体预测。"
        return result
    if scope.startswith("station-"):
        station_id = scope.removeprefix("station-")
        station = next((row for row in BUSINESS_STATIONS if row[0] == station_id), None)
        result = deepcopy(results.get(station[2])) if station else None
        if result is not None:
            result["label"] = station[1]
            result["scope_note"] = f"UrbanEV来源站点 {station[0]}，采用其所在区域 {station[2]} 的预测口径。"
        return result
    return None
