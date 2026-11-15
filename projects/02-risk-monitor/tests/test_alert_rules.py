"""Alert rules on constructed snapshots and tiny event streams."""
import copy
import dataclasses

import pytest

from rmon import Fill, Quote
from rmon.alerts import (Alert, FillRateRule, LossFromPeakRule, MarkoutRule, PositionRule,
                         RuleState, build_rules)
from rmon.engine import Engine
from rmon.limits import limits_from_dict
from rmon.state import MonitorState

S = 1_000_000  # one second in us

RAW = {
    "position": {"warn": 60, "breach": 100, "hysteresis": 20, "cooldown_us": 60 * S},
    "loss_from_peak": {"warn": 100, "breach": 200, "hysteresis": 25, "cooldown_us": 300 * S},
    "fill_rate": {"window_us": 60 * S, "min_fills": 6, "max_fills": 150,
                  "warmup_us": 120 * S, "hysteresis": 4, "cooldown_us": 120 * S},
    "markout": {"horizon_us": S, "window_us": 300 * S, "floor_bps": -0.75,
                "min_samples": 50, "hysteresis_bps": 0.5, "cooldown_us": 300 * S},
}
LIM = limits_from_dict(RAW)


def snap(ts, position=0.0, drawdown=0.0, window_fills=40, mo_bps=0.3, mo_n=100):
    return {
        "ts_us": ts, "mid": 100.0, "position": position, "equity": -drawdown,
        "peak_equity": 0.0, "drawdown": drawdown, "max_drawdown": drawdown,
        "fills": 0, "window_fills": window_fills, "window_qty": 2.0 * window_fills,
        "fill_window_us": 60 * S,
        "markouts": [
            {"horizon_us": 0, "rolling_n": mo_n, "rolling_qty": 2.0 * mo_n, "rolling_bps": 0.5},
            {"horizon_us": S, "rolling_n": mo_n, "rolling_qty": 2.0 * mo_n,
             "rolling_bps": None if mo_n == 0 else mo_bps},
        ],
    }


def drive(rule, snaps, state=None):
    """Feed snapshots through one rule; return (final state, all alerts)."""
    out = []
    for s in snaps:
        state, a = rule.evaluate(s, state)
        out.extend(a)
    return state, out


def sev(alerts):
    return [a.severity for a in alerts]


# ----------------------------------------------------------------- position
class TestPosition:
    rule = PositionRule(LIM.position)

    def test_quiet_inside_limits(self):
        _, a = drive(self.rule, [snap(i * S, position=p) for i, p in
                                 enumerate([0, 30, -55, 59.9, -59, 10])])
        assert a == []

    def test_warn_then_breach_short_side(self):
        st, a = drive(self.rule, [snap(0, position=-10), snap(S, position=-60),
                                  snap(2 * S, position=-80), snap(3 * S, position=-101)])
        assert sev(a) == ["warn", "breach"]
        assert (a[0].ts_us, a[0].metric_value, a[0].threshold) == (S, 60.0, 60.0)
        assert (a[1].ts_us, a[1].threshold) == (3 * S, 100.0)
        assert "position -101" in a[1].message
        assert st.level == "breach"

    def test_breach_directly(self):
        _, a = drive(self.rule, [snap(0, position=150)])
        assert sev(a) == ["breach"]

    def test_hysteresis_no_flapping(self):
        # oscillate around the warn level: one warn, no clear until < warn - hysteresis
        ps = [59, 61, 58, 62, 45, 61, 41, 59, 40.0]
        st, a = drive(self.rule, [snap(i * S, position=p) for i, p in enumerate(ps)])
        assert sev(a) == ["warn"]
        st, a2 = drive(self.rule, [snap(20 * S, position=39.9)], st)
        assert sev(a2) == ["clear"] and a2[0].threshold == 40.0
        assert st.level is None

    def test_cooldown_suppresses_refire_but_not_escalation(self):
        st, a = drive(self.rule, [snap(0, position=70), snap(S, position=10),
                                  snap(2 * S, position=70),      # within 60 s: suppressed
                                  snap(30 * S, position=75),     # still suppressed
                                  snap(60 * S, position=75)])    # cooldown over: fires
        assert [(x.severity, x.ts_us) for x in a] == [("warn", 0), ("clear", S), ("warn", 60 * S)]
        # escalation while active is immediate even inside the cooldown
        st, a = drive(self.rule, [snap(61 * S, position=120)], st)
        assert sev(a) == ["breach"]

    def test_suppressed_then_recovered_never_fires(self):
        _, a = drive(self.rule, [snap(0, position=70), snap(S, position=0),
                                 snap(2 * S, position=70), snap(3 * S, position=0),
                                 snap(100 * S, position=0)])
        assert sev(a) == ["warn", "clear"]


