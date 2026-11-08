"""rmon: live risk/strategy monitor built on the pta (Project 1) schemas.

Public core interface:
    Quote, Fill                      event NamedTuples (rmon.events)
    MonitorState                     incremental state (rmon.state)
    MarkoutTracker                   streaming markouts (rmon.markouts_stream)
    replay, paced, run_session       session replay helpers (rmon.replay)

Alerting layer:
    Limits, load_limits, default_limits, LimitsError   limits config (rmon.limits)
    Alert, RuleState, PositionRule, LossFromPeakRule,
    FillRateRule, MarkoutRule, build_rules,
    write_alerts, read_alerts, AlertLog                 rules + JSONL log (rmon.alerts)
    Engine                                              state + rules (rmon.engine)
    scenarios                                           demo sessions (rmon.scenarios)
"""
from .events import Fill, Quote
from .markouts_stream import MarkoutTracker
from .replay import paced, replay, run_session
from .state import DEFAULT_HORIZONS_US, MonitorState
from .limits import Limits, LimitsError, default_limits, limits_from_dict, load_limits
from .alerts import (Alert, AlertLog, FillRateRule, LossFromPeakRule, MarkoutRule,
                     PositionRule, RuleState, build_rules, read_alerts, write_alerts)
from .engine import Engine
from . import scenarios

__version__ = "0.1.0"
__all__ = ["Quote", "Fill", "MonitorState", "MarkoutTracker", "replay", "paced",
           "run_session", "DEFAULT_HORIZONS_US", "__version__",
           "Limits", "LimitsError", "load_limits", "limits_from_dict", "default_limits",
           "Alert", "AlertLog", "RuleState", "PositionRule", "LossFromPeakRule",
           "FillRateRule", "MarkoutRule", "build_rules", "write_alerts", "read_alerts",
           "Engine", "scenarios"]
