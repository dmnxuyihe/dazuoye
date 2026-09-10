"""Serve a packaged, reproducible historical experiment; never train in requests."""

import json
from copy import deepcopy
from datetime import date, datetime, time, timedelta
from functools import lru_cache
from pathlib import Path

import numpy as np


DATASET_START = date(2022, 9, 1)
DATASET_END = date(2023, 2, 28)
DATASET_RANGE_TEXT = "2022-09-01 ~ 2023-02-28"


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


def map_current_date_to_dataset_date(current_date: date) -> date | None:
    """Map a natural date to UrbanEV's matching month/day, if covered."""
    if 9 <= current_date.month <= 12:
        year = 2022
    elif 1 <= current_date.month <= 2:
        year = 2023
    else:
        return None
    try:
        mapped = date(year, current_date.month, current_date.day)
    except ValueError:
        return None
    return mapped if DATASET_START <= mapped <= DATASET_END else None


def _scores(prediction, actual):
    prediction = np.asarray(prediction, dtype=float)
    actual = np.asarray(actual, dtype=float)
    error = prediction - actual
    denominator = np.abs(actual).sum()
    return {
        "mae": round(float(np.abs(error).mean()), 3),
        "rmse": round(float(np.sqrt(np.mean(error**2))), 3),
        "wape": round(float(np.abs(error).sum() / denominator * 100), 3)
        if denominator
        else 0.0,
    }


def _feature(day_values, day_index, hour, weekday, scale):
    lags = [day_values[day_index - lag, hour] / scale for lag in range(1, 8)]
    previous_mean = float(day_values[day_index - 1].mean()) / scale
    return np.array(
        lags
        + [
            previous_mean,
            np.sin(hour * np.pi / 12),
            np.cos(hour * np.pi / 12),
            np.sin(weekday * 2 * np.pi / 7),
            np.cos(weekday * 2 * np.pi / 7),
            float(weekday >= 5),
            1.0,
        ],
        dtype=float,
    )


def _ridge_fit_predict(day_values, dates, train_stop, predict_index):
    """Fit only on days before train_stop and predict predict_index."""
    scale = max(float(day_values[:train_stop].mean()), 1.0)
    features, targets = [], []
    for day_index in range(7, train_stop):
        for hour in range(24):
            features.append(
                _feature(day_values, day_index, hour, dates[day_index].weekday(), scale)
            )
            targets.append(day_values[day_index, hour] / scale)
    if len(features) < 24 * 7:
        return None
    x = np.stack(features)
    y = np.asarray(targets)
    regularizer = np.eye(x.shape[1])
    regularizer[-1, -1] = 0
    weights = np.linalg.solve(x.T @ x + regularizer, x.T @ y)
    prediction = [
        max(
            0.0,
            float(
                _feature(
                    day_values, predict_index, hour, dates[predict_index].weekday(), scale
                )
                @ weights
                * scale
            ),
        )
        for hour in range(24)
    ]
    return np.asarray(prediction)


def historical_same_period_forecast(rows, current_date: date, label: str):
    """Build a target-day simulation from observations strictly before that day."""
    mapped = map_current_date_to_dataset_date(current_date)
    common = {
        "ready": True,
        "available": mapped is not None,
        "current_date": current_date.isoformat(),
        "mapped_date": mapped.isoformat() if mapped else None,
        "dataset_range": {"start": DATASET_START.isoformat(), "end": DATASET_END.isoformat()},
        "forecast_type": "historical_same_period",
    }
    if mapped is None:
        return {**common, "message": "当前日期暂无对应的历史同期数据"}

    target_start = datetime.combine(mapped, time.min)
    hourly = {moment: float(value) for moment, value in rows if moment < target_start}
    first_day = min(hourly).date() if hourly else mapped
    dates = []
    cursor = first_day
    while cursor < mapped:
        if all(datetime.combine(cursor, time(hour)) in hourly for hour in range(24)):
            dates.append(cursor)
        cursor += timedelta(days=1)
    if not dates:
        return {**common, "available": False, "message": "目标日期之前没有可用的完整历史数据"}

    values = np.asarray(
        [[hourly[datetime.combine(day, time(hour))] for hour in range(24)] for day in dates],
        dtype=float,
    )
    # Append an empty target row only for lag feature addressing; it is never fitted.
    extended = np.vstack([values, np.zeros((1, 24))])
    model_dates = dates + [mapped]
    target_index = len(values)
    sufficient = len(values) >= 21
    candidates = {}
    if len(values) >= 1:
        candidates["昨日同小时"] = values[-1].copy()
    if len(values) >= 7:
        candidates["上周同小时"] = values[-7].copy()
    if sufficient:
        ridge = _ridge_fit_predict(extended, model_dates, target_index, target_index)
        if ridge is not None:
            candidates["岭回归"] = ridge
    same_hour_days = values[-min(7, len(values)) :]
    fallback = same_hour_days.mean(axis=0)
    if not candidates:
        candidates["同小时历史平均"] = fallback

    validation_days = min(7, max(0, len(values) - 7))
    metric_rows = []
    validation_predictions = {name: [] for name in candidates}
    validation_actual = []
    for index in range(len(values) - validation_days, len(values)):
        validation_actual.extend(values[index])
        for name in candidates:
            if name == "昨日同小时":
                pred = values[index - 1]
            elif name == "上周同小时":
                pred = values[index - 7]
            elif name == "岭回归":
                pred = _ridge_fit_predict(values, dates, index, index)
                if pred is None:
                    pred = values[index - 1]
            else:
                pred = values[:index][-min(7, index) :].mean(axis=0)
            validation_predictions[name].extend(pred)
    for name, prediction in candidates.items():
        score = _scores(validation_predictions[name], validation_actual) if validation_actual else {
            "mae": 0.0, "rmse": 0.0, "wape": 0.0
        }
        metric_rows.append({"model": name, **score, "validation_mae": score["mae"], "selected": False})
    selected = min(metric_rows, key=lambda item: item["mae"])["model"] if validation_actual else next(iter(candidates))
    for item in metric_rows:
        item["selected"] = item["model"] == selected
    prediction = candidates[selected]
    residuals = np.abs(np.asarray(validation_predictions[selected]) - np.asarray(validation_actual)) if validation_actual else np.array([])
    radius = float(np.quantile(residuals, 0.9)) if residuals.size else max(float(prediction.mean()) * 0.2, 1.0)
    threshold = float(np.quantile(values, 0.95))
    history_count = min(48, values.size)
    history = values.reshape(-1)[-history_count:]
    history_start = target_start - timedelta(hours=history_count)
    return {
        **common,
        "label": label,
        "model": selected,
        "history_sufficient": sufficient,
        "strategy_message": "" if sufficient else "历史样本较少，本次预测使用简化预测策略",
        "history": history.round(3).tolist(),
        "history_dates": [(history_start + timedelta(hours=i)).isoformat() for i in range(history_count)],
        "prediction": prediction.round(3).tolist(),
        "forecast": prediction.round(3).tolist(),
        "future_dates": [(target_start + timedelta(hours=i)).isoformat() for i in range(24)],
        "lower": np.maximum(0, prediction - radius).round(3).tolist(),
        "upper": (prediction + radius).round(3).tolist(),
        "threshold": round(threshold, 3),
        "high_hours": int((prediction > threshold).sum()),
        "metrics": metric_rows,
        "test_coverage": 0.0,
        "protocol": "历史同期模拟：所有训练与滞后输入均严格早于同期映射日期。",
        "limitations": "这是基于 UrbanEV 历史截面的同期模拟，不代表对当前年份真实负荷的实时预测。",
    }


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
