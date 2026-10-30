# 16-week roadmap: Quant Analyst portfolio

**Target:** Quant Analyst roles, using Graviton Research Capital's QA / QA-Risk postings as the reference job description (see [firm-landscape.md](firm-landscape.md)).
**Profile:** 1-4 years in finance or product. Budget is 10-15 hrs/week, roughly 16 weeks.
**Split:** about 8-11 hrs/week on projects and about 4 hrs/week on interview prep, every week.

## 1. Why these projects: scoring matrix

The reference JD describes the Quant Analyst job like this: review production trade data, build monitoring tools and post-trade analytics, write "scalable scripts in C++ and Python for risk attribution analysis, performance breakdowns, and strategy evaluation", and build tools to analyse market data across exchanges. Each project gets a 1-5 score on each criterion below. A project needs a weighted score of **at least 3.5** to go on the roadmap.

| Criterion | Weight | Question |
|---|---|---|
| JD fit | 35% | Does it look like a day of the reference job? |
| Interview signal | 25% | Will it set the candidate apart and give interviewers something to probe? |
| Feasibility | 20% | Can someone from finance/product finish it inside the time box? |
| Data access | 10% | Is usable data available offline or for free? |
| Reuse | 10% | Do later projects build on it? |

| # | Project | JD fit | Signal | Feasible | Data | Reuse | **Score** |
|---|---|---|---|---|---|---|---|
| 1 | Post-trade analytics & PnL/risk attribution engine (Python + C++ kernel) | 5 | 5 | 4 | 5 | 5 | **4.80** |
| 2 | Real-time risk & strategy monitoring dashboard | 5 | 4 | 4 | 4 | 3 | **4.25** |
| 3 | Multi-exchange market-data normalizer + order-book rebuild (C++) | 5 | 4 | 3 | 4 | 4 | **4.15** |
| 4 | Event-driven market-making backtester (latency, queue position) | 4 | 5 | 3 | 4 | 4 | **4.05** |
| 5 | Order-flow-imbalance alpha study with honest validation | 3 | 4 | 3 | 4 | 3 | **3.35**, below threshold (optional stretch) |

**Corrections to the original plan.** I recomputed every row as score = 0.35·JD + 0.25·Signal + 0.20·Feasible + 0.10·Data + 0.10·Reuse. Two rows in the plan were wrong:
- **Project 1:** 1.75 + 1.25 + 0.80 + 0.50 + 0.50 = **4.80**. The plan said 4.85.
- **Project 5:** 1.05 + 1.00 + 0.60 + 0.40 + 0.30 = **3.35**. The plan said 3.40.

Neither correction changes the ranking or which projects pass. Rows 2-4 were already correct.

The scores are judgement calls, not measurements. The weights matter more than the decimals. If you are aiming at Quant Researcher roles instead, raise the weight on interview signal for research skills, and Project 5 moves up.

## 2. Timeline at a glance

| Weeks | Project | Main language | Builds on |
|---|---|---|---|
| 1-5 | P1 Post-trade analytics engine (**built**) | Python + small C++ kernel | - |
| 6-8 | P2 Risk & strategy monitoring dashboard | Python | P1 outputs (fills, attribution, risk) |
| 9-12 | P3 Market-data normalizer + order-book rebuild | C++17 | P1's quote/fill schema |
| 13-15 | P4 Market-making backtester (latency, queues) | C++ core, Python analysis | P3 books, P1 analytics |
| 16 | Polish, write-ups, applications | - | all |
| any | P5 OFI alpha study (stretch, only if ahead) | Python | P3 data, P1 markouts |

## 3. Project 1: Post-trade analytics engine (weeks 1-5), status BUILT

Code: [`projects/01-post-trade-analytics`](../projects/01-post-trade-analytics). Sample output: [`examples/sample_report.md`](../projects/01-post-trade-analytics/examples/sample_report.md).

**What it is.** A pipeline driven by the CLI (`pta simulate | analyze | report`). It takes fills (`ts_us, side, px, qty, fee`) and quotes (`ts_us, bid, ask, mid`) and answers two questions: where did the PnL come from, and was the edge real?

