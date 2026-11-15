# Late-arriving markouts

## What it is
A fill's 1-second markout cannot be known at the fill, only a second later, so it sits in a waiting line (pending queue) until the quote that answers it arrives.

## Why it matters in a quant analyst job
A live monitor that watches adverse selection is always looking at the past: its markout number describes fills from at least one horizon ago. You need to know how stale the signal is, and what to do with fills that never resolve.

## A worked example
In `projects/02-risk-monitor/examples/demo_bad.txt` (synthetic data) the adverse phase starts at 1800.0 s and the markout_degradation breach fires at 1836.2 s with value -0.7666 against a floor of -0.75 bps. That is 36.2 s after the phase began. Two things contribute: each fill's 1s markout resolves a second after the fill, and the rule stays silent until it has at least 50 resolved samples (`min_samples` in `config/limits.toml`). I did not measure how the 36.2 s splits between them. In Project 1's `examples/sample_report.md`, fills near the session end have no markout at longer horizons: n falls from 15,895 at 0s to 15,852 at 60s. The monitor leaves those fills pending, which matches that exclusion.

## Common mistake
Filling a missing markout with zero, or measuring the window by fill time. Zero biases the average toward "fine". The streaming code windows by resolution time, so a long horizon reports what was learned recently instead of lagging by the horizon.

## Interview angle
- Q: Why not alert the moment a fill looks bad? A: You cannot know it is bad until the price has moved; at the fill only the 0s markout exists.
- Q: What do you do with fills that never resolve? A: Exclude them from that horizon and count them (`pending`), never impute a value.

## Where it lives in the code
- `projects/02-risk-monitor/src/rmon/markouts_stream.py`: `MarkoutTracker.on_fill`, `on_quote`, `advance`, `rolling_bps`, and the `pending` deque per horizon.
- `MarkoutRule` in `projects/02-risk-monitor/src/rmon/alerts.py`: `measure` returns nothing below `min_samples`.
- Batch rule: `compute` in `projects/01-post-trade-analytics/src/pta/markouts.py`.

## Explainer
[Project 2 explainer](https://claude.ai/artifact/KxGTJVjza6dBftFdujEp7T)
