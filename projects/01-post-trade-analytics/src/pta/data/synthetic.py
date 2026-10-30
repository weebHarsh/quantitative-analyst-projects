"""Seeded synthetic market with a short-lived order-flow signal.

The latent flow process ``f`` is an AR(1) with unit variance. It does two things:
it drifts the price in the direction of the flow, and it skews which side of the
book aggressors hit. A passive market maker therefore gets filled more often just
before the price moves against it, which is the adverse selection that markouts
are meant to expose.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import pandas as pd


@dataclass(frozen=True)
class MarketConfig:
    seed: int = 7
    hours: float = 6.0
    dt_us: int = 100_000            # 100 ms grid
    s0: float = 100.0
    tick: float = 0.01
    sigma_bps_sqrt_s: float = 1.0   # diffusive vol, bps per sqrt(second)
    jump_rate_per_s: float = 1 / 300
    jump_bps: float = 5.0           # jump size std, bps
    flow_tau_s: float = 2.0         # half-life scale of the flow signal
    alpha_bps_per_s: float = 0.6    # drift per unit of flow, bps per second
    wide_spread_prob: float = 0.15  # share of ticks with a 2-tick spread


def simulate_market(cfg: MarketConfig = MarketConfig()) -> tuple[pd.DataFrame, np.ndarray]:
    """Return (market, flow). market has int64 ts_us plus float bid, ask, mid."""
    rng = np.random.default_rng(cfg.seed)
    n = int(cfg.hours * 3600 * 1e6 / cfg.dt_us)
    dt_s = cfg.dt_us / 1e6

    rho = np.exp(-dt_s / cfg.flow_tau_s)
    innov = np.sqrt(1 - rho**2) * rng.standard_normal(n)
    flow = np.empty(n)
    flow[0] = rng.standard_normal()
    for i in range(1, n):
        flow[i] = rho * flow[i - 1] + innov[i]

    diffusion = cfg.sigma_bps_sqrt_s * 1e-4 * np.sqrt(dt_s) * rng.standard_normal(n)
    jumps = (rng.random(n) < cfg.jump_rate_per_s * dt_s) * rng.normal(0, cfg.jump_bps * 1e-4, n)
    drift = cfg.alpha_bps_per_s * 1e-4 * dt_s * flow
    log_ret = diffusion + jumps + drift
    log_ret[0] = 0.0
    price = cfg.s0 * np.exp(np.cumsum(log_ret))

    spread_ticks = 1 + (rng.random(n) < cfg.wide_spread_prob)
    bid = np.floor(price / cfg.tick) * cfg.tick - (spread_ticks - 1) * cfg.tick
    ask = bid + spread_ticks * cfg.tick
    market = pd.DataFrame({
        "ts_us": np.arange(n, dtype=np.int64) * cfg.dt_us,
        "bid": bid,
        "ask": ask,
        "mid": (bid + ask) / 2,
    })
    return market, flow
