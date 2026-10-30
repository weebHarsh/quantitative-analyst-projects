"""CSV schemas, validation and the export used by the C++ kernel.

market.csv : ts_us (int64 microseconds, strictly increasing), bid, ask, mid
fills.csv  : ts_us (int64, non-decreasing), side (+1 buy / -1 sell), px, qty, fee
             fee is a cost when positive and a rebate when negative.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd

MARKET_COLS = ["ts_us", "bid", "ask", "mid"]
FILL_COLS = ["ts_us", "side", "px", "qty", "fee"]


def validate(fills: pd.DataFrame, market: pd.DataFrame) -> None:
    missing = [c for c in MARKET_COLS if c not in market.columns]
    if missing:
        raise ValueError(f"market is missing columns {missing}")
    missing = [c for c in FILL_COLS if c not in fills.columns]
    if missing:
        raise ValueError(f"fills is missing columns {missing}")
    if len(market) < 2:
        raise ValueError("market needs at least two rows")
    mts = market["ts_us"].to_numpy()
    if np.any(np.diff(mts) <= 0):
        raise ValueError("market ts_us must be strictly increasing")
    if len(fills) == 0:
        return
    fts = fills["ts_us"].to_numpy()
    if np.any(np.diff(fts) < 0):
        raise ValueError("fills ts_us must be non-decreasing")
    if not set(np.unique(fills["side"])) <= {-1, 1}:
        raise ValueError("fills side must be +1 or -1")
    if np.any(fills["qty"].to_numpy() <= 0):
        raise ValueError("fills qty must be positive")
    if fts[0] < mts[0] or fts[-1] > mts[-1]:
        raise ValueError("fills must fall inside the market time range")


def load_market(path: str | Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["ts_us"] = df["ts_us"].astype("int64")
    return df[MARKET_COLS]


def load_fills(path: str | Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    for c in ("ts_us", "side", "qty"):
        df[c] = df[c].astype("int64")
    return df[FILL_COLS]


def market_from_quotes(df: pd.DataFrame, ts_col: str, bid_col: str, ask_col: str,
                       ts_unit: str = "us") -> pd.DataFrame:
    """Build a market frame from any best-bid/best-ask table (e.g. a real feed).

    ts_unit is the unit of ts_col: 'ns', 'us' or 'ms'. Duplicate timestamps keep
    the last quote. Column names are yours to supply; no vendor layout is assumed.
    """
    scale = {"ns": 1e-3, "us": 1.0, "ms": 1e3}[ts_unit]
    out = pd.DataFrame({
        "ts_us": (df[ts_col].to_numpy(dtype="float64") * scale).astype("int64"),
        "bid": df[bid_col].to_numpy(dtype="float64"),
        "ask": df[ask_col].to_numpy(dtype="float64"),
    })
    out = out.drop_duplicates("ts_us", keep="last").sort_values("ts_us").reset_index(drop=True)
    out["mid"] = (out["bid"] + out["ask"]) / 2
    return out[MARKET_COLS]


def save_csv(df: pd.DataFrame, path: str | Path) -> None:
    df.to_csv(path, index=False, float_format="%.17g")


def export_for_cpp(fills: pd.DataFrame, market: pd.DataFrame, out_dir: str | Path) -> tuple[Path, Path]:
    """Write the two slim files markout_kernel reads: fills and (ts_us, mid)."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    fpath, mpath = out / "fills_cpp.csv", out / "market_cpp.csv"
    save_csv(fills[FILL_COLS], fpath)
    save_csv(market[["ts_us", "mid"]], mpath)
    return fpath, mpath
