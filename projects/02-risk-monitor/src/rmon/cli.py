"""Command line: ``rmon check-config | run | demo | serve``."""
from __future__ import annotations

import argparse
import sys
import tomllib
from pathlib import Path

from pta.data.loaders import load_fills, load_market, save_csv

from . import scenarios
from .alerts import write_alerts
from .engine import Engine
from .limits import DEFAULT_LIMITS_PATH, Limits, LimitsError, default_limits, load_limits
from .replay import replay
from .server import MonitorDriver, dumps_strict, serve


def _err(msg: str) -> None:
    print(f"rmon: {msg}", file=sys.stderr)


def _limits(path: str | None) -> Limits:
    return load_limits(path) if path else default_limits()


# ----------------------------------------------------------------- output
def _g(x: float) -> str:
    return f"{x:.4g}"


def print_alerts(alerts, start_us: int, phases=None, out=print) -> None:
    if not alerts:
        out("alerts: none")
        return
    rows = []
    for a in alerts:
        row = [f"{(a.ts_us - start_us) / 1e6:9.1f}", a.rule, a.severity,
               f"{_g(a.metric_value)} vs {_g(a.threshold)}"]
        if phases:
            row.append(scenarios.phase_at(phases, a.ts_us) or "-")
        rows.append(row)
    head = ["t (s)", "rule", "severity", "value vs threshold"] + (["phase"] if phases else [])
    widths = [max(len(head[i]), *(len(r[i]) for r in rows)) for i in range(len(head))]
    out("  ".join(h.ljust(w) if i else h.rjust(w) for i, (h, w) in enumerate(zip(head, widths))))
    for r in rows:
        out("  ".join(c.ljust(w) if i else c.rjust(w) for i, (c, w) in enumerate(zip(r, widths))))


def print_final(snap: dict, out=print) -> None:
    out(f"final PnL: total {snap['total_pnl']:.2f} = spread {snap['spread_capture']:.2f}"
        f" + stock moves {snap['inventory_pnl']:.2f} - fees {snap['fees']:.2f}")
    out(f"position {snap['position']:g}, max drawdown {snap['max_drawdown']:.2f}, "
        f"fills {snap['fills']}")


def print_phases(phases, start_us: int, out=print) -> None:
    out("phases (session seconds):")
    for p in phases:
        out(f"  {p.name:<14}{(p.start_us - start_us) / 1e6:9.1f} .. {(p.end_us - start_us) / 1e6:9.1f}")


def _process(market, fills, limits, out_dir: Path | None, phases=None):
    eng = Engine(limits)
    alerts = eng.run(replay(market, fills))
    snap = eng.snapshot()
    start = int(market["ts_us"].iloc[0])
    if phases:
        print_phases(phases, start)
    print_alerts(alerts, start, phases)
    print_final(snap)
    if out_dir is not None:
        write_alerts(alerts, out_dir / "alerts.jsonl")
        (out_dir / "snapshot.json").write_text(dumps_strict(snap, indent=2) + "\n")
    return alerts, snap


# --------------------------------------------------------------- commands
def cmd_check_config(args) -> int:
    path = args.limits or DEFAULT_LIMITS_PATH
    try:
        lim = _limits(args.limits)
    except (LimitsError, OSError, tomllib.TOMLDecodeError) as exc:
        _err(str(exc))
        return 2
    print(f"limits OK: {path}")
    p, l, f, m = lim.position, lim.loss_from_peak, lim.fill_rate, lim.markout
    print(f"  position        warn {_g(p.warn)}, breach {_g(p.breach)} (absolute)")
    print(f"  loss_from_peak  warn {_g(l.warn)}, breach {_g(l.breach)} (drawdown)")
    print(f"  fill_rate       {f.min_fills}..{f.max_fills} fills per {f.window_us / 1e6:g} s")
    print(f"  markout         rolling {m.horizon_us / 1e6:g} s markout floor {_g(m.floor_bps)} bps "
          f"over {m.window_us / 1e6:g} s")
    return 0


