# Gap detection and resync

## What it is
Every update from a venue carries a sequence number (a running count). If the next number is not the one you expect, something went missing: that is a gap. Recovery means marking the book stale (untrusted), buffering later updates, and rebuilding from the next full snapshot (a resync).

## Why it matters in a quant analyst job
A book with one missing update looks perfectly normal but is silently wrong. Analysis on top of it, such as spreads or fill quality, inherits the error with no warning. Refusing to publish a doubtful book is cheaper than explaining a bad number later.

## A worked example
In `session_buffered_straddle_a` (`projects/03-market-data/tests/test_session.cpp`), a snapshot at sequence 99 is followed by an update covering 101-103. The session expects 100, so it counts a gap, goes stale and buffers 101-103, 104-106 and 107. A snapshot at 104 arrives. Update 101-103 is dropped (at or before the snapshot), 104-106 and 107 are applied, and the session is live again at 107. On the 15-minute faulted feed (`projects/03-market-data/README.md`, D30 in `docs/decision-log.md`), each venue reports gaps 2, resyncs 3, duplicates_ignored 1, out_of_order 1 and crossed_detected 1, and the rebuilt book matched the venue's checkpoints with 0 mismatches.

## Common mistake
Applying a late or repeated message anyway. A duplicate (a sequence already applied) is ignored and counted. An out-of-order message (a lower sequence arriving after a higher one) takes the gap path: in a swap, the early arrival opens the gap and the late one is counted and buffered (`session_swap_is_out_of_order`).

## Interview angle
- Q: What do you do when you detect a gap? A: Stop publishing, buffer, and resync from a snapshot.
- Q: How do you know recovery worked? A: Compare the rebuilt book to the venue's checkpoint snapshots and watch the counters.

## Where it lives in the code
- `projects/03-market-data/include/md/session.hpp`: `VenueSession`, `VenueCounters`.
- `projects/03-market-data/src/session.cpp`.
- `projects/03-market-data/tests/test_session.cpp`.

## Explainer
[Project 3 explainer](https://claude.ai/artifact/FveVf5qjA11cXmogARD5jV)
