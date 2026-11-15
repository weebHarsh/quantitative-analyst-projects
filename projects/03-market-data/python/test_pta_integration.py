"""mdtool export -> Project 1 (pta) integration.

The exported CSV must load with ``pta.data.loaders.load_market``, pass its validation
(strictly increasing ts_us, columns ts_us,bid,ask,mid), and be usable by the Project 1
market-maker replay and attribution. All feeds are SYNTHETIC (tools/gen_feeds.py).

The build is located via env MD_BUILD_DIR (default ``build``); if mdtool is missing the tests try
``make all`` once and skip with a clear message if that does not produce it.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "tools" / "gen_feeds.py"
GOLDEN = ROOT / "tests" / "golden"

pta_loaders = pytest.importorskip("pta.data.loaders", reason="Project 1 package `pta` is not importable")
from pta import pnl, report  # noqa: E402
from pta.strategy.market_maker import simulate_market_maker  # noqa: E402


def _build_dir() -> Path:
    d = Path(os.environ.get("MD_BUILD_DIR", "build"))
    return d if d.is_absolute() else ROOT / d


@pytest.fixture(scope="module")
def mdtool() -> Path:
    exe = _build_dir() / "mdtool"
    if not exe.exists() and "MD_BUILD_DIR" not in os.environ:
        subprocess.run(["make", "-C", str(ROOT), "all"], capture_output=True, text=True)
    if not exe.exists():
        pytest.skip(f"mdtool not built at {exe}; run `make -C {ROOT} all` or set MD_BUILD_DIR")
    return exe


def run_export(mdtool: Path, a: Path, b: Path, out: Path, venue: str = "merged") -> dict:
    r = subprocess.run([str(mdtool), "export", "--a", str(a), "--b", str(b), "--out", str(out),
                        "--venue", venue], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return json.loads(r.stdout)


def empty_fills() -> pd.DataFrame:
    return pd.DataFrame({"ts_us": pd.Series([], dtype="int64"), "side": pd.Series([], dtype="int64"),
                         "px": pd.Series([], dtype="float64"), "qty": pd.Series([], dtype="float64"),
                         "fee": pd.Series([], dtype="float64")})


def check_market(path: Path, rows: int) -> pd.DataFrame:
    market = pta_loaders.load_market(path)
    assert list(market.columns) == ["ts_us", "bid", "ask", "mid"]
    assert len(market) == rows
    pta_loaders.validate(empty_fills(), market)  # raises on non-increasing ts_us or bad columns
    assert (np.diff(market["ts_us"].to_numpy()) > 0).all()
    assert (market["bid"] < market["ask"]).all()
    # mid is exact: (bid + ask) / 2 with three decimals
    assert np.allclose(market["mid"], (market["bid"] + market["ask"]) / 2, atol=1e-9, rtol=0)
    # bid/ask are on the 0.01 tick grid
    assert np.allclose(market["bid"] * 100, np.round(market["bid"] * 100), atol=1e-6, rtol=0)
    assert np.allclose(market["ask"] * 100, np.round(market["ask"] * 100), atol=1e-6, rtol=0)
    return market


def test_golden_export_loads_and_validates(mdtool, tmp_path):
    out = tmp_path / "golden_export.csv"
    summary = run_export(mdtool, GOLDEN / "venueA.jsonl", GOLDEN / "venueB.csv", out)
    assert out.read_bytes() == (GOLDEN / "expected_export.csv").read_bytes()
    market = check_market(out, summary["rows"])
    assert summary["rows"] == 38
    expected = pta_loaders.load_market(GOLDEN / "expected_export.csv")
    pd.testing.assert_frame_equal(market, expected)


@pytest.mark.parametrize("venue", ["A", "B"])
def test_golden_single_venue_export_loads(mdtool, tmp_path, venue):
    out = tmp_path / f"golden_{venue}.csv"
    summary = run_export(mdtool, GOLDEN / "venueA.jsonl", GOLDEN / "venueB.csv", out, venue)
    check_market(out, summary["rows"])


@pytest.fixture(scope="module")
def faulted_feed(tmp_path_factory) -> Path:
    d = tmp_path_factory.mktemp("faulted15m")
    subprocess.run([sys.executable, str(GEN), "--seed", "7", "--hours", "0.25", "--faults",
                    "--no-verify", "--out", str(d)], check=True, capture_output=True, text=True)
    return d


@pytest.mark.parametrize("venue", ["merged", "A", "B"])
def test_faulted_15_minute_export_loads_and_is_strictly_increasing(mdtool, faulted_feed, tmp_path, venue):
    out = tmp_path / f"export_{venue}.csv"
    summary = run_export(mdtool, faulted_feed / "venueA.jsonl", faulted_feed / "venueB.csv", out, venue)
    market = check_market(out, summary["rows"])
    # 15 minutes of a 100 ms grid is 9,000 steps; the export has many thousands of rows
    assert summary["rows"] > 5000
    span_s = (market["ts_us"].iloc[-1] - market["ts_us"].iloc[0]) / 1e6
    assert 800 < span_s < 1000
    # the injected faults leave stale periods, but never a crossed or locked exported row
    assert (market["bid"] < market["ask"]).all()


def test_faulted_export_is_deterministic(mdtool, faulted_feed, tmp_path):
    o1, o2 = tmp_path / "o1.csv", tmp_path / "o2.csv"
    run_export(mdtool, faulted_feed / "venueA.jsonl", faulted_feed / "venueB.csv", o1)
    run_export(mdtool, faulted_feed / "venueA.jsonl", faulted_feed / "venueB.csv", o2)
    assert o1.read_bytes() == o2.read_bytes()


def test_export_feeds_project1_replay_and_attribution(mdtool, faulted_feed, tmp_path):
    out = tmp_path / "export.csv"
    run_export(mdtool, faulted_feed / "venueA.jsonl", faulted_feed / "venueB.csv", out)
    market = pta_loaders.load_market(out)
    fills = simulate_market_maker(market, None)
    if len(fills) == 0:
        pytest.skip("market-maker replay produced no fills on this export")
    pta_loaders.validate(fills, market)
    res = report.analyze(fills, market)
    t = res["totals"]
    assert t["total"] == pytest.approx(t["spread_capture"] + t["inventory"] - t["fees"], abs=1e-9)
    curve = pnl.equity_curve(fills, market)
    assert curve["equity"].iloc[-1] == pytest.approx(t["total"], abs=1e-6)
