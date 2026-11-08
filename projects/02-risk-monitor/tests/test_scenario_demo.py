"""End-to-end demo: the clean session is silent, the bad one fires in the intended order.

Calibration (config/limits.toml) was done on pta's clean session, seeds 1..20 over
2 h plus seed 7 over 6 h; none of those sessions raises any alert, warn included.
"""
import io

import numpy as np
import pytest
from pta.data.synthetic import MarketConfig, simulate_market
from pta.strategy.market_maker import MMConfig, simulate_market_maker

from rmon import replay
from rmon.alerts import read_alerts, write_alerts
from rmon.engine import Engine
from rmon.limits import default_limits
from rmon.scenarios import bad, clean, phase_at, simulate_mm_segment

HOURS = 2.0
_RANK = {"warn": 0, "breach": 1, "clear": 2}


def run(sc):
    eng = Engine(default_limits())
    return eng, eng.run(replay(sc.market, sc.fills))


@pytest.fixture(scope="module")
def clean_run():
    sc = clean(seed=7, hours=HOURS)
    return (sc, *run(sc))


@pytest.fixture(scope="module")
def bad_run():
    sc = bad(seed=7, hours=HOURS)
    return (sc, *run(sc))


# ------------------------------------------------------------------- clean
def test_clean_session_raises_nothing(clean_run):
    # The calibrated limits leave no breach AND no warn on the clean session.
    sc, eng, alerts = clean_run
    assert len(sc.fills) > 4000
    assert [a for a in alerts if a.severity == "breach"] == []
    assert alerts == []
    assert all(v is None for v in eng.active().values())


def test_clean_session_margins(clean_run):
    """Pin the calibration margins quoted in limits.toml (seed 7, 2 h)."""
    sc, eng, _ = clean_run
    lim = default_limits()
    snap = eng.snapshot()
    pos = np.cumsum((sc.fills["side"] * sc.fills["qty"]).to_numpy())
    assert np.abs(pos).max() <= 0.6 * lim.position.warn
    assert snap["max_drawdown"] <= 0.3 * lim.loss_from_peak.warn
    one_s = next(m for m in snap["markouts"] if m["horizon_us"] == lim.markout.horizon_us)
    assert one_s["avg_bps"] > lim.markout.floor_bps + 1.0


# --------------------------------------------------------------------- bad
def test_bad_phases_are_contiguous(bad_run):
    sc, _, _ = bad_run
    names = [p.name for p in sc.phases]
    assert names == ["normal", "adverse", "inventory_run", "drought", "recovery"]
    assert sc.phases[0].start_us == 0
    for a, b in zip(sc.phases, sc.phases[1:]):
        assert a.end_us == b.start_us
    ts = sc.market["ts_us"].to_numpy()
    assert sc.phases[-1].end_us == ts[-1] + 100_000
    assert np.all(np.diff(ts) == 100_000)                    # one uniform grid
    assert len(ts) == int(HOURS * 36_000)


def test_bad_stitching_is_continuous(bad_run):
    sc, _, _ = bad_run
    m = sc.market.set_index("ts_us")["mid"]
    for p in sc.phases[1:]:
        jump = abs(m.loc[p.start_us] - m.loc[p.start_us - 100_000])
        assert jump <= 0.01 + 1e-9                            # at most one tick at a join


def test_bad_fires_expected_rules_in_order(bad_run):
    sc, eng, alerts = bad_run
    first = {}
    for a in alerts:
        first.setdefault((a.rule, a.severity), a)
    for rule, severity, allowed in sc.expected_order:
        assert (rule, severity) in first, f"never fired: {rule} {severity}"
        a = first[(rule, severity)]
        assert phase_at(sc.phases, a.ts_us) in allowed, (rule, severity, a.ts_us)
    # phases along the expected list never go backwards -> cross-phase order holds
    idx = {p.name: i for i, p in enumerate(sc.phases)}
    starts = [min(idx[x] for x in allowed) for _, _, allowed in sc.expected_order]
    assert starts == sorted(starts)
    times = [first[(r, s)].ts_us for r, s, _ in sc.expected_order]
    for i, (ri, si, ai) in enumerate(sc.expected_order):
        for j in range(i + 1, len(sc.expected_order)):
            rj, sj, aj = sc.expected_order[j]
            if max(idx[x] for x in ai) < min(idx[x] for x in aj) or ri == rj:
                assert times[i] < times[j], (sc.expected_order[i], sc.expected_order[j])
    # per rule, severities appear in warn -> breach -> clear order
    for rule in {r for r, _, _ in sc.expected_order}:
        seq = [s for r, s, _ in sc.expected_order if r == rule]
        assert seq == sorted(seq, key=_RANK.get)


def test_bad_nothing_before_the_problem_starts(bad_run):
    sc, _, alerts = bad_run
    normal = sc.phases[0]
    assert all(a.ts_us >= normal.end_us for a in alerts)


def test_bad_recovers_with_clear_events(bad_run):
    sc, eng, alerts = bad_run
    rec = sc.phases[-1]
    clears = {a.rule for a in alerts if a.severity == "clear" and a.ts_us >= rec.start_us}
    assert {"position", "fill_rate"} <= clears
    act = eng.active()
    assert act["position"] is None and act["fill_rate"] is None
    assert act["markout_degradation"] is None
    # every clear follows an active alert of the same rule
    level = {}
    for a in alerts:
        if a.severity == "clear":
            assert level.get(a.rule) is not None
            level[a.rule] = None
        else:
            level[a.rule] = a.severity


def test_bad_exact_sequence_seed7(bad_run):
    """Regression pin of the full seed-7 alert sequence (deterministic)."""
    sc, _, alerts = bad_run
    got = [(a.rule, a.severity, phase_at(sc.phases, a.ts_us)) for a in alerts]
    assert got == [
        ("markout_degradation", "breach", "adverse"),
        ("loss_from_peak", "warn", "adverse"),
        ("position", "warn", "inventory_run"),
        ("position", "breach", "inventory_run"),
        ("markout_degradation", "clear", "inventory_run"),
        ("loss_from_peak", "breach", "inventory_run"),
        ("fill_rate", "breach", "drought"),
        ("fill_rate", "clear", "recovery"),
        ("position", "clear", "recovery"),
    ]


# ------------------------------------------------- determinism and the log
def test_alert_log_is_deterministic_and_roundtrips(bad_run, tmp_path):
    sc, eng, alerts = bad_run
    _, again = run(bad(seed=7, hours=HOURS))
    buf1, buf2 = io.StringIO(), io.StringIO()
    write_alerts(alerts, buf1)
    write_alerts(again, buf2)
    assert buf1.getvalue() == buf2.getvalue() and buf1.getvalue().count("\n") == len(alerts)
    p = tmp_path / "alerts.jsonl"
    write_alerts(alerts, p)
    assert p.read_text() == buf1.getvalue()
    assert read_alerts(p) == alerts
    first = buf1.getvalue().splitlines()[0]
    assert first.startswith('{"ts_us":') and first.index('"rule"') < first.index('"severity"')
    assert eng.snapshot()["alerts"] == [a.to_dict() for a in alerts]


def test_mm_port_matches_pta():
    market, flow = simulate_market(MarketConfig(seed=3, hours=0.25))
    ref = simulate_market_maker(market, flow, MMConfig(seed=5))
    got, inv = simulate_mm_segment(market, flow, MMConfig(seed=5))
    assert got.equals(ref)
    assert inv == int((ref["side"] * ref["qty"]).sum())
