# Decision log

Every decision that shaped this repo, with the reason, the alternatives, and where to find the evidence. Append-only: when a decision changes, add a new entry that supersedes the old one and say which. The chronological view is in [work-log.md](work-log.md); the learning path is in [learning/STUDY-GUIDE.md](learning/STUDY-GUIDE.md).

Format: **Decision**, **Why**, **Alternatives**, **Consequence**, **Evidence**, **Status**. Commit hashes are short SHAs on branch `claude/pensive-clarke-m1ixdr`.

## Goal and scope

### D01. Aim at Quant Analyst first (2026-10-30)
- **Decision:** Target Quant Analyst / Quant Analyst-Risk roles, with Graviton Research Capital's postings as the reference job description.
- **Why:** Your background is 1-4 years in finance/product. Graviton's Quant Analyst postings ask for 2-5 years and the work is post-trade analytics, monitoring and market-data tooling. Quant Researcher hiring there is reported to run mostly through campus drives.
- **Alternatives:** Quant Researcher (alpha research), Quant Trader (market making, mental maths). Both stay in the docs as later options.
- **Consequence:** Projects are built to look like the job, not like a generic ML notebook.
- **Evidence:** `docs/firm-landscape.md`, `docs/roadmap.md` section 1. All job facts are secondhand (see D27).
- **Status:** Active.

### D02. Pick projects by a weighted score with a pass mark (2026-10-30)
- **Decision:** Score each project 1-5 on JD fit (35%), interview signal (25%), feasibility (20%), data access (10%), reuse (10%). A project needs 3.5 or more.
- **Why:** Makes the order and the cut-off explainable instead of a gut call.
- **Alternatives:** Pick by interest; copy a generic quant project list.
- **Consequence:** Order is P1 4.80, P2 4.25, P3 4.15, P4 4.05; P5 3.35 is below the bar and out of scope.
- **Evidence:** `docs/roadmap.md` section 1. An agent recomputed the plan's table and corrected two scores (P1 4.85 to 4.80, P5 3.40 to 3.35); I recomputed them by hand and confirmed (commit `0ed03df`).
- **Status:** Active. Re-score if the target role changes.

### D03. Project 5 stays out (2026-10-30)
- **Decision:** Do not build the order-flow-imbalance alpha study.
- **Why:** Scores 3.35, below the pass mark; it would pull toward Quant Researcher, which is not the chosen track.
- **Evidence:** `docs/roadmap.md` sections 1 and 8.
- **Status:** Active.

## Data and honesty

### D04. Synthetic data, and never call it real edge (2026-10-30)
- **Decision:** Projects run on seeded synthetic data. No synthetic or backtest result is described as real or live edge.
- **Why:** The sandbox could not reach real data hosts, and synthetic data makes every result reproducible. The sample run is break-even (t-statistic 0.16), which shows the tooling and not a strategy.
- **Alternatives:** Download public data; tune the simulator until PnL looks good (rejected: that would be misleading).
- **Consequence:** READMEs lead with the limitation. Interview answers must say it too.
- **Evidence:** `projects/01-post-trade-analytics/README.md`, `docs/roadmap.md` decision rule 6.
- **Status:** Active.

### D27. Tag every web-derived claim as secondhand or unverified (2026-10-30)
- **Decision:** Firm and job-description facts carry a `[secondhand]` or `[unverified]` tag, with a "how to verify" list.
- **Why:** Direct fetches of gravitontrading.com, janestreet.com and prep sites failed in the sandbox (DNS refused). Everything came from search-result summaries.
- **Alternatives:** Fill gaps from memory (rejected: invents facts).
- **Consequence:** You must check live careers pages before applying. Some applications rows (for example India offices for Optiver, QRT, Citadel Securities, Millennium) are marked unverified.
- **Evidence:** `docs/firm-landscape.md`, `docs/applications.md` (commit `0ed03df`).
- **Status:** Active.

## Project 1: post-trade analytics

