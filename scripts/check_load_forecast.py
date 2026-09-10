"""Check causal feature boundaries, serialized outputs and protected forecast API."""
import importlib.util
import json
import os
from datetime import datetime, timedelta
from pathlib import Path
import urllib.request
import urllib.error
import numpy as np

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('training', root / 'scripts/train_load_forecast.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
rng = np.random.default_rng(42)
y = rng.uniform(1, 10, (3, 40, 24))
dates = [datetime(2023, 1, 1) + timedelta(days=i) for i in range(40)]
pred, weights, scale = m.fit_predict(y, dates, 30, 31)
changed = y.copy()
changed[:, 30:] = 1000000
p2, w2, s2 = m.fit_predict(changed, dates, 30, 31)
np.testing.assert_allclose(pred, p2)
np.testing.assert_allclose(weights, w2)
np.testing.assert_allclose(scale, s2)
# Even changing later hours on the target day cannot alter that day's inputs.
x1 = m.features(y, dates, scale)
x2 = m.features(changed, dates, scale)
np.testing.assert_allclose(x1[:, :24 * (31 - 7)], x2[:, :24 * (31 - 7)])
artifact = json.loads((root / 'src/charging_core/data/load_forecast.json').read_text())
meta = artifact['metadata']
assert meta['train_end'] < meta['validation_start'] < meta['validation_end'] < meta['test_start'] <= meta['test_end'] < meta['future_dates'][0]
assert len(artifact['results']) == 276 and meta['rows'] == 1194600
for result in artifact['results'].values():
    assert len(result['history']) == 48
    assert len(result['prediction']) == len(result['upper']) == len(result['lower']) == 24
    assert all(0 <= lo <= p <= hi for lo, p, hi in zip(result['lower'], result['prediction'], result['upper']))
    selected = next(m for m in result['metrics'] if m['selected'])
    assert selected['validation_mae'] == min(m['validation_mae'] for m in result['metrics'])
    assert 0 <= result['test_coverage'] <= 100


def request(path, token=None, method='GET'):
    req = urllib.request.Request(os.environ.get('CHARGING_TEST_API', 'http://127.0.0.1:4173') + path, method=method, headers={'Authorization': 'Bearer ' + token} if token else {})
    try:
        with urllib.request.urlopen(req, timeout=20) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)

assert request('/admin/console/forecast')[0] == 401
_, session = request('/auth/console', method='POST')
token = session['access_token']
status, body = request('/admin/console/forecast?mode=artifact&scope=all', token)
assert status == 200 and body['scope'] == 'all' and body['ready']
assert request('/admin/console/forecast?mode=artifact&scope=station-1075', token)[1]['scope'] == 'station-1075'
status, same = request('/admin/console/forecast', token)
assert status == 200 and same['scope'] == 'business' and same['ready']
assert same['forecast_type'] == 'historical_same_period'
assert len(same['scopes']) >= 2
if same['available']:
    assert len(same['prediction']) == 24
    assert len(same['history']) == len(same['history_dates'])
    assert same['history_dates'][-1] < same['future_dates'][0]
assert request('/admin/console/forecast?scope=station-999999', token)[0] == 404
assert request('/admin/console/forecast?scope=../x', token)[0] == 422
_, demo = request('/auth/demo', method='POST')
assert request('/admin/console/forecast', demo['access_token'])[0] == 403
print('PASS: future-target perturbation leaves forecast/weights/scale unchanged; chronological partitions; 276 valid scopes; validation-only model selection; API admin auth/region/404/422/read-only role protection.')
