# Markouts and adverse selection

## What it is
A markout is the price move after your fill, measured from your side of the trade (a post-fill markout); adverse selection is the tendency for that move to go against you because the people who trade with you know something.

## Why it matters in a quant analyst job
Spread capture looks like profit at the moment of the fill. Markouts show how much of it survives once the market moves, which is the main test of whether a market maker's quotes are being picked off.

## A worked example
From `projects/01-post-trade-analytics/examples/sample_report.md` (synthetic data), quantity-weighted average in bps of fill price: 0s +0.598, 1s +0.371, 5s +0.111, 60s +0.021. The 0s value is the spread captured against mid. By 5s, 0.487 bps of it is gone (the report's adverse_vs_0_bps column). The share of fills with a positive markout falls from 100.0% at 0s to 50.5% at 5s, which is a coin flip. The formula is markout_h = side * (mid(ts + h) - px).

## Common mistake
Reading long horizons as edge or as adverse selection. The report says long-horizon markouts mix in whatever the market drifted by, so read them as inventory risk. At 60s the buys average +0.552 bps and the sells -0.510 bps, opposite signs that reflect where the market drifted while we held inventory, not a signal.

## Interview angle
- Q: You buy at 100.00, mid at the fill is 100.02, mid 1s later is 100.01. Markouts? A: 2 bps at 0s, 1 bp at 1s, so 1 bp of adverse selection (Q10 in `docs/interview-prep.md`).
- Q: Why is the 0s markout always positive for a passive fill? A: You bought below mid or sold above it; that gap is the half-spread you captured.

## Where it lives in the code
- `projects/01-post-trade-analytics/src/pta/markouts.py`: `compute` (per fill, per horizon) and `summarize` (averages, pct_positive).
- `mid_asof` in `projects/01-post-trade-analytics/src/pta/pnl.py` supplies the mid at ts + h.
- Tests: `projects/01-post-trade-analytics/tests/test_markouts.py`.
- C++ reference: `projects/01-post-trade-analytics/cpp/markout_kernel.cpp`.

## Explainer
[Project 1 explainer](https://claude.ai/artifact/9JtqoccdFaeneuCV5VFCgu)