### D05. PnL attribution identity (2026-10-30)
- **Decision:** Total PnL = spread capture (fill vs mid at the fill) + inventory (price move while holding, marked to the final mid) - fees.
- **Why:** This is the "where did the PnL come from" split a desk asks for, and it can be proved exactly.
- **Consequence:** A test computes equity a second, independent way (cash + position x mid - fees) and a third way (position x change in mid over the grid); all three must agree.
- **Evidence:** `src/pta/pnl.py` (`attribute`, `equity_curve`, `inventory_pnl_on_grid`), `tests/test_pnl_identity.py`. Note: [attribution-identity](learning/attribution-identity.md).
- **Status:** Active.

### D06. Integer microsecond timestamps (2026-10-30)
- **Decision:** Timestamps are int64 microseconds, not float seconds.
- **Why:** With floats, `i*0.1 + 1.0` can differ from `(i+10)*0.1` in the last bit and move an as-of lookup by one row.
- **Evidence:** `src/pta/data/loaders.py`; every later module relies on it.
- **Status:** Active.

### D07. As-of mid rule and dropped markouts (2026-10-30)
- **Decision:** The mid at time t is the last quote at or before t. A markout whose time + horizon is past the last quote is dropped, not extrapolated.
- **Why:** No look-ahead and no invented values.
- **Consequence:** `n` falls at long horizons. A C++ kernel implements the same rule and a test checks it matches Python.
- **Evidence:** `src/pta/markouts.py`, `cpp/markout_kernel.cpp`, `tests/test_markouts.py`, `tests/test_cpp_parity.py`. Note: [markouts](learning/markouts-and-adverse-selection.md).
- **Status:** Active.

### D08. Show the t-statistic next to the Sharpe ratio (2026-10-30)
- **Decision:** Report a t-statistic beside the annualised Sharpe, and say the Sharpe assumes 390 one-minute bars a day and 252 days.
- **Why:** The first smoke run printed Sharpe 2.6 on a near-zero mean over 6 hours; the t-statistic of 0.16 is the honest reading.
- **Evidence:** `src/pta/risk.py`, sample report. Note: [t-statistic-vs-sharpe](learning/t-statistic-vs-sharpe.md).
- **Status:** Active.

### D18. Loader fixes from the independent review (2026-10-30)
- **Decision:** Convert nanosecond timestamps with integer division; keep fill quantities as floats.
- **Why:** Review reproduced two bugs: float64 cannot hold epoch nanoseconds exactly (quotes shifted and merged), and `astype(int64)` truncated a quantity of 1.5 to 1.
- **Evidence:** `src/pta/data/loaders.py`, `tests/test_loaders.py` (commit `0ed03df`).
- **Status:** Active.

## Project 2: risk monitor

### D09. Web dashboard, standard library only (2026-11-08)
- **Decision:** The live UI is one web page served by a stdlib HTTP server. No third-party UI libraries.
- **Why:** You chose it (asked twice; the second answer was "Web page"). It lets the README carry a real screenshot.
- **Alternatives:** Terminal UI; both.
- **Evidence:** `src/rmon/server.py`, `ui/index.html`, `examples/screenshot.png`.
- **Status:** Active.

### D10. Inventory PnL is marked to the current mid live (2026-11-08)
- **Decision:** The monitor marks inventory to the current mid, because the final mid is unknown during a session.
- **Why:** The only way to run live. At the end of a session it equals Project 1's total.
- **Evidence:** `src/rmon/state.py`, `tests/test_equivalence.py` (15 tests: 5 checks across 3 seeds; an independent review matched all 72,000 ticks on a fresh seed). Note: [incremental-vs-batch](learning/incremental-vs-batch.md).
- **Status:** Active.

### D11. At the same timestamp a quote is applied before its fills (2026-11-08)
- **Why:** In Project 1 a fill stamped on a quote uses that quote's mid; the incremental code must see the same order.
- **Evidence:** `src/rmon/replay.py`, `tests/test_state_edge_cases.py`.
- **Status:** Active.

### D12. Drawdown is measured once per distinct timestamp (2026-11-08)
- **Why:** The dip between a quote and the fills sharing its timestamp is an artefact, not a loss.
- **Evidence:** `src/rmon/state.py`.
- **Status:** Active.

