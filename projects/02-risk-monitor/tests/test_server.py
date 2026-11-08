"""Live monitor server on an ephemeral port with an instant bad-scenario replay."""
import http.client
import json

import pytest

from rmon import Engine, default_limits, replay, scenarios
from rmon.server import HISTORY_CAP, MonitorDriver, dumps_strict, make_server

HOURS = 2.0


def _strict(text):
    def boom(c):
        raise ValueError(f"non-strict JSON constant {c}")
    return json.loads(text, parse_constant=boom)


@pytest.fixture(scope="module")
def sc():
    return scenarios.bad(seed=7, hours=HOURS)


@pytest.fixture(scope="module")
def batch(sc):
    eng = Engine(default_limits())
    alerts = eng.run(replay(sc.market, sc.fills))
    return eng, alerts


@pytest.fixture(scope="module")
def srv(sc):
    drv = MonitorDriver(sc.market, sc.fills, default_limits(), speed=0, phases=sc.phases,
                        label="bad")
    s = make_server(drv, port=0).start()
    assert drv.wait(30)
    yield s
    s.close()


def get(srv, path, method="GET"):
    c = http.client.HTTPConnection("127.0.0.1", srv.port, timeout=10)
    c.request(method, path)
    r = c.getresponse()
    body = r.read().decode()
    hdr = dict(r.getheaders())
    c.close()
    return r.status, hdr, body


def test_binds_loopback_only(srv):
    assert srv.server_address[0] == "127.0.0.1"
    assert srv.port > 0


def test_json_endpoints_headers(srv):
    for p in ("/snapshot.json", "/history.json", "/alerts.json"):
        st, h, body = get(srv, p)
        assert st == 200
        assert h["Content-Type"] == "application/json"
        assert h["Cache-Control"] == "no-store"
        _strict(body)


def test_snapshot_matches_batch_engine(srv, batch, sc):
    eng, _ = batch
    st, _, body = get(srv, "/snapshot.json")
    snap = _strict(body)
    assert snap["done"] is True and snap["progress"] == 1.0
    assert snap["total_pnl"] == eng.snapshot()["total_pnl"]
    assert snap["position"] == eng.snapshot()["position"]
    assert snap["session_start_us"] == 0
    assert snap["session_end_us"] == int(sc.market["ts_us"].iloc[-1])
    assert [p["name"] for p in snap["phases"]] == [p.name for p in sc.phases]
    assert "limits" in snap and "active" in snap


def test_alerts_match_batch(srv, batch):
    _, alerts = batch
    got = _strict(get(srv, "/alerts.json")[2])
    assert got == [a.to_dict() for a in alerts]
    assert len(got) > 0


def test_history_bounded_and_ordered(srv, batch):
    hist = _strict(get(srv, "/history.json")[2])
    assert 10 < len(hist) <= HISTORY_CAP
    ts = [p["ts_us"] for p in hist]
    assert ts == sorted(set(ts))
    assert hist[-1]["total_pnl"] == batch[0].snapshot()["total_pnl"]
    for k in ("total_pnl", "spread_capture", "inventory_pnl", "fees", "position", "drawdown",
              "equity", "rolling_markout_bps", "window_fills"):
        assert k in hist[0]
    gaps = [b - a for a, b in zip(ts, ts[1:-1])]
    assert min(gaps) >= 10_000_000          # sampled on session time


def test_history_thinning_keeps_cap(sc):
    drv = MonitorDriver(sc.market, sc.fills, default_limits(), speed=0)
    import rmon.server as m
    old = m.HISTORY_CAP, m.HISTORY_INTERVAL_US
    m.HISTORY_CAP, drv._interval_us = 40, 1_000_000
    try:
        drv.start()
        assert drv.wait(30)
    finally:
        m.HISTORY_CAP, _ = old
    h = drv.history()
    assert len(h) <= 40
    assert [p["ts_us"] for p in h] == sorted(p["ts_us"] for p in h)
    assert h[-1]["ts_us"] == int(sc.market["ts_us"].iloc[-1])


def test_index_page(srv):
    st, h, body = get(srv, "/")
    assert st == 200 and h["Content-Type"].startswith("text/html")
    assert body.lower().startswith("<!doctype html>")
    assert "<title>Risk Monitor</title>" in body


def test_404_and_405(srv):
    st, _, body = get(srv, "/nope")
    assert st == 404 and "error" in _strict(body)
    for m in ("POST", "PUT", "DELETE", "PATCH"):
        st, h, _ = get(srv, "/snapshot.json", method=m)
        assert st == 405 and h["Allow"] == "GET"


def test_strict_json_converts_nonfinite():
    assert dumps_strict({"a": float("nan"), "b": [float("inf"), 1.5]}) == '{"a": null, "b": [null, 1.5]}'


def test_progress_while_running_and_clean_shutdown(sc):
    drv = MonitorDriver(sc.market, sc.fills, default_limits(), speed=3000, label="bad")
    s = make_server(drv, port=0).start()
    try:
        snap = _strict(get(s, "/snapshot.json")[2])
        assert 0.0 <= snap["progress"] <= 1.0
        port = s.port
    finally:
        s.close()
    assert not s._thread
    with pytest.raises(OSError):
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
        c.request("GET", "/snapshot.json")
        c.getresponse()
