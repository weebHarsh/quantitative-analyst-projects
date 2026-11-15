"""Engine: MonitorState plus the alert rules, evaluated once per instant.

Rules are evaluated on *committed* instants only. When an event with a later
timestamp than the state's clock arrives, every event of the previous instant has
been applied, so the engine first evaluates the rules on that instant's snapshot
and only then applies the new event. The transient state between a quote and the
fills that share its timestamp (or between two fills at one timestamp) is never
seen by a rule, mirroring how :class:`MonitorState` treats drawdown. The final
instant of a stream is evaluated by :meth:`Engine.flush` (``run`` calls it).

Cost per event: one comparison, plus at most one ``snapshot()`` and four rule
evaluations per distinct timestamp, each O(number of markout horizons), so O(1)
amortised per event. Alerts are appended to a list.
"""
from __future__ import annotations

from typing import Iterable

from .alerts import Alert, RuleState, build_rules
from .events import Event, Quote
from .limits import Limits
from .state import DEFAULT_HORIZONS_US, MonitorState


class Engine:
    """Feed events with :meth:`step` (or a whole stream with :meth:`run`)."""

    def __init__(self, limits: Limits, state: MonitorState | None = None) -> None:
        self.limits = limits
        mk, fr = limits.markout, limits.fill_rate
        if state is None:
            hs = sorted(set(DEFAULT_HORIZONS_US) | {mk.horizon_us})
            state = MonitorState(horizons_us=hs, markout_window_us=mk.window_us,
                                 fill_window_us=fr.window_us)
        else:
            if state.fill_window_us != fr.window_us:
                raise ValueError(f"state fill_window_us {state.fill_window_us} != limits "
                                 f"fill_rate.window_us {fr.window_us}")
            if state.markouts.window_us != mk.window_us:
                raise ValueError(f"state markout window {state.markouts.window_us} != limits "
                                 f"markout.window_us {mk.window_us}")
            if mk.horizon_us not in state.markouts.horizons_us:
                raise ValueError(f"state does not track markout horizon {mk.horizon_us}")
        self.state = state
        self.rules = build_rules(limits)
        self.rule_states: dict[str, RuleState] = {r.name: RuleState() for r in self.rules}
        self.alerts: list[Alert] = []
        # True while the state holds events no rule has seen yet. A flush in the middle of
        # an instant evaluates what is there, and a later event at the same timestamp makes
        # the instant dirty again, so it is evaluated once more when it closes.
        self._dirty = False

    # ------------------------------------------------------------------ feed
    def _evaluate(self) -> list[Alert]:
        st = self.state
        if st.ts_us is None or not self._dirty:
            return []
        self._dirty = False
        snap = st.snapshot()
        out: list[Alert] = []
        for rule in self.rules:
            new, emitted = rule.evaluate(snap, self.rule_states[rule.name])
            self.rule_states[rule.name] = new
            if emitted:
                out.extend(emitted)
        if out:
            self.alerts.extend(out)
        return out

    def step(self, ev: Event) -> list[Alert]:
        """Apply one event; return the alerts raised for the instant it closed."""
        st = self.state
        out = []
        if st.ts_us is not None and ev.ts_us > st.ts_us:
            out = self._evaluate()
        if type(ev) is Quote:
            st.on_quote(ev)
        else:
            st.on_fill(ev)
        self._dirty = True      # only reached if the state accepted the event
        return out

    def flush(self) -> list[Alert]:
        """Evaluate the current (last) instant; call at the end of a stream."""
        return self._evaluate()

    def run(self, events: Iterable[Event]) -> list[Alert]:
        """Step through ``events``, flush the last instant, return all alerts so far."""
        step = self.step
        for ev in events:
            step(ev)
        self.flush()
        return list(self.alerts)

    # ------------------------------------------------------------- read side
    def active(self) -> dict[str, str | None]:
        return {name: rs.level for name, rs in self.rule_states.items()}

    def snapshot(self) -> dict:
        """State snapshot plus 'alerts' (dicts), 'active' and 'limits'."""
        snap = self.state.snapshot()
        snap["alerts"] = [a.to_dict() for a in self.alerts]
        snap["active"] = self.active()
        snap["limits"] = self.limits.to_dict()
        return snap
