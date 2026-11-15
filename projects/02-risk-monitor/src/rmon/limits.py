"""Risk limits: dataclasses, TOML loader and validation.

The limits file has exactly four tables, ``[position]``, ``[loss_from_peak]``,
``[fill_rate]`` and ``[markout]``. Every key is required and unknown keys are an
error: a typo in a risk file must fail loudly rather than silently fall back to a
default. Validation also rejects negative magnitudes, non-integer microsecond
fields, booleans posing as numbers, and inconsistent thresholds (a warn level
above its breach level, a hysteresis band that would put the recovery threshold
below zero or across the opposite bound).

Units: inventory in contracts/shares (absolute), losses in currency, windows and
cooldowns in microseconds of event time, markouts in bps.
"""
from __future__ import annotations

import math
import tomllib
from dataclasses import asdict, dataclass, fields
from pathlib import Path
from typing import Any


class LimitsError(ValueError):
    """Raised for any malformed or inconsistent limits configuration."""


@dataclass(frozen=True)
class PositionLimits:
    warn: float           # |position| >= warn  -> warn
    breach: float         # |position| >= breach -> breach
    hysteresis: float     # clears once |position| < warn - hysteresis
    cooldown_us: int      # minimum gap between a fire and the next fire after a clear


@dataclass(frozen=True)
class LossFromPeakLimits:
    warn: float           # drawdown (peak equity - equity) >= warn -> warn
    breach: float
    hysteresis: float     # clears once drawdown < warn - hysteresis
    cooldown_us: int


@dataclass(frozen=True)
class FillRateLimits:
    window_us: int        # rolling window over which fills are counted
    min_fills: int        # window_fills < min_fills -> breach (drought)
    max_fills: int        # window_fills > max_fills -> breach (runaway)
    warmup_us: int        # no evaluation until this long after the first instant
    hysteresis: int       # clears once min+h <= window_fills <= max-h
    cooldown_us: int


@dataclass(frozen=True)
class MarkoutLimits:
    horizon_us: int       # which markout horizon to watch
    window_us: int        # rolling (resolution-time) window of that horizon
    floor_bps: float      # rolling_bps < floor_bps -> breach (may be negative)
    min_samples: int      # never fires with fewer resolved markouts in the window
    hysteresis_bps: float # clears once rolling_bps >= floor_bps + hysteresis_bps
    cooldown_us: int


@dataclass(frozen=True)
class Limits:
    position: PositionLimits
    loss_from_peak: LossFromPeakLimits
    fill_rate: FillRateLimits
    markout: MarkoutLimits

    def to_dict(self) -> dict:
        return asdict(self)


_SECTIONS: dict[str, type] = {
    "position": PositionLimits,
    "loss_from_peak": LossFromPeakLimits,
    "fill_rate": FillRateLimits,
    "markout": MarkoutLimits,
}
# 100 bps is 1%: no quantity-weighted markout at a one-second horizon gets near it, so a
# larger hysteresis can only be a typo that stops the markout alert from ever clearing.
MAX_MARKOUT_HYSTERESIS_BPS = 100.0

_SIGNED = {("markout", "floor_bps")}          # the only field allowed to be negative
_POSITIVE = {("position", "warn"), ("position", "breach"),
             ("loss_from_peak", "warn"), ("loss_from_peak", "breach"),
             ("fill_rate", "window_us"), ("markout", "window_us"),
             ("markout", "min_samples")}


def _is_int_field(name: str, typ: Any) -> bool:
    return typ is int or typ == "int"


def _coerce(section: str, name: str, typ: Any, value: Any) -> Any:
    where = f"[{section}].{name}"
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise LimitsError(f"{where} must be a number, got {value!r}")
    if not math.isfinite(value):
        raise LimitsError(f"{where} must be finite, got {value!r}")
    if _is_int_field(name, typ):
        if isinstance(value, float) and not value.is_integer():
            raise LimitsError(f"{where} must be an integer, got {value!r}")
        value = int(value)
    else:
        value = float(value)
    if (section, name) not in _SIGNED and value < 0:
        raise LimitsError(f"{where} must be >= 0, got {value!r}")
    if (section, name) in _POSITIVE and value <= 0:
        raise LimitsError(f"{where} must be > 0, got {value!r}")
    return value


