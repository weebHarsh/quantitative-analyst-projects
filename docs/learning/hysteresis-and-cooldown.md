# Alert hysteresis and cooldown

## What it is
Hysteresis means an alert clears at a different, safer level than the one that fired it; cooldown means a minimum quiet time before the same alert may fire again. Both stop a number hovering near a limit from flickering on and off.

## Why it matters in a quant analyst job
A risk desk that gets forty alerts for one problem stops reading alerts. Good thresholds are a design choice you defend with numbers, not a default.

## A worked example
From `projects/02-risk-monitor/examples/demo_bad.txt` (synthetic data) and `config/limits.toml`:
- Position: warn 60, breach 100, hysteresis 20. It warned at 61, breached at 101, and cleared at 37, which is below the recovery level 60 - 20 = 40. Cooldown is 60 s.
- Markout: floor -0.75 bps, hysteresis 0.5 bps, so it clears at -0.25 or better. The log shows the clear at -0.2261. Cooldown is 300 s.
- Fill rate: minimum 6 fills, hysteresis 4, so it clears once back to 10. The breach was 5 and the clear was 10.
- Loss from peak: breached at 200.7 and never cleared, because it needs drawdown below 100 - 25 = 75.

## Common mistake
Thinking cooldown delays an escalation. In `alerts.py` it does not: a higher severity fires immediately, and cooldown only damps repeats of the same or lower severity. Another error is a hysteresis band so wide the alert never clears, as with the loss rule here.

## Interview angle
- Q: Why not clear at the same threshold you fired at? A: A value of 59.9 then 60.1 then 59.9 would alert and clear repeatedly; the gap makes clearing mean real recovery.
- Q: How did you pick the limits? A: From the clean synthetic runs (clean max |position| 36, warn at 60), so they prove nothing about false-alarm rates on real flow.

## Where it lives in the code
- `projects/02-risk-monitor/src/rmon/alerts.py`: `_Rule.evaluate` (state machine and cooldown), `_TwoLevelRule.recovery`, `FillRateRule.recovery`, `MarkoutRule.recovery`.
- `projects/02-risk-monitor/config/limits.toml`: the `hysteresis` and `cooldown_us` keys.
- Tests: `projects/02-risk-monitor/tests/test_alert_rules.py`.

## Explainer
[Project 2 explainer](https://claude.ai/artifact/KxGTJVjza6dBftFdujEp7T)
