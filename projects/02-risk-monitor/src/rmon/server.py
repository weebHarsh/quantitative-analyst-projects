"""Live monitor server: replays a session through the Engine and serves it over HTTP.

Standard library only. A daemon thread (:class:`MonitorDriver`) pulls events from
``paced(replay(...), speed)`` and steps the Engine; HTTP handler threads read the
engine under the same lock. Endpoints (GET only, strict JSON, ``no-store``):

    /snapshot.json   engine.snapshot() + progress, done, session_start_us,
                     session_end_us, phases
    /history.json    thinned time series, one point per ~10 s of *session* time
    /alerts.json     every alert so far, as dicts
    /                the UI page (ui/index.html)

The server binds to 127.0.0.1 only.
"""
from __future__ import annotations

import json
import math
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from .engine import Engine
from .limits import Limits
from .replay import paced, replay

HOST = "127.0.0.1"
HISTORY_INTERVAL_US = 10_000_000
HISTORY_CAP = 1500
HISTORY_HORIZON_US = 1_000_000

_HTML_HEAD = ('<!doctype html><html><head><meta charset="utf-8">'
              '<meta name="viewport" content="width=device-width,initial-scale=1"></head><body>')
_HTML_TAIL = "</body></html>"


# ------------------------------------------------------------------ JSON
def to_jsonable(x):
    """Recursively replace non-finite floats by None (strict JSON has no NaN/Infinity)."""
    if isinstance(x, float):
        return x if math.isfinite(x) else None
    if isinstance(x, dict):
        return {k: to_jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [to_jsonable(v) for v in x]
    if hasattr(x, "item") and not isinstance(x, (str, bytes)):     # numpy scalar
        return to_jsonable(x.item())
    return x


def dumps_strict(obj, **kw) -> str:
    return json.dumps(to_jsonable(obj), allow_nan=False, **kw)


# ---------------------------------------------------------------- driver
class MonitorDriver:
    """Owns the Engine, the replay thread and the recorded history."""

    def __init__(self, market, fills, limits: Limits, speed: float = 600.0,
                 phases=None, label: str = "session") -> None:
        self.engine = Engine(limits)
        self.market, self.fills = market, fills
        self.speed = speed
        self.label = label
        self.phases = [{"name": p.name, "start_us": int(p.start_us), "end_us": int(p.end_us)}
                       for p in (phases or [])]
        self.start_us = int(market["ts_us"].iloc[0])
        self.end_us = int(market["ts_us"].iloc[-1])
        self.lock = threading.Lock()
        self.done = threading.Event()
        self.error: str | None = None
        self._history: list[dict] = []
        self._interval_us = HISTORY_INTERVAL_US
        self._next_us: int | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

    # ---- lifecycle
    def start(self) -> "MonitorDriver":
        if self._thread is None:
            self._thread = threading.Thread(target=self._run, name="rmon-replay", daemon=True)
            self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=5)

    def wait(self, timeout: float | None = None) -> bool:
        return self.done.wait(timeout)

    def _run(self) -> None:
        eng = self.engine
        try:
            events = paced(replay(self.market, self.fills), self.speed)
            for ev in events:
                if self._stop.is_set():
                    return
                with self.lock:
                    eng.step(ev)
                    ts = ev.ts_us
                    if self._next_us is None or ts >= self._next_us:
                        self._record()
                        self._next_us = ts + self._interval_us
            with self.lock:
                eng.flush()
                self._record(force=True)
        except Exception as exc:                    # surfaced via /snapshot.json
            self.error = f"{type(exc).__name__}: {exc}"
        finally:
            self.done.set()

    # ---- history (call with the lock held)
    def _record(self, force: bool = False) -> None:
        st = self.engine.state
        if st.ts_us is None:
            return
        if self._history and self._history[-1]["ts_us"] == st.ts_us:
            if not force:
                return
            self._history.pop()
        roll = None
        for m in st.markouts.summary():
            if m["horizon_us"] == HISTORY_HORIZON_US:
                roll = m["rolling_bps"]
        self._history.append({
            "ts_us": st.ts_us,
            "total_pnl": st.total_pnl,
            "spread_capture": st.spread_capture,
            "inventory_pnl": st.inventory_pnl,
            "fees": st.fees,
            "position": st.position,
            "drawdown": st.drawdown,
            "equity": st.equity,
            "rolling_markout_bps": roll,
            "window_fills": len(st._win),
        })
        if len(self._history) > HISTORY_CAP:
            last = self._history[-1]
            self._history = self._history[::2]
            if self._history[-1] is not last:
                self._history.append(last)
            self._interval_us *= 2

    # ---- read side
    def snapshot(self) -> dict:
        with self.lock:
            snap = self.engine.snapshot()
            ts = snap["ts_us"]
        span = max(self.end_us - self.start_us, 1)
        done = self.done.is_set()
        prog = 1.0 if done and self.error is None else (
            0.0 if ts is None else min(max((ts - self.start_us) / span, 0.0), 1.0))
        snap.update(progress=prog, done=done, session_start_us=self.start_us,
                    session_end_us=self.end_us, phases=self.phases, label=self.label,
                    error=self.error)
        return snap

    def history(self) -> list[dict]:
        with self.lock:
            return list(self._history)

    def alerts(self) -> list[dict]:
        with self.lock:
            return [a.to_dict() for a in self.engine.alerts]


