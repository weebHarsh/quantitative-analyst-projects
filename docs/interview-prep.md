# Interview prep: Quant Analyst track

Budget: about 4 hrs/week of the 10-15 hrs/week total, for 16 weeks (about 64 hrs).

**Caveat on formats.** What follows about interview formats comes from third-party prep sites. It is secondhand and unverified. Reported shape: a ~1h written test of proof-style probability and game-theory problems, then technical rounds. Software-leaning tracks reportedly test DSA, C++, OS, Python, Linux and data processing. Jane Street-style rounds reportedly test reasoning out loud, probability and recovering from mistakes, not finance knowledge. Confirm the actual process with the recruiter for each firm.

## 1. Topic checklist and drills

Tick a topic only when you can do the drill cold, on paper, without notes.

### Probability and expected value
- [ ] Linearity of expectation, indicator variables, tower property.
- [ ] Geometric waiting times; waiting for patterns (HH vs HT); first-step analysis (set up E = 1 + sum p_i E_i).
- [ ] Gambler's ruin, symmetric random walk, reflection principle.
- [ ] Optimal stopping with a reroll.
- Drill: solve 5 problems/week from Fifty Challenging Problems; for each, write a 3-line proof, not just a number. Re-solve any miss one week later.

### Conditional probability and Bayes
- [ ] Bayes with base rates; "at least one" vs "this specific one" conditioning.
- [ ] Law of total probability; independence vs conditional independence.
- Drill: take any problem and restate it as a table of 10,000 people or a list of equally likely outcomes, then count. Always do this to check a formula answer.

### Game theory
- [ ] Backward induction on take-away / Nim-style games (find the losing positions).
- [ ] 2x2 zero-sum mixed equilibrium via indifference.
- [ ] Second-price auction (truthful bidding), "guess 2/3 of the average" (iterated dominance).
- Drill: 2 games/week; name the losing positions or the equilibrium mix, then state why a deviation does not help.

### Mental maths
- [ ] Multiplication of 2-digit by 2-digit, percentages, fractions to decimals (1/7 = 0.142857), squares to 30.
- [ ] Quick estimation of sqrt, log, exp (ln 2 ~ 0.693, e^0.1 ~ 1.105).
- Drill: Zetamac, 10 minutes, 4 days/week (counts toward the 4 hrs). Record your score weekly and raise the difficulty ranges once the score plateaus.

### Statistics and regression
- [ ] OLS slope = corr * sd_y / sd_x; R^2 = corr^2 (one regressor); what omitted variables and collinearity do to coefficients.
- [ ] Standard error, t-stat, confidence intervals, multiple testing, overfitting.
- [ ] Bias-variance; why a Sharpe from a few hours of data means little (your own Project 1 report shows this: Sharpe 2.62 annualised, t-stat 0.16).
- Drill: derive OLS (matrix form) from scratch once a month; each week explain one statistic aloud in 60 seconds.

### Time series basics
- [ ] Stationarity, autocorrelation, AR(1): Var = sigma^2/(1-phi^2), autocorr at lag k = phi^k.
- [ ] Random walk vs mean reversion, spurious regression, look-ahead bias.
- Drill: simulate an AR(1) in Python, compare sample vs theoretical autocorrelation; repeat with phi near 1.

### C++ (memory, STL, complexity)
- [ ] Stack vs heap, RAII, smart pointers, rule of 0/3/5, move semantics, dangling references.
- [ ] vector growth/reallocation and iterator invalidation; map vs unordered_map; cache locality and why contiguous arrays win.
- [ ] Big-O of common operations; sorting, binary search, heaps, two pointers, hash maps.
- Drill: weekly, write one small program from a blank file in 30 minutes (e.g. a ring buffer, an LRU cache, a streaming mean/variance). Read your own `cpp/markout_kernel.cpp` and be ready to explain every line, its complexity and why the Python parity test exists.

### Linux / Bash
- [ ] grep/awk/sed/sort/uniq/cut/xargs pipelines; file descriptors and redirection; pipes.
- [ ] ps, top, lsof, strace basics; permissions; cron; git.
- Drill: take a CSV (use `out/fills.csv` from Project 1) and answer 5 questions with one-line pipelines (fills per minute, top-5 largest fills, buy/sell counts).

