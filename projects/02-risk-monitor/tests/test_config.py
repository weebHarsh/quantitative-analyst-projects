"""Limits loader: the shipped file loads; every class of bad config raises."""
import copy
import tomllib

import pytest

from rmon.limits import (DEFAULT_LIMITS_PATH, Limits, LimitsError, default_limits,
                         limits_from_dict, load_limits)


@pytest.fixture
def raw():
    with open(DEFAULT_LIMITS_PATH, "rb") as fh:
        return tomllib.load(fh)


def test_shipped_file_loads():
    lim = default_limits()
    assert isinstance(lim, Limits)
    assert lim.position.warn < lim.position.breach
    assert lim.loss_from_peak.warn < lim.loss_from_peak.breach
    assert lim.fill_rate.min_fills < lim.fill_rate.max_fills
    assert lim.markout.horizon_us == 1_000_000
    assert isinstance(lim.fill_rate.window_us, int)
    assert isinstance(lim.position.warn, float)
    d = lim.to_dict()
    assert set(d) == {"position", "loss_from_peak", "fill_rate", "markout"}
    assert limits_from_dict(d) == lim          # dict round trip


def test_load_from_path(tmp_path, raw):
    p = tmp_path / "l.toml"
    p.write_bytes(DEFAULT_LIMITS_PATH.read_bytes())
    assert load_limits(p) == default_limits()


def _bad(raw, mutate):
    r = copy.deepcopy(raw)
    mutate(r)
    with pytest.raises(LimitsError):
        limits_from_dict(r)


def test_missing_section(raw):
    _bad(raw, lambda r: r.pop("markout"))


def test_unknown_section(raw):
    _bad(raw, lambda r: r.__setitem__("extra", {}))


def test_missing_key(raw):
    _bad(raw, lambda r: r["position"].pop("cooldown_us"))


def test_unknown_key(raw):
    _bad(raw, lambda r: r["fill_rate"].__setitem__("min_fill", 3))   # typo


@pytest.mark.parametrize("sec,key", [("position", "warn"), ("loss_from_peak", "hysteresis"),
                                     ("fill_rate", "warmup_us"), ("markout", "cooldown_us"),
                                     ("markout", "hysteresis_bps")])
def test_negative_values(raw, sec, key):
    _bad(raw, lambda r: r[sec].__setitem__(key, -1))


def test_floor_bps_may_be_negative_or_positive(raw):
    r = copy.deepcopy(raw)
    r["markout"]["floor_bps"] = 0.2
    assert limits_from_dict(r).markout.floor_bps == 0.2


@pytest.mark.parametrize("value", ["10", True, float("nan"), float("inf"), 1.5])
def test_bad_types(raw, value):
    # window_us is an integer field: strings, booleans, non-finite and fractional fail
    _bad(raw, lambda r: r["fill_rate"].__setitem__("window_us", value))


def test_zero_where_positive_required(raw):
    _bad(raw, lambda r: r["markout"].__setitem__("min_samples", 0))
    _bad(raw, lambda r: r["fill_rate"].__setitem__("window_us", 0))


def test_warn_above_breach(raw):
    _bad(raw, lambda r: r["position"].update(warn=120, breach=100))
    _bad(raw, lambda r: r["loss_from_peak"].update(warn=300, breach=200))


def test_hysteresis_not_below_warn(raw):
    _bad(raw, lambda r: r["position"].update(hysteresis=60))


def test_fill_rate_inconsistent(raw):
    _bad(raw, lambda r: r["fill_rate"].update(min_fills=200, max_fills=150))
    _bad(raw, lambda r: r["fill_rate"].update(min_fills=10, max_fills=20, hysteresis=6))


def test_invalid_toml_and_path_in_message(tmp_path):
    p = tmp_path / "bad.toml"
    p.write_text("[position\nwarn = 1\n")
    with pytest.raises(LimitsError, match="invalid TOML"):
        load_limits(p)
    p.write_text(DEFAULT_LIMITS_PATH.read_text().replace("breach = 100", "breach = 10"))
    with pytest.raises(LimitsError, match="bad.toml"):
        load_limits(p)
