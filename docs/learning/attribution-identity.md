# PnL attribution identity

## What it is
A way of splitting profit into parts that must add back to the total (the PnL attribution identity): spread capture plus inventory, minus fees, equals mark-to-market PnL.

## Why it matters in a quant analyst job
A single PnL number does not tell a trader whether the strategy earns its spread or just got lucky on price moves. An attribution that reconciles to the total, to the cent, lets you trust each piece and find which one broke.

## A worked example
From `projects/01-post-trade-analytics/examples/sample_report.md` (synthetic data): spread capture +188.94, inventory -151.95, fees -31.58, total +5.42. Adding the three rounded components gives 5.41; the report's 5.42 is the unrounded sum. The spread was real money (+188.94), but holding stock while the price moved took most of it back (-151.95). For one fill with side s, quantity q, price px, mid at the fill m and final mid M: s*q*(M - px) = s*q*(m - px) + s*q*(M - m). The first term is spread capture, the second is inventory.

## Common mistake
Computing the parts and the total from the same formula, so they agree by construction. The check only means something if the total comes from an independent route. Here `equity_curve` computes cash + position*mid - fees without using the split, and the tests compare the two.

## Interview angle
- Q: Why split PnL at all? A: To tell skill (spread captured) from exposure (price moves while holding), which need different fixes.
- Q: How do you know your attribution is right? A: It reconciles to an independently computed equity curve, and a hand-worked test case matches.

## Where it lives in the code
- `projects/01-post-trade-analytics/src/pta/pnl.py`: `attribute` (per-fill parts and totals), `equity_curve` (the independent total), `mid_asof`.
- Tests: `projects/01-post-trade-analytics/tests/test_pnl_identity.py`.
- Live version: `MonitorState` in `projects/02-risk-monitor/src/rmon/state.py`.

## Explainer
[Project 1 explainer](https://claude.ai/artifact/9JtqoccdFaeneuCV5VFCgu)
