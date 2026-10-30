# Quantitative Analyst projects

A portfolio built to get hired as a **Quant Analyst** at a systematic trading firm.
The reference job description is Graviton Research Capital's Quant Analyst / Quant
Analyst-Risk role: post-trade analytics, risk attribution, performance breakdowns and
monitoring tools, written in C++ and Python.

Each project is a piece of that job done end to end, with tests and a written result,
rather than a generic "predict the stock price" notebook.

## Projects

| # | Project | Status | What it shows |
|---|---|---|---|
| 1 | [Post-trade analytics engine](projects/01-post-trade-analytics) | Built | PnL attribution, markouts, risk stats, C++ kernel with a Python parity test |
| 2 | Real-time risk and strategy monitoring dashboard | Planned (weeks 6-8) | Limits, alerts, live monitoring on Project 1's outputs |
| 3 | Multi-exchange market-data normalizer and order-book rebuild (C++) | Planned (weeks 9-12) | Market-data tooling across venues |
| 4 | Event-driven market-making backtester | Planned (weeks 13-15) | Latency, queue position, honest strategy evaluation |

## Docs

- [Roadmap](docs/roadmap.md): 16-week plan, project scoring, milestones and exit criteria
- [Firm landscape](docs/firm-landscape.md): what Graviton, Jane Street, Optiver, Citadel, Tower and Quadeye do, and how they hire
- [Interview prep](docs/interview-prep.md): topics, drills and sample questions
- [Applications](docs/applications.md): target firms, resume bullets, outreach
- [Agent workflow](docs/agent-workflow.md): how work is routed between models

## Read this before relying on the research

The firm and job-description details come from web-search summaries and third-party
prep sites. Direct fetches of the company sites were blocked in the environment that
built this, so none of it was read from the primary pages. The docs tag claims as
secondhand or unverified. Check the live careers pages before you apply.

Project 1 runs on synthetic data, so it demonstrates tooling and method, not a
trading edge.
