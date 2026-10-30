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
- Built a post-trade analytics pipeline in Python that decomposes PnL into spread capture (+188.94), inventory (-151.95) and fees (-31.58), reconciling to total PnL of +5.42 on a 15,895-fill session; identity enforced by a unit test.
- Computed quantity-weighted markouts from 0s to 60s horizons (0s: +0.598 bps decaying to +0.021 bps at 60s) to separate captured spread from adverse selection.
- Reported risk metrics (max drawdown 31.27, VaR/ES at 95% and 99%) and flagged a Sharpe of 2.62 as not significant (t-stat 0.16).
- Wrote a C++ markout kernel with a Python parity test, [X]x faster than the Python version on [N] fills.

### Project 2 (planned): Real-time risk and strategy monitoring dashboard
- Built a real-time monitoring dashboard tracking [list metrics, e.g. PnL, inventory, drawdown, fill rate] across [N] strategies with [X] ms end-to-end update latency.
- Implemented [N] alert rules (e.g. inventory limit, drawdown, stale data) that fired on [X] of [N] injected faults in testing.
- Tested with [N] unit/integration tests; runs from one command (`[command]`).

### Project 3 (planned): Multi-exchange market-data normaliser and order-book rebuild (C++)
- Built a C++ normaliser for [N] exchange feed formats into one schema, rebuilding the order book at [X] million messages/second on [hardware].
- Validated rebuilt books against [reference/snapshots] with [X] mismatches in [N] messages; handled gaps and out-of-order packets via [method].
- Profiled and reduced per-message latency from [X] ns to [Y] ns by [change].

### Project 4 (planned): Event-driven market-making backtester
- Built an event-driven backtester in [language] simulating a market maker with queue-position and fill-model assumptions on [N] days of [data source].
- Reproduced [X] expected effect (e.g. adverse selection visible in markouts) and quantified sensitivity of PnL to fill-model assumption: [X]% change in PnL.
- Ran [N] unit tests including a PnL identity check; results reproducible with fixed seed [X].

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
