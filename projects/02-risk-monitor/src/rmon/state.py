"""Incremental session state: PnL attribution, equity, drawdown, markouts.

Everything is O(1) amortised per event and reproduces Project 1 (``pta``) exactly:

* mid at time t is the last quote with ts <= t; a fill stamped on a quote's
  timestamp uses that quote's mid (so at equal timestamps the quote must be fed
  first; :func:`rmon.replay.replay` does this).
* cash accumulates ``-side*qty*px``; fees accumulate ``fee``;
  ``equity = cash + position*mid - fees``, recomputed on every quote and fill.
* ``spread_capture = sum side*qty*(mid_at_fill - px)``.
* ``inventory_pnl = sum side*qty*(mid_now - mid_at_fill)``, kept as
  ``mid_now * S1 - S2`` with ``S1 = sum side*qty`` (the position) and
  ``S2 = sum side*qty*mid_at_fill``. Project 1 marks to the final mid; this marks
  to the current mid, so at the end of the session it equals pta's total, and at
  every instant ``spread_capture + inventory_pnl - fees == equity``.

Drawdown is evaluated per *instant* (distinct event timestamp), on the equity
after every event at that timestamp has been applied. This matches
``pta.risk.max_drawdown`` over the tick equity curve (peak starts at 0) when
fills sit on quote timestamps; the transient equity between a quote and the fills
sharing its timestamp is never a drawdown point. An instant is committed when an
event with a later timestamp arrives; ``snapshot()`` folds in the current,
uncommitted instant without committing it.

Ordering rules (ValueError otherwise): no fill before the first quote; no event
earlier than the last one; a quote must be strictly later than the previous event
(a quote at the timestamp of an already-applied fill would retroactively change
that fill's mid).
"""
from __future__ import annotations

from collections import deque
from typing import Iterable

from .events import Fill, Quote
from .markouts_stream import MarkoutTracker

try:  # keep the default horizons in lockstep with Project 1
    from pta.markouts import DEFAULT_HORIZONS_US
except Exception:  # pragma: no cover
    DEFAULT_HORIZONS_US = (0, 100_000, 1_000_000, 5_000_000, 30_000_000, 60_000_000)


