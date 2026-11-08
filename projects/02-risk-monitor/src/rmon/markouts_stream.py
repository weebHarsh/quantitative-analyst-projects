"""Streaming markouts with the same semantics as ``pta.markouts``.

For a fill (ts, side, px, qty) and horizon h the per-unit markout is
``side * (mid(ts + h) - px)`` where ``mid(t)`` is the last quote with ts <= t.

Resolution rule (the earliest moment the answer is knowable):

* h == 0 resolves at the fill itself, from the current mid.
* otherwise, when a quote arrives with ts == target the answer is that quote's
  mid; when a quote arrives with ts > target the answer is the previous quote's
  mid. Targets past the last quote of the session never resolve and stay pending,
  which is exactly pta's "NaN and excluded" rule.

Fills arrive in time order, so per horizon the targets are non-decreasing and a
FIFO deque is enough: each fill is pushed and popped once per horizon, so the
work is amortised O(1) per fill and horizon.

Rolling window: each horizon also keeps the markouts *resolved* within the last
``window_us`` microseconds of event time (resolution time = timestamp of the event
that resolved it, i.e. the fill for h=0 and the resolving quote otherwise). An
entry resolved at time r is in the window at clock ``now`` while
``r > now - window_us``. The window exposes a quantity-weighted average markout in
bps, ``sum(qty * mo / px * 1e4) / sum(qty)``, the same weighting as pta's
``avg_bps``. Windows are keyed on resolution time (not fill time) so that a long
horizon reports what was learned recently rather than lagging by h.
"""
from __future__ import annotations

from collections import deque
from typing import Iterable


class _Horizon:
    __slots__ = ("h", "pending", "n", "qty", "total", "qbps", "n_pos",
                 "win", "win_qty", "win_qbps")

    def __init__(self, h: int) -> None:
        self.h = h
        self.pending: deque = deque()     # (target_ts, side, px, qty)
        self.n = 0
        self.qty = 0.0
        self.total = 0.0                  # sum qty * markout (currency)
        self.qbps = 0.0                   # sum qty * markout_bps
        self.n_pos = 0
        self.win: deque = deque()         # (resolved_ts, qty, qty * markout_bps)
        self.win_qty = 0.0
        self.win_qbps = 0.0

    def resolve(self, side: int, px: float, qty: float, mid: float, now: int) -> None:
        mo = side * (mid - px)
        qbps = qty * mo / px * 1e4
        self.n += 1
        self.qty += qty
        self.total += qty * mo
        self.qbps += qbps
        if mo > 0:
            self.n_pos += 1
        self.win.append((now, qty, qbps))
        self.win_qty += qty
        self.win_qbps += qbps


class MarkoutTracker:
    """Pending and resolved markouts for a fixed set of horizons.

    Driven by :class:`rmon.state.MonitorState`; it can also be used on its own as
    long as calls arrive in event order with the quote-before-fill rule at equal
    timestamps.
    """

    def __init__(self, horizons_us: Iterable[int], window_us: int = 300_000_000) -> None:
        hs = sorted({int(h) for h in horizons_us})
        if any(h < 0 for h in hs):
            raise ValueError("markout horizons must be >= 0")
        if window_us <= 0:
            raise ValueError("window_us must be positive")
        self.horizons_us: tuple[int, ...] = tuple(hs)
        self.window_us = int(window_us)
        self._hz = [_Horizon(h) for h in hs]

    def on_fill(self, ts_us: int, side: int, px: float, qty: float, mid: float) -> None:
        """Register a fill; ``mid`` is the as-of mid at the fill (resolves h=0)."""
        for hz in self._hz:
            if hz.h == 0:
                hz.resolve(side, px, qty, mid, ts_us)
            else:
                hz.pending.append((ts_us + hz.h, side, px, qty))

    def on_quote(self, ts_us: int, mid: float, prev_mid: float | None) -> None:
        """Resolve everything this quote makes knowable. Call BEFORE the quote's
        mid becomes current, passing the previous quote's mid."""
        for hz in self._hz:
            p = hz.pending
            while p and p[0][0] < ts_us:
                _, s, px, q = p.popleft()
                hz.resolve(s, px, q, prev_mid, ts_us)
            while p and p[0][0] == ts_us:
                _, s, px, q = p.popleft()
                hz.resolve(s, px, q, mid, ts_us)

    def advance(self, now_us: int) -> None:
        """Evict window entries resolved at or before ``now_us - window_us``."""
        cutoff = now_us - self.window_us
        for hz in self._hz:
            w = hz.win
            if w and w[0][0] <= cutoff:
                while w and w[0][0] <= cutoff:
                    _, q, b = w.popleft()
                    hz.win_qty -= q
                    hz.win_qbps -= b
                if not w:                  # kill floating-point residue
                    hz.win_qty = 0.0
                    hz.win_qbps = 0.0

    def rolling_bps(self, horizon_us: int) -> float | None:
        """Quantity-weighted markout in bps over the window, None if empty."""
        hz = self._hz[self.horizons_us.index(int(horizon_us))]
        return hz.win_qbps / hz.win_qty if hz.win else None

    def summary(self) -> list[dict]:
        """One JSON-serialisable dict per horizon (ascending). Undefined averages
        are None. ``n``/``qty``/``total`` match ``pta.markouts.summarize`` at the end
        of a session."""
        out = []
        for hz in self._hz:
            out.append({
                "horizon_us": hz.h,
                "n": hz.n,
                "qty": hz.qty,
                "total": hz.total,
                "avg_per_unit": hz.total / hz.qty if hz.qty else None,
                "avg_bps": hz.qbps / hz.qty if hz.qty else None,
                "pct_positive": hz.n_pos / hz.n if hz.n else None,
                "pending": len(hz.pending),
                "rolling_n": len(hz.win),
                "rolling_qty": hz.win_qty,
                "rolling_bps": hz.win_qbps / hz.win_qty if hz.win else None,
            })
        return out
