"""Mark-to-market PnL and its attribution.

For fill j (side s, qty q, price px, mid m_j at the fill) and final mid M:

    s*q*(M - px) = s*q*(m_j - px)      spread capture: edge against mid at the fill
                 + s*q*(M - m_j)       inventory: price move while holding the fill

Summed over fills and net of fees this is exactly cash + position*M - fees, which
``equity_curve`` computes without using the decomposition. The tests check that
the two agree.
"""
from __future__ import annotations

import numpy as np
import pandas as pd


def mid_asof(market: pd.DataFrame, ts: np.ndarray) -> np.ndarray:
    """Last mid at or before each ts. NaN before the first quote or after the last."""
    mts = market["ts_us"].to_numpy()
    idx = np.searchsorted(mts, ts, side="right") - 1
    out = np.full(len(ts), np.nan)
    ok = (idx >= 0) & (ts <= mts[-1])
    out[ok] = market["mid"].to_numpy()[idx[ok]]
    return out


def attribute(fills: pd.DataFrame, market: pd.DataFrame) -> tuple[pd.DataFrame, dict]:
    """Per-fill components plus totals, marked to the final market mid."""
    s = fills["side"].to_numpy(dtype=float)
    q = fills["qty"].to_numpy(dtype=float)
    px = fills["px"].to_numpy()
    fee = fills["fee"].to_numpy()
    m = mid_asof(market, fills["ts_us"].to_numpy())
    if np.isnan(m).any():
        raise ValueError("some fills have no market mid at or before their timestamp")
    final_mid = float(market["mid"].iloc[-1])

    per_fill = pd.DataFrame({
        "ts_us": fills["ts_us"].to_numpy(),
        "mid_at_fill": m,
        "spread_capture": s * q * (m - px),
        "inventory": s * q * (final_mid - m),
        "fee": fee,
    })
    totals = {
        "spread_capture": float(per_fill["spread_capture"].sum()),
        "inventory": float(per_fill["inventory"].sum()),
        "fees": float(per_fill["fee"].sum()),
        "final_mid": final_mid,
    }
    totals["total"] = totals["spread_capture"] + totals["inventory"] - totals["fees"]
    return per_fill, totals


def equity_curve(fills: pd.DataFrame, market: pd.DataFrame) -> pd.DataFrame:
    """Equity = cash + position * mid - cumulative fees, on every market timestamp."""
    signed = (fills["side"] * fills["qty"]).to_numpy(dtype=float)
    pos_cum = np.concatenate([[0.0], np.cumsum(signed)])
    cash_cum = np.concatenate([[0.0], np.cumsum(-signed * fills["px"].to_numpy())])
    fee_cum = np.concatenate([[0.0], np.cumsum(fills["fee"].to_numpy())])
    k = np.searchsorted(fills["ts_us"].to_numpy(), market["ts_us"].to_numpy(), side="right")
    mid = market["mid"].to_numpy()
    return pd.DataFrame({
        "ts_us": market["ts_us"].to_numpy(),
        "position": pos_cum[k],
        "equity": cash_cum[k] + pos_cum[k] * mid - fee_cum[k],
    })


def inventory_pnl_on_grid(curve: pd.DataFrame, market: pd.DataFrame) -> float:
    """Sum of position_t * (mid_{t+1} - mid_t). Equals the per-fill inventory total
    when fills sit on market timestamps."""
    return float(np.sum(curve["position"].to_numpy()[:-1] * np.diff(market["mid"].to_numpy())))


def cumulative_components(fills: pd.DataFrame, market: pd.DataFrame) -> pd.DataFrame:
    """Running spread / inventory / fee / total, marked at the mid of each fill."""
    per_fill, _ = attribute(fills, market)
    pos_after = (fills["side"] * fills["qty"]).cumsum().to_numpy(dtype=float)
    dm = np.diff(per_fill["mid_at_fill"].to_numpy())
    inv_cum = np.concatenate([[0.0], np.cumsum(pos_after[:-1] * dm)])
    spread_cum = per_fill["spread_capture"].cumsum().to_numpy()
    fee_cum = per_fill["fee"].cumsum().to_numpy()
    return pd.DataFrame({
        "ts_us": per_fill["ts_us"].to_numpy(),
        "spread_capture": spread_cum,
        "inventory": inv_cum,
        "fees": -fee_cum,
        "total": spread_cum + inv_cum - fee_cum,
    })
