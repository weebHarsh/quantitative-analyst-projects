"""A deliberately simple inventory-skewed passive market maker.

It joins the best bid and ask. Aggressors arrive as a Poisson process whose
intensity on each side is tilted by the flow signal. When inventory passes
``skew_inv`` the quote on the side that would add to it is moved one tick behind
the touch (lower fill probability). At ``max_inv`` that side is pulled.

This exists to generate realistic-looking fills for the analytics, not to be a
profitable strategy.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import pandas as pd


@dataclass(frozen=True)
class MMConfig:
    seed: int = 11
    lam_per_s: float = 0.4     # aggressor arrival rate per side at the touch
    beta: float = 0.5          # how strongly flow tilts arrivals
    kappa: float = 1.5         # fill-probability decay per tick of depth
    max_inv: int = 50
    skew_inv: int = 20
    max_lot: int = 3
    tick: float = 0.01
    fee_bps: float = 0.1       # positive = cost, negative = rebate


def simulate_market_maker(market: pd.DataFrame, flow: np.ndarray | None,
                          cfg: MMConfig = MMConfig()) -> pd.DataFrame:
    """Return fills with columns ts_us, side (+1 buy / -1 sell), px, qty, fee.

    ``flow=None`` replays on any market data (for example real quotes) with no
    link between flow and price; useful for plumbing, not for conclusions.
    """
    rng = np.random.default_rng(cfg.seed)
    n = len(market)
    ts = market["ts_us"].to_numpy()
    bid = market["bid"].to_numpy()
    ask = market["ask"].to_numpy()
    f = np.zeros(n) if flow is None else np.asarray(flow)
    dt_s = float(np.median(np.diff(ts[:1000]))) / 1e6

    u_hit_ask = rng.random(n)   # a buyer lifts our ask  -> we sell
    u_hit_bid = rng.random(n)   # a seller hits our bid  -> we buy
    lots = rng.integers(1, cfg.max_lot + 1, size=(n, 2))
    p_buy_arr = np.clip(cfg.lam_per_s * dt_s * (1 + cfg.beta * f), 0, 1)
    p_sell_arr = np.clip(cfg.lam_per_s * dt_s * (1 - cfg.beta * f), 0, 1)

    inv = 0
    rows = []
    for i in range(n):
        ask_depth = 1 if inv < -cfg.skew_inv else 0
        bid_depth = 1 if inv > cfg.skew_inv else 0
        if inv > -cfg.max_inv and u_hit_ask[i] < p_buy_arr[i] * np.exp(-cfg.kappa * ask_depth):
            q = int(lots[i, 0])
            px = ask[i] + ask_depth * cfg.tick
            rows.append((ts[i], -1, px, q))
            inv -= q
        if inv < cfg.max_inv and u_hit_bid[i] < p_sell_arr[i] * np.exp(-cfg.kappa * bid_depth):
            q = int(lots[i, 1])
            px = bid[i] - bid_depth * cfg.tick
            rows.append((ts[i], 1, px, q))
            inv += q

    fills = pd.DataFrame(rows, columns=["ts_us", "side", "px", "qty"])
    fills["ts_us"] = fills["ts_us"].astype("int64")
    fills["side"] = fills["side"].astype("int64")
    fills["qty"] = fills["qty"].astype("int64")
    fills["fee"] = cfg.fee_bps * 1e-4 * fills["px"] * fills["qty"]
    return fills
