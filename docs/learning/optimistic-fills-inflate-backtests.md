# Optimistic fills inflate backtests

## What it is
If a simulator assumes your resting orders fill whenever the market trades at your price (OPTIMISTIC), it hands you fills that a real queue would have denied. More fills means more spread captured, so PnL looks better. The size of the gift is what Project 4's sensitivity study measures.

## Why it matters in a quant analyst job
A backtest is a model of execution, not a record of it. You must know which assumption produced the headline number, and show it moves.

## A worked example
`projects/04-backtester/results/sensitivity_table.md`, table 2.8 (cross-fill rule OFF, mean of 3 seeds, 0 ms): OPTIMISTIC total PnL +9.71 versus FIFO_BACK -11.31, with 12,025 versus 1,993 fills and a 1 s markout of +0.448 versus +0.406 bps. Same strategy, same feed; only the fill assumption changed.

The cross-fill rule (a resting order fills once at its own price when the market moves onto it) changes the picture. With it ON, table 1.5 shows about 76% of FIFO_BACK's fills (0.762) come from the rule, with spread capture near -29, and PnL barely moves with latency (-42.7 at 0 ms, -42.4 at 20 ms). With it OFF, FIFO_BACK's markout falls from +0.406 to +0.279 bps as latency rises to 20 ms. The rule masks the latency effect; see decision-log D45.

## Common mistake
Reading the default result as the truth, or calling it live edge. These numbers come from a synthetic market and do not transfer to a real venue. The direction (optimistic assumptions raise fills and PnL) is the finding, not the magnitudes. A backtest is not live PnL: it cannot reproduce real queues, other participants' reactions or real latency distributions.

## Interview angle
- Q: How would you test a fill model? A: Run the optimistic and pessimistic bounds and the ablation, and report the spread between them.
- Q: Does a good backtest prove edge? A: No; it needs a live track record.

## Where it lives in the code
- `projects/04-backtester/results/sensitivity.csv` and `sensitivity_table.md`; `python/sensitivity.py`.
- `cross_fills` in `projects/04-backtester/include/bt/backtest.hpp`; DESIGN.md sections 3.5 and 7.

## Explainer
[Project 4 explainer](https://claude.ai/artifact/6Tq5k8xBN7VtWiVUoMJMZC)
