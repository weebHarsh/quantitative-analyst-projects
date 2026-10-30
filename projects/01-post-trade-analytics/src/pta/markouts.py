"""Post-fill markouts.

markout_h = side * (mid(ts + h) - px), per unit. Positive means the market moved
in our favour after the fill. At h=0 it equals the spread captured against mid;
the drop from h=0 to larger h is the adverse-selection cost.

A fill whose ts + h is past the last quote has no markout (NaN) and is excluded.
"""
from __future__ import annotations

import numpy as np
import pandas as pd

from .pnl import mid_asof

DEFAULT_HORIZONS_US = (0, 100_000, 1_000_000, 5_000_000, 30_000_000, 60_000_000)


def compute(fills: pd.DataFrame, market: pd.DataFrame,
            horizons_us: tuple[int, ...] = DEFAULT_HORIZONS_US) -> pd.DataFrame:
    """Per-fill markout per unit for each horizon, columns mo_<h_us>."""
    ts = fills["ts_us"].to_numpy()
    s = fills["side"].to_numpy(dtype=float)
    px = fills["px"].to_numpy()
    cols = {}
    for h in horizons_us:
        cols[f"mo_{h}"] = s * (mid_asof(market, ts + h) - px)
    return pd.DataFrame(cols)


def summarize(fills: pd.DataFrame, per_fill: pd.DataFrame) -> pd.DataFrame:
    """One row per horizon: n, qty, total PnL (sum of qty * markout), averages."""
    q = fills["qty"].to_numpy(dtype=float)
    px = fills["px"].to_numpy()
    s = fills["side"].to_numpy()
    rows = []
    for col in per_fill.columns:
        h = int(col.split("_")[1])
        mo = per_fill[col].to_numpy()
        ok = ~np.isnan(mo)
        n = int(ok.sum())
        qty = float(q[ok].sum())
        total = float((q[ok] * mo[ok]).sum())
        row = {"horizon_us": h, "n": n, "qty": qty, "total": total}
        row["avg_per_unit"] = total / qty if qty else np.nan
        row["avg_bps"] = float(np.average(mo[ok] / px[ok] * 1e4, weights=q[ok])) if n else np.nan
        row["pct_positive"] = float((mo[ok] > 0).mean()) if n else np.nan
        for side, name in ((1, "buy"), (-1, "sell")):
            sel = ok & (s == side)
            row[f"avg_bps_{name}"] = (float(np.average(mo[sel] / px[sel] * 1e4, weights=q[sel]))
                                      if sel.any() else np.nan)
        rows.append(row)
    return pd.DataFrame(rows)
