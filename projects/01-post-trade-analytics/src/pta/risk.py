"""Risk and performance statistics from the equity curve and the fills.

Bars are fixed-width time buckets (default 1 minute) of the mark-to-market
equity. Annualisation assumes ``bars_per_day`` bars in a trading day and 252
days; change both if your session differs.
"""
from __future__ import annotations

import numpy as np
import pandas as pd


def bar_pnl(curve: pd.DataFrame, bar_us: int = 60_000_000) -> tuple[np.ndarray, np.ndarray]:
    """Return (bar-end equity, per-bar PnL). The first bar is measured from 0."""
    ts = curve["ts_us"].to_numpy()
    bucket = (ts - ts[0]) // bar_us
    last = np.flatnonzero(np.diff(bucket, append=bucket[-1] + 1))
    eq = curve["equity"].to_numpy()[last]
    return eq, np.diff(eq, prepend=0.0)


def max_drawdown(equity: np.ndarray) -> float:
    peak = np.maximum.accumulate(np.concatenate([[0.0], equity]))[1:]
    return float(np.max(peak - equity))


def var_es(pnl: np.ndarray, conf: float) -> tuple[float, float]:
    """Historical VaR and expected shortfall as positive loss numbers."""
    cut = np.quantile(pnl, 1 - conf)
    tail = pnl[pnl <= cut]
    return float(-cut), float(-tail.mean())


def summary(fills: pd.DataFrame, curve: pd.DataFrame, total_pnl: float,
            bar_us: int = 60_000_000, bars_per_day: int = 390) -> dict:
    eq, pnl = bar_pnl(curve, bar_us)
    ann = np.sqrt(bars_per_day * 252)
    mean, std = float(pnl.mean()), float(pnl.std(ddof=1))
    downside = float(np.sqrt(np.mean(np.minimum(pnl, 0.0) ** 2)))

    ts = curve["ts_us"].to_numpy()
    w = np.diff(ts, append=ts[-1]).astype(float)
    absinv = np.abs(curve["position"].to_numpy())
    avg_abs_inv = float(np.average(absinv, weights=w))

    notional = float((fills["qty"] * fills["px"]).sum())
    var95, es95 = var_es(pnl, 0.95)
    var99, es99 = var_es(pnl, 0.99)
    return {
        "total_pnl": total_pnl,
        "bars": int(len(pnl)),
        "mean_bar_pnl": mean,
        "std_bar_pnl": std,
        "sharpe_annualised": mean / std * ann if std > 0 else float("nan"),
        "sharpe_tstat": mean / std * np.sqrt(len(pnl)) if std > 0 else float("nan"),
        "sortino_annualised": mean / downside * ann if downside > 0 else float("nan"),
        "max_drawdown": max_drawdown(eq),
        "var95_bar": var95, "es95_bar": es95,
        "var99_bar": var99, "es99_bar": es99,
        "fills": int(len(fills)),
        "traded_qty": float(fills["qty"].sum()),
        "traded_notional": notional,
        "pnl_bps_of_notional": total_pnl / notional * 1e4 if notional else float("nan"),
        "avg_abs_inventory": avg_abs_inv,
        "max_abs_inventory": float(absinv.max()),
        "turnover_x_avg_inventory": float(fills["qty"].sum() / avg_abs_inv) if avg_abs_inv else float("nan"),
    }