| Module | What it does |
|---|---|
| `data/synthetic.py` | Seeded market on a 100 ms grid. Price is a jump-diffusion with a latent AR(1) order-flow signal that both moves the price and skews which side aggressors hit. This builds in adverse selection on purpose. |
| `strategy/market_maker.py` | A simple passive market maker that joins the touch, skews by inventory and pulls a side at a cap. It exists to produce fills, not to make money. |
| `pnl.py` | Attribution: total = spread capture (fill vs mid at fill) + inventory (mid at fill to final mid) − fees. A separate cash + position × mid equity curve checks it. |
| `markouts.py` | Quantity-weighted markouts at 0, 0.1, 1, 5, 30 and 60 s, split by buy and sell, with the adverse-selection decay measured against 0 s. |
| `risk.py` | Max drawdown, Sharpe and its t-statistic, Sortino, historical VaR/ES at 95/99%, and average/max absolute inventory, all on 1-minute bars. |
| `report.py` | Markdown report plus three charts (attribution, markouts, equity/inventory/drawdown). |
| `data/loaders.py` | Schema validation and `market_from_quotes`, a generic adapter that turns any bid/ask table into the market schema (ns/us/ms timestamps). |
| `cpp/markout_kernel.cpp` | C++17 two-pointer kernel, O(n+m), that computes the same attribution and markouts. |
| `tests/` | Hand-computed toy cases, an attribution-vs-equity identity check, and Python/C++ parity to 1e-9. |

**What it demonstrates against the JD.** Risk attribution and performance breakdown ("where did the PnL come from"), markouts as the standard check on edge versus adverse selection, Python and C++ code that agree with each other, and statistical honesty about results. For example, the sample report shows an annualised Sharpe of 2.62 next to a t-statistic of 0.16 and says plainly that the mean is not distinguishable from zero.

**Honest limitation.** All of the headline numbers come from **synthetic data generated by the project's own simulator**, so they say nothing about real markets. The project makes **no claim of real edge or of a profitable strategy**: the market maker is a fill generator. The real-data path (`market_from_quotes`, and `flow=None` replay) has not been run against a real feed in this repo. It also cannot model queue position, latency or real fill probability; that gap is what Project 4 is for.

**Remaining for weeks 4-5, if not yet done:**
- Run on one day of real public quote data locally and add a short "real vs synthetic" section.
- Write the project README (problem → method → results → limitations → next steps).
- Record a 2-minute walkthrough you can give out loud in an interview.

**Exit criteria (all must hold):**
- `pytest` is green, including the identity and C++ parity tests.
- The report regenerates from one seeded command.
- The README states the limitation above in its first screen.
- You can explain every number in the sample report without notes.

## 4. Project 2: Risk & strategy monitoring dashboard (weeks 6-8)

**Goal.** Build what a trader looks at during the session: live PnL by component, inventory against limits, markout drift, and alerts.

| Week | Milestone |
|---|---|
| 6 | Streaming replay of P1 fills and quotes at configurable speed. Incremental (O(1) per event) PnL, inventory and markout state that matches P1's batch numbers. |
| 7 | Limits engine (position, loss-from-peak, fill rate, markout degradation) with alert rules and an alert log. Config file for limits. |
| 8 | Simple UI (terminal or lightweight web) and an injected "bad session" scenario where you show which alerts fire and when. Write-up. |

**Exit criteria:**
- The incremental numbers equal P1's batch numbers at the end of the session (test).
- Every alert rule has a test that triggers it and one that doesn't.
- The demo scenario fires the right alerts in the right order.
- README with a screenshot and the alert-design reasoning (false positives vs missed breaches).

## 5. Project 3: Multi-exchange market-data normalizer + order-book rebuild in C++ (weeks 9-12)

**Goal.** Take raw feeds from two or more venues (public crypto feeds are the realistic free source), normalise them into one event schema, and rebuild L2 books.

