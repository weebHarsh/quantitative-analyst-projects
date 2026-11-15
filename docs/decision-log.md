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
- **Why:** Project 1's loader needs a strictly increasing schema. Locked merged quotes occur legitimately because Venue B lags Venue A (1,100 in 15 minutes).
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

## Not done on purpose
- No real-venue data or adapters (D25).
- No public hosting of the monitor (D20).
- No Project 5 (D03).
- Did not tick the CodeRabbit "Autofix" checkbox on PR #1: it would invite a bot to push to your branch, which you have not asked for (2026-11-15).

## Open items
- Project 3 C++ implementation and Project 4 are being built; add their decisions as D34 onward.
- Verify firm and job facts against the live pages (see `docs/firm-landscape.md`).
