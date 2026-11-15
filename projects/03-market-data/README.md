# Project 3: market-data normalizer and order-book rebuild (C++17)

A C++17 normalizer that turns two synthetic venue feeds into one fixed-point event schema. It rebuilds L2 books with gap detection and resync, merges the two venues, and exports quotes that Project 1 can analyse.

## Read this first

> **The feeds are synthetic.** `tools/gen_feeds.py` writes both feeds. The two venue formats are modelled on common conventions (sequence-numbered diff feeds and chained-id delta feeds). They are not the format of any real venue.
>
> **No real venue was tested.** The sandbox could not reach real venue hosts: `data.binance.vision` and `api.binance.com` were both refused. There is no real-venue adapter in this project, and none is claimed.
>
> **Benchmark numbers come from one shared machine, on synthetic data.** Treat them as orders of magnitude, not precise figures.

## Results

- **C++ tests:** 76 pass in the normal build and in the AddressSanitizer + UBSan build. Both build with `-Werror` and zero warnings.
- **Python tests:** 32 pass: 24 generator tests (`python/test_gen_feeds.py`) and 8 integration tests (`python/test_pta_integration.py`).
- **Golden files:** events, merge and export outputs are byte-identical to the committed expected files in `tests/golden/`.
- **Faulted feed:** on a 15-minute feed generated with `--faults`, each venue reports `gaps=2, out_of_order=1, duplicates_ignored=1, crossed_detected=1, resyncs=3`. The rebuilt book equals the venue's own checkpoint snapshots.
- **Benchmark** (measured 2026-11-15 with GCC 13.3.0 `-O3` on 1,083,204 messages, 1,236,823 events):

| Phase | Result |
|---|---|
| Parse | 3.95 million events/s |
| Apply (pre-parsed events in merged order, no merge step) | 23.1 million events/s |
| End-to-end (parse + sessions + merge + latency summary) | 2.02 million events/s |
| Per-update latency | p50 71 ns, p99 158 ns |

The latency figures include about 28 ns of timer overhead. That overhead is reported but not subtracted. Throughput varied by roughly 30 percent between runs on this machine.

## How it works

**Pipeline**

1. **Parse.** `ParserA` and `ParserB` turn each raw line into normalised events. Prices are int64 ticks, quantities are int64 lots, times are int64 nanoseconds. No floating point.
2. **Sequence.** One `VenueSession` per venue checks continuity, buffers updates and resyncs from snapshots.
3. **Apply.** Each event updates that venue's `Book`.
4. **Merge.** Both venues' top of book are combined into one quote stream, ordered by local capture time.
5. **Export.** `QuoteExporter` writes `ts_us,bid,ask,mid` for Project 1.

**Venue A (JSON lines).** One object per line. A `snapshot` carries `seq` and the full `b`/`a` levels. A `depth` diff carries `first_seq`, `last_seq` and absolute quantities, where `"0.000"` deletes a level. A `trade` carries `tid`, `px`, `qty` and `side`. Prices look like `"100.01"` and quantities like `"1.250"`, and both are parsed with integer arithmetic only.

**Venue B (CSV).** Header `recv_ns,kind,exch_ns,id,prev_id,action,side,px_e4,size`. Snapshots are `SB`, `SL` and `SE` rows. Updates are `U` rows whose `prev_id` chains to the previous `id`, with action `N`, `C` or `D`. Trades are `T` rows. Prices are scaled by 10^4 and must divide evenly by 100.

**Gap policy**

- Continuity: for A, `first_seq <= last + 1 <= last_seq`; for B, `prev_id == last`.
- A continuity break marks the venue STALE. Its top of book is no longer published, and later updates are buffered.
- The next venue snapshot resyncs. Buffered updates at or before it are dropped, and the rest are applied in order. A hole left after the snapshot keeps the venue STALE.
- A duplicate (a sequence already applied) is ignored and counted.
- An out-of-order update takes the gap path. In a swap, the early message opens the gap, and the late one is counted as out-of-order and buffered.
- A crossed or locked single-venue book is treated as corruption: the venue goes STALE and recovers at the next snapshot.

**Book invariants.** Bid prices strictly decrease from the best level and ask prices strictly increase, with no duplicate prices. No zero quantity is stored, and a delete removes the level. The book may be crossed or locked because it stores what it is told. The session detects that, not the book.

