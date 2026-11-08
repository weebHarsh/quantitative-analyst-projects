"""Streaming state must reproduce Project 1's batch results exactly."""
import numpy as np
import pytest

from pta import markouts, pnl
from pta.data.synthetic import MarketConfig, simulate_market
from pta.risk import max_drawdown
from pta.strategy.market_maker import MMConfig, simulate_market_maker

from rmon import MonitorState, Quote, replay

TOL = dict(rel=1e-9, abs=1e-9)
SEEDS = [(1, 2), (7, 11), (23, 5)]


def session(seed_m, seed_f):
    market, flow = simulate_market(MarketConfig(seed=seed_m, hours=0.5))
    fills = simulate_market_maker(market, flow, MMConfig(seed=seed_f))
    assert len(fills) > 100
    return market, fills


def stream(market, fills):
    """Replay; record equity after each quote's instant (all events at that ts)."""
    st = MonitorState(horizons_us=markouts.DEFAULT_HORIZONS_US)
    eq, ident = [], []
    started = False
    for ev in replay(market, fills):
        if type(ev) is Quote:
            if started:
                eq.append(st.equity)
                ident.append(st.spread_capture + st.inventory_pnl - st.fees - st.equity)
            started = True
            st.on_quote(ev)
        else:
            st.on_fill(ev)
    eq.append(st.equity)
    ident.append(st.spread_capture + st.inventory_pnl - st.fees - st.equity)
    return st, np.array(eq), np.array(ident)


@pytest.fixture(scope="module", params=SEEDS, ids=lambda s: f"seed{s[0]}-{s[1]}")
def run(request):
    market, fills = session(*request.param)
    st, eq, ident = stream(market, fills)
    return market, fills, st, eq, ident


def test_final_attribution_matches(run):
    market, fills, st, _, _ = run
    _, t = pnl.attribute(fills, market)
    snap = st.snapshot()
    assert snap["spread_capture"] == pytest.approx(t["spread_capture"], **TOL)
    assert snap["inventory_pnl"] == pytest.approx(t["inventory"], **TOL)
    assert snap["fees"] == pytest.approx(t["fees"], **TOL)
    assert snap["total_pnl"] == pytest.approx(t["total"], **TOL)
    assert snap["equity"] == pytest.approx(t["total"], **TOL)
    assert snap["mid"] == t["final_mid"]
    assert snap["fills"] == len(fills)
    assert snap["traded_qty"] == pytest.approx(float(fills["qty"].sum()))
    assert snap["position"] == pytest.approx(float((fills["side"] * fills["qty"]).sum()))


def test_equity_on_every_tick_matches(run):
    market, fills, _, eq, ident = run
    curve = pnl.equity_curve(fills, market)
    ref = curve["equity"].to_numpy()
    assert len(eq) == len(ref)
    np.testing.assert_allclose(eq, ref, rtol=1e-9, atol=1e-9)
    # explicitly: first, last and every fill timestamp
    idx = np.searchsorted(market["ts_us"].to_numpy(), fills["ts_us"].to_numpy())
    for i in [0, len(ref) - 1, *np.unique(idx)]:
        assert eq[i] == pytest.approx(ref[i], **TOL)
    # attribution identity holds at every instant
    assert np.max(np.abs(ident)) < 1e-9


def test_max_drawdown_matches(run):
    market, fills, st, _, _ = run
    ref_eq = pnl.equity_curve(fills, market)["equity"].to_numpy()
    peak = np.maximum.accumulate(np.concatenate([[0.0], ref_eq]))[1:]
    ref = float(np.max(peak - ref_eq))
    assert ref == pytest.approx(max_drawdown(ref_eq), abs=1e-12)
    assert st.max_drawdown == pytest.approx(ref, **TOL)
    assert st.snapshot()["max_drawdown"] == pytest.approx(ref, **TOL)
    assert st.peak_equity == pytest.approx(max(0.0, float(ref_eq.max())), **TOL)


def test_markouts_match(run):
    market, fills, st, _, _ = run
    ref = markouts.summarize(fills, markouts.compute(fills, market, markouts.DEFAULT_HORIZONS_US))
    ref = ref.set_index("horizon_us")
    got = {m["horizon_us"]: m for m in st.snapshot()["markouts"]}
    assert set(got) == set(markouts.DEFAULT_HORIZONS_US)
    for h in markouts.DEFAULT_HORIZONS_US:
        g, r = got[h], ref.loc[h]
        assert g["n"] == r["n"]
        assert g["pending"] == len(fills) - r["n"]
        assert g["qty"] == pytest.approx(r["qty"], **TOL)
        assert g["total"] == pytest.approx(r["total"], **TOL)
        assert g["avg_bps"] == pytest.approx(r["avg_bps"], **TOL)
        assert g["pct_positive"] == pytest.approx(r["pct_positive"], **TOL)
    # long horizons lose fills at the end of the session
    assert got[60_000_000]["n"] < got[0]["n"] == len(fills)


def test_snapshot_is_json_serialisable(run):
    import json
    _, _, st, _, _ = run
    json.loads(json.dumps(st.snapshot(), allow_nan=False))
