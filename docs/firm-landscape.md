# Firm landscape: what each firm does, hires for and tests

**How to read this.** Every source here is a search-result summary. Direct fetches of company sites were blocked in the sandbox where this was written, so every factual claim carries a tag:
- **[secondhand]**: summarised from a job posting or a firm page as it appeared in search results.
- **[unverified]**: from third-party prep sites or individual anecdotes, which may be unreliable or out of date.

"Not found" means there was no evidence either way. Nothing here (salaries, headcounts, round counts) goes beyond the evidence. Check everything against the live pages before you rely on it (see the last section).

## Summary

| Firm | Relevant role signal | What they test (reported) | Fit for this profile |
|---|---|---|---|
| Graviton | QA / QA-Risk: post-trade analytics, risk attribution, monitoring, C++/Python [secondhand] | Probability and game-theory written test, then technical rounds [unverified] | **Primary target.** The duties map almost one-to-one onto Projects 1-3. |
| Jane Street | Curiosity, collaboration, reasoning under uncertainty [secondhand] | Statistics, probability, mathematical reasoning; not finance knowledge [secondhand] | Reach. Prep transfers; projects matter less than reasoning. |
| Optiver | Graduate QR open to Maths/Stats/Physics/CS/Econometrics; Python preferred [secondhand] | Not found | Graduate-track posting; lateral fit not found. |
| Citadel / Citadel Securities | Master's/PhD common; C++, Python, probability, stats, ML, time series [secondhand] | Not found | Reach. Degree expectations are a gap. |
| Tower Research | Campus intern route; technical test [unverified] | Technical test (content not found) [unverified] | Lateral route not found. |
| Quadeye | Intern projects needed only basic finance [unverified] | Not found | Lateral route not found. |

## Graviton Research Capital

**What it does.**
- QA and QA-Risk roles in Gurugram work with senior traders on automated trading strategies and develop or improve HFT strategies [secondhand].
- Day to day, the role reviews production trade data and builds monitoring tools and post-trade analytics [secondhand].
- It writes "scalable scripts in C++ and Python for risk attribution analysis, performance breakdowns, and strategy evaluation" and builds tools to analyse market data across exchanges [secondhand].
- Quant Researcher roles centre on finding and explaining trading edges with ML and statistics, "not just a backtest that looks good" [secondhand, job-board snippets].

**How it hires.**
- **Education:** a degree in an analytical field (Engineering, Maths, CS) from top universities [secondhand].
- **Skills:** basic C/C++ or scripting, Linux, strong quantitative reasoning and good communication [secondhand].
- **Experience:** asks vary between postings, 2-5 years or 2-4 years [secondhand]. The Risk track asks for 3+ years with Python and Shell/Bash [secondhand].
- **Channel:** Graviton reportedly hires overwhelmingly through IIT campus drives [unverified].
- **Stale listings:** one posting seen during research had been removed, so a listing may be out of date [secondhand].

**What it tests (all from third-party prep sites).**
- **Written test:** about an hour of proof-style probability and game-theory problems, followed by technical rounds the same night [unverified].
- **Tracks:** software and quant-risk are separate [unverified].
- **Software track:** the online assessment covers Python, Linux and data processing; developer rounds involve hard DSA plus C++ and operating systems [unverified].
- **Difficulty:** about 3.8/5 on Glassdoor [unverified].
- **Sample question:** risk attribution on an HFT strategy [unverified].

**Implications for this user.**
- The QA duties are what Projects 1 (attribution, markouts), 2 (monitoring) and 3 (cross-exchange market data) are built to show.
- The experience floor (2-5 years) roughly overlaps the user's 1-4 years. Finance or product experience is an asset only if it is framed as trader-facing analytics.
- If campus hiring dominates [unverified], the lateral QA route is the realistic way in, and a referral matters.
- Proof-style probability and game theory need dedicated prep (roadmap weeks 5-8).
- The "top universities" requirement [secondhand] may be a filter you cannot change; the portfolio is the counterweight.

## Jane Street

**What it does.** Not found in the evidence gathered. Check the firm's site.

**How it hires.** It looks for intellectual curiosity, collaboration and comfort with uncertainty [secondhand]. Interviewers care about your reasoning and how you recover from mistakes [secondhand].

**What it tests.**
- Research interviews lean on statistics, probability and mathematical reasoning [secondhand].
- The firm's own guidance says it does not test finance or economics knowledge [secondhand, cited indirectly].

**Implications.**
- The portfolio carries less weight here than thinking out loud on probability problems.
- The roadmap's 4 hrs/week of prep is the part that transfers.
- A finance background is not an advantage in the interview itself.

## Optiver

**What it does.** Graduate QR roles apply statistical models and ML to trading [secondhand].

