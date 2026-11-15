# Applications playbook

**Listings change.** Nothing here is a current opening. Check each firm's live careers page before applying, and read the actual JD. No salaries, openings or contacts are listed here on purpose.

## 1. Target firms (name and role type to search)

Search terms are titles to try on the firm's careers page and on LinkedIn/job boards; exact titles vary.

### India
| Firm | Search for |
|---|---|
| Graviton Research Capital (Gurugram) | Quant Analyst, Quant Analyst - Risk, Quant Researcher |
| Tower Research Capital (Gurugram) | Quantitative Analyst, Quantitative Researcher |
| Optiver (India presence unverified; check the careers page) | Quantitative Researcher / Trader, Graduate quant roles |
| Quadeye | Quantitative Analyst / Researcher |
| NK Securities | Quantitative Researcher |
| Alphagrep | Quantitative Researcher / Developer |
| Qube Research & Technologies (India office unverified) | Quantitative Researcher, Quantitative Developer |
| Citadel Securities / Millennium (India offices unverified) | Quantitative Analyst, Risk |
| Large Indian banks and brokers with quant/risk desks | Quantitative Analyst, Market Risk Analyst |

### Global
| Firm | Search for |
|---|---|
| Jane Street | Quantitative Trader, Quantitative Researcher |
| Citadel / Citadel Securities | Quantitative Researcher, Quantitative Analyst |
| Jump Trading | Quantitative Researcher, Quantitative Developer |
| Hudson River Trading | Algorithm Developer, Quantitative Researcher |
| Optiver / IMC Trading | Quantitative Trader, Quantitative Researcher |
| Two Sigma / D. E. Shaw | Quantitative Researcher, Quantitative Analyst |
| Millennium / Point72 | Quantitative Analyst, Risk Analyst |

Where you are in the funnel: start with firms whose JD mentions post-trade analytics, risk or tooling (closest to Project 1), then widen.

## 2. Resume bullets by project

Rule: the number must come from a run you can reproduce. Replace every [X] only after running the code. If you cannot fill a placeholder, delete the clause rather than guess.

### Project 1: Post-trade analytics (built)
Real numbers from `examples/sample_report.md` (simulated 6-hour session, 15,895 fills); say "simulated" on the resume.
- Built a post-trade analytics pipeline in Python that decomposes PnL into spread capture (+188.94), inventory (-151.95) and fees (-31.58), reconciling to total PnL of +5.42 on a simulated 15,895-fill session; identity enforced by a unit test against an independently computed equity curve.
- Computed quantity-weighted markouts from 0s to 60s horizons (0s: +0.598 bps decaying to +0.021 bps at 60s) to separate captured spread from adverse selection.
- Reported risk metrics (max drawdown 31.27, VaR/ES at 95% and 99%) and flagged a Sharpe of 2.62 as not significant (t-stat 0.16); 16 tests, synthetic data, no claim of edge.
- Wrote a C++17 markout kernel as an independent second implementation that agrees with the Python version to 1e-9; it is a cross-check, not an optimisation (it is slower end to end: about 123 ms against 11 ms in Python on 15,895 fills, dominated by CSV parsing; see D47). <!-- timing: docs/decision-log.md D47 -->

<!-- sources: examples/sample_report.md and projects/01-post-trade-analytics/README.md (attribution, markouts, risk numbers); docs/work-log.md (16 tests); docs/roadmap.md section 3 (parity to 1e-9) -->

### Project 2 (built): Real-time risk and strategy monitoring dashboard
- Built a real-time monitor in Python (standard-library web dashboard) tracking PnL by component, inventory, drawdown, fill rate and markouts, with incremental O(1)-per-event state that matches Project 1's batch numbers on all 72,000 equity ticks of an independent check on a fresh seed.
- Implemented 4 alert rules (position, loss from peak, fill rate, markout degradation) with hysteresis and cooldown; on a simulated 5-phase injected bad session they raised 9 alerts in the expected order, and a clean session raised none (thresholds calibrated to the synthetic simulator only).
- 123 tests; independent review found 5 alert-layer bugs, all fixed with regression tests; full engine processed about 110,000 events/s on a 6-hour synthetic session; one command (`rmon serve --scenario bad --speed 600`).

<!-- sources: projects/02-risk-monitor/README.md "How it was checked" and "What the demo shows"; docs/decision-log.md D10, D14b, D16, D17, D26 -->

