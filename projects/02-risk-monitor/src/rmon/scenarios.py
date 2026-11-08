"""Deterministic demo sessions in pta schemas: one clean, one with injected problems.

``clean(seed, hours)`` is pta's default market (``MarketConfig(seed, hours)``) and
default market maker (``MMConfig()``), i.e. the session the limits are calibrated
on.

``bad(seed, hours)`` is ONE continuous session built from five segments, each
simulated with its own pta configuration:

=============  =====  ============================================================
phase          share  configuration change versus the pta defaults
=============  =====  ============================================================
normal         25 %   none
adverse        15 %   market ``alpha_bps_per_s`` 0.6 -> 15, MM ``beta`` 0.5 -> 0.95:
                      flow now moves price 25x harder and decides almost every
                      aggressor, so the maker is picked off and 1 s markouts go
                      negative
inventory_run  10 %   skew and max-inventory logic disabled, and a constant +0.5
                      bias added to the flow the *maker* sees (not the price), so
                      buyers keep lifting our offer and inventory runs short
                      through the position limits
drought        10 %   MM ``lam_per_s`` 0.4 -> 0.01: the maker is almost silent
recovery       40 %   defaults again; the skew logic, seeing the inherited short
                      inventory, pulls the offer and buys it back
=============  =====  ============================================================

Stitching
---------
* Market: each segment is ``pta.data.synthetic.simulate_market`` with its own
  seed, its duration as ``hours`` and ``s0`` set to the previous segment's last
  mid, then shifted so its first tick is one ``dt_us`` after the previous
  segment's last tick. Timestamps are therefore one uniform 100 ms grid and the
  price is continuous (the first tick of a segment is the previous last mid,
  re-rounded to the tick grid, so the join moves the mid by at most one tick).
  The flow process restarts with a fresh stationary draw at each join: a one-tick
  break in its autocorrelation (half-life about 1.4 s), harmless at these phase
  lengths of minutes.
* Fills: pta's ``simulate_market_maker`` always starts flat, which would make its
  skew and max-inventory logic wrong after the first segment. So
  :func:`simulate_mm_segment` is a line-for-line port of it with three extra
  arguments: the starting inventory (carried across joins), ``skew`` on/off and a
  ``flow_bias`` added to the flow the maker sees. With the defaults it reproduces
  pta's fills exactly (tested in tests/test_scenario_demo.py).

``bad`` also returns ``phases`` (name, start_us, end_us), half-open
``[start, end)``, and ``expected_order``: (rule, severity, allowed phases) in
the order intended. The first occurrence of each entry must fall inside one of
its allowed phases; since phases are listed in time order this fixes the order
across phases, and per rule the entries are in firing order (warn, breach,
clear). Entries sharing a phase are not ordered against each other (for example
whether the markout or the drawdown trips first inside 'adverse' depends on the
seed). The drawdown breach is allowed in 'adverse' or 'inventory_run' because
the extra loss of a large short position depends on the price path.
The loss rule is not expected to clear: drawdown only recovers if equity climbs
back to within 75 of its peak, which the recovery phase does not guarantee.
"""
from __future__ import annotations

from dataclasses import replace
from typing import NamedTuple

import numpy as np
import pandas as pd
from pta.data.synthetic import MarketConfig, simulate_market
from pta.strategy.market_maker import MMConfig, simulate_market_maker


class Phase(NamedTuple):
    name: str
    start_us: int
    end_us: int        # exclusive


class Scenario(NamedTuple):
    market: pd.DataFrame
    fills: pd.DataFrame
    phases: list[Phase]
    expected_order: list[tuple[str, str, tuple[str, ...]]]  # (rule, severity, phases)


