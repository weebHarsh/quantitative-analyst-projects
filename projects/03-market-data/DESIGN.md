# Project 3 design: multi-venue market-data normalizer and L2 book rebuild (C++17)

Status: design frozen for implementation. Implement exactly what is written here. Where a
choice is left open it says "implementer's choice". The Python reference in
`tools/gen_feeds.py` (`normalize_a`, `normalize_b`, `RefSession`, `merge_quote`,
`export_rows`, `latency_summary`) implements sections 1-9 and produced every expected file
under `tests/golden/`; if this text and that code ever disagree, raise it, do not guess.

**Data honesty.** The sandbox cannot reach any venue, so both feeds are SYNTHETIC, written by
`tools/gen_feeds.py`. "Venue A" and "Venue B" are modelled on common conventions of
sequence-numbered diff feeds and chained-id delta feeds. They are not the format of any real
venue and must never be described as one (code comments, README, write-up).

Contents

1. Normalised event schema
2. Venue A raw format (JSON lines)
3. Venue B raw format (CSV)
4. Generator outputs: truth, faults manifest, expected files
5. Fault injection and expected handling
6. Sequencing, gap detection and resync (VenueSession)
7. Book invariants and checks
8. Cross-venue merge and latency statistics
9. Export to Project 1's market schema
10. Benchmark definition
11. Module layout, build, public API and mdtool CLI
12. Test plan

Units everywhere: price in **ticks** (int64, tick = 0.01), quantity in **lots** (int64,
lot = 0.001 units), time in **nanoseconds** since the Unix epoch (int64). No floating point
in parsers, book, session, merge or export. Floating point is allowed only in statistics
output (mean, events/s).

---

## 1. Normalised event schema

`include/md/types.hpp`:

```cpp
namespace md {

enum class Venue : std::uint8_t { A = 0, B = 1 };          // bit in masks: 1u << id
enum class Side  : std::uint8_t { None = 0, Bid = 1, Ask = 2 };
enum class EventType : std::uint8_t {
    SnapshotBegin = 0, SnapshotLevel = 1, SnapshotEnd = 2,
    Add = 3, Modify = 4, Set = 5, Delete = 6, Trade = 7
};
constexpr std::uint8_t kLastInMsg = 1;   // Event::flags bit 0
constexpr std::int64_t kNone = -1;       // absent sequence field

struct Event {
    std::int64_t price_ticks = 0;   // 0 when not applicable
    std::int64_t qty_lots    = 0;   // see table; 0 when not applicable
    std::int64_t exch_ts_ns  = 0;   // venue (exchange) time, converted to ns
    std::int64_t local_ts_ns = 0;   // local receive time (capture clock), ns
    std::int64_t seq_first   = kNone;
    std::int64_t seq_last    = kNone;
    std::int64_t prev_seq    = kNone;
    std::int64_t msg_index   = 0;   // 0-based raw line index (Venue B: header excluded)
    Venue venue = Venue::A;
    EventType type = EventType::Set;
    Side side = Side::None;
    std::uint8_t flags = 0;         // kLastInMsg on the last event of a raw message
};
}
```

A **raw message** is one line of a feed file (Venue A: one JSON line; Venue B: one CSV data
row). One raw message becomes one or more events, contiguous in the stream, sharing
`msg_index`, `exch_ts_ns`, `local_ts_ns` and the sequence fields. The last of them has
`flags & kLastInMsg`.

Semantics of each type. **All book quantities are absolute (the new resting quantity at that
price), never deltas.**

| type | side | price_ticks | qty_lots | book effect (section 6/7) |
|---|---|---|---|---|
| SnapshotBegin | None | 0 | number of SnapshotLevel events that follow | starts collecting a snapshot; book untouched |
| SnapshotLevel | Bid/Ask | level price | level qty (> 0) | appended to the pending snapshot |
| SnapshotEnd | None | 0 | 0 | the pending snapshot is complete: it **replaces the whole book** (clear, then load all levels) subject to section 6 |
| Add | Bid/Ask | price | qty > 0 | level must be absent: insert. If present: replace and count `add_on_existing` |
| Modify | Bid/Ask | price | qty > 0 | level must be present: replace qty. If absent: insert and count `modify_missing` |
| Set | Bid/Ask | price | qty > 0 | upsert (insert or replace); no expectation about prior state |
| Delete | Bid/Ask | price | 0 | remove the level. If absent: no-op and count `delete_missing` |
| Trade | Bid = buyer was aggressor, Ask = seller was aggressor | trade price | trade qty | **none**. Trades never change the book; the venue publishes the book change as a separate update right after |

Venue A has no add/modify distinction (absolute level quantities, qty 0 = delete), so it
produces `Set`/`Delete`. Venue B carries an explicit action, so it produces
`Add`/`Modify`/`Delete`. A snapshot clears the book only at `SnapshotEnd`, never at
`SnapshotBegin`.

Sequence fields per source:

| source | seq_first | seq_last | prev_seq |
|---|---|---|---|
| A snapshot (all 3 kinds) | `seq` | `seq` | -1 |
| A depth diff | `first_seq` | `last_seq` | -1 |
| A trade | `tid` | `tid` | -1 |
| B SB/SL/SE | `id` (snapshot id) | `id` | -1 |
| B U | `id` | `id` | `prev_id` |
| B T | `id` (trade id) | `id` | -1 |

Trade ids are their own namespace and are never used for gap detection.

Text form of an event (used by `mdtool normalize` and the golden files), header exactly:

```
venue,type,side,price_ticks,qty_lots,exch_ts_ns,local_ts_ns,seq_first,seq_last,prev_seq,msg_index,flags
```

`venue` is `A`/`B`; `type` is one of `SNAPSHOT_BEGIN SNAPSHOT_LEVEL SNAPSHOT_END ADD MODIFY
SET DELETE TRADE`; `side` is `NONE`/`BID`/`ASK`; all other fields decimal integers; `\n`
line endings; final line terminated.

---

## 2. Venue A raw format (JSON lines)

File `venueA.jsonl`. One JSON object per line, no whitespace anywhere, `\n` endings. Values
are only: non-negative decimal integers (no sign, no exponent, fit in int64), strings
without escapes, and for `b`/`a` an array of 2-element string arrays. Keys appear in the
order shown (the generator always writes this order), but **the parser must locate keys by
name and must reject**: unknown keys, missing keys, duplicate keys, any whitespace, nested
objects, or wrong value types.

