# Study guide: learn everything in this repo from one place

Start here. Everything is traceable: each concept links to a picture-first explainer, a short note, the code, the tests that prove it, the decision that shaped it, and the commit that introduced it.

## How to use it
1. **See it** in the explainer (a few minutes, mostly pictures).
2. **Read it** in the note (about 5 minutes, ends with interview questions).
3. **Run it** with the exercise below (copy-paste commands).
4. **Break it** on purpose and watch the test fail, so you know what the test protects.
5. **Explain it out loud** using [docs/interview-prep.md](../interview-prep.md) and, once written, `docs/walkthroughs.md`.

Everything here uses synthetic data. It teaches the method; none of it is evidence of a real trading edge.

## Learning path, in order

| Step | Topic | See it | Read it | Where the code is |
|---|---|---|---|---|
| 0 | Orientation: the job, the plan | [README](../../README.md), [roadmap](../roadmap.md), [firm landscape](../firm-landscape.md) | [decision log](../decision-log.md) | n/a |
| 1 | Project 1 big picture | [Project 1 explainer](https://claude.ai/artifact/9JtqoccdFaeneuCV5VFCgu) (source: `explainers/project-1.html`) | [Project 1 README](../../projects/01-post-trade-analytics/README.md) | `projects/01-post-trade-analytics/src/pta/` |
| 2 | Where PnL comes from | same | [attribution identity](attribution-identity.md) | `pnl.py`: `attribute`, `equity_curve`, `inventory_pnl_on_grid` |
| 3 | Was the edge real? | same | [markouts and adverse selection](markouts-and-adverse-selection.md) | `markouts.py`: `compute`, `summarize`; `cpp/markout_kernel.cpp` |
| 4 | Skill or luck | same | [t-statistic vs Sharpe](t-statistic-vs-sharpe.md) | `risk.py`: `summary`, `bar_pnl`, `max_drawdown`, `var_es` |
| 5 | Project 2 big picture | [Project 2 explainer](https://claude.ai/artifact/KxGTJVjza6dBftFdujEp7T) (source: `explainers/project-2.html`) | [Project 2 README](../../projects/02-risk-monitor/README.md) | `projects/02-risk-monitor/src/rmon/` |
| 6 | Live numbers equal end-of-day numbers | same | [incremental vs batch](incremental-vs-batch.md) | `state.py`: `MonitorState`; `replay.py` |
| 7 | A verdict that arrives later | same | [late markouts](late-markouts.md) | `markouts_stream.py`: `MarkoutTracker` |
| 8 | Alerts that do not flap | same | [hysteresis and cooldown](hysteresis-and-cooldown.md) | `alerts.py`, `engine.py`, `limits.py`, `config/limits.toml` |
| 9 | Project 3: market-data normalizer | [Project 3 explainer](https://claude.ai/artifact/FveVf5qjA11cXmogARD5jV) (source: `explainers/project-3.html`) | [Project 3 README](../../projects/03-market-data/README.md) (being written) | `projects/03-market-data/` (`include/md/`, `src/`, `DESIGN.md`) |
| 10 | Project 4: backtester | pending | pending | `projects/04-backtester/` (planned) |

## Trace map: concept to code, test, decision, commit

| Concept | Code | Test that proves it | Decision | Commit |
|---|---|---|---|---|
| Attribution identity | `pta/pnl.py` | `tests/test_pnl_identity.py` | D05 | `18c47b8` |
| Integer microsecond time | `pta/data/loaders.py` | `tests/test_loaders.py` | D06, D18 | `18c47b8`, `0ed03df` |
| As-of mid, dropped markouts | `pta/markouts.py` | `tests/test_markouts.py`, `tests/test_cpp_parity.py` | D07 | `18c47b8` |
| t-statistic beside Sharpe | `pta/risk.py` | sample report | D08 | `18c47b8` |
| Synthetic data, honesty | `pta/data/synthetic.py` | seeded runs | D04 | `18c47b8` |
| Incremental PnL equals batch | `rmon/state.py` | `tests/test_equivalence.py` | D10 | `77b1089` |
| Quote before fills at one timestamp | `rmon/replay.py` | `tests/test_state_edge_cases.py` | D11 | `77b1089` |
| Drawdown per timestamp | `rmon/state.py` | `tests/test_state_edge_cases.py` | D12 | `77b1089` |
| Four alert rules | `rmon/alerts.py` | `tests/test_alert_rules.py` | D14 | `96af4c2` |
| Cooldown policy (changed after review) | `rmon/alerts.py` | `tests/test_review_regressions.py` | D14b | `0867c1b` |
| Calibrated limits | `config/limits.toml` | `tests/test_scenario_demo.py` | D15 | `96af4c2` |
| Bad-session scenario | `rmon/scenarios.py` | `tests/test_scenario_demo.py` | D16 | `96af4c2` |
| Fixed-point prices and quantities | `include/md/fixed.hpp`, `src/fixed.cpp` | `tests/test_fixed.cpp` | D33 | `fad9aca`, `6a14fbc` |
| Gap detection and resync | `include/md/session.hpp`, `src/session.cpp` (DESIGN section 6) | `tests/test_session.cpp`, `tests/test_golden.cpp` | D30 | `6a14fbc` |
| L2 book invariants | `include/md/book.hpp` | `tests/test_book.cpp` | D30, D34 | `6a14fbc` |
| Cross-venue merge, export to Project 1 | `src/merge.cpp`, `src/export.cpp` | `tests/test_merge.cpp`, `tests/test_export.cpp`, `python/test_pta_integration.py` | D31, D32 | `8f9ee06` |
| Benchmark method | `bench/run_bench.sh`, `src/bench.cpp` | `make bench` | D35 | `8f9ee06` |
| Config validation | `rmon/limits.py` | `tests/test_config.py` | D17 | `96af4c2`, `0867c1b` |
| Live dashboard, localhost only | `rmon/server.py`, `ui/index.html` | `tests/test_server.py` | D09, D20 | `0867c1b` |

For any row: `git show <commit> --stat` lists the files, and `pytest <test file> -v` runs the proof.

## Exercises (each was run and gives the stated result)

Setup once from the repo root: `make install`.

**1. Run Project 1 end to end.**
```
cd projects/01-post-trade-analytics
pta simulate --seed 7 --out out
pta report --fills out/fills.csv --market out/market.csv --out out
```
Open `out/report.md`. You should see spread capture +188.94, inventory -151.95, fees -31.58, total +5.42, and a t-statistic of 0.16. Can you explain each number?

**2. Break the identity on purpose, then undo it.** In `src/pta/pnl.py` change `... - totals["fees"]` to `... + totals["fees"]` in `attribute`, then run `pytest tests/test_pnl_identity.py`. Five tests fail. That is what the identity test protects. Revert with `git checkout src/pta/pnl.py`.

**3. Make the market maker more adversely selected.** Run this and compare to the baseline (0.598, 0.371, 0.111 bps at 0 s, 1 s, 5 s):
```
python3 - <<'EOF'
from dataclasses import replace
from pta.data.synthetic import MarketConfig, simulate_market
from pta.strategy.market_maker import simulate_market_maker
from pta import markouts
m, f = simulate_market(replace(MarketConfig(), alpha_bps_per_s=3.0))
fills = simulate_market_maker(m, f)
s = markouts.summarize(fills, markouts.compute(fills, m)).set_index("horizon_us")
print([round(float(s.loc[h, "avg_bps"]), 3) for h in (0, 1_000_000, 5_000_000)])
EOF
```
With the flow signal five times stronger, the 1 s markout goes from +0.371 to -0.501 bps and the 5 s markout to -1.974. The spread earned is unchanged (0.604) while the damage after the fill is much larger.

**4. Watch the monitor catch a bad day.**
```
cd projects/02-risk-monitor
rmon demo --scenario bad --out out/bad      # 9 alerts, each in the phase that causes it
rmon demo --scenario clean --out out/clean  # no alerts
rmon serve --scenario bad --speed 600       # then open http://127.0.0.1:8765
```

**5. Tighten a limit and see false alarms appear.** In Python, load the default limits, set `position.warn` to 30 and `position.hysteresis` to 10, and replay the clean 2-hour scenario (seed 7). You get two position warns (at 60.8 s and 974.4 s) that the default limits never raise. That is the false-positive side of the calibration trade-off in the Project 2 README.

## Glossary (plain phrase first, real term in bold)

| Plain phrase | Term | Meaning |
|---|---|---|
| The halfway price between buy and sell | **mid** | (best bid + best ask) / 2 |
| The gap between buy and sell prices | **spread** | ask minus bid |
| Edge against the mid at the moment of a trade | **spread capture** | side x qty x (mid at fill - fill price) |
| Stock the booth is sitting on | **inventory / position** | signed quantity held |
| Profit or loss from price moving while holding stock | **inventory PnL** | side x qty x (later mid - mid at fill) |
| A trade's later verdict | **markout** | side x (mid at fill time + h - fill price) |
| Customers who know a move is coming | **adverse selection** | fills that precede moves against you |
| One hundredth of a percent | **basis point (bps)** | 0.01% |
| How many standard errors the average is from zero | **t-statistic** | mean / std x sqrt(n) |
| Return per unit of risk, scaled to a year | **Sharpe ratio** | mean / std x sqrt(periods per year) |
| Fall from the best total so far | **drawdown** | running peak minus equity |
| Worst-case loss at a confidence level | **VaR / expected shortfall** | quantile and tail mean of bar PnL |
| A reset gap so an alarm does not flicker | **hysteresis** | clears at a different level than it fires |
| Quiet time before an alarm may repeat | **cooldown** | minimum gap between same-or-lower alerts |
| Running tally vs adding everything at close | **incremental vs batch** | same answer, computed differently |
| Price ticks in a feed | **quotes (L1)** | best bid and ask over time |
| Trades you made | **fills** | side, price, quantity, fee |
| Seeded random data made by a program | **synthetic data** | reproducible, not real |

## Self-test (answer out loud, then check the note)
1. State the attribution identity and name the two independent ways the tests verify it.
2. Why is a markout past the last quote dropped instead of extrapolated?
3. The annualised Sharpe is 2.62 and the t-statistic is 0.16. Why do both come from the same data, and which do you trust?
4. Why must the live monitor equal the batch report at the end of the session, and which three conventions make that true?
5. A warn alert fired, cleared, then a breach appeared 9 s later. What should happen, and what was the bug before the review?
6. Name two limits of the thresholds in `config/limits.toml`.

## Keeping this guide current
When a project or decision is added: append to the [decision log](../decision-log.md), add a row to the [work log](../work-log.md), and update the path and trace map above. Projects 3 and 4 rows are filled in when they are built.