**How it hires.** Open to Maths, Statistics, Physics, CS and Econometrics backgrounds. Any programming language is accepted, preferably Python [secondhand]. The evidence is a graduate posting; lateral or Quant Analyst-equivalent roles were not found.

**What it tests.** Not found.

**Implications.** Python projects with honest statistics (Project 1's t-statistic discipline, Project 5 if done) fit the stated skill set. Whether a lateral profile is eligible is not found.

## Citadel / Citadel Securities

**What it does.** Not found beyond the role description below.

**How it hires.**
- A master's or PhD in a quantitative field is common [secondhand].
- Top skills: C++ and Python, plus probability, statistics, ML and time series [secondhand].
- It looks for demonstrated interest in markets and for independent research [secondhand].
- The evidence comes from a post-doctorate QR posting, so it may overstate what non-research roles require.

**What it tests.** Not found.

**Implications.**
- Degree expectations are a likely gap for this profile.
- The C++ projects (3 and 4) and an independent research write-up are the most relevant signals.
- Treat Citadel as a reach.

## Tower Research Capital

**What it does.** Not found.

**How it hires.** One IIT Kanpur intern account says resume selection was mostly CPI (grade point average) plus branch, and that Tower had a technical test [unverified, single anecdote, 2022].

**What it tests.** A technical test; content not found [unverified].

**Implications.** The evidence covers campus internships only. Lateral Quant Analyst hiring was not found. Do not plan around Tower.

## Quadeye

**What it does.** Not found.

**How it hires.** A Quadeye intern wrote that the projects needed only basic finance knowledge [unverified, single anecdote].

**What it tests.** Not found.

**Implications.** Weak evidence. It suggests that deep finance knowledge is not the bar there, but the lateral route was not found.

## Cross-firm takeaways

- **Senior market-making roles are out of reach for now.** Senior HFT market-making researcher postings ask for 2+ years of live HFT experience with a strong-Sharpe PnL record [secondhand]. Projects, backtests included, do not substitute at that level. Target analyst-level roles.
- **No source says what a portfolio should contain.** Portfolio advice found online was generic, and none of the sources gave firm-specific project requirements [secondhand]. The project choices in [roadmap.md](roadmap.md) are therefore inferred from job duties, not stated by any firm.
- **Probability and statistics come up everywhere.** They appear in the reported tests or requirements of Graviton, Jane Street, Optiver and Citadel [secondhand/unverified]. This is why prep runs all 16 weeks.
- **C++ helps where it is asked for.** It appears for Graviton and Citadel [secondhand], which justifies the C++ in Projects 1, 3 and 4.

## Sources

| Source | Used for |
|---|---|
| https://job-boards.greenhouse.io/gravitonresearchcapital/jobs/7835859002 | Graviton QA duties and requirements |
| https://job-boards.greenhouse.io/gravitonresearchcapital/jobs/6947471002 | Graviton QA duties and requirements |
| https://builtin.com/job/quant-analyst-risk/6377232 | Graviton QA-Risk requirements |
| https://www.tradermath.org/articles/graviton-research-capital-interview-guide | Graviton interview process (third party) |
| https://quantvault.org/graviton-research-capital-interview-questions.html | Graviton interview process (third party) |
| https://dataford.io/interview-guides/llp/quantitative-analyst | Graviton interview process (third party) |
| https://www.janestreet.com/join-jane-street/interviewing/ | Jane Street guidance (cited indirectly) |
| https://www.gradcracker.com/hub/820/jane-street/additional-content/2615/interview-process | Jane Street process |
| https://quantvault.org/jane-street-interview-guide.html | Jane Street process (third party) |
| https://job-boards.greenhouse.io/optiverneurips/jobs/8223343002 | Optiver graduate QR |
| https://builtin.com/job/global-quantitative-strategies-post-doctorate-quantitative-researcher/7936528 | Citadel requirements |
| https://spo.iitk.ac.in/insights/2022-intern-antreev-singh-brar-tower-research-capital | Tower intern anecdote |
| https://summerblog.insightiitb.org/?p=59487 | Quadeye intern anecdote |

## How to verify

1. Open each Greenhouse/BuiltIn link and confirm the posting is still live. Re-read the duties, the experience range and the location, and record the date you checked.
2. Check Graviton's own careers page for current QA / QA-Risk openings and whether lateral hiring is stated.
3. Read Jane Street's interviewing page directly to confirm the "no finance knowledge tested" guidance.
4. Treat the prep-site interview formats as hypotheses. Confirm them with one or two people who interviewed recently (LinkedIn, alumni networks), ideally lateral hires.
5. For Optiver, Citadel, Tower and Quadeye, look for current analyst-level (not graduate or post-doc) postings in your location before adding them to the target list.
6. Once a claim is confirmed, change its tag from [secondhand]/[unverified] to [verified, YYYY-MM-DD].