Common keys:
- `r`: local receive time in **ns** (added by the capture process, not by the venue).
- `e`: message type string: `"snapshot"`, `"depth"` or `"trade"`.
- `E`: venue event time in **microseconds**; `exch_ts_ns = E * 1000`.

Decimal strings: price `"<digits>.<2 digits>"`, ticks = value x 100 exactly
(`"100.01"` -> 10001). Quantity `"<digits>.<3 digits>"`, lots = value x 1000 exactly
(`"1.250"` -> 1250, `"0.000"` -> 0). Exactly 2 and exactly 3 fraction digits; anything else
is a parse error. Parse with integer arithmetic only.

```
{"r":1700000000000487848,"e":"snapshot","E":1700000000000000,"seq":1000000,"b":[["100.00","5.000"],["99.99","3.100"]],"a":[["100.01","2.100"]]}
{"r":1700000000048691657,"e":"depth","E":1700000000047972,"first_seq":1000001,"last_seq":1000003,"b":[["99.99","0.000"]],"a":[["100.01","1.600"]]}
{"r":1700000000048691656,"e":"trade","E":1700000000047972,"tid":1,"px":"100.01","qty":"0.500","side":"buy"}
```

- **snapshot** keys `r,e,E,seq,b,a`. The full book as of sequence `seq` (every level; the
  generator keeps at most 20 levels per side, so "top-20" equals the full book). `b` is in
  descending price, `a` ascending; every qty > 0; either array may be empty. The first line
  of the file is a snapshot (initial image); later snapshots are periodic **checkpoints**
  (every 30 s of venue time), published in-stream right after the diff whose `last_seq ==
  seq`. Events: `SnapshotBegin(qty=len(b)+len(a))`, one `SnapshotLevel` per entry (all `b`
  then all `a`, in file order), `SnapshotEnd` with `kLastInMsg`.
- **depth** keys `r,e,E,first_seq,last_seq,b,a`. The book changes covering venue sequence
  numbers `first_seq..last_seq` inclusive (`first_seq <= last_seq`). Each entry is the
  level's final quantity as of `last_seq`; qty `"0.000"` deletes the level. A given
  (side, price) appears at most once per message. At least one entry in total (an empty
  diff is a parse error). Do **not** assume `last_seq - first_seq + 1` equals the number of
  entries (the generator happens to use that, but it is not part of the contract). Events:
  one per entry, all `b` then all `a` in file order, type `Set` if qty > 0 else `Delete`,
  `kLastInMsg` on the last. Consecutive diffs satisfy `first_seq = previous last_seq + 1`
  in a clean feed.
- **trade** keys `r,e,E,tid,px,qty,side`; `side` is `"buy"` (buyer aggressor -> `Side::Bid`)
  or `"sell"` (-> `Side::Ask`). One `Trade` event with `kLastInMsg`.

`r` is strictly increasing down the file. `E` is non-decreasing in a clean feed (not after
an out-of-order fault).

---

## 3. Venue B raw format (CSV)

File `venueB.csv`. First line is exactly the header:

```
recv_ns,kind,exch_ns,id,prev_id,action,side,px_e4,size
```

Then one data row per raw message: exactly 9 comma-separated fields, no quoting, no spaces,
`\n` endings. Empty fields are empty strings. Field use by `kind`:

| kind | id | prev_id | action | side | px_e4 | size | meaning |
|---|---|---|---|---|---|---|---|
| `SB` | snapshot id | empty | empty | empty | empty | level count n | snapshot begins; book as of update `id` |
| `SL` | snapshot id | empty | empty | `1`/`2` | price | qty > 0 | one snapshot level |
| `SE` | snapshot id | empty | empty | empty | empty | empty | snapshot ends |
| `U` | update id | previous update id | `N`/`C`/`D` | `1`/`2` | price | qty (0 for `D`) | one level update |
| `T` | trade id | empty | empty | `1`/`2` | price | qty | trade |

- `recv_ns`: local receive time, ns. `exch_ns`: venue event time in **nanoseconds** (venue
  clock; see section 8 on offset).
- `side`: `1` = bid (for `T`: buyer aggressor -> `Side::Bid`), `2` = ask (for `T`: seller
  aggressor -> `Side::Ask`).
- `px_e4`: price x 10^4 as an integer; must be divisible by 100; ticks = px_e4 / 100
  (`1000100` -> 10001). Not divisible -> parse error.
- `size`: integer lots (0.001 units).
- `action`: `N` new level -> `Add`, `C` change -> `Modify`, `D` delete -> `Delete`.
- Mapping: `SB` -> `SnapshotBegin(qty=n)`, `SL` -> `SnapshotLevel`, `SE` -> `SnapshotEnd`,
  `U` -> Add/Modify/Delete, `T` -> `Trade`. **Every row is its own raw message**, so every B
  event has `kLastInMsg`.
- A snapshot is `SB`, then exactly n `SL` rows (bids descending, then asks ascending), then
  `SE`, contiguous, all with the same `id`, `recv_ns` and `exch_ns`. First data row of the file
  is an `SB` (initial image); later snapshots are checkpoints every 20 s of venue time, offset
  10 s from step 0.
- Chain: each `U` row has `prev_id` equal to the `id` of the previous `U` row (or to the
  snapshot id for the first `U` after the initial snapshot). Ids are strictly increasing but
  **not contiguous** (they advance by 1-3). A snapshot's id is the id of the last `U` before it.
- Within one venue-side batch of changes the generator orders rows deletes first, then
  changes, then new levels, so the book is never crossed between rows of a clean feed.
- Parse errors: wrong field count, unknown kind/action/side, non-integer where an integer is
  required, non-empty field where the table says empty, missing required field, header
  mismatch (header mismatch is an I/O error, exit 3).

`recv_ns` is non-decreasing; equal values occur only within one snapshot (SB..SE).

---

## 4. Generator outputs

`python3 tools/gen_feeds.py --seed S --hours H --out DIR [--faults] [--expected] [--no-verify]`
and `--golden --out tests/golden`. Deterministic: same arguments -> byte-identical files.