class MonitorState:
    """Live session state fed by :meth:`on_quote` / :meth:`on_fill`.

    Parameters
    ----------
    horizons_us : markout horizons in microseconds (default pta's).
    markout_window_us : rolling window, in resolution time, for ``rolling_bps``.
    fill_window_us : rolling window for ``window_fills`` / ``window_qty``; a fill at
        ts is counted while ``ts > now - fill_window_us``.
    """

    def __init__(self, horizons_us: Iterable[int] = DEFAULT_HORIZONS_US,
                 markout_window_us: int = 300_000_000,
                 fill_window_us: int = 60_000_000) -> None:
        if fill_window_us <= 0:
            raise ValueError("fill_window_us must be positive")
        self.markouts = MarkoutTracker(horizons_us, markout_window_us)
        self.fill_window_us = int(fill_window_us)

        self.ts_us: int | None = None        # clock: last event timestamp
        self.last_quote_ts: int | None = None
        self.bid = self.ask = self.mid = None
        self.quotes = 0

        self.position = 0.0                  # S1 = sum side*qty
        self._s2 = 0.0                       # sum side*qty*mid_at_fill
        self.cash = 0.0
        self.fees = 0.0
        self.spread_capture = 0.0
        self.fills = 0
        self.traded_qty = 0.0
        self.equity = 0.0

        self._peak = 0.0                     # committed (completed instants)
        self._max_dd = 0.0
        self._win: deque = deque()           # (ts, qty)
        self._win_qty = 0.0

    # ------------------------------------------------------------------ events
    def on_quote(self, q: Quote) -> None:
        ts = int(q.ts_us)
        if self.ts_us is not None:
            if ts < self.ts_us:
                raise ValueError(f"quote ts_us {ts} is before the last event at {self.ts_us}")
            if ts == self.ts_us:
                raise ValueError(f"quote ts_us {ts} repeats the last event's timestamp; quotes "
                                 "must be strictly increasing and precede fills at equal ts")
            self._commit()
        self.markouts.on_quote(ts, q.mid, self.mid)
        self.ts_us = self.last_quote_ts = ts
        self.bid, self.ask, self.mid = q.bid, q.ask, q.mid
        self.quotes += 1
        self.equity = self.cash + self.position * self.mid - self.fees
        self._evict(ts)

    def on_fill(self, f: Fill) -> None:
        if self.mid is None:
            raise ValueError("fill received before any quote")
        ts = int(f.ts_us)
        if ts < self.ts_us:
            raise ValueError(f"fill ts_us {ts} is before the last event at {self.ts_us}")
        if f.side not in (1, -1):
            raise ValueError("fill side must be +1 or -1")
        if not f.qty > 0:
            raise ValueError("fill qty must be positive")
        if ts > self.ts_us:
            self._commit()
            self.ts_us = ts
        sq = f.side * f.qty
        m = self.mid
        self.position += sq
        self._s2 += sq * m
        self.cash -= sq * f.px
        self.fees += f.fee
        self.spread_capture += sq * (m - f.px)
        self.fills += 1
        self.traded_qty += f.qty
        self.equity = self.cash + self.position * m - self.fees
        self._win.append((ts, f.qty))
        self._win_qty += f.qty
        self.markouts.on_fill(ts, f.side, f.px, f.qty, m)
        self._evict(ts)

    def on_event(self, ev: Quote | Fill) -> None:
        """Dispatch on type (Quote or Fill)."""
        if isinstance(ev, Quote):
            self.on_quote(ev)
        else:
            self.on_fill(ev)

    # ------------------------------------------------------------- internals
    def _commit(self) -> None:
        eq = self.equity
        if eq > self._peak:
            self._peak = eq
        dd = self._peak - eq
        if dd > self._max_dd:
            self._max_dd = dd

    def _evict(self, now: int) -> None:
        cutoff = now - self.fill_window_us
        w = self._win
        if w and w[0][0] <= cutoff:
            while w and w[0][0] <= cutoff:
                self._win_qty -= w.popleft()[1]
            if not w:
                self._win_qty = 0.0
        self.markouts.advance(now)

    # ------------------------------------------------------------- read side
    @property
    def inventory_pnl(self) -> float:
        return 0.0 if self.mid is None else self.mid * self.position - self._s2

    @property
    def total_pnl(self) -> float:
        return self.spread_capture + self.inventory_pnl - self.fees

    @property
    def peak_equity(self) -> float:
        return max(self._peak, self.equity)

    @property
    def drawdown(self) -> float:
        return self.peak_equity - self.equity

    @property
    def max_drawdown(self) -> float:
        return max(self._max_dd, self.drawdown)

    def snapshot(self) -> dict:
        """Plain JSON-serialisable dict of the current state (None where undefined)."""
        return {
            "ts_us": self.ts_us,
            "last_quote_ts_us": self.last_quote_ts,
            "bid": self.bid,
            "ask": self.ask,
            "mid": self.mid,
            "quotes": self.quotes,
            "position": self.position,
            "cash": self.cash,
            "fees": self.fees,
            "spread_capture": self.spread_capture,
            "inventory_pnl": self.inventory_pnl,
            "total_pnl": self.total_pnl,
            "equity": self.equity,
            "peak_equity": self.peak_equity,
            "drawdown": self.drawdown,
            "max_drawdown": self.max_drawdown,
            "fills": self.fills,
            "traded_qty": self.traded_qty,
            "fill_window_us": self.fill_window_us,
            "window_fills": len(self._win),
            "window_qty": self._win_qty,
            "markout_window_us": self.markouts.window_us,
            "markouts": self.markouts.summary(),
        }
