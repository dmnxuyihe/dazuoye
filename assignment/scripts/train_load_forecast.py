"""Reproducible daily-origin, next-24-hour UrbanEV experiment. No DB writes."""

import asyncio
import hashlib
import json
from datetime import timedelta
from pathlib import Path

import asyncpg
import numpy as np
from charging_core.config import Settings

ROOT = Path(__file__).resolve().parents[1]
NAMES = ["昨日同小时", "上周同小时", "岭回归"]


def features(y, dates, scale):
    # Every target day's 24 predictions use only data strictly before midnight.
    blocks = [
        y[:, 7 - lag : len(dates) - lag] / scale[:, None, None] for lag in range(1, 8)
    ]
    previous_mean = np.repeat(y[:, 6:-1].mean(axis=2)[:, :, None], 24, axis=2)
    blocks += [previous_mean / scale[:, None, None]]
    hour = np.broadcast_to(np.arange(24), y[:, 7:].shape)
    weekday = np.broadcast_to(
        np.array([d.weekday() for d in dates[7:]])[None, :, None], hour.shape
    )
    blocks += [
        np.sin(hour * np.pi / 12),
        np.cos(hour * np.pi / 12),
        np.sin(weekday * 2 * np.pi / 7),
        np.cos(weekday * 2 * np.pi / 7),
        np.ones_like(hour),
    ]
    return np.stack(blocks, axis=-1).reshape(y.shape[0], -1, len(blocks))


def fit_predict(y, dates, end, stop):
    scale = np.maximum(y[:, :end].mean(axis=(1, 2)), 1)
    x = features(y, dates, scale)
    train = x[:, : (end - 7) * 24]
    target = y[:, 7:end].reshape(y.shape[0], -1) / scale[:, None]
    regularizer = np.eye(x.shape[-1])
    regularizer[-1, -1] = 0
    weights = np.linalg.solve(
        np.einsum("sni,snj->sij", train, train) + regularizer,
        np.einsum("sni,sn->si", train, target)[..., None],
    )[..., 0]
    pred = np.maximum(
        0,
        np.einsum("sni,si->sn", x[:, (end - 7) * 24 : (stop - 7) * 24], weights)
        * scale[:, None],
    )
    return pred, weights, scale


def scores(pred, actual):
    error = pred - actual
    denominator = np.abs(actual).sum()
    return {
        "mae": round(float(np.abs(error).mean()), 3),
        "rmse": round(float(np.sqrt((error**2).mean())), 3),
        "wape": (
            round(float(np.abs(error).sum() / denominator * 100), 3)
            if denominator
            else None
        ),
    }