### Market microstructure
- [ ] Bid-ask spread; mid, microprice; who earns the spread and why.
- [ ] Adverse selection: informed flow picks you off; show it with markouts (Project 1: 0s markout +0.598 bps decays to +0.111 bps at 5s and +0.021 bps at 60s).
- [ ] Queue position and priority (price-time); cost of being at the back; cancellations.
- [ ] Inventory risk: the same sample report shows spread capture of +188.94 offset by inventory PnL of -151.95. Be able to say why.
- Drill: explain in 2 minutes, aloud, "why does a market maker widen the spread when volatility rises?" Then do the same for queue position.

## 2. 16-week cadence (about 4 hrs/week)

| Weeks | Focus | Weekly split (hrs) |
|---|---|---|
| 1-2 | Baseline: take a timed 1h mixed problem set to find weak areas; set up Zetamac routine | 2 probability, 1 mental maths, 1 review |
| 3-5 | Probability + Bayes depth (Green Book, Fifty Challenging Problems) | 2 probability, 1 mental maths, 1 C++ |
| 6-7 | Game theory + stats/regression | 1.5 game theory, 1.5 stats, 0.5 mental maths, 0.5 C++ |
| 8-9 | C++ and Linux (memory, STL, complexity, pipelines) | 2 C++, 1 Linux, 0.5 mental maths, 0.5 probability |
| 10 | Time series + microstructure (link to your projects) | 1.5 time series, 1.5 microstructure, 1 mock |
| 11-12 | Mixed timed written tests (1h, closed notes) and mark them | 1 timed test, 1 review, 1 weak-area, 1 aloud-practice |
| 13-14 | Mock interviews: solve aloud with a friend or recorded; practise recovering from a wrong step | 2 mock, 1 review, 1 C++/coding |
| 15 | Firm-specific prep: re-read each JD, align stories to projects | 2 projects talk-through, 2 weak areas |
| 16 | Light: timed test, rest, project walk-through, logistics | 1 test, 1 walkthrough, 2 buffer |

Rules: log every miss in a mistakes file with the correct approach; re-do each miss after 7 days. Applications start around week 8 so that interviews arrive when you are warmed up; see `docs/applications.md`.

## 3. Worked sample questions

Each answer was checked by calculation (exact fractions or simulation).

**Q1. Two sixes.** A fair die is rolled repeatedly. Expected number of rolls until two 6s in a row?
**Answer: 42.** Let E0 = expected rolls from scratch, E1 = after one 6. E1 = 1 + (5/6)E0 (a 6 ends it, otherwise reset). E0 = 1 + (1/6)E1 + (5/6)E0. Substitute: E0/6 = 1 + (1/6)(1 + 5E0/6) so E0/36 = 7/6, E0 = 42.

**Q2. Positive test.** Prevalence 1%, sensitivity 95%, false-positive rate 5%. P(disease | positive)?
**Answer: about 16.1%.** True positives 0.95 x 0.01 = 0.0095. False positives 0.05 x 0.99 = 0.0495. P = 0.0095 / (0.0095 + 0.0495) = 0.0095 / 0.059 = 0.161.

**Q3. Two children.** You learn a family with two children has at least one boy. P(both boys)? What if you learn the older child is a boy?
**Answer: 1/3, then 1/2.** Equally likely ordered outcomes BB, BG, GB, GG. "At least one boy" leaves BB, BG, GB, so 1/3. "Older is a boy" leaves BB, BG, so 1/2.

**Q4. Take-away game.** 21 stones, players alternately take 1, 2 or 3; whoever takes the last stone wins. Do you move first or second, and what is the strategy?
**Answer: move first, take 1.** Positions that are multiples of 4 (0, 4, 8, ...) are losing for the player to move: from 4k any take of 1-3 leaves 4k-1 to 4k-3, and the opponent takes the complement to 4. 21 = 4x5 + 1, so take 1 to leave 20, then always restore a multiple of 4.

**Q5. Mixed strategy.** Zero-sum game, row payoffs: (A,A)=2, (A,B)=-1, (B,A)=-1, (B,B)=1. Find the row mix and the value.
**Answer: play A with prob 2/5; value = 1/5.** Make the column indifferent: if row plays A with prob p, column A pays row 2p - (1-p) = 3p - 1, column B pays -p + (1-p) = 1 - 2p. Set 3p - 1 = 1 - 2p so p = 2/5. Value = 3(2/5) - 1 = 1/5 (check column B: 1 - 4/5 = 1/5).