### D13. Alert rules run once per committed timestamp (2026-11-08)
- **Why:** Rules must never see half-applied instants.
- **Evidence:** `src/rmon/engine.py`; refined by D17b below.
- **Status:** Active.

### D14. Alert design: hysteresis, cooldown, warm-up, minimum samples (2026-11-08)
- **Decision:** Each rule clears at a different, safer level than it fires; repeats are damped by a cooldown; the fill-rate rule waits for a warm-up; the markout rule needs 50 samples.
- **Why:** Stops flapping and noise.
- **Evidence:** `src/rmon/alerts.py`, `config/limits.toml`, `tests/test_alert_rules.py`. Note: [hysteresis-and-cooldown](learning/hysteresis-and-cooldown.md).
- **Status:** Active. See D14b for the cooldown policy, which changed.

### D14b. Cooldown policy: a higher severity than the last alert always fires (2026-11-08)
- **History (kept on purpose):**
  1. First version: the cooldown suppressed any new fire after a clear.
  2. Independent review reproduced a bug: warn, clear, then a breach 9 s later was hidden for 50 s (drawdown breach 5 minutes).
  3. My first fix let every breach bypass the cooldown. That broke an existing test (a markout breach 1 s after a clear must stay damped).
  4. Final rule: the cooldown damps repeats of the same or lower severity; a higher severity than the last alert always fires.
- **Evidence:** `src/rmon/alerts.py`, `tests/test_review_regressions.py`.
- **Status:** Active; supersedes the first version.

### D15. Thresholds calibrated on clean sessions (2026-11-08)
- **Decision:** Set limits from 20 clean 2-hour sessions plus one 6-hour run, with a stated margin above the worst clean value (position warn 60 vs clean max 36; later seed 24 reached 37).
- **Why:** Makes the false-positive vs missed-breach trade-off a number.
- **Consequence:** Limits fit this simulator only; real flow needs recalibration.
- **Evidence:** `config/limits.toml` comments, `projects/02-risk-monitor/README.md`.
- **Status:** Active.

### D16. The bad session is stitched from five simulated segments (2026-11-08)
- **Decision:** normal, adverse selection, inventory run, fill drought, recovery, each simulated separately and joined on one continuous grid with carried inventory; the market-maker copy adds carried inventory, a skew switch and a flow bias.
- **Why:** Lets each alert be tested in the phase that should cause it.
- **Evidence:** `src/rmon/scenarios.py`, `tests/test_scenario_demo.py`, `examples/demo_bad.txt` (9 alerts in order; the clean run has none).
- **Status:** Active.

### D17. Reject configs that cannot behave (2026-11-08)
- **Decision:** Fill-rate warm-up must be at least the window; markout hysteresis is capped at 100 bps. Review also fixed: a mid-instant flush freezing an instant (dirty flag), `bad()` dropping rows for awkward durations, and a fill-rate drought then runaway raising no second alert.
- **Evidence:** `src/rmon/limits.py`, `src/rmon/engine.py`, `tests/test_review_regressions.py` (14 tests; commit `0867c1b`).
- **Status:** Active.

### D19. Python 3.11 or newer (2026-11-08)
- **Why:** Limits use `tomllib`. The package metadata said 3.10, which was wrong; fixed.
- **Status:** Active.

### D20. The dashboard is localhost-only and not deployed (2026-11-08)
- **Decision:** The server binds to 127.0.0.1 with no authentication and has no `--host` option. It is not put on the public internet.
- **Why:** No auth, and the data is a demo. Publishing it would add risk for no portfolio value.
- **Consequence:** The only hosted items are the private explainer pages.
- **Status:** Active.

### D26. Quote measured numbers, not an agent's (2026-11-08)
- **Decision:** The README states the full engine's measured 110,000 events/s (231,895 events in 2.1 s), not the agent's 420,000 figure, which was the state machine alone.
- **Evidence:** My own timing run before writing the README.
- **Status:** Active.

## Process

