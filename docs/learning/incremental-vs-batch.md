# Incremental vs batch PnL

## What it is
Batch means recomputing from the whole history after the session; incremental (streaming, online) means updating running totals as each event arrives, at constant cost per event.

## Why it matters in a quant analyst job
A risk monitor has to answer "how are we doing right now" during the session, not after it. The catch is that a fast live number is worthless unless it agrees with the slow, trusted number computed afterwards.

## A worked example
`projects/02-risk-monitor/examples/demo_bad.txt` (synthetic data) replays 72,000 quotes and 4,851 fills. Its last line of PnL reads: total -114.81 = spread 57.87 + stock moves -162.92 - fees 9.76 (57.87 - 162.92 - 9.76 = -114.81). The monitor never reprocessed old fills to get this. It keeps position S1 = sum side*qty and S2 = sum side*qty*mid_at_fill, so inventory PnL is mid_now * S1 - S2, a single multiply and subtract. The Project 2 README reports an independent review on a fresh seed (31) matched Project 1's batch numbers on every one of 72,000 equity ticks.

## Common mistake
Trusting the streaming code because it looks right. The two versions differ in small ways: Project 1 marks inventory to the final mid, the monitor marks to the current mid. They agree at the end of the session, and the monitor's own check is spread_capture + inventory_pnl - fees == equity at every instant. Without a test against the batch result, such differences go unnoticed.

## Interview angle
- Q: How do you update a running mean or total in O(1)? A: Keep sufficient statistics (sums, counts) and adjust them per event; evict old entries from a deque for rolling windows.
- Q: How do you know the streaming result is correct? A: Replay a session through both paths and compare exactly, as `test_equivalence.py` does.

## Where it lives in the code
- `projects/02-risk-monitor/src/rmon/state.py`: `MonitorState.on_quote`, `on_fill`, `inventory_pnl`, `snapshot`.
- `projects/02-risk-monitor/src/rmon/replay.py`: `replay` orders events, quote first at equal timestamps.
- `projects/02-risk-monitor/tests/test_equivalence.py`.
- Batch side: `attribute` in `projects/01-post-trade-analytics/src/pta/pnl.py`.

## Explainer
[Project 2 explainer](https://claude.ai/artifact/KxGTJVjza6dBftFdujEp7T)
