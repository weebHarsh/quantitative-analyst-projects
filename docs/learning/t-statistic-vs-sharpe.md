# Why Sharpe on a few hours of data misleads

## What it is
The t-statistic (how many standard errors the mean is from zero) and the annualised Sharpe ratio (return per unit of risk, scaled to a year) are the same ratio mean/std, scaled by different factors.

## Why it matters in a quant analyst job
An annualised Sharpe built from a few hours looks impressive because the scaling factor is large, not because the evidence is strong. The t-statistic scales only by the amount of data you have, so it shows how little you know.

## A worked example
From `projects/01-post-trade-analytics/examples/sample_report.md` (synthetic data): 6 hours of 1-minute bars is 360 bars, mean bar PnL +0.0151, std 1.8043. Let r = mean/std = 0.0151 / 1.8043 = 0.00837.
- t = r * sqrt(360) = 0.00837 * 18.97 = 0.159, reported as 0.16.
- Sharpe_annualised = r * sqrt(390*252) = 0.00837 * 313.5 = 2.62.
I checked both in Python. The ratio between them is sqrt(98,280 / 360) = 16.5, so 0.16 * 16.5 = 2.62. Same data, two scalings: the Sharpe pretends 6 hours is a year, and a t-statistic below about 2 means the mean is not distinguishable from zero.

## Common mistake
Quoting the annualised Sharpe without the bar count or the t-statistic. The Sharpe also assumes 390 bars a day and 252 days, which do not match a 6-hour session.

## Interview angle
- Q: The backtest shows Sharpe 2.6. Do you believe it? A: Not until I know how many observations produced it; here t = 0.16 on 360 bars, so no.
- Q: How much data would you need for t = 2? A: t grows with sqrt(bars), so at the same mean and std I need (2 / 0.159)^2, about 158 times as many bars, roughly 57,000, assuming the mean holds.

## Where it lives in the code
- `summary` in `projects/01-post-trade-analytics/src/pta/risk.py`: keys `sharpe_annualised`, `sharpe_tstat`, `bars`; `bar_pnl` builds the bars.
- Defaults: `bar_us` = 60,000,000 and `bars_per_day` = 390.
- Drill context: `docs/interview-prep.md`.

## Explainer
[Project 1 explainer](https://claude.ai/artifact/9JtqoccdFaeneuCV5VFCgu)