def cmd_run(args) -> int:
    try:
        lim = _limits(args.limits)
        market, fills = load_market(args.market), load_fills(args.fills)
        out_dir = Path(args.out) if args.out else None
        if out_dir:
            out_dir.mkdir(parents=True, exist_ok=True)
        _process(market, fills, lim, out_dir)
    except (LimitsError, OSError, ValueError, KeyError, tomllib.TOMLDecodeError) as exc:
        _err(str(exc))
        return 2
    return 0


def _scenario(args):
    fn = scenarios.clean if args.scenario == "clean" else scenarios.bad
    return fn(seed=args.seed, hours=args.hours)


def cmd_demo(args) -> int:
    out_dir = Path(args.out) if args.out else Path(f"rmon-demo-{args.scenario}")
    out_dir.mkdir(parents=True, exist_ok=True)
    sc = _scenario(args)
    save_csv(sc.market, out_dir / "market.csv")
    save_csv(sc.fills, out_dir / "fills.csv")
    print(f"scenario {args.scenario}: seed {args.seed}, {args.hours:g} h, "
          f"{len(sc.market)} quotes, {len(sc.fills)} fills")
    _process(sc.market, sc.fills, default_limits(), out_dir,
             phases=sc.phases if args.scenario == "bad" else None)
    print(f"wrote {out_dir}/ market.csv fills.csv alerts.jsonl snapshot.json")
    return 0


def cmd_serve(args) -> int:
    try:
        lim = _limits(args.limits)
        if args.scenario:
            sc = _scenario(args)
            market, fills = sc.market, sc.fills
            phases = sc.phases if args.scenario == "bad" else []
            label = f"{args.scenario} scenario (seed {args.seed})"
        elif args.market and args.fills:
            market, fills = load_market(args.market), load_fills(args.fills)
            phases, label = [], Path(args.market).name
        else:
            _err("serve needs --scenario or both --market and --fills")
            return 2
        driver = MonitorDriver(market, fills, lim, speed=args.speed, phases=phases, label=label)
    except (LimitsError, OSError, ValueError, KeyError, tomllib.TOMLDecodeError) as exc:
        _err(str(exc))
        return 2
    try:
        serve(driver, args.port)
    except OSError as exc:
        _err(f"cannot listen on port {args.port}: {exc}")
        return 2
    return 0


# ----------------------------------------------------------------- parser
def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="rmon", description="Live risk/strategy monitor")
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("check-config", help="validate a limits file")
    c.add_argument("--limits", help="limits TOML (default: config/limits.toml)")
    c.set_defaults(fn=cmd_check_config)

    r = sub.add_parser("run", help="batch-replay market and fills CSVs")
    r.add_argument("--market", required=True)
    r.add_argument("--fills", required=True)
    r.add_argument("--limits")
    r.add_argument("--out", help="write alerts.jsonl and snapshot.json here")
    r.set_defaults(fn=cmd_run)

    d = sub.add_parser("demo", help="generate a scenario and run it")
    d.add_argument("--scenario", choices=["clean", "bad"], required=True)
    d.add_argument("--seed", type=int, default=7)
    d.add_argument("--hours", type=float, default=2.0)
    d.add_argument("--out", help="output directory (default rmon-demo-<scenario>)")
    d.set_defaults(fn=cmd_demo)

    s = sub.add_parser("serve", help="live monitor on 127.0.0.1")
    s.add_argument("--scenario", choices=["clean", "bad"])
    s.add_argument("--seed", type=int, default=7)
    s.add_argument("--hours", type=float, default=2.0)
    s.add_argument("--market")
    s.add_argument("--fills")
    s.add_argument("--limits")
    s.add_argument("--speed", type=float, default=600.0,
                   help="session seconds per wall second; 0 or inf = instant")
    s.add_argument("--port", type=int, default=8765)
    s.set_defaults(fn=cmd_serve)
    return ap


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if args.cmd == "serve" and args.scenario and (args.market or args.fills):
        _err("use either --scenario or --market/--fills, not both")
        return 2
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
