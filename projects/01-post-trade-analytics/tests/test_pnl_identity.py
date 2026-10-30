import numpy as np
import pandas as pd
import pytest

from pta import pnl
from pta.data.loaders import validate
from pta.data.synthetic import MarketConfig, simulate_market
from pta.strategy.market_maker import MMConfig, simulate_market_maker


def toy():
    market = pd.DataFrame({
        "ts_us": [0, 1, 2, 3],
        "bid": [99.9, 100.9, 99.9, 101.9],
        "ask": [100.1, 101.1, 100.1, 102.1],
        "mid": [100.0, 101.0, 100.0, 102.0],
    })
    fills = pd.DataFrame({
        "ts_us": [0, 2], "side": [1, -1], "px": [99.5, 100.5], "qty": [1, 1], "fee": [0.1, 0.1],
    })
    return fills, market


def test_toy_attribution_by_hand():
    fills, market = toy()
    _, t = pnl.attribute(fills, market)
    # buy 1 @99.5 vs mid 100 -> +0.5; sell 1 @100.5 vs mid 100 -> +0.5
    assert t["spread_capture"] == pytest.approx(1.0)
    # held +1 from mid 100 to final 102 (+2), then -1 from the sell (-2)
    assert t["inventory"] == pytest.approx(0.0)
    assert t["fees"] == pytest.approx(0.2)
    # cash +1.0, flat, less fees
    assert t["total"] == pytest.approx(0.8)


def test_toy_equity_matches_independent_cash_calculation():
    fills, market = toy()
    _, t = pnl.attribute(fills, market)
    curve = pnl.equity_curve(fills, market)
    assert curve["equity"].iloc[-1] == pytest.approx(t["total"])
    assert curve["position"].tolist() == [1, 1, 0, 0]


def test_flat_book_has_no_inventory_pnl():
    market = pd.DataFrame({"ts_us": [0, 1, 2], "bid": [0, 0, 0], "ask": [0, 0, 0], "mid": [10.0, 50.0, 10.0]})
    fills = pd.DataFrame({"ts_us": [0, 0], "side": [1, -1], "px": [10.0, 10.0], "qty": [3, 3], "fee": [0.0, 0.0]})
    _, t = pnl.attribute(fills, market)
    assert t["inventory"] == pytest.approx(0.0)
    assert t["total"] == pytest.approx(0.0)


@pytest.mark.parametrize("seed", [1, 2, 3])
def test_components_reconcile_to_equity_on_simulated_session(seed):
    market, flow = simulate_market(MarketConfig(seed=seed, hours=0.5))
    fills = simulate_market_maker(market, flow, MMConfig(seed=seed + 100))
    validate(fills, market)
    assert len(fills) > 100

    _, t = pnl.attribute(fills, market)
    curve = pnl.equity_curve(fills, market)
    # attribution total vs cash + position * mid computed without the decomposition
    assert t["total"] == pytest.approx(curve["equity"].iloc[-1], abs=1e-9)
    # per-fill inventory vs position_t * delta mid summed over the market grid
    assert t["inventory"] == pytest.approx(pnl.inventory_pnl_on_grid(curve, market), abs=1e-9)
    # running components end at the same total
    comp = pnl.cumulative_components(fills, market)
    last_mid = pnl.mid_asof(market, fills["ts_us"].to_numpy()[-1:])[0]
    final_eq_at_last_fill = curve["equity"][curve["ts_us"] == fills["ts_us"].iloc[-1]].iloc[-1]
    assert comp["total"].iloc[-1] == pytest.approx(final_eq_at_last_fill, abs=1e-9)
    assert np.isfinite(last_mid)


def test_fill_outside_market_range_is_rejected():
    fills, market = toy()
    fills.loc[1, "ts_us"] = 10
    with pytest.raises(ValueError):
        validate(fills, market)