### D21. Route work by difficulty; review independently (2026-10-30, refined 2026-11-08)
- **Decision:** Opus for judgement and independent review, Sonnet for specified implementation, Haiku for text assembled from verified output. The orchestrator owns git. Reviewers get the code, not my conclusions, and must reproduce each bug.
- **Evidence:** `docs/agent-workflow.md`. Reviews found 2 loader bugs (P1) and 5 alert-layer bugs (P2), all fixed with regression tests.
- **Status:** Active.

### D22. Explain complex things with `/eli5` (your instruction, 2026-11-08)
- **Decision:** Approach and architecture get a picture-first explainer per project; concepts get a short note linking it. Published as private artifacts; the HTML source lives in the repo.
- **Evidence:** `docs/agent-workflow.md`, `docs/learning/`. Each explainer got one phone-width dark-mode look and the collisions found were fixed.
- **Status:** Active.

### D23. Only commit verified work (2026-11-08)
- **Decision:** In-progress agent files are not committed; they go in once their tests pass. A stop hook asking for commits was answered by committing only finished files.
- **Status:** Active.

### D24. Create `main` at the first commit so a PR can open (2026-11-15)
- **Decision:** Pushed commit `18c47b8` to a new `main` branch, then opened draft PR #1 from the work branch. The default branch is untouched.
- **Why:** The repo had one branch, so no PR base existed. You told me to do everything and gave the access.
- **Evidence:** [PR #1](https://github.com/weebHarsh/quantitative-analyst-projects/pull/1).
- **Status:** Active.

### D28. Single-command entry points (2026-11-15)
- **Decision:** A root `Makefile` (`make test-all`, `make test-p1` ...).
- **Evidence:** `Makefile` (commit `f7edc49`).
- **Status:** Active.

## Project 3: market-data normalizer

### D25. Synthetic two-venue feeds; no real-venue adapters (2026-11-15)
- **Decision:** Generate two modelled venue feeds (Venue A diff feed with sequence numbers; Venue B chained-id feed) with checkpoints and injected faults. Do not write adapters for real venues.
- **Why:** I probed `data.binance.vision` and `api.binance.com` and the proxy refused both. A real-venue parser written without seeing a real file would be a guess.
- **Alternatives:** Ask you to allow hosts through the environment settings; that stays optional later.
- **Consequence:** The formats are described as modelled on common conventions, never as any real venue's.
- **Evidence:** `projects/03-market-data/DESIGN.md`, `projects/03-market-data/tools/gen_feeds.py`; the proxy refusal was checked with `curl` on 2026-11-15.
- **Status:** Active.

### D29. Two venue formats with different conventions (2026-11-15)
- **Decision:** Venue A is JSON lines with absolute quantities (a quantity of 0 deletes) and `first_seq`/`last_seq` per diff; Venue B is CSV with explicit add/change/delete actions, chained `prev_id`/`id`, nanosecond exchange time and prices scaled by 10^4.
- **Why:** Different conventions force the normalizer to do real work, and chained ids vs sequence ranges are the two common continuity schemes. Both are labelled synthetic.
- **Evidence:** `DESIGN.md` sections 2, 3; `python/test_gen_feeds.py` (24 tests).
- **Status:** Active.

### D30. Gap policy: stale, buffer, resync from the next snapshot (2026-11-15)
- **Decision:** A gap marks the venue stale and stops emitting; later diffs are buffered; the next venue snapshot resyncs by dropping buffered diffs at or before it and applying the rest. Duplicates are ignored. Out-of-order takes the gap path. A locked single-venue book is treated like a crossed one.
- **Why:** The standard safe recovery; never publish a book that might be wrong.
- **Consequence / limitation:** The generated feeds send snapshots in-stream, so the "apply the rest" branch is not exercised by them; a hand-built unit test is in the test plan to cover it.
- **Evidence:** `DESIGN.md` section 6. On a 15-minute faulted feed each venue shows gaps=2, out_of_order=1, duplicates_ignored=1, crossed_detected=1, resyncs=3, checked against both the generator's reference and an independent sequencer in the test.
- **Status:** Active.

### D31. Export to Project 1: 2-decimal bid/ask, 3-decimal mid (2026-11-15)
- **Decision:** The export writes `ts_us,bid,ask,mid`; a half-tick mid cannot be written exactly in 2 decimals, so mid has 3. Rows appear only when both sides exist and the merged book is not stale; locked merged quotes are filtered; timestamps are strictly increasing.
- **Why:** Project 1's loader needs a strictly increasing schema. Locked merged quotes occur legitimately because Venue B lags Venue A (1,017 on the seed-7, 15-minute faulted feed, re-measured by me on 2026-11-15; the design agent reported about 1,100 on its own run).
- **Evidence:** `DESIGN.md` section 9; Project 1's `analyze` ran on the reference export.
- **Status:** Active.

### D32. Document the clock offset, do not correct it (2026-11-15)
- **Decision:** Venue B's clock runs 300 microseconds ahead of the local clock by construction; latency stats report it rather than hiding it.
- **Why:** Correcting an offset needs a model of the true one-way delay, which a single clock cannot give.
- **Status:** Active.

### D33. Fixed-point only; no external C++ dependencies; small committed fixtures, large feed generated (2026-11-15)
- **Decision:** Prices are int64 ticks and quantities int64 lots. Tests use a hand-rolled harness and flat-file expected counters (no JSON library, no gtest). Golden fixtures (40 KB) are committed; the 109 MB benchmark feed (502,883 Venue A lines, 580,321 Venue B rows) is generated by `tools/gen_feeds.py --hours 3.8` and never committed.
- **Why:** Exact arithmetic in the book, a build that needs nothing installed, and a repo that stays small.
- **Status:** Active.

### D34. Project 3 implementation choices beyond the spec (2026-11-15)
- **Decision:** (a) Build with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror`, stricter than the roadmap's `-Wall -Wextra`. (b) Zero-quantity snapshot levels in Venue A, and `D` with a nonzero size or `N`/`C`/`SL` with a zero size in Venue B, are parse errors (counted, never a crash). (c) `mdtool` exit codes: 2 usage, 3 I/O, 4 parse errors, 5 checkpoint mismatch, with 5 taking priority over 4. (d) `Book::set_level` with quantity 0 removes the level without an assert, because the sanitizer build is a Debug build and an assert would abort.
- **Why:** The spec left these open; making them strict surfaces bad input instead of hiding it.
- **Known design mismatch:** `DESIGN.md` section 12 names `python/test_cpp_integration.py`; the integration test that exists is `python/test_pta_integration.py` (it runs `mdtool export` and loads the result with Project 1).
- **Evidence:** `projects/03-market-data/src/`, `tests/` (at pass 1: 51 tests, 21,758 checks; after pass 2 the suite was 76 tests (85 after the review fixes, D43); both the normal and the ASan+UBSan builds pass with zero warnings, rerun by me from a clean state).
- **Status:** Active.

### D35. Benchmark method and how to read it (2026-11-15)
- **Decision:** One command (`make bench`) builds Release with `-Werror`, generates and caches a synthetic feed, discards one warm-up rep, and reports the median of five timed reps. Latency is `steady_clock` around each apply call, and the timer's own cost (about 28 ns per call pair) is reported but not subtracted.
- **Why:** Reproducible and honest about measurement error. Per-update latencies near the timer cost are an upper bound, not a precise figure; throughput moved by roughly 30% between runs on this shared machine.
- **My measured run (2026-11-15, GCC 13.3.0, -O3, after the review fixes):** 1,083,204 messages and 1,236,823 events; parse 3.95M events/s; merged apply 23.1M events/s; end-to-end 2.02M events/s; update latency p50 71 ns, p99 158 ns; timer cost about 28 ns.
- **Earlier run, before the merge was rewritten to sort globally (D36 review fix):** parse 3.48M, apply 21.8M, end-to-end 2.23M events/s; p50 75 ns, p99 169 ns. The differences are inside the roughly 30% run-to-run variation, but the code path changed, so the later run is the one quoted elsewhere.
- **Limits:** synthetic feeds, a single machine, no network or disk effects; not a claim about a production feed handler.
- **Evidence:** `projects/03-market-data/bench/run_bench.sh`, `src/bench.cpp`.
- **Status:** Active.

### D43. Project 3 review fixes (2026-11-15)
- **Findings (independent Opus review, each reproduced by the reviewer and again by me):**
  1. The merge kept each feed's file order, so a feed with out-of-order local timestamps produced an export whose `ts_us` went backwards and repeated, and the merge disagreed with the generator's reference pipeline (gaps 1 vs 2).
  2. `bench` exited 6 ("counters unstable") whenever a feed had a parse error, because only one phase counted bad lines.
  3. Signed int64 overflow (undefined behaviour) on extreme input values in the latency sum, the cross-venue quantity sums, the latency difference and the export mid.
- **Decision:** (1) `run_merged` now parses both feeds, stable-sorts all messages by (local time, venue id) as the reference does, then applies them. (2) The benchmark takes parse-error lines out of the end-to-end counters before comparing, so it is comparable. (3) Sums and differences of untrusted values saturate (`include/md/saturate.hpp`); an export row whose mid could overflow is skipped.
- **Proof it was broken and is fixed:** I built the committed original in a throwaway worktree. On the same inputs it hit UBSan signed-overflow errors, exported a negative mid (`-0.015`) for absurd prices, and produced non-increasing timestamps; the fixed build does none of these. Nine new regression tests in `tests/test_review_regressions.cpp` pass in both builds, and a mutation check (disabling the sort) makes them fail.
- **Fuzzing result from the review:** 5,200 mutated-feed iterations, no crashes, hangs or ASan findings; the only UBSan hits were the overflows fixed here.
- **Cost:** `run_merged` now holds all parsed events in memory before applying them (the benchmark already did). The end-to-end rate moved from 2.23M to 2.02M events/s, within run-to-run noise.
- **Known design deviation, kept as built:** `DESIGN.md` section 10 says the benchmark builds in `build-release` and caches the feed in `/tmp/md_bench_feeds`; the implementation uses `build/` and `build/bench-feed/` so the cache stays inside the project and is ignored by git.
- **Evidence:** `projects/03-market-data/src/merge.cpp`, `src/bench.cpp`, `include/md/saturate.hpp`, `tests/test_review_regressions.cpp`.
- **Status:** Active.

## Project 4: backtester

### D36. Reuse Project 3's book code; one normalised-events file between them (2026-11-15)
- **Decision:** Project 4 links Project 3's book, session and export code and reads the normalised-events CSV that `mdtool normalize` already writes (Venue A). Its `market.csv` must be byte-identical to `mdtool export --venue A`, checked by an integration test.
- **Why:** One book implementation to trust, and Project 3 needs no change.
- **Evidence:** `projects/04-backtester/DESIGN.md` sections 1 and 10.
- **Status:** Active.

### D37. Deterministic event ordering; orders and cancels share one channel (2026-11-15)
- **Decision:** At equal timestamps market events go first, then exchange arrivals, then reports, then timers. Orders and cancels travel on one first-in-first-out channel, so a cancel can never overtake the order it cancels.
- **Evidence:** `DESIGN.md` section 2.
- **Status:** Active.

### D38. One latency value; optional seeded jitter (2026-11-15)
- **Decision:** A single latency L applies to order entry, cancel, and each ack direction. The only randomness is optional jitter from a seeded generator, so a run is reproducible.
- **Why:** One knob keeps the sensitivity study readable; separate entry and cancel latencies can be added later.
- **Status:** Active.

### D39. Three queue models as pure functions in integer lots (2026-11-15)
- **Decision:** OPTIMISTIC (fills whenever the market trades at or through our price), FIFO_BACK (join the back of the displayed queue; only trades at that price advance it) and FIFO_CANCEL_AHEAD with fraction f (a share f of unexplained displayed-size drops counts as ahead of us). Queue ahead is capped at the displayed size, which is forced arithmetic and not an assumption. With f = 0 the cancel-ahead model equals FIFO_BACK.
- **Evidence:** `DESIGN.md` section 3.
- **Status:** Active.

### D40. Post-only orders; one fill when the market moves onto a resting order (2026-11-15)
- **Decision:** An order that would cross on arrival is rejected. If the market later moves onto a resting order it fills once at our price; a switch turns that off for an ablation.
- **Why:** Keeps the strategy a passive market maker and makes the optimistic assumption explicit and removable.
- **Status:** Active.

### D41. What may be asserted about the model ordering (2026-11-15)
- **Decision:** OPTIMISTIC >= CANCEL_AHEAD(0.5) >= CANCEL_AHEAD(0.25) >= FIFO_BACK fills is proven only for a fill-independent order schedule, so tests assert it per order for such a schedule. For the market maker, where fills change later orders, it is reported and not asserted.
- **Why:** An assertion that can fail for legitimate reasons would be a flaky test or a false claim.
- **Status:** Active.

### D42. Limits the synthetic feed puts on the study (2026-11-15)
- **Findings (from the design agent, seed 7, 0.25 h):** all 3,198 of 3,198 trades are immediately followed by an exact one-level reduction, so trades and cancels are perfectly separable. This is a property of the generator, not of real feeds. Non-trade decreases at the touch (about 7.6k) outnumber trades (about 3.2k), so the cancel-ahead fraction f cannot be identified from the data. Touch moves delete and re-add levels in one message, about 5.8 per second.
- **Consequence:** The sensitivity study can show direction and rough size of the effect of queue assumptions in this simulator. It cannot say what f is, and its magnitudes do not transfer to a real venue. The write-up must say so.
- **Study design and hash:** 4 latencies x 4 queue models x 3 seeds, 1 hour each, budget at most 10 minutes; the run fingerprint is FNV-1a 64 over the bytes of `fills.csv`.
- **Status:** Active.

### D44. Project 4 core: how it was built and what was verified (2026-11-15)
- **Decision:** Project 3 is pulled in with `add_subdirectory(../03-market-data ... EXCLUDE_FROM_ALL)` so Project 3's own build and tests are untouched; the sanitizer build directory is `build-san/` (the design said `build-asan`). The design's statement that Project 3 was not implemented was corrected.
- **Evidence (rerun by me from a clean state):** 66 tests, 13,837 checks, in both the normal and the ASan+UBSan builds, zero warnings; Project 3's 85 tests still pass. On a freshly generated 0.25 h feed (seed 7): `fills.csv` is byte-identical across two runs (hash `3e93c91016a904b8`), `market.csv` is byte-identical to `mdtool export --venue A`, and Project 1 loads, validates and analyses the fills (2,663 fills). Fill counts by model at 1 ms latency on that feed: optimistic 4,890, cancel-ahead(0.5) 2,663, back-of-queue 2,533.
- **Not covered by tests yet:** the CLI exit codes (checked by hand) and a generated-feed integration test; both belong to the Python pass.
- **Cosmetic issue left open:** some fee values in `fills.csv` print with 17 significant digits (for example `0.00020000000000000001`). They parse correctly; formatting can be tightened in the Python pass.
- **Status:** Active.

### D45. The sensitivity study must show the cross-fill rule ON and OFF (2026-11-15)
- **What I saw:** The first full study (48 cells, 3 seeds x 1 hour, rerun by me byte-for-byte) showed latency barely changing PnL or fills, and strongly negative spread capture (about -27 to -30) for every FIFO model. That looked wrong, so I ran the design's ablation switch (`--cross-fills off`) on the seed-7 feed.
- **Finding:** With the cross-fill rule ON (the design's default: when the market moves onto a resting order it fills once at our price, regardless of queue position), about 7,500 of FIFO_BACK's 9,622 fills come from that rule, and those fills are what drive spread capture to -28.9. With it OFF, FIFO_BACK has 2,087 fills, spread capture +5.9 and a 1 s markout of +0.405 bps at 0 ms, and latency now clearly matters: the markout falls to +0.253 bps and spread capture to +4.6 at 20 ms, while fills rise from 2,087 to 2,505. So with the rule ON the latency effect is hidden, not absent.
- **Decision:** Report both settings side by side in the results, the table and the charts; say plainly that the rule ON results are dominated by cross-rule fills and that its queue-less filling is itself an optimistic-style assumption; keep the default unchanged (it is the design's choice) rather than quietly switching to the more flattering setting.
- **Why:** A study that only showed the default would have told readers latency does not matter, which is an artefact of one modelling choice. This is exactly the kind of inflation the project is meant to expose.
- **Evidence:** `projects/04-backtester/results/` (both settings), `python/sensitivity.py`; my ablation run on seed 7 is reproduced in the harness.
- **Status:** Active.

### D46. Project 4 review fixes (2026-11-15)
- **Review result (independent Opus, read-only):** no defects in the queue models, cancel-in-flight handling, the order/cancel channel, event ordering, post-only, cross fills, bookkeeping or determinism. 5,000 fuzz iterations under ASan/UBSan (515k fills, 531k orders, 44.7M queue-invariant checks) found nothing; a 500-feed per-order model-ordering fuzz found no violation. On a fill-independent schedule, per-order filled lots satisfy OPTIMISTIC >= CA(1) >= CA(0.5) >= CA(0.25) >= CA(0) = BACK. For the market maker the order id sets differ between models, so per-order ordering cannot be tested; totals still came out in model order at every latency.
- **Defects found and fixed (each reproduced by the reviewer and by me):**
  1. `--schedule-period-us 0` re-armed a timer at the same instant forever and exhausted memory (my repro with a 600 MB cap aborted). `bt_run` now rejects it with exit 2; the library guarantees time advances by at least 1 ns.
  2. Huge latency values overflowed signed int64 (undefined behaviour, visible under UBSan). `bt_run` now rejects any latency or jitter outside 0 to 1,000,000,000 microseconds (1000 s) with exit 2, and arrival-time arithmetic saturates in the library.
  3. Two test checks could not fail (a path compared with itself; `x + 0 == 0`). Replaced with real assertions: the nested output directory exists, and the CSV row count equals the number of fills produced.
- **Doc/code gap closed, not changed:** `cancels_in_flight_filled` is counted once per order when its first cancel arrives. `DESIGN.md` said "in every case", which differs only for duplicate cancels; neither built-in strategy sends them, so numbers are unaffected. The design now says so.
- **Effect on results:** none. I reran the full 96-cell study and the CSV and tables are byte-identical.
- **Tests:** 67 C++ tests in both builds (zero warnings), 17 Python tests, including six out-of-range CLI cases.
- **Evidence:** `projects/04-backtester/src/bt_run.cpp`, `src/latency.cpp`, `src/strategy_schedule.cpp`, `tests/test_latency.cpp`, `tests/test_strategy_mm.cpp`, `python/test_integration.py`.
- **Status:** Active.

### D47. The C++ markout kernel is a cross-check, not an optimisation (2026-11-15)
- **What happened:** The week-16 résumé template had an "[X]x faster" placeholder for the Project 1 C++ kernel, because no measurement existed. I measured it before letting anyone fill it in.
- **Measurement (6-hour synthetic session, 15,895 fills, 216,000 quotes, median of 7, `-O2`):** about 123 ms for the C++ command-line kernel (including reading both CSV files) against about 11 ms for the in-memory vectorised Python (attribution plus six horizons). The kernel is about ten times slower end to end, because CSV parsing dominates while numpy does the lookups in memory.
- **Decision:** Never claim a speedup. Describe the kernel as an independent second implementation that must agree with the Python to 1e-9, which is its real value (it catches errors in either). Corrected in `docs/resume.md`, `docs/applications.md`, `docs/walkthroughs.md` and the Project 1 README.
- **Limit of the measurement:** it compares the whole process with the in-memory Python; it does not time the C++ compute alone. That would need an in-process benchmark.
- **Lesson:** an unmeasured performance claim on a résumé is an interview question that ends badly.
- **Status:** Active.

## Not done on purpose
- No real-venue data or adapters (D25).
- No public hosting of the monitor (D20).
- No Project 5 (D03).
- Did not tick the CodeRabbit "Autofix" checkbox on PR #1: it would invite a bot to push to your branch, which you have not asked for (2026-11-15).

## Open items
- Project 4 implementation is in progress; add its decisions as D48 onward.
- Verify firm and job facts against the live pages (see `docs/firm-landscape.md`).
