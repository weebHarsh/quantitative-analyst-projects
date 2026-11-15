"""CLI: check-config exit codes, demo outputs, and run reproducing demo alerts."""
import json
from pathlib import Path

import pytest

from rmon.alerts import read_alerts
from rmon.cli import main
from rmon import scenarios
from rmon.scenarios import BAD_EXPECTED_ORDER

LIMITS = Path(__file__).resolve().parents[1] / "config" / "limits.toml"


def test_check_config_default_ok(capsys):
    assert main(["check-config"]) == 0
    assert "limits OK" in capsys.readouterr().out


def test_check_config_good_file(capsys):
    assert main(["check-config", "--limits", str(LIMITS)]) == 0


def test_check_config_bad_values(tmp_path, capsys):
    p = tmp_path / "bad.toml"
    p.write_text(LIMITS.read_text().replace("warn = 60 ", "warn = 600", 1))   # warn > breach
    assert main(["check-config", "--limits", str(p)]) == 2
    cap = capsys.readouterr()
    assert cap.err.strip() and cap.out == ""


def test_check_config_unknown_key_and_missing_file(tmp_path, capsys):
    p = tmp_path / "typo.toml"
    p.write_text(LIMITS.read_text() + "\n[position2]\nwarn = 1\n")
    assert main(["check-config", "--limits", str(p)]) == 2
    assert main(["check-config", "--limits", str(tmp_path / "nope.toml")]) == 2
    p.write_text("not [valid toml")
    assert main(["check-config", "--limits", str(p)]) == 2


@pytest.fixture(scope="module")
def clean_demo(tmp_path_factory):
    d = tmp_path_factory.mktemp("clean")
    assert main(["demo", "--scenario", "clean", "--hours", "0.3", "--out", str(d)]) == 0
    return d


@pytest.fixture(scope="module")
def bad_demo(tmp_path_factory):
    d = tmp_path_factory.mktemp("bad")
    assert main(["demo", "--scenario", "bad", "--out", str(d)]) == 0
    return d


FILES = ["market.csv", "fills.csv", "alerts.jsonl", "snapshot.json"]


def test_demo_clean_writes_files_and_is_silent(clean_demo):
    for f in FILES:
        assert (clean_demo / f).is_file()
    assert (clean_demo / "alerts.jsonl").read_text() == ""
    snap = json.loads((clean_demo / "snapshot.json").read_text())
    assert snap["alerts"] == [] and snap["fills"] > 0


def test_demo_bad_alerts_follow_expected_order(bad_demo):
    for f in FILES:
        assert (bad_demo / f).is_file()
    alerts = read_alerts(bad_demo / "alerts.jsonl")
    assert alerts
    # Entries inside one phase are not ordered against each other (see scenarios.py), so
    # check the first occurrence of each entry lands in an allowed phase and that the
    # phases never go backwards along BAD_EXPECTED_ORDER.
    phases = scenarios.bad(seed=7, hours=2.0).phases
    idx = {p.name: i for i, p in enumerate(phases)}
    first = {}
    for a in alerts:
        first.setdefault((a.rule, a.severity), a)
    last_phase = -1
    for rule, sev, allowed in BAD_EXPECTED_ORDER:
        a = first[(rule, sev)]
        ph = scenarios.phase_at(phases, a.ts_us)
        assert ph in allowed, (rule, sev, ph)
        assert idx[ph] >= last_phase
        last_phase = idx[ph]
    for rule in {r for r, _, _ in BAD_EXPECTED_ORDER}:
        times = [first[(r, s)].ts_us for r, s, _ in BAD_EXPECTED_ORDER if r == rule]
        assert times == sorted(times)


def test_demo_bad_prints_phases(tmp_path, capsys):
    assert main(["demo", "--scenario", "bad", "--hours", "0.2", "--out", str(tmp_path)]) == 0
    out = capsys.readouterr().out
    for name in ("normal", "adverse", "inventory_run", "drought", "recovery"):
        assert name in out


def test_run_reproduces_demo_alerts(bad_demo, tmp_path, capsys):
    out = tmp_path / "run"
    assert main(["run", "--market", str(bad_demo / "market.csv"),
                 "--fills", str(bad_demo / "fills.csv"), "--out", str(out)]) == 0
    # CSV text round-trip may move a float in its last digits, so compare with a tolerance
    got, want = read_alerts(out / "alerts.jsonl"), read_alerts(bad_demo / "alerts.jsonl")
    assert [(a.ts_us, a.rule, a.severity) for a in got] == [(a.ts_us, a.rule, a.severity) for a in want]
    for a, b in zip(got, want):
        assert a.metric_value == pytest.approx(b.metric_value, rel=1e-9, abs=1e-9)
    a = json.loads((out / "snapshot.json").read_text())
    b = json.loads((bad_demo / "snapshot.json").read_text())
    assert a["total_pnl"] == pytest.approx(b["total_pnl"], rel=1e-12, abs=1e-9)
    text = capsys.readouterr().out
    assert "final PnL" in text and "markout_degradation" in text


def test_run_bad_input_exits_2(tmp_path, capsys):
    assert main(["run", "--market", str(tmp_path / "x.csv"), "--fills", str(tmp_path / "y.csv")]) == 2


def test_serve_argument_errors(capsys):
    assert main(["serve"]) == 2
    with pytest.raises(SystemExit):
        main(["serve", "--host", "0.0.0.0", "--scenario", "bad"])    # no --host option exists
