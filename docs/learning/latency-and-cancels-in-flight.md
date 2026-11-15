# Latency and cancels in flight

## What it is
Messages take time. Order-entry latency is the delay before your order reaches the exchange, cancel latency the delay for a cancel, and ack latency the delay before the exchange's reply (fill, cancel, reject) reaches you. A cancel that is still travelling (cancel in flight) does not protect the order: a trade that arrives first still fills it, and until the cancel lands the quote you think you withdrew is a stale quote.

## Why it matters in a quant analyst job
Every pull of a quote has a window in which you can still be hit. A backtest with zero latency has no such window, so it overstates how safely a strategy steps away from the market.

## A worked example
`projects/04-backtester/results/sensitivity_table.md`, table 1.8 (cross-fill rule ON, mean over 3 seeds, FIFO_BACK): cancels that were in flight yet filled per run are 0 at 0 ms, 10.0 at 1 ms, 56.3 at 5 ms and 243.7 at 20 ms. With L = 0 it is exactly 0 by design (tested in `s4e_zero_latency_never_has_a_fill_in_cancel_flight`). Hand-built case `s3a_trade_between_cancel_decision_and_arrival_fills` in `tests/test_exchange.cpp` (5 ms each way): a 1000-lot bid arrives at 6 ms, the cancel is sent at 10 ms and arrives at 15 ms. A 300-lot trade at 12 ms fills it. The strategy hears the fill at 17 ms and the Canceled report (700 left) at 20 ms.

## Common mistake
Assuming the cancel worked the moment it was sent, or that the strategy knows a fill instantly. Orders and cancels share one channel, so a cancel never overtakes its order. A post-only order that would cross on arrival is rejected (WouldCross), tested in `s5_post_only_rejection_would_cross`.

## Interview angle
- Q: Can you cancel your way out of a fill? A: Not while the cancel travels; the report says Filled before Canceled, or CancelRejected(TooLateToCancel).
- Q: Why does a 1 ms feed delay matter? A: Quotes are stale for that long.

Counts come from a synthetic market and do not transfer to a real venue.

## Where it lives in the code
- `projects/04-backtester/include/bt/latency.hpp`: `LatencyModel::entry_arrival`, `cancel_arrival`, `report_arrival`.
- `projects/04-backtester/DESIGN.md` sections 2.3 to 2.5; `src/backtest.cpp`.

## Explainer
Project 4 explainer: to be published (see docs/learning/README.md)
