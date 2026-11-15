# Order book basics

## What it is
An order book is the list of prices at which people are waiting to buy (bids) and to sell (asks), with the quantity waiting at each price (a level). The best bid and best ask together are the top of book. The gap between them is the spread, and the halfway point is the mid. A snapshot (a full copy of the book) shows everything at one moment; a diff (an incremental update) lists only the levels that changed.

## Why it matters in a quant analyst job
Every price you analyse, such as a markout reference or a fill benchmark, comes from a book someone rebuilt. If the rebuild is wrong, every number built on it is wrong too, and nothing downstream will warn you.

## A worked example
The golden snapshot in `projects/03-market-data/tests/golden/venueA.jsonl` (synthetic data) has bids 100.00 x 5.000, 99.99 x 3.100, 99.98 x 0.900 and asks 100.01 x 2.100, 100.02 x 2.400, 100.03 x 2.600. Top of book: bid 100.00, ask 100.01. Spread = 100.01 - 100.00 = 0.01, one tick (tick size 0.01, `projects/03-market-data/tests/golden/meta.json`). Mid = (100.00 + 100.01) / 2 = 100.005, a half tick, which is why the export writes the mid with 3 decimals (D31 in `docs/decision-log.md`). A diff such as `"a":[["100.01","1.600"]]` replaces the quantity at 100.01 with the new absolute value, and a quantity of `"0.000"` deletes the level.

## Common mistake
Treating an odd book as real market information. A crossed book (best bid above best ask) or a locked book (best bid equal to best ask) cannot happen at one venue, so Project 3 treats either as corruption: the venue goes stale until the next snapshot (`docs/decision-log.md`, D30). Across two venues, a locked combined quote is legitimate, because one feed lags the other (D31).

## Interview angle
- Q: What is the difference between a snapshot and a diff feed? A: A snapshot is self-contained but large; diffs are small but only correct if none are missed.
- Q: Why is the mid not always a price you can trade at? A: It is the average of two prices and can sit between ticks.

## Where it lives in the code
- `projects/03-market-data/include/md/book.hpp`: `Book`, `best`, `crossed`, `locked`, `top_n`.
- `projects/03-market-data/include/md/session.hpp`: `VenueSession::top`.
- `projects/03-market-data/tests/golden/venueA.jsonl`.

## Explainer
[Project 3 explainer](https://claude.ai/artifact/FveVf5qjA11cXmogARD5jV)
