"""Event types consumed by :class:`rmon.state.MonitorState`.

Both are NamedTuples: cheap to build (the replay creates one per row) and
immutable. Field meanings follow the pta schemas exactly:

Quote : ts_us (int microseconds), bid, ask, mid
Fill  : ts_us (int microseconds), side (+1 buy / -1 sell), px, qty (> 0, float ok),
        fee (positive = cost, negative = rebate)
"""
from __future__ import annotations

from typing import NamedTuple, Union


class Quote(NamedTuple):
    ts_us: int
    bid: float
    ask: float
    mid: float


class Fill(NamedTuple):
    ts_us: int
    side: int
    px: float
    qty: float
    fee: float


Event = Union[Quote, Fill]