**Q6. Reroll.** You roll a fair die and may keep the result as your payoff or roll once more (and must accept the second roll). Expected payoff under the best strategy?
**Answer: 4.25.** A second roll is worth 3.5, so keep 4, 5 or 6 and reroll 1-3. EV = (1/2)(5) + (1/2)(3.5) = 2.5 + 1.75 = 4.25.

**Q7. Gambler's ruin.** Start with 3, bet 1 at a time on a fair coin, stop at 0 or 10. P(reach 10)?
**Answer: 3/10.** In a fair game wealth is a martingale, so by optional stopping 3 = 10 * P(reach 10) + 0 * P(ruin), giving P = 0.3.

**Q8. Regression slope.** Corr(x, y) = 0.5, sd(x) = 2, sd(y) = 4. Slope of y on x, and R^2?
**Answer: slope 1, R^2 = 0.25.** Slope = rho * sd_y / sd_x = 0.5 x 4 / 2 = 1. With one regressor R^2 = rho^2 = 0.25.

**Q9. AR(1).** x_t = 0.8 x_{t-1} + e_t, e_t ~ iid mean 0, variance 1. Find Var(x_t) and the lag-2 autocorrelation.
**Answer: Var = 25/9 (about 2.78); lag-2 autocorrelation = 0.64.** Stationarity gives Var = Var(0.8 x) + 1 = 0.64 Var + 1, so Var = 1/0.36 = 25/9. Autocorrelation at lag k is phi^k, so 0.8^2 = 0.64.

**Q10. Markout.** You buy 1 lot at 100.00. Mid at the fill is 100.02; mid 1s later is 100.01. Give the 0s and 1s markouts in bps and the adverse selection.
**Answer: 2 bps, 1 bp, adverse selection 1 bp.** Buy markout = (mid_t - fill) / fill x 10,000. 0s: 0.02 / 100 = 2 bps (spread captured vs mid). 1s: 0.01 / 100 = 1 bp. The 1 bp drop (2 to 1) is adverse selection: price moved against the position after you traded.

**Bonus Q11. Pattern waiting.** Expected flips of a fair coin until HH, and until HT?
**Answer: 6 and 4.** Expected wait = sum of 2^k over each k where the pattern's first k flips equal its last k flips. HT overlaps only at k=2 (itself): 4. HH overlaps at k=1 and k=2: 2 + 4 = 6. Simulation gave about 6.01 and 4.00.

## 4. Standard resources (by title, search for them yourself)

- A Practical Guide to Quantitative Finance Interviews (the Green Book), Xinfeng Zhou
- Fifty Challenging Problems in Probability, Frederick Mosteller
- Heard on the Street: Quantitative Questions from Wall Street Job Interviews, Timothy Crack
- Zetamac arithmetic game (mental maths practice)
- Cracking the Coding Interview (DSA warm-up, if your target role is software-leaning)
- Effective Modern C++, Scott Meyers; A Tour of C++, Bjarne Stroustrup
- Introduction to Probability, Blitzstein and Hwang (for gaps)
- Trades, Quotes and Prices (Bouchaud et al.) or Algorithmic and High-Frequency Trading (Cartea, Jaimungal, Penalva) for microstructure depth

## 5. Telling your finance / PM background as a strength

You are not a pure-maths graduate; do not pretend to be. Make the other half of the job your edge.

- **You know what the number is for.** Post-trade analytics exists so a trader or risk manager can act. As a finance/PM person you have used these metrics to make decisions; say so with a concrete example.
- **Communication.** The JD asks for it. PM work is translating quantitative output for non-quant stakeholders. Practise a 60-second explanation of your markout chart to a non-technical person.
- **Scoping and prioritisation.** You can say what to build first and what to ignore. Project 1 shows it: attribution identity first (tested), markouts second, risk stats third, C++ kernel only for the hot loop.
- **Honesty about limits.** Your report flags the weak Sharpe (t-stat 0.16). Interviewers value someone who says "this is not significant" over someone who sells a headline number.
- **Close the gaps openly.** Say where you are building depth (probability drills, C++) and show the dated plan in this doc and the repo.
- **Prepare three stories**: a decision you improved with data, a time you found an error in your own analysis and fixed it, a time you explained a technical result to a non-technical audience. Each needs a number you can defend; fill in [X] from your real work.