Model: the touch of each venue follows Project 1's price path (`pta.data.synthetic.
simulate_market(MarketConfig(seed, hours))`, 100 ms grid, bid/ask rounded to ticks). Each
venue keeps an exact internal book (the oracle) of up to 20 levels per side within a 20-tick
window of its touch; per 100 ms step it moves the touch (deleting levels that would cross,
adding the new touch level), then applies Poisson(2.5) random add/modify/delete actions
near the touch (geometric distance) and with probability 0.35 a trade that consumes at the
touch (aggressor tilted by Project 1's flow signal), followed by the book update. The oracle
is asserted never crossed or locked. Venue B tracks the same reference touch but is
sometimes one tick wider per side, applies its touch move 5-40 ms into the step (A: 0.1-2 ms),
has its own sequence ids, latency (A: 300 us + Exp(200 us); B: 1.2 ms + Exp(600 us)) and an
exchange clock 300 us ahead of the local clock. Local receive time per message is
`max(previous + 1 ns, true time + latency)` (B snapshot rows share one receive time).

Files in DIR:

| file | content |
|---|---|
| `venueA.jsonl`, `venueB.csv` | the feeds (section 2, 3) |
| `truth_A.csv`, `truth_B.csv` | expected book at every venue snapshot, top 10 per side |
| `faults.json` | fault manifest and expected counters (always written; empty list without `--faults`) |
| `meta.json` | parameters, line counts, clock offsets, `"synthetic": true` |
| `expected_*` (only with `--expected` / `--golden`) | reference pipeline outputs, below |

Truth format, header `checkpoint,seq,msg_index,local_ts_ns,side,level,price_ticks,qty_lots`.
`checkpoint` counts snapshots from 0 (0 = initial image), `seq` is the snapshot's sequence
(A `seq`, B `id`), `msg_index` is the line index of the snapshot's first line (A: the
snapshot line; B: the `SB` row), `side` `BID`/`ASK`, `level` 0 = best. Rows are the oracle's
book at that sequence (identical to the snapshot content, truncated to 10 levels per side).
Note two B snapshots can carry the same id when no update happened between them; key truth
lookups by `msg_index`, not by `seq`.

`faults.json`:

```json
{"seed": 7, "faults_enabled": true,
 "faults": [{"kind": "drop|duplicate|swap|crossed", "venue": "A|B", "msg_index": 123,
             "clean_msg_index": 123, "first_seq": 1, "last_seq": 1,      // A
             "id": 5, "prev_id": 4,                                       // B
             "side": "BID", "price_ticks": 9999,                          // crossed only
             "note": "..."}],
 "expected_counters": {"A": {"gaps": 2, "resyncs": 3, "duplicates_ignored": 1,
     "out_of_order": 1, "crossed_detected": 1, "locked_detected": 0,
     "checkpoints_matched": 26, "checkpoints_mismatched": 0,
     "checkpoints_skipped_stale": 3, "snapshots_ignored_late": 0, "parse_errors": 0},
   "B": {...}},
 "msg_index_rule": "0-based line index in the written file; Venue B excludes the header"}
```

`msg_index` meaning per kind: drop = index of the message that now follows the hole;
duplicate = index of the copy (the original is at `msg_index - 1`); swap = the pair occupies
`msg_index` and `msg_index + 1` (clean order reversed); crossed = the corrupted message.
`first_seq/last_seq` or `id/prev_id` describe the affected clean message.

Expected files (golden and `--expected`), all produced by the Python reference pipeline on
the written (possibly faulted) feeds:
- `expected_events_A.csv`, `expected_events_B.csv`: `mdtool normalize` output (section 1 text form).
- `expected_counters.json` and the same values flat in `expected_counters.csv`
  (`scope,key,value`, scope `A`/`B`/`merge`; keys are the counter names of section 6,
  `final_state`, `latency_count, latency_sum_ns, latency_mean_ns, latency_p50_ns,
  latency_p99_ns, latency_min_ns, latency_max_ns`, and for `merge`: `merged_quotes,
  merged_crossed, merged_locked, export_rows`). `latency_mean_ns` is Python `round(x, 1)`;
  compare means with tolerance 0.05 or recompute from sum/count.
- `expected_merged.csv`: `mdtool merge` output (section 8).
- `expected_export.csv`: `mdtool export` output (section 9).

Golden fixture (`tests/golden/`, ~40 KB): 1.5 s of market, 3-level window, snapshots every
3 steps; A has 44 lines, B 87 rows (about half are snapshot rows). Faults per venue: one drop
(gap), one duplicate, one crossed (no swap; the swap is covered by the 15-minute feed).
Expected golden counters: A and B both `gaps=1, resyncs=2, duplicates_ignored=1,
out_of_order=0, crossed_detected=1, checkpoints_skipped_stale=2`; A `checkpoints_matched=2`,
B `checkpoints_matched=3`. Worked example (A, 0-based line indices): line 13 is diff 1000014; the drop removed
diff 1000015, so line 14 (`first_seq` 1000016) opens the gap; lines 21 and 22 are the same
diff 1000025 (22 is the copy, ignored); line 29 (diff 1000033) carries an injected bid at
99.99 against a best ask of 99.98, which crosses the book.

---

## 5. Fault injection (`--faults`)

Per venue, four faults, each placed in a different interval between two consecutive
snapshots (so each resolves at its own next snapshot and the expected counters add up),
always on update messages (A `depth`, B `U`), never on snapshots or trades:

| kind | what the file contains | correct handling | observable effect |
|---|---|---|---|
| drop | one update removed | next update fails continuity -> gap -> STALE, buffer, resync at next snapshot | `gaps+1, resyncs+1, checkpoints_skipped_stale+1` |
| duplicate | the update repeated immediately after the original (same body, later `r`/`recv_ns`) | ignored (its sequence is already applied) | `duplicates_ignored+1`; book unchanged; no stale period |
| swap | updates i and i+1 delivered as i+1, i (receive times stay increasing; exchange times go backwards or stay equal) | i+1 fails continuity -> gap -> STALE; i arrives with a lower sequence than one already received -> counted out-of-order, buffered; resync at next snapshot | `gaps+1, out_of_order+1, resyncs+1, checkpoints_skipped_stale+1` |
| crossed | one update corrupted: A gets an extra bid entry at (best ask after the message) + 1 tick, qty 700; B's row is replaced by `N` bid at (best ask before the row) + 1 tick, size 700. Sequence fields unchanged | apply (it is in sequence), then the post-message check sees bid > ask -> STALE (reason Crossed), buffer, resync at next snapshot | `crossed_detected+1, resyncs+1, checkpoints_skipped_stale+1`; not a gap |

Totals for a `--faults` feed per venue: `gaps=2, out_of_order=1, duplicates_ignored=1,
crossed_detected=1, locked_detected=0, resyncs=3, checkpoints_skipped_stale=3,
checkpoints_mismatched=0, checkpoints_matched = snapshots - 1 - 3`, final state LIVE. The
truth files are the clean oracle, so the corrupted level never appears in truth. While a
venue is stale its top of book is not published (section 8), and no checkpoint comparison
is made for it.

---

## 6. Sequencing, gap detection and resync (VenueSession)

State per venue session: `state in {INIT, LIVE, STALE}` (INIT = before the first
snapshot), `book`, `last` (last applied sequence: A `last_seq`, B `id`; -1 in INIT), `hi`
(highest update sequence received so far: A `last_seq`, B `id`; -1 initially), `buf`
(buffered update messages, each kept as its full list of events, keyed by A `(first_seq,
last_seq)` / B `(id)`), `pending` (snapshot being assembled).

Definitions for an update message m: `seq(m)` = A `last_seq`, B `id`.
`continuous(m)` = A: `m.first_seq <= last + 1 <= m.last_seq`; B: `m.prev_seq == last`.

Counters (`VenueCounters`, all int64, output in this order):
`messages, events, parse_errors, trades, snapshots, gaps, resyncs, duplicates_ignored,
out_of_order, crossed_detected, locked_detected, checkpoints_matched,
checkpoints_mismatched, checkpoints_skipped_stale, snapshots_ignored_late, snapshot_errors,
buffered_dropped, buffered_max, buffer_overflows, add_on_existing, modify_missing,
delete_missing`.

`messages` = raw lines handed to the session (parsed lines + parse errors); `events` =
events fed; `trades` = Trade events; `snapshots` = completed snapshots (valid SnapshotEnd).

```
on_event(e):
    events += 1
    if e.type in {SnapshotBegin, SnapshotLevel, SnapshotEnd}: snapshot_event(e)
    elif e.type == Trade: trades += 1                     # never buffered, never sequenced
    else:                                                 # Add/Modify/Set/Delete
        if pending: snapshot_errors += 1; pending = none  # update inside a snapshot aborts it
        if e starts a new message (msg_index differs from the current one):
            decision = classify(e)                        # once per message
        if decision == APPLY:  apply e to book (section 7 rules, anomaly counters)
                               if e is last in msg: last = seq(e); hi = max(hi, last); check_book()
        if decision == BUFFER: collect e; if e is last in msg: buffer(message); hi = max(hi, seq(e))
        if decision == DROP:   nothing
    if e.flags & kLastInMsg: messages += 1

