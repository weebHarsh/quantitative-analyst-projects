import numpy as np
import pandas as pd
import pytest

from pta import markouts, pnl

from rmon import Fill, MonitorState, Quote, paced, replay, run_session

S = 1_000_000


def q(t, mid):
    return Quote(t, mid - 0.5, mid + 0.5, mid)


def toy_market():
    return pd.DataFrame({"ts_us": [0, S, 2 * S, 3 * S], "bid": 0.0, "ask": 0.0,
                         "mid": [100.0, 101.0, 100.0, 102.0]})


def toy_fills():
    return pd.DataFrame({"ts_us": [0, S], "side": [1, -1], "px": [99.5, 101.5],
                         "qty": [2.0, 1.0], "fee": [0.1, -0.05]})


def by_h(st):
    return {m["horizon_us"]: m for m in st.snapshot()["markouts"]}


def test_hand_computed_toy_session():
    st = run_session(toy_market(), toy_fills(), horizons_us=(0, S, 1_500_000, 5 * S))
    s = st.snapshot()
    # cash = -2*99.5 + 101.5 = -97.5, position 1, fees 0.1 - 0.05
    assert s["position"] == 1.0
    assert s["cash"] == pytest.approx(-97.5)
    assert s["fees"] == pytest.approx(0.05)
    assert s["equity"] == pytest.approx(-97.5 + 102 - 0.05)
    assert s["spread_capture"] == pytest.approx(2 * 0.5 + 0.5)
    assert s["inventory_pnl"] == pytest.approx(2 * (102 - 100) - (102 - 101))
    assert s["total_pnl"] == pytest.approx(4.45)
    # instants: t0 0.9, t1 3.45, t2 2.45, t3 4.45 -> peak 4.45, max dd 1.0
    assert s["peak_equity"] == pytest.approx(4.45)
    assert s["drawdown"] == pytest.approx(0.0)
    assert s["max_drawdown"] == pytest.approx(1.0)
    m = by_h(st)
    assert m[0]["n"] == 2 and m[0]["total"] == pytest.approx(2 * 0.5 + 0.5)
    assert m[S]["n"] == 2 and m[S]["total"] == pytest.approx(2 * 1.5 + 1.5)
    # buy @99.5 at t0 + 1.5s -> mid(1.5s) = 101 ; sell @101.5 at 1s + 1.5s -> mid 100
    assert m[1_500_000]["total"] == pytest.approx(2 * 1.5 + 1.5)
    assert m[5 * S]["n"] == 0 and m[5 * S]["pending"] == 2 and m[5 * S]["avg_bps"] is None
    _, t = pnl.attribute(toy_fills(), toy_market())
    assert s["total_pnl"] == pytest.approx(t["total"])


def test_fill_on_quote_timestamp_uses_that_quote():
    st = MonitorState(horizons_us=(0,))
    st.on_quote(q(0, 100.0))
    st.on_quote(q(10, 105.0))
    st.on_fill(Fill(10, 1, 104.0, 1.0, 0.0))
    assert st.spread_capture == pytest.approx(1.0)        # vs 105, not 100
    assert by_h(st)[0]["total"] == pytest.approx(1.0)


def test_replay_puts_quote_before_fills_at_equal_ts():
    market = toy_market()
    fills = pd.DataFrame({"ts_us": [S, S, 2 * S + 5], "side": [1, -1, 1],
                          "px": [100.0, 102.0, 99.0], "qty": [1.0, 1.0, 1.0], "fee": 0.0})
    evs = list(replay(market, fills))
    kinds = [(type(e).__name__, e.ts_us) for e in evs]
    assert kinds == [("Quote", 0), ("Quote", S), ("Fill", S), ("Fill", S),
                     ("Quote", 2 * S), ("Fill", 2 * S + 5), ("Quote", 3 * S)]


def test_duplicate_fill_timestamps():
    market = toy_market()
    fills = pd.DataFrame({"ts_us": [S, S, S], "side": [1, 1, -1], "px": [100.5, 100.0, 101.5],
                          "qty": [1.0, 2.0, 1.5], "fee": [0.01, 0.02, -0.01]})
    st = run_session(market, fills, horizons_us=markouts.DEFAULT_HORIZONS_US)
    _, t = pnl.attribute(fills, market)
    assert st.spread_capture == pytest.approx(t["spread_capture"])
    assert st.inventory_pnl == pytest.approx(t["inventory"])
    assert st.equity == pytest.approx(t["total"])
    ref = markouts.summarize(fills, markouts.compute(fills, market)).set_index("horizon_us")
    for h, m in by_h(st).items():
        assert m["n"] == ref.loc[h, "n"]
        assert m["total"] == pytest.approx(ref.loc[h, "total"])


def test_fill_before_first_quote_rejected():
    st = MonitorState()
    with pytest.raises(ValueError, match="before any quote"):
        st.on_fill(Fill(0, 1, 100.0, 1.0, 0.0))


