"""Alert rules over :meth:`rmon.state.MonitorState.snapshot` dicts.

Every rule is a small immutable object with one method::

    new_state, alerts = rule.evaluate(snapshot, prev_state)

It is pure and deterministic: the only inputs are the snapshot dict and the
previous :class:`RuleState`; time is the snapshot's ``ts_us`` (event time, never
the wall clock). The rule object itself holds only its limits.

Shared state machine (levels ``None < 'warn' < 'breach'``):

* armed (level None) and the metric crosses a threshold -> fire that severity,
  unless the previous fire was less than ``cooldown_us`` ago, in which case the
  fire is suppressed (it will fire on the first evaluation after the cooldown if
  the condition still holds). Cooldown is what stops a metric oscillating around
  the recovery threshold from producing a fire/clear pair every few ticks.
* active and the metric crosses a higher threshold -> escalate immediately
  (escalation is never delayed by the cooldown).
* active and the metric has recovered past the *recovery* threshold (strictly on
  the safe side of the warn level by ``hysteresis``) -> emit ``'clear'`` and
  re-arm. Between the warn level and the recovery threshold nothing happens, so a
  metric hovering at the threshold cannot flap.
* when the metric is undefined (fill-rate warm-up, fewer than ``min_samples``
  resolved markouts, no quote yet) the rule holds its state and emits nothing.

The level does not step down from breach to warn; it stays at the highest level
reached until it clears.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, replace
from pathlib import Path
from typing import IO, Iterable

from .limits import (FillRateLimits, Limits, LossFromPeakLimits, MarkoutLimits,
                     PositionLimits)

SEVERITIES = ("warn", "breach", "clear")
_RANK = {None: 0, "warn": 1, "breach": 2}
ALERT_KEYS = ("ts_us", "rule", "severity", "metric_value", "threshold", "message")


@dataclass(frozen=True)
class Alert:
    ts_us: int
    rule: str
    severity: str          # 'warn' | 'breach' | 'clear'
    metric_value: float
    threshold: float       # the level crossed (for 'clear': the recovery threshold)
    message: str

    def to_dict(self) -> dict:
        return {k: getattr(self, k) for k in ALERT_KEYS}

    @classmethod
    def from_dict(cls, d: dict) -> "Alert":
        if set(d) != set(ALERT_KEYS):
            raise ValueError(f"alert record keys {sorted(d)} != {sorted(ALERT_KEYS)}")
        if d["severity"] not in SEVERITIES:
            raise ValueError(f"bad severity {d['severity']!r}")
        return cls(int(d["ts_us"]), str(d["rule"]), d["severity"], float(d["metric_value"]),
                   float(d["threshold"]), str(d["message"]))


@dataclass(frozen=True)
class RuleState:
    level: str | None = None          # None, 'warn' or 'breach'
    last_fire_ts: int | None = None   # ts of the last warn/breach emitted
    start_ts: int | None = None       # first instant evaluated (for warm-up)
    detail: str | None = None         # direction of the current breach (rules that have one)
    last_fire_sev: str | None = None  # severity of the last warn/breach emitted


def _g(x: float) -> str:
    return f"{x:.6g}"


class _Rule:
    name: str = ""
    cooldown_us: int = 0

    # subclasses: measure -> metric or None; classify -> (severity|None, threshold);
    # recovery -> (recovered?, recovery threshold); describe -> message text
    def measure(self, snap: dict, state: RuleState) -> float | None:
        raise NotImplementedError

    def classify(self, v: float) -> tuple[str | None, float]:
        raise NotImplementedError

    def recovery(self, v: float) -> tuple[bool, float]:
        raise NotImplementedError

    def describe(self, snap: dict, v: float, sev: str, thr: float) -> str:
        raise NotImplementedError

    def detail(self, v: float) -> str | None:
        """Which way the metric is out of bounds, for rules where that matters."""
        return None

    def evaluate(self, snap: dict, state: RuleState | None = None
                 ) -> tuple[RuleState, list[Alert]]:
        state = state or RuleState()
        ts = snap.get("ts_us")
        if ts is None or snap.get("mid") is None:
            return state, []
        ts = int(ts)
        if state.start_ts is None:
            state = replace(state, start_ts=ts)
        v = self.measure(snap, state)
        if v is None:
            return state, []
        sev, thr = self.classify(v)
        if state.level is None:
            if sev is None:
                return state, []
            # Cooldown damps repeats of the same or a lower severity than the last alert.
            # A higher severity (warn, clear, then breach) always fires, the same way an
            # escalation while still active skips the cooldown.
            if (state.last_fire_ts is not None and ts - state.last_fire_ts < self.cooldown_us
                    and _RANK[sev] <= _RANK[state.last_fire_sev]):
                return state, []                      # suppressed by cooldown
            return self._fire(snap, state, ts, v, sev, thr)
        if _RANK[sev] > _RANK[state.level]:
            return self._fire(snap, state, ts, v, sev, thr)   # escalation
        if sev is not None and sev == state.level and self.detail(v) != state.detail:
            return self._fire(snap, state, ts, v, sev, thr)   # same level, other direction
        if sev is None:
            ok, rthr = self.recovery(v)
            if ok:
                a = Alert(ts, self.name, "clear", float(v), float(rthr),
                          self.describe(snap, v, "clear", rthr))
                return replace(state, level=None, detail=None), [a]
        return state, []

    def _fire(self, snap, state, ts, v, sev, thr):
        a = Alert(ts, self.name, sev, float(v), float(thr), self.describe(snap, v, sev, thr))
        return replace(state, level=sev, last_fire_ts=ts, last_fire_sev=sev,
                       detail=self.detail(v)), [a]


class _TwoLevelRule(_Rule):
    """|metric| >= warn -> warn, >= breach -> breach, clears below warn - hysteresis."""

    def __init__(self, lim: PositionLimits | LossFromPeakLimits) -> None:
        self.lim = lim
        self.cooldown_us = lim.cooldown_us

    def classify(self, v):
        if v >= self.lim.breach:
            return "breach", self.lim.breach
        if v >= self.lim.warn:
            return "warn", self.lim.warn
        return None, self.lim.warn

    def recovery(self, v):
        r = self.lim.warn - self.lim.hysteresis
        return v < r, r


class PositionRule(_TwoLevelRule):
    """Absolute inventory against warn/breach levels."""
    name = "position"

    def measure(self, snap, state):
        return abs(float(snap["position"]))

    def describe(self, snap, v, sev, thr):
        p = float(snap["position"])
        if sev == "clear":
            return f"position {_g(p)} back below {_g(thr)}"
        return f"|position| {_g(v)} (position {_g(p)}) >= {sev} level {_g(thr)}"


class LossFromPeakRule(_TwoLevelRule):
    """Drawdown from the running equity peak (currency)."""
    name = "loss_from_peak"

    def measure(self, snap, state):
        return float(snap["drawdown"])

    def describe(self, snap, v, sev, thr):
        if sev == "clear":
            return f"drawdown {_g(v)} back below {_g(thr)}"
        return (f"drawdown {_g(v)} from peak {_g(float(snap['peak_equity']))} "
                f">= {sev} level {_g(thr)}")


class FillRateRule(_Rule):
    """Fills in the rolling window outside [min_fills, max_fills], after a warm-up."""
    name = "fill_rate"

    def __init__(self, lim: FillRateLimits) -> None:
        self.lim = lim
        self.cooldown_us = lim.cooldown_us

    def measure(self, snap, state):
        if int(snap["ts_us"]) - state.start_ts < self.lim.warmup_us:
            return None
        return float(snap["window_fills"])

    def classify(self, v):
        if v < self.lim.min_fills:
            return "breach", float(self.lim.min_fills)
        if v > self.lim.max_fills:
            return "breach", float(self.lim.max_fills)
        return None, float(self.lim.min_fills)

    def detail(self, v):
        if v < self.lim.min_fills:
            return "low"
        if v > self.lim.max_fills:
            return "high"
        return None

    def recovery(self, v):
        lo = self.lim.min_fills + self.lim.hysteresis
        hi = self.lim.max_fills - self.lim.hysteresis
        if v < lo:
            return False, lo
        if v > hi:
            return False, hi
        # report the nearer recovery bound
        return True, float(lo if v - lo <= hi - v else hi)

    def describe(self, snap, v, sev, thr):
        w = self.lim.window_us / 1e6
        if sev == "clear":
            return f"{int(v)} fills in {_g(w)}s window, back inside band (bound {_g(thr)})"
        side = "below min" if v < self.lim.min_fills else "above max"
        return f"{int(v)} fills in {_g(w)}s window, {side} {_g(thr)}"


class MarkoutRule(_Rule):
    """Rolling quantity-weighted markout at one horizon below a bps floor."""
    name = "markout_degradation"

    def __init__(self, lim: MarkoutLimits) -> None:
        self.lim = lim
        self.cooldown_us = lim.cooldown_us

    def _entry(self, snap):
        for m in snap["markouts"]:
            if int(m["horizon_us"]) == self.lim.horizon_us:
                return m
        raise KeyError(f"snapshot has no markout horizon {self.lim.horizon_us}")

    def measure(self, snap, state):
        m = self._entry(snap)
        if m["rolling_bps"] is None or m["rolling_n"] < self.lim.min_samples:
            return None
        return float(m["rolling_bps"])

    def classify(self, v):
        if v < self.lim.floor_bps:
            return "breach", self.lim.floor_bps
        return None, self.lim.floor_bps

    def recovery(self, v):
        r = self.lim.floor_bps + self.lim.hysteresis_bps
        return v >= r, r

    def describe(self, snap, v, sev, thr):
        h = self.lim.horizon_us / 1e6
        n = self._entry(snap)["rolling_n"]
        if sev == "clear":
            return f"{_g(h)}s markout {_g(v)} bps (n={n}) recovered to >= {_g(thr)}"
        return f"{_g(h)}s markout {_g(v)} bps (n={n}) below floor {_g(thr)}"


def build_rules(limits: Limits) -> list[_Rule]:
    """The four rules in a fixed evaluation order."""
    return [PositionRule(limits.position), LossFromPeakRule(limits.loss_from_peak),
            FillRateRule(limits.fill_rate), MarkoutRule(limits.markout)]


RULE_NAMES = ("position", "loss_from_peak", "fill_rate", "markout_degradation")


# --------------------------------------------------------------------- JSONL log
def alert_to_json(a: Alert) -> str:
    return json.dumps(a.to_dict(), allow_nan=False, separators=(",", ":"))


def write_alerts(alerts: Iterable[Alert], dest: str | Path | IO[str]) -> None:
    """Write one JSON object per line, keys in ALERT_KEYS order."""
    if hasattr(dest, "write"):
        for a in alerts:
            dest.write(alert_to_json(a) + "\n")
        return
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        write_alerts(alerts, fh)


def read_alerts(src: str | Path | IO[str]) -> list[Alert]:
    """Read a JSONL alert log written by :func:`write_alerts` (blank lines ignored)."""
    if not hasattr(src, "read"):
        with open(src, encoding="utf-8") as fh:
            return read_alerts(fh)
    return [Alert.from_dict(json.loads(line)) for line in src if line.strip()]


class AlertLog:
    """Append-only JSONL writer that flushes each line (for a live monitor)."""

    def __init__(self, path: str | Path) -> None:
        self._fh = open(path, "a", encoding="utf-8", newline="\n")

    def write(self, alert: Alert) -> None:
        self._fh.write(alert_to_json(alert) + "\n")
        self._fh.flush()

    def close(self) -> None:
        self._fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
