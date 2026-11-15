# Work log

What was done, in order, who did it (model tier), and the evidence. Append a row for every milestone. Decisions behind each step are in [decision-log.md](decision-log.md) (D-numbers). Check any commit with `git show <sha>`.

| Date | Step | Commit | Who | Evidence | Decisions |
|---|---|---|---|---|---|
| 2026-10-30 | Researched Graviton, Jane Street, Optiver, Citadel, Tower, Quadeye (search summaries only; direct fetches blocked) | n/a | me | `docs/firm-landscape.md` | D01, D27 |
| 2026-10-30 | Planned the portfolio: weighted scoring, 16-week sequence | n/a | me (plan file) | `docs/roadmap.md` | D02, D03 |
| 2026-10-30 | Built Project 1: PnL attribution, markouts, risk stats, C++ kernel, synthetic market and market maker, report generator | `18c47b8` | me | 12 tests pass; sample report; charts checked by eye | D04-D08 |
| 2026-10-30 | Project 1 README | `1a047b3` | Haiku (checked by me against the sample report) | README numbers match `examples/sample_report.md` | D04 |
| 2026-10-30 | Roadmap, firm landscape, interview prep, applications docs; independent review of Project 1 found two loader bugs, fixed | `0ed03df` | Opus (roadmap, landscape, review), Sonnet (prep, applications), me (fixes) | 16 tests; scores recomputed by hand | D02, D18, D21, D27 |
| 2026-11-08 | Recorded the explanation rule; created the learning index | `8bebd69` | me | `docs/agent-workflow.md` | D22 |
| 2026-11-08 | Project 2 streaming core: incremental PnL, drawdown, markouts | `77b1089` | Opus | 28 tests; matches Project 1 on 3 seeds | D10-D12 |
| 2026-11-08 | Project 1 explainer (private page, source in repo) | `5503eeb` | me | one phone-width look; two label collisions fixed | D22 |
| 2026-11-08 | Project 2 alert layer: limits, four rules, engine, bad scenario | `96af4c2` | Opus | 89 tests; clean run no alerts, bad run 9 alerts in order | D13-D16 |
| 2026-11-08 | Project 2 CLI, server, dashboard; five review findings fixed with regression tests | `0867c1b` | Sonnet (web layer), Opus (review), me (fixes) | 123 tests; screenshot, no console errors | D09, D14b, D17, D19, D20 |
| 2026-11-08 | Project 2 README, demo evidence, explainer; marked built | `41af273` | Haiku (README), me (checks, explainer) | README numbers checked against `config/limits.toml` and demo output; throughput measured by me | D26 |
| 2026-11-15 | Created `main`, opened draft PR #1, subscribed | n/a (branch push) | me | [PR #1](https://github.com/weebHarsh/quantitative-analyst-projects/pull/1) | D24 |
| 2026-11-15 | Root Makefile | `f7edc49` | me | `make test-p1` passes | D28 |
| 2026-11-15 | Six concept learning notes | `4cd2d3d` | Sonnet (checked by me: t-stat arithmetic recomputed) | `docs/learning/*.md` | D22 |
| 2026-11-15 | Decision log, work log, study guide | `2e258fa` | me | these files | D21-D23 |
| 2026-11-15 | Project 3 design spec (810 lines), seeded two-venue feed generator with fault injection, 24 generator tests, 40 KB golden fixtures | `fad9aca` | Opus | 24 tests pass (rerun by me) | D25, D29-D33 |
| 2026-11-15 | Project 3 C++ core: event types, fixed-point, both parsers, L2 book, sequencer with gap/resync, mdtool normalize and replay | `6a14fbc` | Sonnet | 51 tests, 21,758 checks; normal and sanitizer builds clean | D34 |
| 2026-11-15 | Project 3 merge, export to Project 1 schema, benchmark, Python integration test | `8f9ee06` | Sonnet | 76 C++ tests in both builds, 32 Python tests, `make bench` (rerun by me) | D35 |
| 2026-11-15 | Project 3 explainer (private page, source in repo); one phone-width look, no fixes needed | `a750079` | me | [page](https://claude.ai/artifact/FveVf5qjA11cXmogARD5jV) | D22 |
| 2026-11-15 | Project 3 README (quickstart run end to end by me; locked-quote figure corrected from 1,100 to 1,017) | next commit | Haiku, me (checks) | counters on seed 7: gaps 2, resyncs 3, 0 checkpoint mismatches | D31, D34 |
| 2026-11-15 | Project 4 design spec (about 1,265 lines): event loop, latency, three queue models, strategy, outputs, determinism, sensitivity study, test plan | next commit | Opus | design only; blocker (Project 3 not built) since resolved | D36-D42 |
| 2026-11-15 | Project 3 independent review (Opus: 3 defects, 5,200 fuzz iterations) and fixes: merge sorts globally, bench counters, saturating arithmetic; benchmark rerun and figures updated everywhere | next commit | Opus (review), me (fixes) | 85 C++ tests in both builds, zero warnings, regression tests mutation-checked; original code shown failing on the same inputs | D43 |
| 2026-11-15 | Project 4 C++ core: event loop, latency with cancels in flight, three queue models, market-maker port, outputs in Project 1's schema, determinism hash | next commit | Sonnet (verified by me) | 66 tests in both builds, deterministic hash, output validates in Project 1 | D44 |
| 2026-11-15 | Three Project 3 learning notes (order book basics, gap detection and resync, fixed-point prices); symbols, paths, tick size and golden numbers verified by me | next commit | Sonnet (checked by me) | `docs/learning/*.md` | D22 |

## Test totals at each milestone
- After Project 1: 16 tests.
- After Project 2: 16 + 123.
- Reproduce: `make test-all`.

## How to trace anything
1. Find the concept in [learning/STUDY-GUIDE.md](learning/STUDY-GUIDE.md); it points to the code, tests, decision and commit.
2. `git show <sha> --stat` shows which files that step changed.
3. Each decision lists its evidence file; run the named test to see it hold.