def simulate_mm_segment(market: pd.DataFrame, flow: np.ndarray, cfg: MMConfig = MMConfig(),
                        init_inv: int = 0, skew: bool = True, flow_bias: float = 0.0
                        ) -> tuple[pd.DataFrame, int]:
    """pta's market maker with a starting inventory; returns (fills, final inventory).

    Identical random draws and decision rule to
    :func:`pta.strategy.market_maker.simulate_market_maker`. ``skew=False`` turns
    off both the one-tick back-off and the max-inventory pull; ``flow_bias`` shifts
    the flow used for the arrival tilt only.
    """
    rng = np.random.default_rng(cfg.seed)
    n = len(market)
    ts = market["ts_us"].to_numpy()
    bid = market["bid"].to_numpy()
    ask = market["ask"].to_numpy()
    f = np.asarray(flow) + flow_bias
    dt_s = float(np.median(np.diff(ts[:1000]))) / 1e6

    u_hit_ask = rng.random(n)
    u_hit_bid = rng.random(n)
    lots = rng.integers(1, cfg.max_lot + 1, size=(n, 2))
    p_buy_arr = np.clip(cfg.lam_per_s * dt_s * (1 + cfg.beta * f), 0, 1)
    p_sell_arr = np.clip(cfg.lam_per_s * dt_s * (1 - cfg.beta * f), 0, 1)

    inv = int(init_inv)
    big = 1 << 62
    skew_inv, max_inv = (cfg.skew_inv, cfg.max_inv) if skew else (big, big)
    rows = []
    for i in range(n):
        ask_depth = 1 if inv < -skew_inv else 0
        bid_depth = 1 if inv > skew_inv else 0
        if inv > -max_inv and u_hit_ask[i] < p_buy_arr[i] * np.exp(-cfg.kappa * ask_depth):
            q = int(lots[i, 0])
            rows.append((ts[i], -1, ask[i] + ask_depth * cfg.tick, q))
            inv -= q
        if inv < max_inv and u_hit_bid[i] < p_sell_arr[i] * np.exp(-cfg.kappa * bid_depth):
            q = int(lots[i, 1])
            rows.append((ts[i], 1, bid[i] - bid_depth * cfg.tick, q))
            inv += q

    fills = pd.DataFrame(rows, columns=["ts_us", "side", "px", "qty"])
    fills["ts_us"] = fills["ts_us"].astype("int64")
    fills["side"] = fills["side"].astype("int64")
    fills["qty"] = fills["qty"].astype("int64")
    fills["fee"] = cfg.fee_bps * 1e-4 * fills["px"] * fills["qty"]
    return fills, inv


def clean(seed: int = 7, hours: float = 2.0, mm_seed: int = 11) -> Scenario:
    """pta's default session (MarketConfig(seed, hours), MMConfig(seed=mm_seed))."""
    market, flow = simulate_market(MarketConfig(seed=seed, hours=hours))
    fills = simulate_market_maker(market, flow, MMConfig(seed=mm_seed))
    end = int(market["ts_us"].iloc[-1]) + MarketConfig().dt_us
    return Scenario(market, fills, [Phase("clean", 0, end)], [])


# (name, share of session, market overrides, MM overrides, skew, flow_bias)
BAD_PHASES = (
    ("normal",        0.25, {},                        {},                  True,  0.0),
    ("adverse",       0.15, {"alpha_bps_per_s": 15.0}, {"beta": 0.95},      True,  0.0),
    ("inventory_run", 0.10, {},                        {},                  False, 0.5),
    ("drought",       0.10, {},                        {"lam_per_s": 0.01}, True,  0.0),
    ("recovery",      0.40, {},                        {},                  True,  0.0),
)

BAD_EXPECTED_ORDER = [
    ("markout_degradation", "breach", ("adverse",)),
    ("loss_from_peak", "warn", ("adverse",)),
    ("loss_from_peak", "breach", ("adverse", "inventory_run")),
    ("position", "warn", ("inventory_run",)),
    ("position", "breach", ("inventory_run",)),
    ("markout_degradation", "clear", ("inventory_run",)),
    ("fill_rate", "breach", ("drought",)),
    ("fill_rate", "clear", ("recovery",)),
    ("position", "clear", ("recovery",)),
]


def bad(seed: int = 7, hours: float = 2.0, mm_seed: int = 11) -> Scenario:
    """One session with adverse selection, an inventory run and a fill drought."""
    base_m, base_mm = MarketConfig(seed=seed), MMConfig(seed=mm_seed)
    dt = base_m.dt_us
    n_total = int(round(hours * 3600 * 1e6 / dt))
    markets, fills_all, phases = [], [], []
    t0, s0, inv = 0, base_m.s0, 0
    used = 0
    for k, (name, share, m_over, mm_over, skew, bias) in enumerate(BAD_PHASES):
        n = (n_total - used) if k == len(BAD_PHASES) - 1 else int(round(share * n_total))
        used += n
        mcfg = replace(base_m, seed=seed * 1000 + k, hours=n * dt / 3600e6, s0=s0, **m_over)
        mkt, flow = simulate_market(mcfg)
        mkt = mkt.iloc[:n].copy()
        flow = flow[:n]
        mkt["ts_us"] = mkt["ts_us"].to_numpy() + t0
        mmcfg = replace(base_mm, seed=mm_seed * 1000 + k, **mm_over)
        f, inv = simulate_mm_segment(mkt, flow, mmcfg, init_inv=inv, skew=skew, flow_bias=bias)
        markets.append(mkt)
        fills_all.append(f)
        phases.append(Phase(name, t0, t0 + n * dt))
        t0 += n * dt
        s0 = float(mkt["mid"].iloc[-1])
    market = pd.concat(markets, ignore_index=True)
    fills = pd.concat(fills_all, ignore_index=True)
    return Scenario(market, fills, phases, list(BAD_EXPECTED_ORDER))


def phase_at(phases: list[Phase], ts_us: int) -> str | None:
    for p in phases:
        if p.start_us <= ts_us < p.end_us:
            return p.name
    return None
