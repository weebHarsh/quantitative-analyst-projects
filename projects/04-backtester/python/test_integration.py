"""Integration tests: generator -> mdtool normalize -> bt_run -> pta / rmon (DESIGN 9.4)."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT.parent / "03-market-data" / "tools" / "gen_feeds.py"
BUILD = Path(os.environ.get("BT_BUILD_DIR", "build"))
BUILD = BUILD if BUILD.is_absolute() else ROOT / BUILD
BT_RUN, MDTOOL = BUILD / "bt_run", BUILD / "md" / "mdtool"


@pytest.fixture(scope="session", autouse=True)
def binaries():
    if not (BT_RUN.exists() and MDTOOL.exists()):
        subprocess.run(["make", "-C", str(ROOT), "build", f"BUILD={BUILD}"], capture_output=True)
    if not (BT_RUN.exists() and MDTOOL.exists()):
        pytest.skip(f"bt_run / mdtool not built under {BUILD} (run `make build`)", allow_module_level=False)


@pytest.fixture(scope="module")
def feed(tmp_path_factory):
    d = tmp_path_factory.mktemp("feed")
    subprocess.run([sys.executable, str(GEN), "--seed", "7", "--hours", "0.1", "--out", str(d), "--no-verify"],
                   check=True, capture_output=True)
    ev = d / "events_A.csv"
    subprocess.run([str(MDTOOL), "normalize", "--venue", "A", "--in", str(d / "venueA.jsonl"), "--out", str(ev)],
                   check=True, capture_output=True)
    return d


def run_bt(feed, out, *args, check=True):
    r = subprocess.run([str(BT_RUN), "--events", str(feed / "events_A.csv"), "--out", str(out), *args],
                       capture_output=True, text=True)
    if check:
        assert r.returncode == 0, r.stderr
    return r


def summary(d):
    return dict(ln.split(",", 1) for ln in (Path(d) / "summary.csv").read_text().splitlines()[1:])


def mm(feed, tmp_path, name, model, lat_us=1000, f=None):
    out = tmp_path / name
    args = ["--strategy", "mm", "--latency-us", str(lat_us), "--model", model]
    if f is not None:
        args += ["--cancel-ahead", str(f)]
    run_bt(feed, out, *args)
    return out


def test_market_csv_identical_to_mdtool_export(feed, tmp_path):
    out = mm(feed, tmp_path, "r", "fifo_cancel_ahead", f=0.5)
    ref = tmp_path / "export.csv"
    subprocess.run([str(MDTOOL), "export", "--a", str(feed / "venueA.jsonl"), "--venue", "A", "--out", str(ref)],
                   check=True, capture_output=True)
    assert (out / "market.csv").read_bytes() == ref.read_bytes()


def test_pta_loads_validates_and_reconciles(feed, tmp_path):
    from pta.data.loaders import load_fills, load_market, validate
    from pta.report import analyze

    out = mm(feed, tmp_path, "r", "fifo_cancel_ahead", f=0.5)
    s = summary(out)
    assert int(s["fills"]) > 0
    assert int(s["fills_outside_market"]) == 0 and int(s["trade_credit_unmatched_lots"]) == 0
    fills, market = load_fills(out / "fills.csv"), load_market(out / "market.csv")
    validate(fills, market)
    res = analyze(fills, market)
    t = res["totals"]
    assert t["total"] == pytest.approx(t["spread_capture"] + t["inventory"] - t["fees"], abs=1e-9)
    assert res["curve"]["equity"].iloc[-1] == pytest.approx(t["total"], abs=1e-9)


def test_fills_fee_has_fixed_precision(feed, tmp_path):
    out = mm(feed, tmp_path, "r", "fifo_back")
    for ln in (out / "fills.csv").read_text().splitlines()[1:]:
        fee = ln.split(",")[4]
        assert len(fee.split(".")[1]) == 12


def test_hash_matches_python_fnv(feed, tmp_path):
    out = mm(feed, tmp_path, "r", "fifo_back")
    h = 14695981039346656037
    for b in (out / "fills.csv").read_bytes():
        h = ((h ^ b) * 1099511628211) % (1 << 64)
    assert summary(out)["fills_fnv1a64"] == f"{h:016x}"


def test_rmon_runs_on_outputs(feed, tmp_path):
    out = mm(feed, tmp_path, "r", "fifo_cancel_ahead", f=0.5)
    r = subprocess.run([sys.executable, "-m", "rmon.cli", "run", "--market", str(out / "market.csv"),
                        "--fills", str(out / "fills.csv")], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_determinism_bytes_and_hash(feed, tmp_path):
    a = run_bt(feed, tmp_path / "a", "--model", "fifo_back", "--latency-us", "5000")
    b = run_bt(feed, tmp_path / "b", "--model", "fifo_back", "--latency-us", "5000")
    for f in ("fills.csv", "summary.csv", "orders.csv", "market.csv", "fills_detail.csv"):
        assert (tmp_path / "a" / f).read_bytes() == (tmp_path / "b" / f).read_bytes()
    assert a.stderr.split("hash")[1].split()[0] == b.stderr.split("hash")[1].split()[0]
    assert summary(tmp_path / "a")["fills_fnv1a64"] == a.stderr.split("hash")[1].split()[0].strip(",")


def test_model_ordering_reported(feed, tmp_path, capsys):
    counts = {}
    for name, model, f in (("OPTIMISTIC", "optimistic", None), ("CA(0.5)", "fifo_cancel_ahead", 0.5),
                           ("FIFO_BACK", "fifo_back", None)):
        counts[name] = int(summary(mm(feed, tmp_path, name.replace("(", "").replace(")", ""), model, f=f))["fills"])
    with capsys.disabled():
        print(f"\n[market-maker fill counts, 0.1 h, L=1ms] {counts}")
    if not (counts["OPTIMISTIC"] >= counts["CA(0.5)"] >= counts["FIFO_BACK"]):
        # DESIGN 3.6: for the market maker the ordering is reported, not asserted
        pytest.xfail(f"market-maker fill-count ordering does not hold on this feed: {counts}")


def test_exit_codes(feed, tmp_path):
    r = subprocess.run([str(BT_RUN), "--events", str(tmp_path / "nope.csv"), "--out", str(tmp_path / "o")],
                       capture_output=True, text=True)
    assert r.returncode == 3
    r = run_bt(feed, tmp_path / "o2", "--model", "no_such_model", check=False)
    assert r.returncode == 2
    r = run_bt(feed, tmp_path / "o3", "--model", "fifo_cancel_ahead", check=False)  # missing --cancel-ahead
    assert r.returncode == 2


def test_sensitivity_tiny_grid_is_deterministic(feed, tmp_path):
    import sensitivity

    def go(tag):
        out = tmp_path / tag
        rc = sensitivity.main(["--bt-run", str(BT_RUN), "--mdtool", str(MDTOOL), "--out", str(out),
                               "--cache", str(tmp_path / "cache"), "--seeds", "7", "--hours", "0.1",
                               "--latencies-ms", "0,5", "--models", "optimistic,fifo_back", "--cross-fills", "on,off",
                               "--jobs", "2"])
        assert rc == 0
        return out

    a, b = go("a"), go("b")
    assert (a / "sensitivity.csv").read_bytes() == (b / "sensitivity.csv").read_bytes()
    assert (a / "sensitivity_table.md").read_bytes() == (b / "sensitivity_table.md").read_bytes()
    assert (a / "sensitivity.png").stat().st_size > 10_000
    assert (a / "sensitivity_cross_off.png").stat().st_size > 10_000
    import pandas as pd
    df = pd.read_csv(a / "sensitivity.csv")
    assert set(df["cross_fills"]) == {"on", "off"} and len(df) == 2 * 2 * 2
    assert (df.loc[df["cross_fills"] == "off", "fills_cross"] == 0).all()
    assert ((df["cross_share"] >= 0) & (df["cross_share"] <= 1)).all()
    assert sensitivity.STATEMENT.replace("\n", " ") in (a / "sensitivity_table.md").read_text().replace("\n> ", " ").replace("\n", " ")


def test_cross_off_has_fewer_fifo_back_fills(feed, tmp_path):
    on = run_bt(feed, tmp_path / "on", "--strategy", "mm", "--latency-us", "1000", "--model", "fifo_back",
                "--cross-fills", "on")
    off = run_bt(feed, tmp_path / "off", "--strategy", "mm", "--latency-us", "1000", "--model", "fifo_back",
                 "--cross-fills", "off")
    assert on.returncode == 0 and off.returncode == 0
    s_on, s_off = summary(tmp_path / "on"), summary(tmp_path / "off")
    assert int(s_off["fills_cross"]) == 0 < int(s_on["fills_cross"])
    assert int(s_off["fills"]) < int(s_on["fills"])


def test_summary_cross_count_matches_fills_detail(feed, tmp_path):
    out = mm(feed, tmp_path, "r", "fifo_back")
    kinds = [ln.split(",")[5] for ln in (out / "fills_detail.csv").read_text().splitlines()[1:]]
    s = summary(out)
    assert kinds.count("CROSS") == int(s["fills_cross"]) and len(kinds) == int(s["fills"])