classify(m):                                     # m = first event of an update message
    if last >= 0 and seq(m) <= last:             duplicates_ignored += 1; return DROP
    if state == LIVE:
        if continuous(m):                        return APPLY
        gaps += 1; state = STALE (reason Gap);   return BUFFER
    # INIT or STALE
    if key(m) in buf:                            duplicates_ignored += 1; return DROP
    if seq(m) < hi:                              out_of_order += 1          # then buffer it
    return BUFFER

buffer(message):
    if len(buf) >= cfg.max_buffered_messages: buf.clear(); buffer_overflows += 1
    buf.append(message); buffered_max = max(buffered_max, len(buf))

check_book():                                    # after each applied update message, and after a snapshot load
    if both sides non-empty and best_bid >  best_ask: crossed_detected += 1; state = STALE (reason Crossed)
    elif both sides non-empty and best_bid == best_ask: locked_detected += 1; state = STALE (reason Locked)
    if state == STALE: hi = max(hi, last)

snapshot_event(e):
    SnapshotBegin: if pending: snapshot_errors += 1
                   pending = {seq: e.seq_first, count: e.qty_lots, levels: [], begin_msg_index: e.msg_index}
    SnapshotLevel: if not pending or e.seq_first != pending.seq: snapshot_errors += 1; pending = none; return
                   pending.levels.append((e.side, e.price_ticks, e.qty_lots))
    SnapshotEnd:   p = pending; pending = none
                   if not p or e.seq_first != p.seq or len(p.levels) != p.count: snapshot_errors += 1; return
                   on_snapshot(p.seq, p.levels)

on_snapshot(S, levels):
    snapshots += 1
    call before_snapshot hook(session, S, begin_msg_index)   # book still holds the pre-snapshot state
    if state == LIVE:
        if S <  last: snapshots_ignored_late += 1; return
        if S == last: if book == image(levels): checkpoints_matched += 1
                      else:                     checkpoints_mismatched += 1
                      book.load(levels); check_book(); return     # adopt the venue image
        # S > last: updates went missing silently
        gaps += 1; checkpoints_skipped_stale += 1; resync(S, levels, counted=true); return
    if state == STALE: checkpoints_skipped_stale += 1; resync(S, levels, counted=true)
    else (INIT):       resync(S, levels, counted=false)

resync(S, levels, counted):
    book.load(levels); last = S
    sort buf ascending by key (A: (first_seq, last_seq); B: id), stable
    for each buffered message m in order:
        if seq(m) <= last: buffered_dropped += 1; continue          # at or before the snapshot
        if continuous(m): apply all its events; last = seq(m); continue
        # a hole remains after the snapshot
        gaps += 1; state = STALE; book.clear(); buf = [m and everything after it]; return
    buf.clear(); state = LIVE; hi = last
    if counted: resyncs += 1
    check_book()
