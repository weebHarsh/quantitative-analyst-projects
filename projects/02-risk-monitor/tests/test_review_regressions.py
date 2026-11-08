"""Regression tests for bugs found by the independent review of the alert layer."""
import copy

import pytest

from rmon import Engine, default_limits
from rmon.alerts import FillRateRule, PositionRule
from rmon.events import Fill, Quote
from rmon.limits import LimitsError, limits_from_dict
from rmon.scenarios import bad

S = 1_000_000
LIM = default_limits()


def q(t):
    return Quote(t, 99.9, 100.1, 100.0)


def run_rule(rule, points, key):
    state, out = None, []
    for t, v in points:
        state, alerts = rule.evaluate({"ts_us": t, "mid": 1.0, key: v}, state)
        out += [(a.ts_us // S, a.severity) for a in alerts]
    return out


# 1. a breach inside the cooldown after a clear must not be hidden
def test_breach_after_clear_is_not_swallowed_by_cooldown():
    out = run_rule(PositionRule(LIM.position), [(0, 61), (S, 10), (10 * S, 150)], "position")
    assert out == [(0, "warn"), (1, "clear"), (10, "breach")]


def test_warn_after_clear_is_still_held_back_by_cooldown():
    out = run_rule(PositionRule(LIM.position), [(0, 61), (S, 10), (10 * S, 70)], "position")
    assert out == [(0, "warn"), (1, "clear")]


def test_repeat_breach_inside_cooldown_stays_damped():
    # same severity as the last alert, so cooldown still applies; after it ends it fires
    cd = LIM.position.cooldown_us
    out = run_rule(PositionRule(LIM.position),
                   [(0, 150), (S, 10), (10 * S, 150), (cd + 2 * S, 150)], "position")
    assert out == [(0, "breach"), (1, "clear"), (cd // S + 2, "breach")]


# 2. flush in the middle of an instant must not freeze that instant
def test_flush_mid_instant_does_not_hide_later_events_at_same_timestamp():
    eng = Engine(LIM)
    for ev in [q(0), Fill(0, 1, 100.0, 10, 0.0)]:
        eng.step(ev)
    eng.flush()
    for ev in [Fill(0, 1, 100.0, 140, 0.0), q(S), Fill(S, -1, 100.0, 150, 0.0), q(2 * S)]:
        eng.step(ev)
    eng.flush()
    assert [(a.ts_us // S, a.rule, a.severity) for a in eng.alerts] == [
        (0, "position", "breach"), (1, "position", "clear")]


def test_rejected_event_does_not_leave_the_instant_unevaluated():
    eng = Engine(LIM)
    eng.step(q(0))
    eng.step(Fill(0, 1, 100.0, 10, 0.0))
    with pytest.raises(ValueError):
        eng.step(Fill(S, 0, 100.0, 1, 0.0))        # bad side, rejected by the state
    eng.step(Fill(0, 1, 100.0, 140, 0.0))          # same instant as before the rejection
    eng.step(q(S))
    eng.step(Fill(S, -1, 100.0, 150, 0.0))
    eng.step(q(2 * S))
    eng.flush()
    assert (0, "position", "breach") in [(a.ts_us // S, a.rule, a.severity) for a in eng.alerts]


def test_run_still_matches_step_by_step():
    events = [q(0), Fill(0, 1, 100.0, 150, 0.0), q(S), Fill(S, -1, 100.0, 150, 0.0), q(2 * S)]
    a = Engine(LIM).run(events)
    eng = Engine(LIM)
    for ev in events:
        eng.step(ev)
    eng.flush()
    assert [x.to_dict() for x in a] == [x.to_dict() for x in eng.alerts]


# 3. every hours value must give a gap-free 100 ms grid with phases that match the data
@pytest.mark.parametrize("hours", [0.11, 0.37, 1.03, 2.0])
def test_bad_scenario_grid_is_uniform_for_awkward_hours(hours):
    sc = bad(7, hours=hours)
    ts = sc.market["ts_us"].to_numpy()
    assert set((ts[1:] - ts[:-1]).tolist()) == {100_000}
    assert sc.phases[-1].end_us == ts[-1] + 100_000
    assert len(sc.market) == int(round(hours * 3600 * 1e6 / 100_000))


# 4. a drought followed by a runaway must raise a second breach
def test_fill_rate_direction_change_raises_a_new_alert():
    rule, state, out = FillRateRule(LIM.fill_rate), None, []
    w = LIM.fill_rate.warmup_us
    for t, n in [(0, 30), (w, 2), (w + S, 300)]:
        state, alerts = rule.evaluate({"ts_us": t, "mid": 1.0, "window_fills": n}, state)
        out += [(a.severity, "below" in a.message, "above" in a.message) for a in alerts]
    assert out == [("breach", True, False), ("breach", False, True)]


def test_fill_rate_same_direction_does_not_repeat():
    rule, state, out = FillRateRule(LIM.fill_rate), None, []
    w = LIM.fill_rate.warmup_us
    for t, n in [(0, 30), (w, 2), (w + S, 1), (w + 2 * S, 0)]:
        state, alerts = rule.evaluate({"ts_us": t, "mid": 1.0, "window_fills": n}, state)
        out += alerts
    assert len(out) == 1


# 5. limits that load but cannot behave must be rejected
def _with(section, key, value):
    d = copy.deepcopy(LIM.to_dict())
    d[section][key] = value
    return d


def test_warmup_shorter_than_window_is_rejected():
    with pytest.raises(LimitsError, match="warmup_us"):
        limits_from_dict(_with("fill_rate", "warmup_us", 0))
    with pytest.raises(LimitsError, match="warmup_us"):
        limits_from_dict(_with("fill_rate", "warmup_us", LIM.fill_rate.window_us - 1))
    limits_from_dict(_with("fill_rate", "warmup_us", LIM.fill_rate.window_us))


def test_absurd_markout_hysteresis_is_rejected():
    with pytest.raises(LimitsError, match="hysteresis_bps"):
        limits_from_dict(_with("markout", "hysteresis_bps", 1e12))
    limits_from_dict(_with("markout", "hysteresis_bps", 1.0))
