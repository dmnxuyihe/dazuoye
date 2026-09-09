"""Single authoritative meter used by live queries, stopping and the coordinator.

Tariff periods use Asia/Shanghai wall-clock hours. The demo multiplier affects
energy delivery, never the clock used to choose the price. Prices are frozen at reservation.
"""
import json
from datetime import timedelta, timezone
from decimal import Decimal, ROUND_HALF_UP

CENT = Decimal('0.01')
KWH = Decimal('0.000001')
LOCAL = timezone(timedelta(hours=8))


def money(value):
    return Decimal(value).quantize(CENT, rounding=ROUND_HALF_UP)


def tariff_rows(value, unit_price):
    if isinstance(value, str):
        value = json.loads(value)
    return value or [dict(start_hour=0, end_hour=24, electricity_price=str(unit_price), service_price='0')]


def meter(order, now, available_balance):
    start = order['started_at']
    if start is None:
        return dict(energy_kwh=Decimal(0), amount=Decimal(0), billing_detail=[], capped=False)
    periods = tariff_rows(order['tariff_snapshot'], order['unit_price'])
    rate = Decimal(order['power_kw']) * Decimal(order['time_scale']) / 3600
    target = order['target_energy_kwh']
    energy, amount = Decimal(0), Decimal(0)
    budget = max(Decimal(0), Decimal(available_balance))
    cursor, finish = start, max(start, now)
    detail = []
    reason = None
    while cursor < finish:
        local = cursor.astimezone(LOCAL)
        band = next(p for p in periods if p['start_hour'] <= local.hour < p['end_hour'])
        midnight = local.replace(hour=0, minute=0, second=0, microsecond=0)
        boundary = midnight + timedelta(hours=band['end_hour'])
        end = min(finish, boundary)
        seconds = Decimal(str((end - cursor).total_seconds()))
        price = Decimal(str(band['electricity_price'])) + Decimal(str(band['service_price']))
        delivery = seconds * rate
        allowed = delivery
        if target is not None and allowed >= target - energy:
            allowed = max(Decimal(0), target - energy)
            reason = 'target_reached'
        if allowed * price >= budget - amount:
            allowed = max(Decimal(0), (budget - amount) / price)
            reason = 'balance_exhausted'
        seconds = allowed / rate
        end = min(end, cursor + timedelta(seconds=float(seconds)))
        cost = allowed * price
        if allowed > 0:
            detail.append(dict(start=cursor.isoformat(), end=end.isoformat(),
                electricity_price=str(band['electricity_price']), service_price=str(band['service_price']),
                unit_price=str(price), energy_kwh=str(allowed.quantize(KWH,rounding=ROUND_HALF_UP)), amount=str(money(cost))))
        energy += allowed
        amount += cost
        cursor = end
        if reason:
            break
    rounded = min(budget, money(amount))
    if detail:
        detail[-1]['amount'] = str(money(Decimal(detail[-1]['amount']) + rounded - sum(Decimal(d['amount']) for d in detail)))
    return dict(energy_kwh=energy.quantize(KWH,rounding=ROUND_HALF_UP), amount=rounded,
                billing_detail=detail, ended_at=cursor, capped=reason is not None, cap_reason=reason)
