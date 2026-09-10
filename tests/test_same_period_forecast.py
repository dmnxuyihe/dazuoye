"""Calendar and missing-observation regressions for admin-local forecasting."""
import unittest
from datetime import date, datetime, timedelta

from charging_core.forecast import historical_same_period_forecast, map_current_date_to_dataset_date


class SamePeriodTest(unittest.TestCase):
    def rows(self, days=35):
        return [(datetime(2022, 9, 1) + timedelta(hours=i), float(100 + i % 24))
                for i in range(days * 24)]

    def test_calendar(self):
        self.assertEqual(map_current_date_to_dataset_date(date(2026, 9, 10)), date(2022, 9, 10))
        self.assertIsNone(map_current_date_to_dataset_date(date(2028, 2, 29)))
        self.assertIsNone(map_current_date_to_dataset_date(date(2026, 6, 1)))

    def test_missing_yesterday_is_not_relabelled(self):
        result = historical_same_period_forecast(self.rows(8), date(2026, 9, 10), 'test')
        self.assertFalse(result['available'])

    def test_missing_day_breaks_lags(self):
        rows = [(t, v) for t, v in self.rows() if t.date() != date(2022, 9, 25)]
        result = historical_same_period_forecast(rows, date(2026, 10, 1), 'test')
        self.assertTrue(result['available'])
        self.assertFalse(result['history_sufficient'])
        self.assertEqual(len(result['metrics']), 1)
        self.assertEqual(result['history_dates'][-1], '2022-09-30T23:00:00')

    def test_invalid_hour(self):
        for bad in (None, float('nan'), float('inf'), -1):
            with self.subTest(bad=bad):
                rows = self.rows(9)
                rows[-1] = (rows[-1][0], bad)
                result = historical_same_period_forecast(rows, date(2026, 9, 10), 'test')
                self.assertFalse(result['available'])

    def test_causal_and_finite(self):
        rows = self.rows()
        result = historical_same_period_forecast(rows, date(2026, 10, 1), 'test')
        changed = [(t, v if t < datetime(2022, 10, 1) else 1e9) for t, v in rows]
        self.assertEqual(result, historical_same_period_forecast(changed, date(2026, 10, 1), 'test'))
        self.assertEqual(len(result['prediction']), 24)
        self.assertEqual(len(result['history']), 48)
        self.assertTrue(result['history_sufficient'])
        self.assertTrue(all(0 <= lo <= p <= hi for lo, p, hi in zip(result['lower'], result['prediction'], result['upper'])))

    def test_short_history_does_not_claim_zero_error(self):
        result = historical_same_period_forecast(self.rows(1), date(2026, 9, 2), 'test')
        self.assertTrue(result['available'])
        self.assertFalse(result['interval_calibrated'])
        self.assertIsNone(result['metrics'][0]['wape'])
        self.assertIsNone(result['test_coverage'])


if __name__ == '__main__':
    unittest.main()