async def main():
    settings = Settings()
    db = await asyncpg.connect(
        settings.database_url,
        server_settings={"search_path": settings.database_schema, "jit": "off"},
    )
    try:
        rows = await db.fetch(
            "SELECT observed_at, array_agg(zone_id ORDER BY zone_id) AS zones, array_agg(energy_kwh ORDER BY zone_id) AS energy FROM urbanev_observation GROUP BY observed_at ORDER BY observed_at"
        )
        metadata = json.loads(
            await db.fetchval(
                "SELECT metadata FROM dataset_metadata WHERE name='UrbanEV-full'"
            )
        )
    finally:
        await db.close()
    assert rows and len(rows) % 24 == 0 and rows[0]["observed_at"].hour == 0
    zones = rows[0]["zones"]
    assert all(row["zones"] == zones for row in rows), "Incomplete region panel"
    assert all(
        b["observed_at"] - a["observed_at"] == timedelta(hours=1)
        for a, b in zip(rows, rows[1:])
    ), "Non-hourly data"
    values = np.array([row["energy"] for row in rows], dtype=np.float64)
    assert (
        np.isfinite(values).all() and (values >= 0).all()
    ), "Missing/invalid targets: abort instead of silently imputing"
    dates = [row["observed_at"] for row in rows[::24]]
    y = np.concatenate([values.sum(axis=1)[None, :], values.T]).reshape(
        len(zones) + 1, -1, 24
    )
    days = len(dates)
    assert days >= 100
    train_end, val_end = days - 42, days - 28
    ridge_val, _, _ = fit_predict(y, dates, train_end, val_end)
    val_actual = y[:, train_end:val_end].reshape(len(y), -1)
    val_predictions = [
        y[:, train_end - lag : val_end - lag].reshape(len(y), -1) for lag in (1, 7)
    ] + [ridge_val]
    errors = np.array([np.abs(p - val_actual).mean(axis=1) for p in val_predictions])
    chosen = errors.argmin(axis=0)  # Validation only; never select using held-out test.
    selected_val = np.stack(val_predictions)[chosen, np.arange(len(y))]
    radius = np.quantile(np.abs(selected_val - val_actual), 0.9, axis=1)
    ridge_test, _, _ = fit_predict(y, dates, val_end, days)
    test_predictions = [
        y[:, val_end - lag : days - lag].reshape(len(y), -1) for lag in (1, 7)
    ] + [ridge_test]
    actual = y[:, val_end:].reshape(len(y), -1)
    selected_test = np.stack(test_predictions)[chosen, np.arange(len(y))]
    # Append a target day only to construct inference features; never fit on it.
    extended = np.concatenate([y, np.zeros((len(y), 1, 24))], axis=1)
    ridge_future, weights, scale = fit_predict(
        extended, dates + [dates[-1] + timedelta(days=1)], days, days + 1
    )
    future = np.stack([y[:, -1], y[:, -7], ridge_future])[chosen, np.arange(len(y))]
    history_dates = [
        (rows[-48]["observed_at"] + timedelta(hours=i)).isoformat() for i in range(48)
    ]
    future_dates = [
        (rows[-1]["observed_at"] + timedelta(hours=i + 1)).isoformat()
        for i in range(24)
    ]
    common = {
        "ready": True,
        "source": "UrbanEV · 数据库 energy_kwh / volume.csv",
        "source_commit": metadata.get("commit"),
        "data_sha256": hashlib.sha256(values.tobytes()).hexdigest(),
        "unit": "kWh/小时",
        "timezone": "Asia/Shanghai（沿用源数据本地小时）",
        "start": rows[0]["observed_at"].isoformat(),
        "cutoff": rows[-1]["observed_at"].isoformat(),
        "train_end": (dates[train_end] - timedelta(hours=1)).isoformat(),
        "validation_start": dates[train_end].isoformat(),
        "validation_end": (dates[val_end] - timedelta(hours=1)).isoformat(),
        "test_start": dates[val_end].isoformat(),
        "test_end": rows[-1]["observed_at"].isoformat(),
        "rows": len(rows) * len(zones),
        "regions": len(zones),
        "numpy_version": np.__version__,
        "ridge_alpha": 1.0,
        "features": "过去7天同小时电量、昨日均值、小时与星期周期编码、截距；按训练期区域均值缩放",
        "protocol": "逐日零点预测未来24小时；验证集选模型；测试期参数固定，滞后输入随日更新；最终使用全部历史重训。",
        "limitations": "历史实验，非实时预测。区域电量由额定功率等估算，不是站点实测功率；源数据已预处理，可能包含非因果缺失填补。阴影为验证集90%绝对误差经验范围，不保证未来覆盖率。全网独立训练，不强制等于区域预测之和。",
        "history_dates": history_dates,
        "future_dates": future_dates,
        "scopes": [{"id": "all", "label": "全网 · 深圳"}]
        + [{"id": str(z), "label": f"区域 {z}"} for z in zones],
    }
    results = {}
    for i, scope in enumerate(common["scopes"]):
        metrics = [
            {
                "model": name,
                **scores(p[i], actual[i]),
                "validation_mae": round(float(errors[j, i]), 3),
                "selected": j == int(chosen[i]),
            }
            for j, (name, p) in enumerate(zip(NAMES, test_predictions))
        ]
        pred = future[i]
        threshold = float(np.quantile(y[i], 0.95))
        results[scope["id"]] = {
            "label": scope["label"],
            "model": NAMES[chosen[i]],
            "metrics": metrics,
            "history": y[i, -2:].reshape(-1).round(3).tolist(),
            "prediction": pred.round(3).tolist(),
            "lower": np.maximum(0, pred - radius[i]).round(3).tolist(),
            "upper": (pred + radius[i]).round(3).tolist(),
            "threshold": round(threshold, 3),
            "high_hours": int((pred > threshold).sum()),
            "test_coverage": round(
                float((np.abs(selected_test[i] - actual[i]) <= radius[i]).mean() * 100),
                2,
            ),
        }
    output = ROOT / "src/charging_core/data/load_forecast.json"
    output.write_text(
        json.dumps(
            {"metadata": common, "results": results},
            ensure_ascii=False,
            allow_nan=False,
        ),
        encoding="utf-8",
    )
    np.savez_compressed(
        ROOT / "src/charging_core/data/load_forecast_weights.npz",
        weights=weights,
        scale=scale,
        chosen=chosen,
        zones=np.array(zones),
        ridge_alpha=np.array(1.0),
    )
    print(
        json.dumps(
            {
                "data": {
                    k: common[k]
                    for k in [
                        "rows",
                        "regions",
                        "cutoff",
                        "train_end",
                        "validation_start",
                        "test_start",
                    ]
                },
                "all_network": results["all"]["metrics"],
                "selected": results["all"]["model"],
                "test_coverage": results["all"]["test_coverage"],
                "selected_counts": {
                    name: int((chosen == i).sum()) for i, name in enumerate(NAMES)
                },
            },
            ensure_ascii=False,
            indent=2,
        )
    )


if __name__ == "__main__":
    asyncio.run(main())