def test_backwards_time_rejected():
    st = MonitorState()
    st.on_quote(q(100, 100.0))
    with pytest.raises(ValueError):
        st.on_quote(q(50, 100.0))
    with pytest.raises(ValueError):
        st.on_quote(q(100, 100.0))            # quotes strictly increasing
    st.on_fill(Fill(120, 1, 100.0, 1.0, 0.0))
    with pytest.raises(ValueError):
        st.on_fill(Fill(110, 1, 100.0, 1.0, 0.0))
    with pytest.raises(ValueError):
        st.on_quote(q(120, 101.0))            # would rewrite the fill's mid


def test_bad_fill_fields_rejected():
    st = MonitorState()
    st.on_quote(q(0, 100.0))
    with pytest.raises(ValueError):
        st.on_fill(Fill(0, 0, 100.0, 1.0, 0.0))
    with pytest.raises(ValueError):
        st.on_fill(Fill(0, 1, 100.0, 0.0, 0.0))


def test_markouts_past_end_are_dropped():
    n = 50
    market = pd.DataFrame({"ts_us": np.arange(n) * S, "bid": 0.0, "ask": 0.0,
                           "mid": 100.0 + np.sin(np.arange(n))})
    fills = pd.DataFrame({"ts_us": np.arange(0, n, 2) * S, "side": 1, "px": 99.0,
                          "qty": 1.0, "fee": 0.0})
    hs = (0, S, 10 * S, 40 * S)
    st = run_session(market, fills, horizons_us=hs)
    m = by_h(st)
    ns = [m[h]["n"] for h in hs]
    assert ns[0] == len(fills) and ns == sorted(ns, reverse=True) and ns[-1] < ns[1]
    ref = markouts.summarize(fills, markouts.compute(fills, market, hs)).set_index("horizon_us")
    for h in hs:
        assert m[h]["n"] == ref.loc[h, "n"]
        assert m[h]["pending"] == len(fills) - m[h]["n"]
        assert m[h]["total"] == pytest.approx(ref.loc[h, "total"])


def test_markout_target_exactly_on_last_quote_resolves():
    st = MonitorState(horizons_us=(S,))
    st.on_quote(q(0, 100.0))
    st.on_fill(Fill(0, 1, 99.0, 1.0, 0.0))
    assert by_h(st)[S]["pending"] == 1
    st.on_quote(q(S, 103.0))
    m = by_h(st)[S]
    assert m["n"] == 1 and m["total"] == pytest.approx(4.0)


def test_markout_resolves_from_previous_quote_when_target_skipped():
    st = MonitorState(horizons_us=(S,))
    st.on_quote(q(0, 100.0))
    st.on_fill(Fill(0, -1, 101.0, 2.0, 0.0))
    st.on_quote(q(S // 2, 100.5))
    assert by_h(st)[S]["pending"] == 1
    st.on_quote(q(3 * S, 90.0))                # jumps past target: answer is 100.5
    m = by_h(st)[S]
    assert m["n"] == 1 and m["total"] == pytest.approx(2 * (101.0 - 100.5))


def test_transient_equity_inside_an_instant_is_not_a_drawdown():
    st = MonitorState(horizons_us=(0,))
    st.on_quote(q(0, 100.0))
    st.on_fill(Fill(0, 1, 99.0, 1.0, 0.0))     # equity 1 at t0
    st.on_quote(q(1, 99.0))                    # transient equity 0
    st.on_fill(Fill(1, -1, 100.0, 1.0, 0.0))   # back to 1 at t1
    st.on_quote(q(2, 50.0))                    # flat, equity stays 1
    assert st.max_drawdown == 0.0
    assert st.peak_equity == pytest.approx(1.0)


def test_rolling_windows():
    st = MonitorState(horizons_us=(0,), markout_window_us=10, fill_window_us=10)
    st.on_quote(q(0, 100.0))
    st.on_fill(Fill(0, 1, 99.0, 1.0, 0.0))     # +1 per unit = 101.0101 bps on px 99
    st.on_fill(Fill(5, 1, 100.0, 3.0, 0.0))    # 0 bps
    s = st.snapshot()
    assert s["window_fills"] == 2 and s["window_qty"] == 4.0
    m = s["markouts"][0]
    assert m["rolling_n"] == 2
    assert m["rolling_bps"] == pytest.approx((1.0 / 99 * 1e4) / 4)
    st.on_quote(q(10, 100.0))                  # t0 entries fall out (ts <= now - 10)
    s = st.snapshot()
    assert s["window_fills"] == 1 and s["window_qty"] == 3.0
    assert s["markouts"][0]["rolling_bps"] == pytest.approx(0.0)
    st.on_quote(q(100, 100.0))
    s = st.snapshot()
    assert s["window_fills"] == 0 and s["markouts"][0]["rolling_bps"] is None
    assert s["markouts"][0]["n"] == 2          # cumulative aggregates are kept


def test_paced_does_not_change_events():
    evs = list(replay(toy_market(), toy_fills()))
    now = [0.0]
    slept = []

    def sleep(d):
        slept.append(d)
        now[0] += d

    out = list(paced(evs, speed=2.0, clock=lambda: now[0], sleep=sleep))
    assert out == evs
    assert sum(slept) == pytest.approx(1.5)    # 3 s of session at 2x
    assert list(paced(evs, speed=0)) == evs
