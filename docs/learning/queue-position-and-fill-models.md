# Queue position and fill models

## What it is
A resting order does not fill the moment its price trades, because other people's orders at that price sit ahead of it in a first-in-first-out line (FIFO queue). The "queue ahead" is the displayed size in front of your order. A simulator must guess how fast that line shrinks, and Project 4 offers three guesses: OPTIMISTIC (you are always first), FIFO_BACK (you join the back; only trades at your price move the line, plus whatever the numbers force) and FIFO_CANCEL_AHEAD(f) (a share f of unexplained size drops is assumed to be cancels ahead of you).

## Why it matters in a quant analyst job
Fill count and fill quality drive a market maker's backtest. If you assume you are first in line, you collect trades a real order would have waited for.

## A worked example
Hand-built test `s1_queue_trajectory_matches_spec_table` and `s1_fifo_back` in `projects/04-backtester/tests/test_exchange.cpp`: a bid at 100 shows 500 lots and we post 200. By the time of a 400-lot sell the level shows 600 (we are behind 500). That sell eats 400 of the 500 ahead, leaving 100 ahead. A later 150-lot sell clears those 100 and fills us 50. The last 150 fill only when the price trades through. OPTIMISTIC fills all 200 at the first sell. With f = 0.25 we fill 100 by then, with f = 0.5 we fill 150. A depth drop with no trade behind it is the guessing game: `queue_depth_spec_examples` in `tests/test_queue.cpp` shows q=500, 800 to 600 gives 500 (FIFO_BACK), 450 (f=0.25), 400 (f=0.5).

## Common mistake
Treating f as something to fit. In the synthetic data non-trade decreases at the touch (about 7.6k) outnumber trades (about 3.2k), yet trades are perfectly separable by construction (decision-log D42). The data cannot reveal which cancels were ahead of you, so f cannot be identified. Magnitudes here come from a synthetic market and do not transfer to a real venue.

## Interview angle
- Q: Why not fill when the price touches? A: Others are ahead; only volume beyond them reaches you.
- Q: Why is queue ahead capped at displayed size? A: It is forced arithmetic, not an assumption (D39).

## Where it lives in the code
- `projects/04-backtester/include/bt/queue.hpp` and `src/queue.cpp`: `queue_on_arrival`, `queue_on_trade`, `queue_on_depth`.
- `projects/04-backtester/DESIGN.md` sections 3.2 to 3.4.

## Explainer
Project 4 explainer: to be published (see docs/learning/README.md)
