# Agent-driven development: how work is routed

Projects 2-4 are built the same way as Project 1: one orchestrator breaks the work
into tasks and sends each to the cheapest model that can do it reliably. Reviews
are done by a model that did not write the code.

| Tier | Model | Use it for | Examples in this repo |
|---|---|---|---|
| High reasoning | Opus | Decisions that are expensive to reverse, ambiguous trade-offs, and independent review of correctness | Scoring and sequencing the projects, the firm-landscape synthesis, correctness review of the PnL and markout code |
| Standard | Sonnet | Well-specified writing and implementation where the design is already settled | Interview-prep drills, application templates, new module code from an agreed spec |
| Light | Haiku | Mechanical, low-risk work with a clear source of truth | README assembled from the sample report, link and formatting checks, file listings |

## Rules

1. **Decide before delegating.** A task goes to Sonnet or Haiku only once the design
   choice is made. If the task contains a judgement call, it goes up a tier.
2. **Brief like a peer.** Each agent gets the goal, the facts it may use, the files to
   read, the single file it may write, and a word limit for the report.
3. **No invented facts.** Agents may only use evidence in the brief or in the repo.
   Anything from the web is tagged secondhand or unverified.
4. **Independent review.** Code is reviewed by an agent that was not told what the
   author concluded, and every reported bug needs a reproduction.
5. **Orchestrator owns git.** Agents write files; commits, pushes and PRs happen in
   one place.
6. **Tests are the contract.** The PnL identity and the Python/C++ parity tests must
   pass before any result is quoted.