### Project 3 (built): Multi-exchange market-data normaliser and order-book rebuild (C++)
- Built a C++17 normaliser for 2 synthetic venue feed formats (JSON-lines diffs and chained-id CSV) into one fixed-point schema (int64 ticks and lots, no floating point), rebuilding L2 books at 2.02 million events/s end to end (p50 71 ns, p99 158 ns per update) on one shared machine; benchmark reproducible with `make bench`.
- Detected sequence gaps, duplicates and out-of-order messages and resynced from snapshots; on a 15-minute faulted feed the rebuilt book equalled the venue checkpoints (0 mismatches) and each venue showed gaps=2, resyncs=3. Exported quotes that Project 1 analyses.
- 85 C++ tests pass in normal and ASan+UBSan builds with -Werror and zero warnings; an independent review found 3 defects (merge ordering, benchmark counters, signed overflow), all fixed with regression tests. Feeds are synthetic; no real venue was tested.

<!-- sources: projects/03-market-data/README.md "Results"; docs/decision-log.md D30, D34, D35, D43. Per-update latency includes about 28 ns timer cost; throughput varied about 30 percent between runs. -->

### Project 4 (built): Event-driven market-making backtester
- Built a deterministic event-driven backtester in C++17 simulating a passive market maker with order latency, cancels in flight and 3 queue-position models, driven by Project 3's book on a synthetic feed (3 seeds x 1 hour); same inputs give byte-identical fills.
- Ran a 96-cell sensitivity study (4 latencies x 4 queue models x cross-fill rule on/off x 3 seeds): the optimistic fill model produced about 2.0x the fills of the back-of-queue model with the cross-fill rule on and about 6.0x with it off (at 0 ms latency), and a total PnL gap of about +21 currency units over FIFO_BACK in both settings (synthetic, not live PnL).
- Found that the default cross-fill rule created about 76% of FIFO_BACK's fills and masked the latency effect (1 s markout +0.406 bps at 0 ms falling to +0.279 bps at 20 ms only with the rule off); reported both settings. 67 C++ tests in normal and sanitizer builds, 17 Python tests; independent review found 3 edge-case defects, all fixed.

<!-- sources: projects/04-backtester/results/sensitivity_table.md (sections 1.4, 1.5, 1.7, 2.3, 2.7), results/run_info.txt; docs/decision-log.md D44, D45, D46. Numbers are seed means of the synthetic study. -->

## 3. Referral and outreach

- **Prefer warm paths.** Ex-colleagues, alumni (college, MBA, employer), people you met at events. A short referral from an employee usually beats a cold application; ask only when you have something concrete to show.
- **Lead with the artefact.** Link the Project 1 README and one chart. Do not send a resume as the first message.
- **One specific ask.** "Would you be willing to refer me, or point me to the right person?" beats "any advice?".
- **Follow up once**, after about a week, then stop. Keep a log (name, date, status) so you do not double-contact people.
- **Apply through the portal as well**, mention the referral in the application if the form allows it.
- **Be honest** about your background and about what is simulated.
- Do not guess email addresses or contact people about roles you have not read.

### Cold-email template (under 120 words)

> Subject: Post-trade analytics project, quant analyst application
>
> Hi [Name],
>
> I'm a [current role] with [N] years in [finance/product], moving into quantitative analysis. I saw [firm]'s [role title] posting and built a small project in that area: a post-trade analytics pipeline (PnL attribution, markouts, risk stats) with a C++ markout kernel and tests: [GitHub link].
>
> Would you be open to a 15-minute chat about how your team uses post-trade analytics, or to pointing me to the right person to speak to?
>
> Thanks,
> [Your name]

(About 75 words including subject; recount after you edit.)

### LinkedIn connection note (under 300 characters)

> Hi [Name], I'm a [role] moving into quant analysis and built a post-trade analytics project (PnL attribution, markouts, C++ kernel): [link]. Would value a quick chat about how your team approaches this. Thanks, [Your name]

(About 220 characters with placeholders; recount after filling in names and link.)

## 4. Pre-application checklist

**GitHub README quality**
- [ ] First screen states what the project does, the headline result and one chart.
- [ ] Says clearly what is simulated or assumed.
- [ ] Install/run steps work on a clean machine in under [X] minutes.
- [ ] Results table matches what the code actually produces.
- [ ] Limitations section (e.g. Sharpe not significant).

**Reproducible runs**
- [ ] Pinned dependencies (`pyproject.toml`), fixed random seed.
- [ ] One command regenerates the report and charts.
- [ ] No absolute local paths or secrets; data is generated or documented.
- [ ] Fresh clone tested.

**Tests**
- [ ] `pytest` passes locally; the PnL identity and C++/Python parity tests are included.
- [ ] CI runs the tests (optional but visible).
- [ ] Edge cases tested (empty data, fills at session end with no markout).

**Application hygiene**
- [ ] Re-read the live JD and map each requirement to a project line or a gap.
- [ ] Resume is one page, every number traceable to a run.
- [ ] Resume bullet placeholders [X] all filled or removed.
- [ ] Prepared explanations of every line of C++ and every metric you list.
- [ ] Interview prep on schedule (see `docs/interview-prep.md`).
