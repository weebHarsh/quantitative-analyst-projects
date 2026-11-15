# Fixed-point prices and quantities

## What it is
Fixed point means storing a price as a whole number of its smallest step (a tick) and a quantity as a whole number of its smallest unit (a lot). Here the tick is 0.01 and the lot is 0.001 (`projects/03-market-data/tests/golden/meta.json`).

## Why it matters in a quant analyst job
Prices are compared constantly: is the bid below the ask, did a level change, do two books match. Those checks must be exact. Floating-point (decimal-style) numbers cannot store most decimals exactly, so equality tests quietly fail.

## A worked example
Price 100.01 becomes 10001 ticks, and quantity 1.250 becomes 1250 lots (`fixed_parse_price_and_qty` in `projects/03-market-data/tests/test_fixed.cpp`). The parser reads the digits as integers and never converts through a float. In Python, `0.1 + 0.2` gives 0.30000000000000004, and `0.1 + 0.2 == 0.3` is False. Integers do not do this. The cost is overflow: whole numbers have a limit. The independent review (D43 in `docs/decision-log.md`) found signed overflow, which is undefined behaviour in C++, on extreme input values. The fix is saturating arithmetic: `sat_add` and `sat_sub` clamp at the limit instead of wrapping. The regression test `latency_sum_saturates_on_extreme_samples` (`projects/03-market-data/tests/test_review_regressions.cpp`) adds three extreme values and gets 9,223,372,036,854,775,807, the largest int64.

## Common mistake
Choosing a type without asking how large the input can get. The parsers accept anything that fits in int64, so sums of untrusted values need saturation (`include/md/saturate.hpp`).

## Interview angle
- Q: Why not use double for prices? A: Most decimals are inexact, so sums and equality checks drift.
- Q: What does saturating arithmetic do? A: It clamps at the maximum or minimum instead of wrapping around.

## Where it lives in the code
- `projects/03-market-data/include/md/fixed.hpp`: `parse_fixed`, `format_fixed`.
- `projects/03-market-data/include/md/saturate.hpp`: `sat_add`, `sat_sub`.
- `projects/03-market-data/tests/test_review_regressions.cpp`.

## Explainer
[Project 3 explainer](https://claude.ai/artifact/FveVf5qjA11cXmogARD5jV)