```

Notes that follow from the pseudocode (tests rely on them):
- The initial snapshot (INIT -> LIVE) is not a resync and not a checkpoint.
- Every snapshot after the first increments exactly one of `checkpoints_matched`,
  `checkpoints_mismatched`, `checkpoints_skipped_stale`, `snapshots_ignored_late`.
- A crossed or locked book does not count as a gap. Recovery from it counts as a resync.
- In LIVE `hi == last`, so `out_of_order` is only ever counted while INIT/STALE.
- While not LIVE, `top()` reports not live and no top-of-book or merged update uses this venue.
- Trades are passed through in every state; they are not book state.
- With in-stream snapshots (as generated) every buffered message has `seq <= S`, so all are
  dropped; the "apply the rest" branch is exercised by a hand-built unit test (section 12).
- `parse_errors`: the caller reports a line that fails to parse with `note_parse_error()`;
  the line is skipped; a skipped update is then caught by continuity like a drop.

Final state is printed as `INIT`, `LIVE` or `STALE`.

---

## 7. Book invariants and checks

`Book` holds two sides of `Level{price_ticks, qty_lots}`. Invariants after every public call:
- bid prices strictly decreasing from best, ask prices strictly increasing from best (no
  duplicate prices);
- every stored qty > 0 (a delete removes the level; a zero qty is never stored; `set_level`
  with qty <= 0 is a programming error: assert in debug, treat as remove in release);
- the book itself may be crossed or locked (it stores what it is told); detection is the
  session's job via `crossed()` / `locked()`.

Definitions: `best(Bid)` = highest bid price level; `best(Ask)` = lowest ask.
`crossed()` = both sides non-empty and best bid price > best ask price.
`locked()` = both sides non-empty and best bid price == best ask price.
Action on detection: section 6 `check_book()` (count, mark STALE, recover at next snapshot).
A single venue's book is never legitimately locked or crossed, so both are treated as
corruption. Across venues, crossed/locked merged quotes are legitimate and only counted
(section 8).

Top-N view: `top_n(side, n)` returns the first `min(n, depth)` levels best-first (bids by
descending price, asks by ascending price). Checkpoint truth uses N = 10. Snapshot
comparison in section 6 compares the full book (every level), not just the top N.

Storage is implementer's choice; recommended: per side a `std::vector<Level>` kept sorted
with the best level at the back (bids ascending, asks descending) so touch updates are O(1)
amortised and lookups are a binary search; benchmark it against `std::map` and record the
numbers in the profiling notes.

---

## 8. Cross-venue merge and latency statistics

**Event ordering.** Both feeds are processed as one stream ordered by
`(local_ts_ns, venue id, msg_index)` of each raw message (A before B on a tie, then file
order). Only the local capture clock is used for ordering; exchange timestamps from different
venues are never compared.

**Per-venue top of book.** After each raw message is fully processed, the venue's
`TopOfBook{live, bid_px, bid_qty, ask_px, ask_qty}` is read from its session: `live` is
`state == LIVE`; an empty side has px = qty = 0; a non-live venue reports all zeros.

**Merged quote.** After each raw message, compute over venues with `live`:
- `bid_px` = max best bid price among live venues with a non-empty bid side; `bid_qty` = sum of
  those venues' best-bid qty **at that price**; `bid_venues` = bit mask of those venues
  (A = 1, B = 2). Ask symmetric with min. Equal prices therefore aggregate; there is no other
  tie-break.
- `live_venues` = mask of live venues. No live venue -> all fields 0.
- Emit a `MergedQuote` (with `local_ts_ns` of the message just processed) only if any field
  other than the timestamp differs from the last emitted one (initial "last" is all zeros).

`mdtool merge` text output, header exactly
`local_ts_ns,bid_ticks,bid_lots,bid_venues,ask_ticks,ask_lots,ask_venues,live_venues`.

Summary counts: `merged_quotes` (rows emitted), `merged_crossed` (emitted rows with both
sides present and bid > ask), `merged_locked` (bid == ask). These happen legitimately
because venue B lags venue A; they are reported, not suppressed (the export filters them).

**Latency statistics (per venue).** One sample per successfully parsed raw line:
`local_ts_ns - exch_ts_ns` (int64, may be negative). Report `count, sum_ns, mean_ns
(double, sum/count, printed %.1f), p50_ns, p99_ns, min_ns, max_ns`. Percentiles are
nearest-rank on the sorted samples: `p(q) = s[max(ceil(q*n/100) - 1, 0)]`, computed in
integers as `s[max((q*n + 99) / 100 - 1, 0)]` for q in {50, 99}. Count 0 -> all zeros.

**Clock offset.** Not corrected. The statistic is "apparent latency" = true one-way
latency + (local clock - venue clock). The generator sets venue A's clock equal to the
local clock and venue B's clock 300 us ahead (recorded in `meta.json` as
`clock_offset_ns`), so B's apparent latency is about 300 us lower than its true latency.
The write-up must state this, and must not compare A and B latencies as if they were on one
clock. Merge ordering is unaffected because it uses local time only.

---

## 9. Export to Project 1's market schema

Input: the merged quote stream (or a single venue via `--venue A|B`, which is the same
pipeline with the other feed empty). Output CSV, header exactly `ts_us,bid,ask,mid`.

1. `ts_us = local_ts_ns / 1000` (integer division; timestamps are positive).
2. Collapse: within one `ts_us`, keep only the **last** merged quote (the state at the end of
   that microsecond).
3. Filter: keep the row only if `live_venues != 0`, `bid_venues != 0`, `ask_venues != 0` and
   `bid_px < ask_px` (both sides present, at least one venue not stale, not crossed or
   locked). Collapse happens before the filter.
4. Format with integers only: `bid` and `ask` as ticks/100 with exactly 2 decimals
   (`9997` -> `99.97`); `mid` = (bid_ticks + ask_ticks) / 2 with exactly **3** decimals,
   computed as `s = (bid + ask) * 5` in units of 0.001 -> `s/1000 "." s%1000 (3 digits)`
   (`9997+9998` -> `99.975`). Three decimals are needed because the mid can be a half tick;
   this is exact and `pta.data.loaders.load_market` reads it.

`ts_us` is strictly increasing by construction. Project 1 loads it with `load_market`.

---

## 10. Benchmark definition

Binary `md_bench` (and `mdtool bench`, same code `md::run_bench`):
`md_bench --a venueA.jsonl --b venueB.csv [--reps 5] [--warmup 1]`.

Both feeds are read fully into memory first (not timed). Each phase runs `warmup + reps`
times; warm-up runs are discarded. Clock: `std::chrono::steady_clock`.

| phase | what is timed | reported |
|---|---|---|
| parse | parsing every line of both feeds into one pre-reserved `std::vector<Event>` (cleared each rep) | `parse_events_per_s` = events / seconds, median over timed reps |
| apply | fresh `VenueSession` per venue per rep; feeding all pre-parsed events in merged order (section 8 ordering), no merge | `apply_events_per_s`, `apply_msgs_per_s`, median |
| latency | separate pass, same as apply, but each **update message** (first event Add/Modify/Set/Delete) is timed alone: `t0 = now()` before its first event, `t1 = now()` after its last; sample = t1 - t0 ns. Other messages are fed but not timed | `update_latency_p50_ns`, `update_latency_p99_ns`, `update_latency_p999_ns` (nearest rank, pooled over timed reps), `latency_samples` |
| end-to-end | `run_merged` (parse + sessions + merge) with a sink that only counts | `e2e_msgs_per_s`, `e2e_events_per_s`, median |

Also report `timer_overhead_ns_p50` (median of 1,000,000 back-to-back `now()` pairs; reported,
not subtracted), `messages`, `events`, `reps`, `warmup`, and `build` (a compile-time string
with compiler id/version and flags). After every rep the session counters must equal those of
the first rep, else abort with exit 6. Output: one JSON line on stdout, keys in the order
listed here.

Single command that reproduces it: `bench/run_bench.sh`. It must:
1. `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMD_SANITIZE=OFF && cmake --build build-release -j`
2. Generate (once, cached by `meta.json` content) `python3 tools/gen_feeds.py --seed 7
   --hours 3.8 --no-verify --out "${MD_BENCH_DIR:-/tmp/md_bench_feeds}"` (about 503k A lines,
   580k B rows, 109 MB, about 15 s).
3. `build-release/md_bench --a .../venueA.jsonl --b .../venueB.csv --reps 5 --warmup 1`.
The feed is never committed.

---

## 11. Module layout, build, public API and CLI

```
projects/03-market-data/
  CMakeLists.txt
  DESIGN.md
  include/md/types.hpp     Venue, Side, EventType, Event, Level, TopOfBook, MergedQuote, name helpers
  include/md/fixed.hpp     integer/decimal parsing and formatting
  include/md/io.hpp        read_lines, Feed, load_feed, event CSV writer, counters JSON writer
  include/md/parser_a.hpp  ParserA
  include/md/parser_b.hpp  ParserB, split_csv
  include/md/book.hpp      Book
  include/md/session.hpp   VenueSession, VenueCounters, SessionConfig, SessionState
  include/md/merge.hpp     Merger, run_merged, MergeSummary
  include/md/export.hpp    QuoteExporter
  include/md/stats.hpp     LatencyStats, LatencySummary
  include/md/bench.hpp     BenchConfig, BenchResult, run_bench, to_json
  src/fixed.cpp io.cpp parser_a.cpp parser_b.cpp book.cpp session.cpp merge.cpp export.cpp stats.cpp bench.cpp
  src/mdtool.cpp           main() of mdtool
  tests/harness.hpp        minimal TEST/CHECK/CHECK_EQ registry (no gtest)
  tests/test_main.cpp tests/test_fixed.cpp tests/test_parsers.cpp tests/test_book.cpp
  tests/test_session.cpp tests/test_merge_export.cpp tests/test_golden.cpp
  tests/golden/            committed fixtures (section 4)
  bench/md_bench.cpp bench/run_bench.sh
  tools/gen_feeds.py
  python/test_gen_feeds.py python/test_cpp_integration.py python/test_pta_integration.py