**Merge rule.** The top of book of each venue is read after every raw message. Only live venues count, and an empty side reads as zero. The merged best bid is the highest bid across live venues, with the quantity summed across venues at that price. The merged best ask is the same with the lowest price. Bit masks record which venues contributed. Ordering uses local capture time only, with A before B on a tie. Exchange timestamps from different venues are never compared. A merged quote is emitted only when something changed.

**Export rule.** `ts_us` is local time in microseconds. Within one microsecond only the last merged quote is kept. A row is written only when both sides exist, at least one venue is live, and bid is below ask. Bid and ask have 2 decimals. Mid has 3 decimals because it can fall on a half tick (99.97 and 99.98 give 99.975), and 3 decimals represent that exactly.

**Clock offset.** Venue B's exchange clock runs 300 microseconds ahead of the local clock, by construction. This is documented and not corrected.

## Quickstart

Run from this directory. You need CMake, a C++17 compiler and Python 3.

```bash
make test            # Release build in build/, then ctest
make test-sanitize   # Debug build with ASan + UBSan in build-san/, then ctest
make bench           # builds, generates and caches a synthetic feed in build/bench-feed/, runs the benchmark
```

`make bench` writes `build/bench-results.txt`. The generated feed is never committed.

Python tests (from this directory):

```bash
python3 -m pytest -q -p no:cacheprovider python
```

Generate a 15-minute feed with faults, then run the tool on it:

```bash
mkdir -p out
python3 tools/gen_feeds.py --seed 7 --hours 0.25 --out out/feeds --faults
build/mdtool replay --venue A --in out/feeds/venueA.jsonl --truth out/feeds/truth_A.csv
build/mdtool replay --venue B --in out/feeds/venueB.csv --truth out/feeds/truth_B.csv
build/mdtool merge  --a out/feeds/venueA.jsonl --b out/feeds/venueB.csv --out out/merged.csv
build/mdtool export --a out/feeds/venueA.jsonl --b out/feeds/venueB.csv --out out/market.csv
```

- `replay` prints one counters JSON line on stdout. With `--truth`, it also reports `truth_checked` and `truth_mismatched`. `--tob PATH` writes the venue's top-of-book log.
- `merge` writes the merged CSV and prints a summary JSON line on stderr.
- `export` writes the Project 1 CSV and prints `{"rows":...,"merged_quotes":...}` on stdout. Add `--venue A` or `--venue B` to export one venue only; then only that feed is needed.
- Exit codes: 0 ok, 2 usage error, 3 I/O error, 4 parse errors (output is still complete), 5 checkpoint or truth mismatch (takes priority over 4), 6 benchmark counters unstable.

**Feeding Project 1.** `pta analyze` and `pta report` read a fills CSV, which mdtool does not write. The integration test in `python/test_pta_integration.py` shows the path that works, using the exported market and Project 1's market-maker replay. Install Project 1 first:

```bash
cd ../01-post-trade-analytics
pip install -e .[test]
```

Then run this in Python, with the market file written by `export` above:

```python
from pta.data.loaders import load_market
from pta.strategy.market_maker import simulate_market_maker
from pta import report

market = load_market("../03-market-data/out/market.csv")
fills = simulate_market_maker(market, None)
print(report.analyze(fills, market)["totals"])
```

The market is synthetic, so the attribution totals describe the tooling, not a strategy.

## Layout

```
CMakeLists.txt                   targets mdlib, mdtool, md_tests, md_bench; -Wall -Wextra ... -Werror by default
Makefile                         make test, make test-sanitize, make bench
DESIGN.md                        frozen design: formats, counters, CLI, test plan
include/md/types.hpp             Event, Level, TopOfBook, MergedQuote, name helpers
include/md/fixed.hpp             fixed-point parsing and formatting
include/md/io.hpp                feed loading, event CSV and counters JSON writers
include/md/parser_a.hpp          Venue A (JSON lines) parser
include/md/parser_b.hpp          Venue B (CSV) parser and split_csv
include/md/book.hpp              L2 book
include/md/session.hpp           VenueSession: sequencing, gaps, buffer, resync, checks
include/md/replay.hpp            replay driver behind mdtool replay (truth check, top-of-book log)
include/md/merge.hpp             cross-venue Merger and run_merged
include/md/export.hpp            QuoteExporter (Project 1 schema)
include/md/stats.hpp             latency statistics (nearest rank)
include/md/bench.hpp             benchmark API
src/*.cpp                        one implementation file per header above
src/mdtool.cpp                   mdtool command-line driver (normalize, replay, merge, export, bench)
tests/harness.hpp                minimal TEST/CHECK registry (no gtest)
tests/test_main.cpp              test runner entry point
tests/test_fixed.cpp             fixed-point tests
tests/test_parsers.cpp           parser tests, including parse-error cases
tests/test_book.cpp              book tests
tests/test_session.cpp           session tests: gaps, resync, snapshots, straddle
tests/test_merge.cpp             merge tests
tests/test_export.cpp            export tests
tests/test_stats.cpp             nearest-rank tests
tests/test_golden.cpp            golden fixtures compared byte for byte and counter by counter
tests/golden/                    committed fixtures: feeds, truth, expected outputs, faults.json, meta.json
bench/md_bench.cpp               benchmark binary
bench/run_bench.sh               one-command benchmark (make bench)
tools/gen_feeds.py               synthetic feed generator and Python reference pipeline
python/test_gen_feeds.py         24 generator tests
python/test_pta_integration.py   8 tests: export loads in Project 1 and runs its replay
```

