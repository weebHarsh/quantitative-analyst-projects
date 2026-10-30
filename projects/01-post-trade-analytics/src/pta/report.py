"""Markdown report and charts from fills + market."""
from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

from . import markouts, pnl, risk  # noqa: E402
from .data.loaders import validate  # noqa: E402


def _hours(ts_us: np.ndarray, t0: int) -> np.ndarray:
    return (ts_us - t0) / 3.6e9


def _thin(n: int, target: int = 4000) -> slice:
    return slice(None, None, max(1, n // target))


def analyze(fills: pd.DataFrame, market: pd.DataFrame,
            horizons_us: tuple[int, ...] = markouts.DEFAULT_HORIZONS_US) -> dict:
    validate(fills, market)
    per_fill, totals = pnl.attribute(fills, market)
    curve = pnl.equity_curve(fills, market)
    mo = markouts.compute(fills, market, horizons_us)
    return {
        "per_fill": per_fill,
        "totals": totals,
        "curve": curve,
        "markouts": markouts.summarize(fills, mo),
        "risk": risk.summary(fills, curve, totals["total"]),
        "components": pnl.cumulative_components(fills, market),
    }


def _charts(res: dict, market: pd.DataFrame, out: Path) -> None:
    t0 = int(market["ts_us"].iloc[0])

    comp = res["components"]
    sl = _thin(len(comp))
    fig, ax = plt.subplots(figsize=(9, 4.5))
    x = _hours(comp["ts_us"].to_numpy(), t0)[sl]
    for col, label in (("spread_capture", "Spread capture"), ("inventory", "Inventory (price move)"),
                       ("fees", "Fees"), ("total", "Total")):
        ax.plot(x, comp[col].to_numpy()[sl], label=label, lw=2.2 if col == "total" else 1.3)
    ax.axhline(0, color="grey", lw=0.6)
    ax.set_xlabel("hours into session")
    ax.set_ylabel("cumulative PnL")
    ax.set_title("PnL attribution over the session")
    ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(out / "pnl_attribution.png", dpi=130)
    plt.close(fig)

    mk = res["markouts"]
    fig, ax = plt.subplots(figsize=(7.5, 4.2))
    h = mk["horizon_us"].to_numpy() / 1e6
    pos = np.arange(len(h))
    ax.plot(pos, mk["avg_bps"], marker="o", lw=2.2, label="All fills")
    ax.plot(pos, mk["avg_bps_buy"], marker=".", lw=1.0, ls="--", label="Buys")
    ax.plot(pos, mk["avg_bps_sell"], marker=".", lw=1.0, ls="--", label="Sells")
    ax.axhline(0, color="grey", lw=0.6)
    ax.set_xticks(pos, [f"{v:g}s" for v in h])
    ax.set_xlabel("horizon after fill")
    ax.set_ylabel("average markout (bps, qty-weighted)")
    ax.set_title("Markout curve")
    ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(out / "markouts.png", dpi=130)
    plt.close(fig)

    curve = res["curve"]
    sl = _thin(len(curve))
    x = _hours(curve["ts_us"].to_numpy(), t0)[sl]
    eq = curve["equity"].to_numpy()
    dd = np.maximum.accumulate(np.maximum(eq, 0)) - eq
    fig, axes = plt.subplots(3, 1, figsize=(9, 7), sharex=True)
    axes[0].plot(x, eq[sl], lw=1.2)
    axes[0].set_ylabel("equity")
    axes[1].plot(x, curve["position"].to_numpy()[sl], lw=0.8)
    axes[1].axhline(0, color="grey", lw=0.6)
    axes[1].set_ylabel("inventory")
    axes[2].fill_between(x, -dd[sl], 0, alpha=0.5)
    axes[2].set_ylabel("drawdown")
    axes[2].set_xlabel("hours into session")
    fig.suptitle("Equity, inventory and drawdown")
    fig.tight_layout()
    fig.savefig(out / "equity_inventory.png", dpi=130)
    plt.close(fig)


def _md_table(df: pd.DataFrame, fmt: dict[str, str]) -> str:
    cols = list(fmt)
    lines = ["| " + " | ".join(cols) + " |", "|" + "|".join(["---"] * len(cols)) + "|"]
    for _, r in df.iterrows():
        lines.append("| " + " | ".join(format(r[c], f) for c, f in fmt.items()) + " |")
    return "\n".join(lines)


def write_report(fills: pd.DataFrame, market: pd.DataFrame, out_dir: str | Path,
                 title: str = "Post-trade analytics report") -> Path:
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    res = analyze(fills, market)
    _charts(res, market, out)

    t, r, mk = res["totals"], res["risk"], res["markouts"].copy()
    mk["horizon"] = [f"{h / 1e6:g}s" for h in mk["horizon_us"]]
    mk["adverse_vs_0_bps"] = mk["avg_bps"] - mk["avg_bps"].iloc[0]
    dur_h = (market["ts_us"].iloc[-1] - market["ts_us"].iloc[0]) / 3.6e9

    md = f"""# {title}

Session: {dur_h:.1f} hours, {r['fills']:,} fills, {r['traded_qty']:,.0f} units traded,
notional {r['traded_notional']:,.0f}. Marked to the final mid of {t['final_mid']:.4f}.

## 1. Where did the PnL come from?

| Component | PnL |
|---|---|
| Spread capture (fill price vs mid at fill) | {t['spread_capture']:+,.2f} |
| Inventory (price move while holding) | {t['inventory']:+,.2f} |
| Fees | {-t['fees']:+,.2f} |
| **Total** | **{t['total']:+,.2f}** |

The three components sum to the mark-to-market equity at the end of the session
(checked independently in `tests/test_pnl_identity.py`). Total PnL is
{r['pnl_bps_of_notional']:+.3f} bps of traded notional.

![PnL attribution](pnl_attribution.png)

## 2. Was the edge real? Markouts

Average markout per fill, quantity-weighted, in bps of fill price. The 0s row is
the spread captured against mid; the decay after it is adverse selection.

{_md_table(mk, {'horizon': 's', 'n': ',d', 'avg_bps': '+.3f', 'avg_bps_buy': '+.3f',
                'avg_bps_sell': '+.3f', 'adverse_vs_0_bps': '+.3f', 'pct_positive': '.1%'})}

Fills near the end of the session have no markout at the longer horizons and are
excluded from those rows (hence the falling `n`). Long-horizon markouts mix in
whatever the market drifted by, so read them as inventory risk, not as edge.

![Markouts](markouts.png)

## 3. Risk and performance

| Metric | Value |
|---|---|
| Max drawdown (1-min bars) | {r['max_drawdown']:,.2f} |
| Bar PnL mean / std | {r['mean_bar_pnl']:+.4f} / {r['std_bar_pnl']:.4f} |
| Annualised Sharpe (assumes 390 bars/day, 252 days) | {r['sharpe_annualised']:.2f} |
| t-statistic of mean bar PnL | {r['sharpe_tstat']:.2f} |
| Annualised Sortino | {r['sortino_annualised']:.2f} |
| 1-min VaR 95% / ES 95% | {r['var95_bar']:.2f} / {r['es95_bar']:.2f} |
| 1-min VaR 99% / ES 99% | {r['var99_bar']:.2f} / {r['es99_bar']:.2f} |
| Average / max absolute inventory | {r['avg_abs_inventory']:.1f} / {r['max_abs_inventory']:.0f} |

Treat the annualised Sharpe with care: it scales a few hours of data up to a year.
The t-statistic is the honest signal of whether the mean is distinguishable from
zero; below roughly 2 it is not.

![Equity, inventory, drawdown](equity_inventory.png)
"""
    path = out / "report.md"
    path.write_text(md)
    return path