def limits_from_dict(raw: dict) -> Limits:
    """Build and validate :class:`Limits` from a plain dict (e.g. parsed TOML)."""
    if not isinstance(raw, dict):
        raise LimitsError("limits must be a table")
    unknown = sorted(set(raw) - set(_SECTIONS))
    if unknown:
        raise LimitsError(f"unknown limits section(s): {unknown}")
    missing = sorted(set(_SECTIONS) - set(raw))
    if missing:
        raise LimitsError(f"missing limits section(s): {missing}")
    built = {}
    for section, cls in _SECTIONS.items():
        tbl = raw[section]
        if not isinstance(tbl, dict):
            raise LimitsError(f"[{section}] must be a table")
        flds = {f.name: f.type for f in fields(cls)}
        unknown = sorted(set(tbl) - set(flds))
        if unknown:
            raise LimitsError(f"unknown key(s) in [{section}]: {unknown}")
        missing = [k for k in flds if k not in tbl]
        if missing:
            raise LimitsError(f"missing key(s) in [{section}]: {missing}")
        built[section] = cls(**{k: _coerce(section, k, t, tbl[k]) for k, t in flds.items()})
    lim = Limits(**built)
    _check_consistency(lim)
    return lim


def _check_consistency(lim: Limits) -> None:
    for name in ("position", "loss_from_peak"):
        s = getattr(lim, name)
        if s.warn > s.breach:
            raise LimitsError(f"[{name}] warn ({s.warn}) must not exceed breach ({s.breach})")
        if s.hysteresis >= s.warn:
            raise LimitsError(f"[{name}] hysteresis ({s.hysteresis}) must be below warn "
                              f"({s.warn}) so the recovery threshold stays positive")
    fr = lim.fill_rate
    if fr.min_fills > fr.max_fills:
        raise LimitsError(f"[fill_rate] min_fills ({fr.min_fills}) exceeds max_fills ({fr.max_fills})")
    if fr.min_fills + fr.hysteresis > fr.max_fills - fr.hysteresis:
        raise LimitsError("[fill_rate] hysteresis bands overlap: need "
                          "min_fills + hysteresis <= max_fills - hysteresis")
    if fr.min_fills == 0 and fr.max_fills == 0:
        raise LimitsError("[fill_rate] min_fills and max_fills cannot both be 0")
    if fr.warmup_us < fr.window_us:
        raise LimitsError(f"[fill_rate] warmup_us ({fr.warmup_us}) must be at least window_us "
                          f"({fr.window_us}); before the window has filled the count is "
                          "artificially low and would raise a false breach")
    mk = lim.markout
    if mk.min_samples < 1:
        raise LimitsError("[markout] min_samples must be >= 1")
    if mk.hysteresis_bps > MAX_MARKOUT_HYSTERESIS_BPS:
        raise LimitsError(f"[markout] hysteresis_bps ({mk.hysteresis_bps}) is above "
                          f"{MAX_MARKOUT_HYSTERESIS_BPS} bps; the recovery level would sit "
                          "above any markout the strategy can reach, so the alert could never clear")


def load_limits(path: str | Path) -> Limits:
    """Parse and validate a TOML limits file."""
    p = Path(path)
    try:
        with p.open("rb") as fh:
            raw = tomllib.load(fh)
    except tomllib.TOMLDecodeError as e:
        raise LimitsError(f"{p}: invalid TOML: {e}") from e
    try:
        return limits_from_dict(raw)
    except LimitsError as e:
        raise LimitsError(f"{p}: {e}") from None


DEFAULT_LIMITS_PATH = Path(__file__).resolve().parents[2] / "config" / "limits.toml"


def default_limits() -> Limits:
    """The shipped, calibrated limits (config/limits.toml)."""
    return load_limits(DEFAULT_LIMITS_PATH)
