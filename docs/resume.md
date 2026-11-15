# Resume template: Quant Analyst application (one page)

Fill every [PLACEHOLDER]. Nothing in square brackets is known to this repo. Every project number below is traceable to a file named in the source line under each bullet; delete the source lines before sending. Export to a one-page PDF.

---

**[YOUR NAME]**
[CITY, COUNTRY] | [PHONE] | [EMAIL] | [LINKEDIN URL] | [GITHUB URL]

## Summary

[YOUR EXPERIENCE: one clause, e.g. "N years in finance / product at COMPANY" - only what is true.] Built four portfolio projects on synthetic data: post-trade PnL attribution and markouts, a real-time risk monitor, a C++17 market-data normaliser with order-book rebuild, and a market-making backtester with a fill-model sensitivity study. Seeking a Quant Analyst role in post-trade analytics, risk and monitoring tooling.

## Projects (all on synthetic data; no live trading and no real-venue data)

**Post-trade analytics engine** (Python, C++17) - [GITHUB LINK]
- Built a PnL attribution (spread capture +188.94, inventory -151.95, fees -31.58) that reconciles to total PnL of +5.42 on a simulated 15,895-fill session, checked against an independent equity curve; 16 tests.
- Computed quantity-weighted markouts (+0.598 bps at 0 s decaying to +0.021 bps at 60 s) and flagged an annualised Sharpe of 2.62 as not significant (t-statistic 0.16): the tooling is the result, not an edge.
- Wrote a C++17 markout kernel with a Python parity test (1e-9); [X]x speedup on [N] fills not yet measured.
<!-- source: projects/01-post-trade-analytics/examples/sample_report.md and README.md; docs/work-log.md (16 tests); docs/roadmap.md section 3 (1e-9). Speedup: no measurement exists in the repo. -->

**Real-time risk and strategy monitor** (Python) - [GITHUB LINK]
- Built incremental O(1)-per-event PnL, drawdown and markout state that matches the Project 1 batch numbers on all 72,000 equity ticks of an independent check on a fresh seed.
- Added 4 alert rules with hysteresis and cooldown: 9 alerts in the expected order on an injected 5-phase bad session, none on the clean session; thresholds are calibrated to the simulator and need recalibrating on real flow.
- 123 tests; an independent review found 5 alert-layer bugs, all fixed with regression tests; about 110,000 events/s for the full engine.
<!-- source: projects/02-risk-monitor/README.md ("What the demo shows", "How it was checked"); docs/decision-log.md D26 -->

**Market-data normaliser and order-book rebuild** (C++17) - [GITHUB LINK]
- Normalised two modelled venue formats (JSON-lines diffs, chained-id CSV) into one fixed-point schema; detected gaps, duplicates and out-of-order messages and resynced from snapshots; rebuilt books matched venue checkpoints on a faulted 15-minute feed.
- One-command benchmark (`make bench`): 2.02 million events/s end to end, update latency p50 71 ns, p99 158 ns (one shared machine, about 30% run-to-run variation, includes about 28 ns timer cost).
- 85 C++ tests in normal and ASan+UBSan builds, zero warnings; an independent review found 3 defects (merge ordering, benchmark counters, signed overflow), all fixed. Synthetic feeds; no real venue tested.
<!-- source: projects/03-market-data/README.md "Results"; docs/decision-log.md D35, D43 -->

**Event-driven market-making backtester** (C++17, Python) - [GITHUB LINK]
- Built a deterministic backtester with order latency, cancels in flight and three queue-position models; ran a 96-cell study (4 latencies x 4 queue models x cross-fill rule on/off x 3 seeds).
- Showed optimistic fills inflate results: about 2.0x the fills of the back-of-queue model with the cross-fill rule on, about 6.0x with it off, and about +21 currency units of PnL over it in both settings (synthetic; not live PnL).
- Found the cross-fill rule created about 76% of back-of-queue fills and masked the latency effect (1 s markout +0.406 bps at 0 ms, +0.279 bps at 20 ms with it off); 67 C++ tests, 17 Python tests; review defects fixed.
<!-- source: projects/04-backtester/results/sensitivity_table.md (1.4, 1.5, 1.7, 2.3, 2.7); docs/decision-log.md D45, D46 -->

## Skills (what the repo demonstrates)

C++17, Python, CMake, AddressSanitizer / UBSan, unit and regression testing (golden files, parity tests), Linux and Make, statistics basics (t-statistic, VaR/ES, drawdown), market microstructure basics (spread, adverse selection, markouts, queue position).

## Experience

[YOUR EXPERIENCE: COMPANY, TITLE, DATES]
- [BULLET: a result you can defend with a number from your real work]
- [BULLET]

[ADD ROLES AS NEEDED]

## Education

[YOUR DEGREE, INSTITUTION, YEAR]

---

## What to verify before sending (delete this section)

- [ ] Every [PLACEHOLDER] is filled or its line is deleted. Nothing about your employer, degree or dates is in this repo.
- [ ] Re-run `make test-all` on a fresh clone; confirm the test counts (16, 123, 85, 67 C++ + 17 Python) still hold.
- [ ] Re-run `make bench` on your machine; replace the 2.02 million events/s and 71/158 ns figures with your own if they differ.
- [ ] Delete the C++ speedup clause unless you have timed it; do not guess [X] and [N].
- [ ] Every bullet still says synthetic. Do not describe any result as real, live or a trading edge (roadmap rule 6).
- [ ] Read the live Graviton job description; the wording used in this repo is secondhand (docs/firm-landscape.md).
- [ ] Everything fits one page; remove the source comments and this checklist.
- [ ] You can explain every number and every line of C++ listed, aloud, without notes.
