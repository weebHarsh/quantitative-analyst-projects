"""rmon: live risk/strategy monitor built on the pta (Project 1) schemas.

Public core interface:
    Quote, Fill                      event NamedTuples (rmon.events)
    MonitorState                     incremental state (rmon.state)
    MarkoutTracker                   streaming markouts (rmon.markouts_stream)
    replay, paced, run_session       session replay helpers (rmon.replay)
"""
from .events import Fill, Quote
from .markouts_stream import MarkoutTracker
from .replay import paced, replay, run_session
from .state import DEFAULT_HORIZONS_US, MonitorState

__version__ = "0.1.0"
__all__ = ["Quote", "Fill", "MonitorState", "MarkoutTracker", "replay", "paced",
           "run_session", "DEFAULT_HORIZONS_US", "__version__"]
