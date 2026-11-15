"""Replay a recorded session (pta market + fills frames) as one ordered event stream.

``replay`` merges the two frames in timestamp order with the quote first at equal
timestamps, which is the order :class:`rmon.state.MonitorState` needs to
reproduce pta's as-of semantics. It walks plain Python lists built once from the
numpy columns (no per-row pandas access), so it is cheap per event.

Pacing is a separate wrapper (``paced``) so results never depend on speed.
"""
from __future__ import annotations

import time
from typing import Callable, Iterable, Iterator

import pandas as pd
from pta.data.loaders import validate as _validate

from .events import Event, Fill, Quote


def replay(market: pd.DataFrame, fills: pd.DataFrame, validate: bool = True) -> Iterator[Event]:
    """Yield Quote and Fill events in time order (quote before fills at equal ts_us)."""
    if validate:
        _validate(fills, market)
    mts = market["ts_us"].to_numpy(dtype="int64").tolist()
    bid = market["bid"].to_numpy(dtype=float).tolist()
    ask = market["ask"].to_numpy(dtype=float).tolist()
    mid = market["mid"].to_numpy(dtype=float).tolist()
    fts = fills["ts_us"].to_numpy(dtype="int64").tolist()
    side = fills["side"].to_numpy(dtype="int64").tolist()
    px = fills["px"].to_numpy(dtype=float).tolist()
    qty = fills["qty"].to_numpy(dtype=float).tolist()
    fee = fills["fee"].to_numpy(dtype=float).tolist()

    nm, nf = len(mts), len(fts)
    j = 0
    for i in range(nm):
        t = mts[i]
        # fills strictly before this quote
        while j < nf and fts[j] < t:
            yield Fill(fts[j], side[j], px[j], qty[j], fee[j])
            j += 1
        yield Quote(t, bid[i], ask[i], mid[i])
    while j < nf:
        yield Fill(fts[j], side[j], px[j], qty[j], fee[j])
        j += 1


def paced(events: Iterable[Event], speed: float = 1.0,
          clock: Callable[[], float] = time.monotonic,
          sleep: Callable[[float], None] = time.sleep) -> Iterator[Event]:
    """Re-yield ``events`` so that event time advances ``speed`` x wall time.

    ``speed <= 0`` or ``inf`` means no pacing. Only timing changes; the events and
    their order are untouched.
    """
    if not speed or speed <= 0 or speed == float("inf"):
        yield from events
        return
    t0_wall = t0_ev = None
    for ev in events:
        if t0_ev is None:
            t0_wall, t0_ev = clock(), ev.ts_us
        else:
            due = t0_wall + (ev.ts_us - t0_ev) / 1e6 / speed
            wait = due - clock()
            if wait > 0:
                sleep(wait)
        yield ev


def run_session(market: pd.DataFrame, fills: pd.DataFrame, state=None, **state_kwargs):
    """Replay a whole session into a MonitorState (new one unless given) and return it."""
    from .state import MonitorState
    st = state if state is not None else MonitorState(**state_kwargs)
    on_q, on_f = st.on_quote, st.on_fill
    for ev in replay(market, fills):
        if type(ev) is Quote:
            on_q(ev)
        else:
            on_f(ev)
    return st