```

**CMake** (`cmake_minimum_required(VERSION 3.20)`, `project(md LANGUAGES CXX)`):
`CMAKE_CXX_STANDARD 17`, required, no extensions; default build type Release when unset.
Options: `MD_WERROR` (default ON, adds `-Werror`), `MD_SANITIZE` (default OFF, adds
`-fsanitize=address,undefined -fno-omit-frame-pointer` to compile and link of all targets).
Warnings on every target: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.
Targets: `mdlib` (STATIC, all `src/*.cpp` except `mdtool.cpp`, public include dir `include/`),
`mdtool` (`src/mdtool.cpp`), `md_tests` (`tests/*.cpp`, compile definition
`MD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"`), `md_bench` (`bench/md_bench.cpp`), all linking
`mdlib`. `enable_testing()` and `add_test(NAME md_tests COMMAND md_tests)`. No external
dependencies, no JSON library, no gtest. Must build with zero warnings on g++ 13.

**Public API** (namespace `md`; signatures are binding, private members are implementer's choice):

```cpp
// types.hpp (in addition to section 1)
struct Level { std::int64_t price_ticks = 0; std::int64_t qty_lots = 0; };
bool operator==(const Level&, const Level&);
struct TopOfBook {
    bool live = false;
    std::int64_t bid_px = 0, bid_qty = 0, ask_px = 0, ask_qty = 0;
};
bool operator==(const TopOfBook&, const TopOfBook&);
struct MergedQuote {
    std::int64_t local_ts_ns = 0;
    std::int64_t bid_px = 0, bid_qty = 0;
    std::int64_t ask_px = 0, ask_qty = 0;
    std::uint8_t bid_venues = 0, ask_venues = 0, live_venues = 0;
};
bool same_quote(const MergedQuote&, const MergedQuote&);   // all fields except local_ts_ns
const char* to_string(Venue); const char* to_string(Side); const char* to_string(EventType);
bool parse_venue(std::string_view s, Venue& out);           // "A" / "B"

// fixed.hpp
bool parse_uint(std::string_view s, std::int64_t& out);     // [0-9]+, overflow -> false
bool parse_fixed(std::string_view s, int frac_digits, std::int64_t& out); // exact frac digits
std::string format_fixed(std::int64_t v, int frac_digits);  // v >= 0, e.g. (9997,2) -> "99.97"

// io.hpp
bool read_lines(const std::string& path, std::vector<std::string>& lines); // strips \n and \r
struct Feed { Venue venue = Venue::A; std::vector<std::string> lines; };   // B: header removed
bool load_feed(const std::string& path, Venue v, Feed& out, std::string& err); // B checks header
void write_event_csv_header(std::ostream& os);
void write_event_csv(std::ostream& os, const Event& e);
void write_counters_json(std::ostream& os, Venue v, const VenueCounters& c,
                         SessionState s, const LatencySummary& lat);   // one line, no newline

// parser_a.hpp / parser_b.hpp
enum class ParseStatus : std::uint8_t { Ok, Error };
class ParserA {
public:
    // Appends this line's events to out; on Error out is left exactly as it was.
    ParseStatus parse(std::string_view line, std::int64_t msg_index, std::vector<Event>& out);
    const std::string& last_error() const;
};
constexpr std::string_view kVenueBHeader = "recv_ns,kind,exch_ns,id,prev_id,action,side,px_e4,size";
std::size_t split_csv(std::string_view line, std::string_view* fields, std::size_t max_fields); // returns count
class ParserB {
public:
    ParseStatus parse(std::string_view line, std::int64_t msg_index, std::vector<Event>& out);
    const std::string& last_error() const;
};

// book.hpp
enum class ApplyResult : std::uint8_t { Inserted, Replaced, Removed, Missing };
class Book {
public:
    void clear();
    ApplyResult set_level(Side s, std::int64_t px, std::int64_t qty); // qty > 0
    ApplyResult remove_level(Side s, std::int64_t px);                // Missing if absent
    void load(const std::vector<std::pair<Side, Level>>& levels);     // clear + insert all
    std::int64_t qty_at(Side s, std::int64_t px) const;                // 0 if absent
    std::size_t depth(Side s) const;
    bool empty(Side s) const;
    Level best(Side s) const;                                          // precondition !empty(s)
    void top_n(Side s, std::size_t n, std::vector<Level>& out) const; // clears out, best first
    bool crossed() const;
    bool locked() const;
    bool invariants_ok() const;
};
bool operator==(const Book&, const Book&);                             // same levels both sides

// session.hpp
enum class SessionState : std::uint8_t { Init, Live, Stale };
enum class StaleReason : std::uint8_t { None, Gap, Crossed, Locked };
const char* to_string(SessionState);
struct VenueCounters { std::int64_t messages = 0, events = 0, parse_errors = 0, trades = 0,
    snapshots = 0, gaps = 0, resyncs = 0, duplicates_ignored = 0, out_of_order = 0,
    crossed_detected = 0, locked_detected = 0, checkpoints_matched = 0,
    checkpoints_mismatched = 0, checkpoints_skipped_stale = 0, snapshots_ignored_late = 0,
    snapshot_errors = 0, buffered_dropped = 0, buffered_max = 0, buffer_overflows = 0,
    add_on_existing = 0, modify_missing = 0, delete_missing = 0; };
bool operator==(const VenueCounters&, const VenueCounters&);
struct SessionConfig { std::size_t max_buffered_messages = 1000000; };
class VenueSession {
public:
    explicit VenueSession(Venue v, SessionConfig cfg = {});
    bool on_event(const Event& e);           // true when e has kLastInMsg
    void note_parse_error();                 // parse_errors += 1, messages += 1
    SessionState state() const;
    StaleReason stale_reason() const;
    std::int64_t last_applied() const;       // -1 in Init
    const Book& book() const;
    const VenueCounters& counters() const;
    TopOfBook top() const;                   // zeros and live=false unless Live
    using SnapshotHook = std::function<void(const VenueSession&, std::int64_t snapshot_seq,
                                            std::int64_t begin_msg_index)>;
    void set_before_snapshot_hook(SnapshotHook h);
};

// stats.hpp
struct LatencySummary { std::int64_t count = 0, sum_ns = 0, p50_ns = 0, p99_ns = 0,
                        min_ns = 0, max_ns = 0; double mean_ns = 0.0; };
std::int64_t nearest_rank(const std::vector<std::int64_t>& sorted, int pct);
class LatencyStats {
public:
    void reserve(std::size_t n);
    void add(std::int64_t ns);
    LatencySummary summary() const;          // sorts a copy
};

// merge.hpp
class Merger {
public:
    // Record venue v's top after a message at local_ts_ns. Returns true if the merged quote
    // changed (then current() holds it with local_ts_ns set).
    bool update(Venue v, const TopOfBook& t, std::int64_t local_ts_ns);
    const MergedQuote& current() const;
};
struct MergeSummary {
    VenueCounters counters[2]; SessionState final_state[2]; LatencySummary latency[2];
    std::int64_t merged_quotes = 0, merged_crossed = 0, merged_locked = 0;
};
// Parses both feeds, runs one VenueSession per venue in section 8 order, calls sink for
// every emitted merged quote. An empty Feed is allowed (single-venue run).
MergeSummary run_merged(const Feed& a, const Feed& b,
                        const std::function<void(const MergedQuote&)>& sink);

// export.hpp
class QuoteExporter {
public:
    explicit QuoteExporter(std::ostream& os);   // writes the header
    void on_quote(const MergedQuote& q);       // section 9 collapse + filter
    void finish();                             // flushes the pending microsecond
    std::int64_t rows() const;
};

// bench.hpp
struct BenchConfig { int reps = 5; int warmup = 1; };
struct BenchResult { std::int64_t messages = 0, events = 0, latency_samples = 0;
    double parse_events_per_s = 0, apply_events_per_s = 0, apply_msgs_per_s = 0,
           e2e_msgs_per_s = 0, e2e_events_per_s = 0;
    std::int64_t update_latency_p50_ns = 0, update_latency_p99_ns = 0,
                 update_latency_p999_ns = 0, timer_overhead_ns_p50 = 0;
    int reps = 0, warmup = 0; std::string build; };
BenchResult run_bench(const Feed& a, const Feed& b, const BenchConfig& cfg);
std::string to_json(const BenchResult& r);
}
```

`p999` uses `nearest_rank` with permille: index `max((999*n + 999)/1000 - 1, 0)`.

Counters JSON (`write_counters_json`), one line, keys in this order: `"venue"`, the 22
counters of section 6 in order, `"final_state"` (string), then `"latency":{"count","sum_ns",
"mean_ns","p50_ns","p99_ns","min_ns","max_ns"}`. Integers plain; `mean_ns` `%.1f`.

**mdtool CLI.** Exit codes: 0 ok; 2 usage error; 3 I/O error (missing file, bad B header);
4 the input had parse errors (output is still complete); 5 checkpoint or truth mismatch.
Unknown flags -> 2. Paths are required unless marked optional; `-` is not special.

| subcommand | arguments | output |
|---|---|---|
| `normalize` | `--venue A\|B --in PATH [--out PATH]` | event CSV (section 1) to `--out` or stdout. Lines that fail to parse are skipped and reported on stderr as `parse error line <msg_index>: <reason>` |
| `replay` | `--venue A\|B --in PATH [--truth PATH] [--tob PATH]` | stdout: counters JSON line (above), with `,"truth_checked":N,"truth_mismatched":M` inserted before `"final_state"` when `--truth` is given. Truth check: in the before-snapshot hook, if state is Live and `last_applied == snapshot_seq`, compare `top_n(.,10)` of both sides with the truth rows whose `msg_index == begin_msg_index`. `--tob` writes `local_ts_ns,live,bid_ticks,bid_lots,ask_ticks,ask_lots` whenever the venue top changes (after each message) |
| `merge` | `--a PATH --b PATH [--out PATH]` | merged CSV (section 8) to `--out` or stdout; stderr: `{"A":<counters json>,"B":<counters json>,"merged_quotes":..,"merged_crossed":..,"merged_locked":..}` |
| `export` | `--a PATH --b PATH --out PATH [--venue merged\|A\|B]` | Project 1 CSV (section 9) to `--out`; stdout: `{"rows":..,"merged_quotes":..,"merged_crossed":..,"merged_locked":..}`. With `--venue A` only `--a` is required (and B only `--b`) |
| `bench` | `--a PATH --b PATH [--reps N] [--warmup N]` | same as `md_bench` |

Determinism: given the same files every subcommand except `bench` produces byte-identical output.

---

## 12. Test plan

C++ (`md_tests`, registered with CTest, also run under `-DMD_SANITIZE=ON`):
1. **fixed**: `parse_fixed` accepts `"100.01"`(2) -> 10001, `"0.000"`(3) -> 0; rejects `"100.1"`,
   `"100.011"`, `"-1.00"`, `"1e2"`, `""`, `"."`, overflow; `format_fixed` round-trips.
2. **parsers**: one hand-written line per message type per venue -> exact events (all fields);
   each parse-error rule of sections 2 and 3 has a negative case; on error the output vector
   is unchanged.
3. **book**: insert/replace/remove, `Missing` delete, ordering after random operations
   (compare against `std::map` model over 10k seeded ops, `invariants_ok()` after each),
   `top_n` with fewer than N levels, `crossed`/`locked`.
4. **session, hand-built event sequences** (A and B variants each):
   in-sync apply; duplicate in LIVE; drop -> gap, STALE, top not live, resync at snapshot,
   counters; swap -> `gaps=1,out_of_order=1`; crossed update -> `crossed_detected=1`, STALE,
   recovery; locked update -> `locked_detected=1`; snapshot with `S < last` (ignored late),
   `S > last` in LIVE (gap+resync); **buffered straddle**: STALE with buffered A diffs
   (101-103), (104-106), (107-107), snapshot S=104 -> first dropped, (104-106) applied because
   `104 <= 105 <= 106`, (107) applied, LIVE, `last=107`, `buffered_dropped=1`; B variant with
   ids; hole after snapshot -> stays STALE and `gaps` increments; buffer overflow with
   `max_buffered_messages=2`; aborted snapshot (update between SB and SE) -> `snapshot_errors`;
   Add on existing / Modify missing / Delete missing counters.
5. **golden** (reads `MD_SOURCE_DIR/tests/golden`): normalize both feeds and compare with
   `expected_events_*.csv` byte for byte; replay each venue and compare every counter, final
   state and latency field with `expected_counters.csv`; with the truth hook, every compared
   checkpoint equals `truth_*.csv` (`truth_mismatched == 0`, `truth_checked ==
   checkpoints_matched`); `run_merged` output equals `expected_merged.csv`; exporter output
   equals `expected_export.csv`; merge summary equals the `merge,*` rows.
6. **merge/export units**: equal best prices aggregate qty and set both mask bits; stale venue
   excluded; no-change messages emit nothing; tie order A before B at equal local time;
   export collapses several quotes in one microsecond to the last; a microsecond whose last
   quote is crossed exports nothing even if an earlier quote in it was fine; output
   `ts_us` strictly increasing; formatting of half-tick mids.
7. **stats**: nearest-rank on n = 1, 2, 100, 101 against hand values.

Python (`python3 -m pytest -q -p no:cacheprovider python`; integration tests locate the build
via env `MD_BUILD_DIR`, default `build`, and skip with a clear message if `mdtool` is absent):
8. `test_gen_feeds.py` (exists, 24 tests): determinism by seed; truth equals an independent
   reapplication of the clean feeds; trades at the touch; timestamps; same underlying market;
   independent sequencer reproduces `faults.json` expected counters on clean, faulted and
   golden feeds; manifest matches the files; golden fixtures reproducible, independently
   normalised, merged and exported; golden < 50 KB.
9. `test_cpp_integration.py`: generate `--seed 7 --hours 0.25 --faults --expected` into a tmp
   dir; `mdtool replay --truth` per venue: every key in `faults.json` `expected_counters`
   matches, `truth_mismatched == 0`, `final_state == LIVE`, exit code 0; `mdtool normalize`
   equals `expected_events_*.csv`; `mdtool merge` equals `expected_merged.csv`; `mdtool export`
   equals `expected_export.csv`; same for a clean feed (all checkpoints matched, zero gaps);
   determinism (two runs identical).
10. `test_pta_integration.py`: `mdtool export` on the 0.25 h clean feed -> `pta.data.loaders.
    load_market` -> `pta.strategy.market_maker.simulate_market_maker(market, None)` ->
    `pta.report.analyze`; assert fills > 0 and the Project 1 attribution identity
    `total == spread_capture + inventory - fees` within 1e-9; also `pta report` CLI writes
    `report.md` from the exported market and those fills.
11. Build check: Release build with `MD_WERROR=ON` has zero warnings; `ctest` passes in
    Release and in a `-DMD_SANITIZE=ON` Debug build.
