"""Command line: pta simulate | analyze | report."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from .data.loaders import export_for_cpp, load_fills, load_market, save_csv
from .data.synthetic import MarketConfig, simulate_market
from .report import analyze, write_report
from .strategy.market_maker import MMConfig, simulate_market_maker


def _simulate(a: argparse.Namespace) -> None:
    market, flow = simulate_market(MarketConfig(seed=a.seed, hours=a.hours))
    fills = simulate_market_maker(market, flow, MMConfig(seed=a.seed + 4))
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    save_csv(market, out / "market.csv")
    save_csv(fills, out / "fills.csv")
    export_for_cpp(fills, market, out)
    print(f"wrote {len(market):,} market rows and {len(fills):,} fills to {out}")


def _analyze(a: argparse.Namespace) -> None:
    res = analyze(load_fills(a.fills), load_market(a.market))
    payload = {"attribution": res["totals"], "risk": res["risk"],
               "markouts": res["markouts"].to_dict(orient="records")}
    print(json.dumps(payload, indent=2, default=float))


def _report(a: argparse.Namespace) -> None:
    path = write_report(load_fills(a.fills), load_market(a.market), a.out)
    print(f"wrote {path}")


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="pta", description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("simulate", help="generate a synthetic market and market-maker fills")
    s.add_argument("--seed", type=int, default=7)
    s.add_argument("--hours", type=float, default=6.0)
    s.add_argument("--out", default="out")
    s.set_defaults(fn=_simulate)

    for name, fn, helptxt in (("analyze", _analyze, "print attribution, markouts and risk as JSON"),
                              ("report", _report, "write report.md and charts")):
        c = sub.add_parser(name, help=helptxt)
        c.add_argument("--fills", required=True)
        c.add_argument("--market", required=True)
        if name == "report":
            c.add_argument("--out", default="out")
        c.set_defaults(fn=fn)

    args = p.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    main()