# ----------------------------------------------------------- loss from peak
class TestLossFromPeak:
    rule = LossFromPeakRule(LIM.loss_from_peak)

    def test_quiet(self):
        _, a = drive(self.rule, [snap(i * S, drawdown=d) for i, d in enumerate([0, 20, 99.9, 50])])
        assert a == []

    def test_warn_breach_clear(self):
        ds = [10, 100, 150, 210, 180, 76, 74.9]
        _, a = drive(self.rule, [snap(i * S, drawdown=d) for i, d in enumerate(ds)])
        assert [(x.severity, x.ts_us) for x in a] == [("warn", S), ("breach", 3 * S),
                                                      ("clear", 6 * S)]
        assert a[2].threshold == 75.0

    def test_cooldown(self):
        ds = [120, 10, 120, 120]
        ts = [0, S, 2 * S, 300 * S]
        _, a = drive(self.rule, [snap(t, drawdown=d) for t, d in zip(ts, ds)])
        assert [(x.severity, x.ts_us) for x in a] == [("warn", 0), ("clear", S), ("warn", 300 * S)]


# --------------------------------------------------------------- fill rate
class TestFillRate:
    rule = FillRateRule(LIM.fill_rate)

    def test_warmup_blocks_early_drought(self):
        # the window is still filling at the start of a session: no alert for 120 s
        snaps = [snap(t * S, window_fills=min(t, 40) // 3) for t in range(0, 120)]
        _, a = drive(self.rule, snaps)
        assert a == []

    def test_drought_after_warmup(self):
        snaps = [snap(t * S, window_fills=40) for t in range(0, 130)]
        snaps += [snap(130 * S, window_fills=5)]
        _, a = drive(self.rule, snaps)
        assert [(x.severity, x.ts_us, x.threshold) for x in a] == [("breach", 130 * S, 6.0)]
        assert "below min" in a[0].message

    def test_warmup_is_relative_to_first_instant(self):
        t0 = 10_000 * S
        _, a = drive(self.rule, [snap(t0, window_fills=0), snap(t0 + 119 * S, window_fills=0),
                                 snap(t0 + 120 * S, window_fills=0)])
        assert [x.ts_us for x in a] == [t0 + 120 * S]

    def test_runaway(self):
        _, a = drive(self.rule, [snap(0), snap(200 * S, window_fills=151)])
        assert sev(a) == ["breach"] and a[0].threshold == 150.0 and "above max" in a[0].message

    def test_quiet_inside_band(self):
        _, a = drive(self.rule, [snap(t * S, window_fills=w) for t, w in
                                 zip(range(0, 600, 100), [6, 150, 40, 6, 150, 7])])
        assert a == []

    def test_hysteresis_and_clear(self):
        ws = [40, 5, 6, 7, 9, 5, 9, 10]
        _, a = drive(self.rule, [snap(0)] + [snap((200 + i) * S, window_fills=w)
                                             for i, w in enumerate(ws)])
        assert [(x.severity, x.ts_us) for x in a] == [("breach", 201 * S), ("clear", 207 * S)]
        assert a[1].threshold == 10.0


# ------------------------------------------------------------------ markout
class TestMarkout:
    rule = MarkoutRule(LIM.markout)

    def test_quiet_above_floor(self):
        _, a = drive(self.rule, [snap(i * S, mo_bps=b) for i, b in enumerate([0.3, -0.2, -0.75, 1])])
        assert a == []

    def test_fires_below_floor(self):
        _, a = drive(self.rule, [snap(0, mo_bps=0.3), snap(S, mo_bps=-0.8)])
        assert [(x.severity, x.ts_us, x.threshold) for x in a] == [("breach", S, -0.75)]
        assert "n=100" in a[0].message

    def test_min_samples(self):
        bad = [snap(i * S, mo_bps=-5.0, mo_n=n) for i, n in enumerate([0, 1, 10, 49])]
        _, a = drive(self.rule, bad)
        assert a == []
        _, a = drive(self.rule, bad + [snap(10 * S, mo_bps=-5.0, mo_n=50)])
        assert [x.ts_us for x in a] == [10 * S]

    def test_undefined_metric_holds_state(self):
        st, a = drive(self.rule, [snap(0, mo_bps=-2.0), snap(S, mo_bps=0.5, mo_n=10),
                                  snap(2 * S, mo_n=0)])
        assert sev(a) == ["breach"] and st.level == "breach"   # no clear on thin data

    def test_hysteresis_and_cooldown(self):
        bps = [-0.8, -0.7, -0.76, -0.3, -0.26, -0.25, -0.8]
        st, a = drive(self.rule, [snap(i * S, mo_bps=b) for i, b in enumerate(bps)])
        assert [(x.severity, x.ts_us) for x in a] == [("breach", 0), ("clear", 5 * S)]
        st, a = drive(self.rule, [snap(299 * S, mo_bps=-1.0), snap(300 * S, mo_bps=-1.0)], st)
        assert [(x.severity, x.ts_us) for x in a] == [("breach", 300 * S)]

    def test_missing_horizon_is_an_error(self):
        s = snap(0)
        s["markouts"] = s["markouts"][:1]
        with pytest.raises(KeyError):
            self.rule.evaluate(s, None)


# ---------------------------------------------------------- shared behaviour
def test_rules_are_pure_and_deterministic():
    rules = build_rules(LIM)
    s = snap(500 * S, position=120, drawdown=250, window_fills=1, mo_bps=-3.0)
    frozen = copy.deepcopy(s)
    st0 = RuleState(start_ts=0)
    for r in rules:
        r1 = r.evaluate(s, st0)
        r2 = r.evaluate(s, st0)
        assert r1 == r2 and len(r1[1]) == 1 and r1[1][0].severity == "breach"
    assert s == frozen and st0 == RuleState(start_ts=0)
    with pytest.raises(dataclasses.FrozenInstanceError):
        st0.level = "warn"


def test_no_quote_yet_is_ignored():
    for r in build_rules(LIM):
        st, a = r.evaluate({"ts_us": None, "mid": None}, None)
        assert a == [] and st == RuleState()


def test_alert_dict_roundtrip():
    a = Alert(5, "position", "warn", 61.0, 60.0, "x")
    assert list(a.to_dict()) == ["ts_us", "rule", "severity", "metric_value", "threshold",
                                 "message"]
    assert Alert.from_dict(a.to_dict()) == a
    with pytest.raises(ValueError):
        Alert.from_dict({**a.to_dict(), "severity": "info"})


# ------------------------------------------------------------------- engine
def q(ts, mid=100.0):
    return Quote(ts, mid - 0.005, mid + 0.005, mid)


def test_engine_ignores_intermediate_states_within_an_instant():
    eng = Engine(LIM)
    # +80 then -80 at the same timestamp: the committed position never leaves 0
    eng.step(q(0))
    eng.step(Fill(0, 1, 100.0, 80, 0.0))
    eng.step(Fill(0, -1, 100.0, 80, 0.0))
    eng.step(q(100_000))
    assert eng.flush() == [] and eng.alerts == []


def test_engine_position_stream_fires_and_clears():
    eng = Engine(LIM)
    evs = [q(0), Fill(0, 1, 100.0, 70, 0.0), q(S), Fill(S, 1, 100.0, 40, 0.0), q(2 * S),
           Fill(3 * S, -1, 100.0, 80, 0.0), q(4 * S)]
    alerts = eng.run(evs)
    pos = [(a.severity, a.ts_us) for a in alerts if a.rule == "position"]
    assert pos == [("warn", 0), ("breach", S), ("clear", 3 * S)]
    snap_ = eng.snapshot()
    assert {"alerts", "active", "limits", "ts_us", "position", "markouts"} <= set(snap_)
    assert snap_["active"] == {"position": None, "loss_from_peak": None, "fill_rate": None,
                               "markout_degradation": None}
    assert snap_["limits"] == LIM.to_dict()
    assert [d["severity"] for d in snap_["alerts"] if d["rule"] == "position"] == \
        ["warn", "breach", "clear"]


def test_engine_evaluates_each_instant_once_and_step_returns_closed_instant():
    eng = Engine(LIM)
    assert eng.step(q(0)) == []
    assert eng.step(Fill(0, 1, 100.0, 120, 0.0)) == []       # instant 0 not closed yet
    out = eng.step(q(S))                                     # closes instant 0
    assert [(a.rule, a.severity, a.ts_us) for a in out] == [("position", "breach", 0)]
    assert eng.active()["position"] == "breach"
    assert eng.flush() == [] and eng.flush() == []           # instant S evaluated once


def test_engine_rejects_mismatched_state():
    with pytest.raises(ValueError):
        Engine(LIM, MonitorState(fill_window_us=30 * S, markout_window_us=300 * S))
    with pytest.raises(ValueError):
        Engine(LIM, MonitorState(horizons_us=[0, 5 * S], fill_window_us=60 * S,
                                 markout_window_us=300 * S))
    Engine(LIM, MonitorState(fill_window_us=60 * S, markout_window_us=300 * S))


def test_engine_markout_rule_via_stream():
    # every fill buys at the ask and the mid then drops 2 bps: 1 s markout ~ -2.5 bps
    eng = Engine(LIM)
    evs, ts, mid = [], 0, 100.0
    for _ in range(80):
        evs += [q(ts, mid), Fill(ts, 1, mid + 0.005, 1, 0.0), q(ts + S, mid - 0.02)]
        ts += 2 * S
    alerts = eng.run(evs)
    mk = [a for a in alerts if a.rule == "markout_degradation"]
    assert sev(mk) == ["breach"]
    # it could not fire before 50 markouts had resolved (the 50th resolves at 99 s)
    assert mk[0].ts_us == 99 * S and mk[0].metric_value < -0.75
