#!/usr/bin/env python3
"""Sensitivity study (DESIGN section 7): latency x queue model x market seed.

All inputs are SYNTHETIC (Project 3's generator).  Nothing here is live PnL.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULT_GEN = ROOT.parent / "03-market-data" / "tools" / "gen_feeds.py"

COLUMNS = ["seed", "hours", "cross_fills", "latency_ms", "model", "cancel_ahead", "label", "total_pnl", "spread_capture",
           "inventory_pnl", "fees", "markout_1s_bps", "fills", "fills_per_min", "filled_qty", "fill_rate",
           "orders_accepted", "cancels_in_flight_filled", "fills_cross", "fills_trade_through",
           "cross_share", "max_abs_position", "fills_fnv1a64", "note"]
METRICS = ["total_pnl", "spread_capture", "inventory_pnl", "fees", "markout_1s_bps", "fills", "fills_per_min",
           "filled_qty", "fill_rate", "orders_accepted", "cancels_in_flight_filled", "fills_cross",
           "fills_trade_through", "cross_share", "max_abs_position"]

STATEMENT = (
    "These numbers come from a synthetic market (Project 3's generator on Project 1's price\n"
    "path) and a backtester with no market impact. Their sizes are properties of that\n"
    "generator: its trade rate, its random re-sizing of book levels, and its touch moves that\n"
    "delete and re-add levels without printing trades. They do not transfer to any real\n"
    "venue. The direction of the effect (optimistic fill assumptions raise fill counts and\n"
    "backtest PnL) is the finding; the magnitudes are not. A backtest is not live PnL: it\n"
    "cannot reproduce real queue dynamics, how other participants react to our orders, or\n"
    "real latency distributions. Senior HFT roles ask for a live track record, and no project\n"
    "here substitutes for one.")

DEFAULT_MODELS = "optimistic,fifo_back,fifo_cancel_ahead:0.25,fifo_cancel_ahead:0.5"
# one colour per model, colour-blind safe (Okabe-Ito), same in every panel
COLORS = {"OPTIMISTIC": "#D55E00", "FIFO_CA(0.5)": "#0072B2", "FIFO_CA(0.25)": "#009E73", "FIFO_BACK": "#333333"}


def parse_models(s: str) -> list[tuple[str, float | None]]:
    out = []
    for tok in s.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if ":" in tok:
            name, f = tok.split(":", 1)
            out.append((name, float(f)))
        else:
            out.append((tok, None))
    return out


def label_of(model: str, f: float | None) -> str:
    if model == "optimistic":
        return "OPTIMISTIC"
    if model == "fifo_back":
        return "FIFO_BACK"
    return f"FIFO_CA({f:g})"


def ensure_feed(seed: int, hours: float, work_dir: Path, gen_feeds: Path, mdtool: Path) -> Path:
    d = Path(work_dir) / f"feed_{seed}_{hours:g}h"
    events = d / "events_A.csv"
    meta = d / "meta.json"
    if events.exists() and meta.exists():
        try:
            m = json.loads(meta.read_text())
            if m.get("seed") == seed and abs(float(m.get("hours", -1)) - hours) < 1e-12:
                return events
        except (ValueError, OSError):
            pass
    d.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(gen_feeds), "--seed", str(seed), "--hours", str(hours), "--out", str(d),
                    "--no-verify"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([str(mdtool), "normalize", "--venue", "A", "--in", str(d / "venueA.jsonl"), "--out", str(events)],
                   check=True, stdout=subprocess.DEVNULL)
    return events


def run_cell(bt_run, events, seed, latency_ms, model, f, out_dir, cross="on", extra_args=()) -> Path:
    run_dir = (Path(out_dir) /
               f"s{seed}_L{latency_ms:g}_{label_of(model, f).replace('(', '').replace(')', '')}_x{cross}")
    cmd = [str(bt_run), "--events", str(events), "--out", str(run_dir), "--strategy", "mm",
           "--latency-us", str(int(round(latency_ms * 1000))), "--model", model,
           "--cross-fills", cross]
    if model == "fifo_cancel_ahead":
        cmd += ["--cancel-ahead", f"{f:g}"]
    cmd += list(extra_args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"bt_run failed for seed {seed} L={latency_ms}ms {label_of(model, f)}: "
                           f"exit {r.returncode}: {r.stderr.strip()}")
    return run_dir


def read_summary(run_dir: Path) -> dict:
    out = {}
    for line in (Path(run_dir) / "summary.csv").read_text().splitlines()[1:]:
        k, v = line.split(",", 1)
        out[k] = v
    return out


def cell_metrics(run_dir: Path) -> dict:
    from pta.data.loaders import load_fills, load_market
    from pta.report import analyze

    run_dir = Path(run_dir)
    s = read_summary(run_dir)
    n_fills = int(s["fills"])
    m = {
        "fills": n_fills,
        "filled_qty": int(s["filled_lots"]) / 1000.0,
        "fill_rate": float(s["fill_rate"]),
        "orders_accepted": int(s["orders_accepted"]),
        "cancels_in_flight_filled": int(s["cancels_in_flight_filled"]),
        "fills_cross": int(s["fills_cross"]),
        "fills_trade_through": int(s["fills_trade_through"]),
        "max_abs_position": int(s["max_abs_position_lots"]) / 1000.0,
        "fills_fnv1a64": s["fills_fnv1a64"],
    }
    dur_min = (int(s["market_last_ts_us"]) - int(s["market_first_ts_us"])) / 6e7
    # share of fills created by the cross rule: summary.csv counts fills by kind (identical to counting
    # kind == CROSS in fills_detail.csv; checked in test_integration)
    m["cross_share"] = m["fills_cross"] / n_fills if n_fills else float("nan")
    m["fills_per_min"] = n_fills / dur_min if dur_min > 0 else float("nan")
    nan = float("nan")
    m.update(total_pnl=nan, spread_capture=nan, inventory_pnl=nan, fees=nan, markout_1s_bps=nan, note="")
    if n_fills < 2:
        m["note"] = "too_few_fills"
        return m
    try:
        res = analyze(load_fills(run_dir / "fills.csv"), load_market(run_dir / "market.csv"))
        t = res["totals"]
        m.update(total_pnl=float(t["total"]), spread_capture=float(t["spread_capture"]),
                 inventory_pnl=float(t["inventory"]), fees=float(t["fees"]))
        mk = res["markouts"]
        row = mk[mk["horizon_us"] == 1_000_000]
        if len(row):
            m["markout_1s_bps"] = float(row["avg_bps"].iloc[0])
    except Exception as e:  # a pta error must not abort the study
        m["note"] = f"pta_error:{type(e).__name__}"
    return m


def _job(args):
    bt_run, events, seed, hours, lat, model, f, cross, runs_dir = args
    run_dir = run_cell(bt_run, events, seed, lat, model, f, runs_dir, cross)
    m = cell_metrics(run_dir)
    row = {"seed": seed, "hours": hours, "cross_fills": cross, "latency_ms": lat, "model": model,
           "cancel_ahead": "" if f is None else f, "label": label_of(model, f)}
    row.update(m)
    return row


def _feed_job(args):
    return ensure_feed(*args)


# ---------------------------------------------------------------- reporting
SETTING_TITLE = {"on": "cross-fill rule ON (default model)", "off": "cross-fill rule OFF (ablation)"}


def _agg(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    return df.groupby(["label", "latency_ms"])[metric].agg(["mean", "min", "max"]).reset_index()


def _range_table(df, metric, fmt, labels, lats) -> str:
    a = _agg(df, metric).set_index(["label", "latency_ms"])
    head = "| model | " + " | ".join(f"L = {l:g} ms" for l in lats) + " |"
    lines = [head, "|---|" + "---|" * len(lats)]
    for lab in labels:
        cells = []
        for l in lats:
            r = a.loc[(lab, l)]
            cells.append(f"{format(r['mean'], fmt)} [{format(r['min'], fmt)}, {format(r['max'], fmt)}]")
        lines.append(f"| {lab} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def _setting_section(d: pd.DataFrame, cross: str, num: int) -> list[str]:
    labels = list(dict.fromkeys(d["label"]))
    lats = sorted(d["latency_ms"].unique())
    mean = d.groupby(["label", "latency_ms"])[METRICS].mean()
    out = [f"# Part {num}: {SETTING_TITLE[cross]}", ""]
    for k, (title, metric, fmt) in enumerate((
            ("total PnL (currency units)", "total_pnl", ".2f"),
            ("spread capture (currency units)", "spread_capture", ".2f"),
            ("qty-weighted 1 s markout (bps)", "markout_1s_bps", ".3f"),
            ("fill count", "fills", ".0f"),
            ("share of fills created by the cross rule", "cross_share", ".3f"),
            ("fill rate (orders with a fill / accepted orders)", "fill_rate", ".3f")), 1):
        out += [f"## {num}.{k} {title}, cross rule {cross.upper()}", "", _range_table(d, metric, fmt, labels, lats), ""]
    out += [f"## {num}.7 optimistic inflation relative to FIFO_BACK (seed means), cross rule {cross.upper()}", "",
            "| L (ms) | PnL OPT - BACK | PnL CA(0.5) - BACK | fills OPT - BACK | fills CA(0.5) - BACK | fills OPT / BACK |",
            "|---|---|---|---|---|---|"]
    for l in lats:
        try:
            o, c, b = (mean.loc[(x, l)] for x in ("OPTIMISTIC", "FIFO_CA(0.5)", "FIFO_BACK"))
        except KeyError:
            continue
        out.append(f"| {l:g} | {o.total_pnl - b.total_pnl:.2f} | {c.total_pnl - b.total_pnl:.2f} | "
                   f"{o.fills - b.fills:.0f} | {c.fills - b.fills:.0f} | {o.fills / b.fills:.2f}x |")
    out += ["", f"## {num}.8 all metrics, seed means, cross rule {cross.upper()}", ""]
    cols = ["total_pnl", "spread_capture", "inventory_pnl", "fees", "markout_1s_bps", "fills", "fills_per_min",
            "fill_rate", "cancels_in_flight_filled", "fills_cross", "fills_trade_through", "cross_share",
            "max_abs_position"]
    out.append("| model | L (ms) | " + " | ".join(cols) + " |")
    out.append("|---|---|" + "---|" * len(cols))
    for lab in labels:
        for l in lats:
            r = mean.loc[(lab, l)]
            out.append(f"| {lab} | {l:g} | " + " | ".join(f"{r[c]:.3f}" if abs(r[c]) < 1000 else f"{r[c]:.1f}"
                                                           for c in cols) + " |")
    out.append("")
    return out


def write_markdown(df: pd.DataFrame, path, params: dict | None = None) -> None:
    seeds = sorted(df["seed"].unique())
    settings = [c for c in ("on", "off") if c in set(df["cross_fills"])]
    out = ["# Sensitivity study: latency x queue model x cross-fill rule (synthetic market)", "",
           "## How to read this", "",
           "The default model has the cross-fill rule ON: when the market moves onto a resting order, the order",
           "fills once at our own price (DESIGN 3.5). On this synthetic market that rule creates most of the",
           "FIFO models' fills, and those fills have strongly negative spread capture (a stale quote picked off",
           "by a price move). The results with the rule ON are therefore dominated by cross-rule fills (see the",
           "cross-share tables), and that mass of fills hides the effect of latency: PnL barely moves with L.",
           "The latency effect is only visible with the rule OFF (ablation, Part 2), where markout and spread",
           "capture fall as L grows. Read the two parts together; neither alone is the whole picture. The",
           "magnitudes are properties of the synthetic market and do not transfer to a real venue, and a",
           "backtest is not live PnL.", "",
           "## Parameters", "",
           f"- seeds: {', '.join(str(s) for s in seeds)} (generator `--seed`, clean feed, no faults)",
           f"- feed length: {df['hours'].iloc[0]:g} h per seed, venue A, normalised by `mdtool normalize`",
           "- strategy: market maker, `MMParams` defaults (quote 1 unit, skew at 20 units, max 50 units, skew 1 tick)",
           "- fee: 0.1 bps, jitter 0; latency L is one way: entry = cancel = ack = L",
           "- cross fills: run with `--cross-fills on` and `--cross-fills off` (grid dimension)",
           f"- generator command: `python3 tools/gen_feeds.py --seed S --hours {df['hours'].iloc[0]:g} --no-verify`",
           "- cells show `mean [min, max]` over seeds",
           "- cross share = fills of kind CROSS / all fills (from `summary.csv`, equal to the CROSS count in `fills_detail.csv`)",
           ""]
    for n, c in enumerate(settings, 1):
        out += _setting_section(df[df["cross_fills"] == c], c, n)
    out += ["Attribution note: `pta` marks each fill against the market row at the fill's own timestamp. For a",
            "CROSS fill that row is already the moved market, so the loss of a stale quote shows up in spread",
            "capture (negative edge against the post-move mid), not in inventory PnL.", "",
            "The market maker's fills change its position, skew and pulls, so its results need not follow the",
            "queue-model ordering (DESIGN 3.6); the tables report what it actually does. The cancel-ahead",
            "fraction f is not identifiable from these data.", "",
            "## Statement", "", *("> " + ln for ln in STATEMENT.splitlines()), ""]
    Path(path).write_text("\n".join(out))


def write_chart(df: pd.DataFrame, path, cross: str = "on") -> None:
    df = df[df["cross_fills"] == cross]
    labels = [l for l in COLORS if l in set(df["label"])] + [l for l in dict.fromkeys(df["label"]) if l not in COLORS]
    lats = sorted(df["latency_ms"].unique())
    x = np.arange(len(lats))
    panels = [("total_pnl", "Total PnL (currency units)", True), ("markout_1s_bps", "1 s markout (bps)", True),
              ("fills", "Fill count (per run)", False), ("spread_capture", "Spread capture (currency units)", True)]
    fig, axes = plt.subplots(2, 2, figsize=(10, 7), sharex=True)
    n = len(labels)
    for ax, (metric, ylabel, zero) in zip(axes.ravel(), panels):
        a = _agg(df, metric).set_index(["label", "latency_ms"])
        for i, lab in enumerate(labels):
            off = (i - (n - 1) / 2) * 0.06  # small horizontal dodge so error bars do not overlap
            mean = np.array([a.loc[(lab, l), "mean"] for l in lats])
            lo = np.array([a.loc[(lab, l), "min"] for l in lats])
            hi = np.array([a.loc[(lab, l), "max"] for l in lats])
            ax.errorbar(x + off, mean, yerr=[mean - lo, hi - mean], color=COLORS.get(lab), marker="o", ms=4,
                        lw=1.6, capsize=2.5, elinewidth=0.9, label=lab)
        if zero:
            ax.axhline(0, color="grey", lw=0.7)
        ax.set_ylabel(ylabel, fontsize=9)
        ax.set_xticks(x, [f"{l:g}" for l in lats])
        ax.grid(axis="y", alpha=0.25)
        ax.tick_params(labelsize=8)
    for ax in axes[1]:
        ax.set_xlabel("one-way latency L (ms)", fontsize=9)
    h, l = axes[0, 0].get_legend_handles_labels()
    fig.legend(h, l, loc="lower center", ncol=len(labels), frameon=False, fontsize=9, title="queue model",
               title_fontsize=9)
    fig.suptitle(f"Latency x queue model, market maker, cross-fill rule {cross.upper()}: synthetic market, "
                 "not live PnL\npoints = mean over seeds, bars = min to max across seeds", fontsize=10.5)
    fig.tight_layout(rect=(0, 0.08, 1, 0.94))
    fig.savefig(path, dpi=130)
    plt.close(fig)


def compiler_info(bt_run: Path) -> str:
    cache = bt_run.parent / "CMakeCache.txt"
    parts = []
    try:
        txt = cache.read_text()
        cxx = [ln.split("=", 1)[1] for ln in txt.splitlines() if ln.startswith("CMAKE_CXX_COMPILER:")]
        bt = [ln.split("=", 1)[1] for ln in txt.splitlines() if ln.startswith("CMAKE_BUILD_TYPE:")]
        if cxx:
            v = subprocess.run([cxx[0], "--version"], capture_output=True, text=True).stdout.splitlines()
            parts.append(v[0] if v else cxx[0])
        if bt:
            parts.append(f"build type {bt[0]}")
    except OSError:
        pass
    return "; ".join(parts) or "unknown"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bt-run", default=str(ROOT / "build" / "bt_run"))
    ap.add_argument("--mdtool", default=str(ROOT / "build" / "md" / "mdtool"))
    ap.add_argument("--out", default=str(ROOT / "results"))
    ap.add_argument("--cache", default=str(ROOT / "build" / "sens_cache"))
    ap.add_argument("--gen-feeds", default=str(DEFAULT_GEN))
    ap.add_argument("--seeds", default="7,8,9")
    ap.add_argument("--hours", type=float, default=1.0)
    ap.add_argument("--latencies-ms", default="0,1,5,20")
    ap.add_argument("--models", default=DEFAULT_MODELS)
    ap.add_argument("--cross-fills", default="on,off", help="comma list of on/off")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--quick", action="store_true")
    a = ap.parse_args(argv)
    if a.quick:
        a.seeds, a.hours, a.latencies_ms, a.models = "7", 0.02, "0,5", "optimistic,fifo_back"
    seeds = [int(s) for s in a.seeds.split(",")]
    lats = [float(s) for s in a.latencies_ms.split(",")]
    models = parse_models(a.models)
    crosses = [c.strip() for c in a.cross_fills.split(",") if c.strip()]
    assert crosses and all(c in ("on", "off") for c in crosses), "--cross-fills takes on and/or off"
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    cache = Path(a.cache)
    runs_dir = cache / "runs"
    t0 = time.time()

    with ProcessPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        feeds = list(ex.map(_feed_job, [(s, a.hours, cache, Path(a.gen_feeds), Path(a.mdtool)) for s in seeds]))
        jobs = [(a.bt_run, feeds[i], s, a.hours, lat, m, f, cx, runs_dir)
                for i, s in enumerate(seeds) for cx in crosses for lat in lats for (m, f) in models]
        rows = list(ex.map(_job, jobs))

    df = pd.DataFrame(rows)[COLUMNS]
    df = df.sort_values(["seed", "cross_fills", "latency_ms", "label"], kind="stable").reset_index(drop=True)
    df.to_csv(out / "sensitivity.csv", index=False, float_format="%.10g", lineterminator="\n")
    write_markdown(df, out / "sensitivity_table.md")
    for c, name in (("on", "sensitivity.png"), ("off", "sensitivity_cross_off.png")):
        if c in crosses:
            write_chart(df, out / name, c)
    wall = time.time() - t0
    info = [f"date: {dt.datetime.now().astimezone().isoformat(timespec='seconds')}",
            f"bt_run: {a.bt_run} ({compiler_info(Path(a.bt_run))})",
            f"seeds: {seeds}", f"feed hours per seed: {a.hours:g}", f"latencies (ms): {lats}",
            f"models: {a.models}", f"cross fills: {crosses}", f"cells: {len(rows)}", f"jobs: {a.jobs}",
            f"total wall time (s): {wall:.1f}", "data: synthetic (Project 3 generator), not live PnL"]
    (out / "run_info.txt").write_text("\n".join(info) + "\n")

    pd.set_option("display.width", 200)
    mean = df.groupby(["cross_fills", "label", "latency_ms"])[["total_pnl", "spread_capture", "inventory_pnl", "markout_1s_bps",
                                                  "fills", "fills_per_min", "cancels_in_flight_filled", "cross_share"]].mean()
    print(mean.round(3).to_string())
    print(f"wall time {wall:.1f}s, {len(rows)} cells -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