## Exit criteria (roadmap section 5)

- [x] **The rebuilt top of book matches the venue's own snapshots at checkpoints.** `tests/test_golden.cpp` (`golden_checkpoints_equal_truth`) checks the golden fixtures against the truth files. On the 15-minute faulted feed, the rebuilt book equals each venue's checkpoint snapshots.
- [x] **Gaps are detected and recovered.** `tests/test_session.cpp` covers drop to gap to stale to resync (`session_drop_gap_stale_resync`), swap (`session_swap_is_out_of_order`) and buffered straddle (`session_buffered_straddle_a` and `_b`). The 15-minute faulted feed shows `gaps=2` and `resyncs=3` per venue.
- [x] **Benchmarks are reproducible with a single command.** `make bench` (which runs `bench/run_bench.sh`).
- [x] **P1 runs on its output.** `python/test_pta_integration.py` loads the export with `load_market`, replays it through Project 1's market maker and runs `report.analyze`.
- [x] **Builds with CMake on Linux with `-Wall -Wextra` and no warnings.** `CMakeLists.txt` sets these flags with `-Werror` on by default. Both the normal and the sanitizer builds have zero warnings.

## Limitations

- **Synthetic feeds only.** Both venues are generated, and the formats are modelled on conventions, not on any real venue.
- **Resync branch coverage.** The generated feeds send snapshots in-stream, so the "apply the rest of the buffered diffs" resync branch is not reached by them. Only the hand-built unit tests (`session_buffered_straddle_a` and `_b`) cover it.
- **Clock offset is documented, not corrected.** Venue B's clock is 300 microseconds ahead by construction, so its apparent latency is about 300 microseconds lower than its true latency. Do not compare A's and B's latencies as if they were on one clock.
- **Single-machine benchmark.** Throughput moved by roughly 30 percent between runs, and the latency numbers include timer overhead that is not subtracted.
- **No network I/O.** The tool reads files only.
- **No real venue format has been verified.**
- **Trades are passed through, not analysed.** Trade events are parsed and counted, and they never change the book.

## Map to the Graviton Quant Analyst job description

The JD wording below is secondhand, taken from job-board postings, not from the employer's own posting.

| JD wording (secondhand) | Where this project does it |
|---|---|
| tools to analyse market data across exchanges | `src/parser_a.cpp` and `src/parser_b.cpp` normalise two venue formats into one schema; `src/session.cpp` sequences each venue; `src/merge.cpp` combines them into one quote stream; `src/export.cpp` writes quotes for Project 1 |
| scripts in C++ and Python | `src/mdtool.cpp` (C++ command line for normalize, replay, merge, export, bench) and `tools/gen_feeds.py` (Python generator and reference pipeline), with `python/` tests |
| review production trade data | Partial. `src/parser_a.cpp` and `src/parser_b.cpp` parse trade rows, and `src/session.cpp` counts them and passes them through. The project does not analyse trades. |

## Links

- Decision log: [D25](../../docs/decision-log.md) (synthetic two-venue feeds, no real-venue adapters), [D29-D35](../../docs/decision-log.md) (venue formats, gap policy, export, clock offset, fixed-point choices, benchmark method).
- Study guide: [docs/learning/STUDY-GUIDE.md](../../docs/learning/STUDY-GUIDE.md).
- Full design: [DESIGN.md](DESIGN.md).
- Project 1, which consumes the export: [projects/01-post-trade-analytics](../01-post-trade-analytics/README.md).
