import numpy as np
import pandas as pd
import pytest

from pta import markouts, pnl

S = 1_000_000


def market():
    return pd.DataFrame({
        "ts_us": [0, S, 2 * S, 3 * S],
        "bid": 0.0, "ask": 0.0,
        "mid": [100.0, 101.0, 100.0, 102.0],
    })


def fills():
    return pd.DataFrame({
        "ts_us": [0, S],
        "side": [1, -1],
        "px": [99.5, 101.5],
        "qty": [2, 1],
        "fee": [0.0, 0.0],
    })


def test_markout_values_by_hand():
    mo = markouts.compute(fills(), market(), horizons_us=(0, S, 5 * S))
    # buy @99.5: h=0 mid 100 -> +0.5; h=1s mid 101 -> +1.5; h=5s is past the end -> NaN
    assert mo.loc[0, "mo_0"] == pytest.approx(0.5)
    assert mo.loc[0, "mo_1000000"] == pytest.approx(1.5)
    assert np.isnan(mo.loc[0, "mo_5000000"])
    # sell @101.5: h=0 mid 101 -> +0.5; h=1s mid 100 -> -1*(100-101.5) = +1.5
    assert mo.loc[1, "mo_0"] == pytest.approx(0.5)
    assert mo.loc[1, "mo_1000000"] == pytest.approx(1.5)


def test_summary_is_quantity_weighted_and_drops_nan():
    f = fills()
    mo = markouts.compute(f, market(), horizons_us=(0, S, 5 * S))
    s = markouts.summarize(f, mo).set_index("horizon_us")
    assert s.loc[S, "n"] == 2
    assert s.loc[S, "qty"] == 3
    assert s.loc[S, "total"] == pytest.approx(2 * 1.5 + 1 * 1.5)
    assert s.loc[5 * S, "n"] == 0
    assert s.loc[0, "pct_positive"] == 1.0


def test_horizon_zero_equals_spread_capture():
    f = fills()
    mo = markouts.compute(f, market(), horizons_us=(0,))
    per_fill, _ = pnl.attribute(f, market())
    expected = per_fill["spread_capture"].to_numpy() / f["qty"].to_numpy()
    assert mo["mo_0"].to_numpy() == pytest.approx(expected)


def test_lookup_between_ticks_uses_last_quote():
    m = market()
    assert pnl.mid_asof(m, np.array([S // 2]))[0] == 100.0
    assert pnl.mid_asof(m, np.array([S + 1]))[0] == 101.0
    assert np.isnan(pnl.mid_asof(m, np.array([-1]))[0])
    assert np.isnan(pnl.mid_asof(m, np.array([3 * S + 1]))[0])