| Week | Milestone |
|---|---|
| 9 | Common event schema (add/modify/delete/trade/snapshot, exchange and local timestamps). Parser for venue A. Golden-file tests. |
| 10 | L2 book with snapshot + incremental updates, sequence-gap detection and resync. Crossed/locked-book checks. |
| 11 | Venue B parser, cross-venue merged view, timestamp/latency stats between venues. |
| 12 | Benchmarks (events/sec, p50/p99 per-update latency), profiling notes, and an export into P1's quote schema. Write-up. |

**Exit criteria:**
- The rebuilt top of book matches the venue's own snapshots at checkpoints (test).
- Gaps are detected and recovered.
- Benchmarks are reproducible with a single command.
- P1 runs on its output.
- Builds with CMake on Linux with `-Wall -Wextra` and no warnings.

## 6. Project 4: Event-driven market-making backtester (weeks 13-15)

**Goal.** Replace P1's toy fill model with an event-driven simulator that has order latency and queue position, then show how much those assumptions change PnL.

| Week | Milestone |
|---|---|
| 13 | Event loop over P3 book events. Order entry/cancel with configurable latency. Queue-position model (FIFO, start at back of queue). |
| 14 | Plug in P1's market maker logic. Output fills in P1's schema, analyse with P1 and monitor with P2. |
| 15 | Sensitivity study: PnL and markouts against latency and queue assumptions. Write-up on how optimistic fill models inflate backtests. |

**Exit criteria:**
- Deterministic replays (same seed and inputs give identical fills).
- Unit tests for queue advancement and cancels in flight.
- A sensitivity table/chart.
- The write-up states plainly that a backtest is not live PnL. Senior HFT postings ask for a live track record (see firm-landscape), and no project substitutes for that.

## 7. Week 16: polish and applications

- Top-level README: one paragraph and one result per project, mapped to JD lines.
- One-page résumé with a bullet per project (what, how, quantified result, limitation).
- Applications and referral outreach. Rehearse each project walkthrough out loud (2 min and 10 min versions).

## 8. Project 5 (optional stretch): order-flow-imbalance alpha study

Score 3.35, below the 3.5 threshold, so it is **not** in the core plan. Do it only if you are two or more weeks ahead, or if you decide to pivot toward Quant Researcher roles. If you do it, the bar is honest validation: out-of-sample periods, transaction costs, multiple-testing awareness, and markouts from P1. A backtest that only looks good is not the deliverable.

## 9. Interview prep: about 4 hrs/week, all 16 weeks

| Weeks | Focus (rotate; keep mental maths daily) |
|---|---|
| 1-4 | Probability and expected value, conditional probability, combinatorics, mental maths drills |
| 5-8 | Game theory and proof-style problems (the format reported for Graviton's written test, see firm-landscape), statistics and regression |
| 9-12 | C++ (memory, STL, complexity), Linux/Bash, data structures and algorithms |
| 13-16 | Market microstructure, mixed timed mocks, project walkthroughs, behavioural ("why quant, from finance/product") |

Suggested split each week: about 1.5 h problems, 1 h mental maths, 1 h topic of the block, 0.5 h review of mistakes. See `docs/interview-prep.md` if present.

## 10. Decision rules

1. **Done means the exit criteria are met, not "feels finished".** Tests pass, the README states results *and* limitations, and you can explain it out loud.
2. **Time box beats scope.** At the end of a project's last week, ship whatever meets the exit criteria and move on. Leftover ideas go into a "next steps" section, not into extra weeks.
3. **Slip rule.** If a project is more than one week behind, cut scope in this order: UI polish, second venue/feature, benchmarks. Never cut tests or the limitations section.
4. **Two-slip rule.** If two projects slip, drop P4's sensitivity study to a smaller grid and protect week 16. Applications do not wait for a perfect portfolio.
5. **Prep is not optional.** If a week runs short, cut project hours, not the 4 hours of prep.
6. **Honesty rule.** No result from synthetic data or a backtest is described as real or live edge.
7. **Re-score triggers.** Re-run the scoring matrix if the target role changes (for example, to Quant Researcher) or if verified JD details differ from the secondhand ones in firm-landscape.md.
8. **Start applying early.** Begin outreach once P1 and P2 are done (around week 8). Interviews take time, and the portfolio can keep growing while they run.