# ------------------------------------------------------------------ HTTP
def find_ui() -> Path | None:
    here = Path(__file__).resolve().parent
    for p in (here.parents[1] / "ui" / "index.html", here / "ui" / "index.html"):
        if p.is_file():
            return p
    return None


def render_page() -> str:
    p = find_ui()
    if p is None:
        raise FileNotFoundError("ui/index.html not found (looked in the source tree and the package)")
    html = p.read_text(encoding="utf-8")
    if not html.lstrip()[:9].lower().startswith("<!doctype"):
        html = _HTML_HEAD + html + _HTML_TAIL
    return html


class _Handler(BaseHTTPRequestHandler):
    server_version = "rmon"
    protocol_version = "HTTP/1.0"

    def log_message(self, *args) -> None:          # keep the terminal quiet
        pass

    def _send(self, code: int, body: bytes, ctype: str, extra: dict | None = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code: int = 200, extra: dict | None = None) -> None:
        self._send(code, dumps_strict(obj).encode(), "application/json", extra)

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        drv: MonitorDriver = self.server.driver
        if path == "/snapshot.json":
            self._json(drv.snapshot())
        elif path == "/history.json":
            self._json(drv.history())
        elif path == "/alerts.json":
            self._json(drv.alerts())
        elif path in ("/", "/index.html"):
            try:
                self._send(200, render_page().encode(), "text/html; charset=utf-8")
            except OSError as exc:
                self._send(500, f"UI unavailable: {exc}\n".encode(), "text/plain; charset=utf-8")
        else:
            self._json({"error": "not found"}, 404)

    def _not_allowed(self) -> None:
        self._json({"error": "method not allowed"}, 405, {"Allow": "GET"})

    do_POST = do_PUT = do_DELETE = do_PATCH = do_HEAD = do_OPTIONS = _not_allowed


class MonitorServer(ThreadingHTTPServer):
    """ThreadingHTTPServer bound to 127.0.0.1 that carries its :class:`MonitorDriver`."""
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, driver: MonitorDriver, port: int = 0) -> None:
        super().__init__((HOST, port), _Handler)
        self.driver = driver
        self._thread: threading.Thread | None = None

    @property
    def port(self) -> int:
        return self.server_address[1]

    @property
    def url(self) -> str:
        return f"http://{HOST}:{self.port}/"

    def start(self) -> "MonitorServer":
        """Start the replay thread and serve requests in a background thread."""
        self.driver.start()
        self._thread = threading.Thread(target=self.serve_forever, kwargs={"poll_interval": 0.1},
                                        name="rmon-http", daemon=True)
        self._thread.start()
        return self

    def close(self) -> None:
        if self._thread is not None:
            self.shutdown()
            self._thread.join(timeout=5)
            self._thread = None
        self.driver.stop()
        self.server_close()


def make_server(driver: MonitorDriver, port: int = 0) -> MonitorServer:
    return MonitorServer(driver, port)


def serve(driver: MonitorDriver, port: int = 8765, announce=print) -> None:
    """Run the monitor until Ctrl-C."""
    srv = make_server(driver, port)
    driver.start()
    announce(f"rmon monitor on {srv.url}  (Ctrl-C to stop)")
    try:
        srv.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        driver.stop()
        srv.server_close()
