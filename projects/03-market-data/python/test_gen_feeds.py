"""Tests for tools/gen_feeds.py (the synthetic two-venue feed generator).

The book applier and sequencer here are written independently of the
generator (they parse the raw text and follow DESIGN.md), so a checkpoint or
counter match is evidence that the feed, the truth files and the manifest agree.
"""
from __future__ import annotations

import csv
import filecmp
import json
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "tools" / "gen_feeds.py"
GOLDEN = ROOT / "tests" / "golden"


def run_gen(out: Path, *args: str) -> None:
    subprocess.run([sys.executable, str(GEN), "--out", str(out), *args], check=True,
                   capture_output=True, text=True)


@pytest.fixture(scope="module")
def feeds(tmp_path_factory) -> dict[str, Path]:
    base = tmp_path_factory.mktemp("feeds")
    d = {"clean": base / "clean", "faults": base / "faults", "faults2": base / "faults2",
         "seed8": base / "seed8", "golden": base / "golden"}
    run_gen(d["clean"], "--seed", "7", "--hours", "0.25")
    run_gen(d["faults"], "--seed", "7", "--hours", "0.25", "--faults")
    run_gen(d["faults2"], "--seed", "7", "--hours", "0.25", "--faults")
    run_gen(d["seed8"], "--seed", "8", "--hours", "0.25", "--faults")
    run_gen(d["golden"], "--golden")
    return d


# ---------------------------------------------------------------------------
# Independent raw parsing (text -> simple messages)
# ---------------------------------------------------------------------------
def dec(s: str, scale: int) -> int:
    whole, frac = s.split(".")
    assert len(frac) == scale, s
    return int(whole) * 10 ** scale + int(frac)


def read_a(path: Path) -> list[dict]:
    out = []
    for i, line in enumerate(path.read_text().splitlines()):
        d = json.loads(line)
        m = {"idx": i, "kind": d["e"], "r": d["r"], "exch_ns": d["E"] * 1000}
        if d["e"] in ("snapshot", "depth"):
            m["levels"] = [(1, dec(p, 2), dec(q, 3)) for p, q in d["b"]] + \
                          [(2, dec(p, 2), dec(q, 3)) for p, q in d["a"]]
        if d["e"] == "snapshot":
            m["seq"] = d["seq"]
        elif d["e"] == "depth":
            m["fs"], m["ls"] = d["first_seq"], d["last_seq"]
        else:
            m.update(tid=d["tid"], side=d["side"], px=dec(d["px"], 2), qty=dec(d["qty"], 3))
        out.append(m)
    return out


def read_b(path: Path) -> list[dict]:
    with path.open() as fh:
        rows = list(csv.DictReader(fh))
    out = []
    for i, r in enumerate(rows):
        m = {"idx": i, "kind": r["kind"], "r": int(r["recv_ns"]), "exch_ns": int(r["exch_ns"]),
             "id": int(r["id"])}
        if r["prev_id"]:
            m["prev"] = int(r["prev_id"])
        if r["px_e4"]:
            assert int(r["px_e4"]) % 100 == 0
            m["px"] = int(r["px_e4"]) // 100
        if r["side"]:
            m["side"] = int(r["side"])
        if r["size"]:
            m["size"] = int(r["size"])
        m["action"] = r["action"]
        out.append(m)
    return out


def b_messages(rows: list[dict]) -> list[dict]:
    """Group Venue B rows into logical messages (a snapshot = SB..SE)."""
    out, i = [], 0
    while i < len(rows):
        r = rows[i]
        if r["kind"] == "SB":
            n = r["size"]
            lv = rows[i + 1:i + 1 + n]
            assert all(x["kind"] == "SL" and x["id"] == r["id"] for x in lv)
            assert rows[i + 1 + n]["kind"] == "SE"
            out.append({"idx": i, "kind": "snapshot", "seq": r["id"],
                        "levels": [(x["side"], x["px"], x["size"]) for x in lv]})
            i += n + 2
        else:
            out.append(r)
            i += 1
    return out


def read_truth(path: Path) -> dict[int, dict]:
    t: dict[int, dict] = {}
    with path.open() as fh:
        for r in csv.DictReader(fh):
            cp = t.setdefault(int(r["checkpoint"]), {"seq": int(r["seq"]), "BID": [], "ASK": []})
            cp[r["side"]].append((int(r["price_ticks"]), int(r["qty_lots"])))
    return t


def top(book: dict[int, dict[int, int]], n: int = 10) -> dict[str, list]:
    return {"BID": sorted(book[1].items(), reverse=True)[:n], "ASK": sorted(book[2].items())[:n]}


# ---------------------------------------------------------------------------
# Clean feed: naive reapplication must hit every snapshot and truth checkpoint
# ---------------------------------------------------------------------------
@pytest.mark.parametrize("venue", ["A", "B"])
def test_truth_equals_independent_reapplication(feeds, venue):
    d = feeds["clean"]
    msgs = read_a(d / "venueA.jsonl") if venue == "A" else b_messages(read_b(d / "venueB.csv"))
    truth = read_truth(d / f"truth_{venue}.csv")
    book = {1: {}, 2: {}}
    cp, last = 0, None
    for m in msgs:
        if m["kind"] == "snapshot":
            img = {1: {}, 2: {}}
            for s, p, q in m["levels"]:
                assert q > 0 and p not in img[s]
                img[s][p] = q
            if cp > 0:
                assert book == img, f"checkpoint {cp}: reapplied book != venue snapshot"
                assert m["seq"] == last
            assert truth[cp]["seq"] == m["seq"]
            assert top(img) == {k: truth[cp][k] for k in ("BID", "ASK")}
            book, last, cp = img, m["seq"], cp + 1
        elif m["kind"] in ("depth", "U"):
            if venue == "A":
                assert m["fs"] == last + 1 and m["ls"] >= m["fs"]
                keys = [(s, p) for s, p, _ in m["levels"]]
                assert len(keys) == len(set(keys)), "a price appears twice in one diff"
                for s, p, q in m["levels"]:
                    if q:
                        book[s][p] = q
                    else:
                        book[s].pop(p)          # deletes always refer to a stored level
                last = m["ls"]
            else:
                assert m["prev"] == last and m["id"] > last
                s, p, q, a = m["side"], m["px"], m["size"], m["action"]
                assert a == ("N" if p not in book[s] else ("D" if q == 0 else "C"))
                if q:
                    book[s][p] = q
                else:
                    del book[s][p]
                last = m["id"]
            if book[1] and book[2]:
                assert max(book[1]) < min(book[2]), "true book crossed or locked"
            assert all(q > 0 for side in book.values() for q in side.values())
            assert len(book[1]) <= 20 and len(book[2]) <= 20
    assert cp == len(truth) and cp >= 20


@pytest.mark.parametrize("venue", ["A", "B"])
def test_trades_happen_at_the_touch(feeds, venue):
    d = feeds["clean"]
    msgs = read_a(d / "venueA.jsonl") if venue == "A" else b_messages(read_b(d / "venueB.csv"))
    book = {1: {}, 2: {}}
    n = 0
    for m in msgs:
        if m["kind"] == "snapshot":
            book = {1: {}, 2: {}}
            for s, p, q in m["levels"]:
                book[s][p] = q
        elif m["kind"] == "depth":
            for s, p, q in m["levels"]:
                book[s][p] = q
                if not q:
                    del book[s][p]
        elif m["kind"] == "U":
            book[m["side"]][m["px"]] = m["size"]
            if not m["size"]:
                del book[m["side"]][m["px"]]
        else:
            buy = m["side"] == "buy" if venue == "A" else m["side"] == 1
            px = m["px"]
            qty = m["qty"] if venue == "A" else m["size"]
            assert px == (min(book[2]) if buy else max(book[1]))
            assert 0 < qty <= (book[2][px] if buy else book[1][px])
            n += 1
    assert n > 100


@pytest.mark.parametrize("venue", ["A", "B"])
def test_timestamps(feeds, venue):
    d = feeds["clean"]
    msgs = read_a(d / "venueA.jsonl") if venue == "A" else read_b(d / "venueB.csv")
    r = [m["r"] for m in msgs]
    assert all(b >= a for a, b in zip(r, r[1:]))
    if venue == "A":
        assert all(b > a for a, b in zip(r, r[1:]))
        assert all(m["exch_ns"] % 1000 == 0 for m in msgs)        # microsecond source
    else:  # equal receive times only inside one snapshot (SB..SE)
        for a, b in zip(msgs, msgs[1:]):
            if b["r"] == a["r"]:
                assert b["kind"] in ("SL", "SE")
    e = [m["exch_ns"] for m in msgs]
    assert all(b >= a for a, b in zip(e, e[1:]))                 # clean feed: in order
    lat = sorted(m["r"] - m["exch_ns"] for m in msgs)
    med = lat[len(lat) // 2]
    meta = json.loads((d / "meta.json").read_text())["venues"][venue]
    assert 0 < med < meta["apparent_mean_latency_ns_approx"] * 2


def test_same_underlying_market(feeds):
    """Both venues track the same price path: their mids stay within a few ticks."""
    d = feeds["clean"]
    meta = json.loads((d / "meta.json").read_text())
    t0, step = meta["t0_ns"], meta["step_ns"]
    braw = read_b(d / "venueB.csv")
    a = {(m["exch_ns"] - t0) // step: m for m in read_a(d / "venueA.jsonl") if m["kind"] == "snapshot"}
    b = {(braw[m["idx"]]["exch_ns"] - t0) // step: m
         for m in b_messages(braw) if m["kind"] == "snapshot"}

    def mid(m):
        bb = max(p for s, p, _ in m["levels"] if s == 1)
        aa = min(p for s, p, _ in m["levels"] if s == 2)
        return (bb + aa) / 2
    common = sorted(set(a) & set(b))
    assert len(common) >= 5
    # snapshots taken at the start of the same 100 ms step: B lags A by one step at most
    assert all(abs(mid(a[s]) - mid(b[s])) <= 4 for s in common)
    assert len({round(mid(m)) for m in a.values()}) > 1           # the price actually moves


# ---------------------------------------------------------------------------
# Determinism
# ---------------------------------------------------------------------------
def test_determinism_by_seed(feeds):
    names = ["venueA.jsonl", "venueB.csv", "truth_A.csv", "truth_B.csv", "faults.json", "meta.json"]
    for n in names:
        assert filecmp.cmp(feeds["faults"] / n, feeds["faults2"] / n, shallow=False), n
    assert (feeds["faults"] / "venueA.jsonl").read_bytes() != (feeds["seed8"] / "venueA.jsonl").read_bytes()
    assert (feeds["faults"] / "venueB.csv").read_bytes() != (feeds["seed8"] / "venueB.csv").read_bytes()


def test_golden_fixture_is_reproducible(feeds):
    committed = sorted(p.name for p in GOLDEN.iterdir())
    fresh = sorted(p.name for p in feeds["golden"].iterdir())
    assert committed == fresh
    for n in committed:
        assert filecmp.cmp(GOLDEN / n, feeds["golden"] / n, shallow=False), f"{n} is stale; regenerate"


# ---------------------------------------------------------------------------
# Independent sequencer (DESIGN.md section 6) for the faulted feeds
# ---------------------------------------------------------------------------
class Seq:
    def __init__(self, venue: str):
        self.v = venue
        self.state = "INIT"
        self.book = {1: {}, 2: {}}
        self.last = -1
        self.hi = -1
        self.buf: dict[tuple, dict] = {}
        self.c = dict(gaps=0, resyncs=0, duplicates_ignored=0, out_of_order=0, crossed_detected=0,
                      locked_detected=0, checkpoints_matched=0, checkpoints_mismatched=0,
                      checkpoints_skipped_stale=0, snapshots_ignored_late=0, parse_errors=0)
        self.live_tops: list[tuple] = []

    def key(self, m):
        return (m["fs"], m["ls"]) if self.v == "A" else (m["id"],)

    def hiseq(self, m):
        return m["ls"] if self.v == "A" else m["id"]

    def cont(self, m):
        return m["fs"] <= self.last + 1 <= m["ls"] if self.v == "A" else m["prev"] == self.last

    def apply(self, m):
        lv = m["levels"] if self.v == "A" else [(m["side"], m["px"], m["size"])]
        for s, p, q in lv:
            if q:
                self.book[s][p] = q
            else:
                self.book[s].pop(p, None)
        self.last = self.hiseq(m)

    def check(self):
        b, a = self.book[1], self.book[2]
        if b and a and max(b) >= min(a):
            self.c["crossed_detected" if max(b) > min(a) else "locked_detected"] += 1
            self.state = "STALE"

    def update(self, m):
        h = self.hiseq(m)
        if self.last >= 0 and h <= self.last:
            self.c["duplicates_ignored"] += 1
            return
        if self.state == "LIVE":
            if self.cont(m):
                self.apply(m)
                self.hi = max(self.hi, self.last)
                self.check()
                return
            self.c["gaps"] += 1
            self.state = "STALE"
        elif self.key(m) in self.buf:
            self.c["duplicates_ignored"] += 1
            return
        elif h < self.hi:
            self.c["out_of_order"] += 1
        self.buf[self.key(m)] = m
        self.hi = max(self.hi, h)

    def snapshot(self, m):
        img = {1: {}, 2: {}}
        for s, p, q in m["levels"]:
            img[s][p] = q
        S = m["seq"]
        if self.state == "LIVE":
            if S < self.last:
                self.c["snapshots_ignored_late"] += 1
                return
            if S == self.last:
                self.c["checkpoints_matched" if img == self.book else "checkpoints_mismatched"] += 1
                self.book = img
                self.check()
                return
            self.c["gaps"] += 1
        counted = self.state != "INIT"
        if counted:
            self.c["checkpoints_skipped_stale"] += 1
        self.book, self.last = img, S
        for k in sorted(self.buf):
            bm = self.buf[k]
            if k[-1] <= self.last:
                continue
            assert self.cont(bm), "generated faults never leave a hole after a snapshot"
            self.apply(bm)
        self.buf = {}
        self.state = "LIVE"
        self.hi = self.last
        if counted:
            self.c["resyncs"] += 1
        self.check()


def replay(venue: str, d: Path) -> Seq:
    msgs = read_a(d / "venueA.jsonl") if venue == "A" else b_messages(read_b(d / "venueB.csv"))
    sq = Seq(venue)
    for m in msgs:
        if m["kind"] == "snapshot":
            sq.snapshot(m)
        elif m["kind"] in ("depth", "U"):
            sq.update(m)
    return sq


@pytest.mark.parametrize("venue", ["A", "B"])
@pytest.mark.parametrize("which", ["clean", "faults", "golden"])
def test_counters_match_manifest(feeds, venue, which):
    d = feeds[which]
    exp = json.loads((d / "faults.json").read_text())["expected_counters"][venue]
    sq = replay(venue, d)
    assert sq.c == exp
    assert sq.state == "LIVE"


@pytest.mark.parametrize("venue", ["A", "B"])
def test_faults_manifest_matches_files(feeds, venue):
    d = feeds["faults"]
    doc = json.loads((d / "faults.json").read_text())
    fl = [f for f in doc["faults"] if f["venue"] == venue]
    assert sorted(f["kind"] for f in fl) == ["crossed", "drop", "duplicate", "swap"]
    if venue == "A":
        lines = (d / "venueA.jsonl").read_text().splitlines()
        clean = (feeds["clean"] / "venueA.jsonl").read_text().splitlines()
        msgs = read_a(d / "venueA.jsonl")
    else:
        lines = (d / "venueB.csv").read_text().splitlines()[1:]
        clean = (feeds["clean"] / "venueB.csv").read_text().splitlines()[1:]
        msgs = read_b(d / "venueB.csv")
    assert len(lines) == len(clean)          # one drop and one duplicate cancel out

    def body(line):                          # message without the local receive time
        return line.split(",", 1)[1] if venue == "B" else line.split(",", 1)[1]

    def seqs(m):
        return (m["fs"], m["ls"]) if venue == "A" else (m["prev"], m["id"])
    prev_update = {}
    last = None
    for m in msgs:
        if m["kind"] in ("depth", "U"):
            prev_update[m["idx"]] = last
            last = m
    for f in fl:
        i = f["msg_index"]
        m = msgs[i]
        assert m["kind"] in ("depth", "U")
        if f["kind"] == "drop":
            p = prev_update[i]
            if venue == "A":
                assert m["fs"] > p["ls"] + 1 and f["first_seq"] == p["ls"] + 1
            else:
                assert m["prev"] != p["id"] and f["prev_id"] == p["id"]
            assert body(clean[f["clean_msg_index"]]) not in {body(x) for x in lines}
        elif f["kind"] == "duplicate":
            assert body(lines[i]) == body(lines[i - 1])
            assert lines[i] != lines[i - 1]                         # later receive time
            assert body(lines[i]) == body(clean[f["clean_msg_index"]])
        elif f["kind"] == "swap":
            a, b = msgs[i], msgs[i + 1]
            assert seqs(a) > seqs(b)                                # later message first
            assert body(lines[i]) == body(clean[f["clean_msg_index"] + 1])
            assert body(lines[i + 1]) == body(clean[f["clean_msg_index"]])
            assert a["r"] < b["r"] and a["exch_ns"] >= b["exch_ns"]
        else:
            lv = m["levels"] if venue == "A" else [(m["side"], m["px"], m["size"])]
            assert (1, f["price_ticks"]) in {(s, p) for s, p, _ in lv}
            assert seqs(m) == ((f["first_seq"], f["last_seq"]) if venue == "A" else (f["prev_id"], f["id"]))


def test_faults_are_separated_by_snapshots(feeds):
    """Each fault resolves at its own snapshot, so the expected counters are additive."""
    d = feeds["faults"]
    doc = json.loads((d / "faults.json").read_text())
    for venue in "AB":
        msgs = read_a(d / "venueA.jsonl") if venue == "A" else read_b(d / "venueB.csv")
        snaps = [m["idx"] for m in msgs if m["kind"] in ("snapshot", "SB")]
        pos = sorted(f["msg_index"] for f in doc["faults"] if f["venue"] == venue)
        buckets = [sum(1 for s in snaps if s < p) for p in pos]
        assert len(set(buckets)) == len(buckets)


# ---------------------------------------------------------------------------
# Golden fixtures
# ---------------------------------------------------------------------------
def normalize_line_a(line: str, idx: int) -> list[list[str]]:
    d = json.loads(line)
    r, E = str(d["r"]), str(d["E"] * 1000)
    rows = []
    if d["e"] == "snapshot":
        s = str(d["seq"])
        lv = [("BID", x) for x in d["b"]] + [("ASK", x) for x in d["a"]]
        rows.append(["A", "SNAPSHOT_BEGIN", "NONE", "0", str(len(lv)), E, r, s, s, "-1", str(idx), "0"])
        rows += [["A", "SNAPSHOT_LEVEL", sd, str(dec(p, 2)), str(dec(q, 3)), E, r, s, s, "-1", str(idx), "0"]
                 for sd, (p, q) in lv]
        rows.append(["A", "SNAPSHOT_END", "NONE", "0", "0", E, r, s, s, "-1", str(idx), "1"])
    elif d["e"] == "depth":
        lv = [("BID", x) for x in d["b"]] + [("ASK", x) for x in d["a"]]
        for j, (sd, (p, q)) in enumerate(lv):
            qq = dec(q, 3)
            rows.append(["A", "SET" if qq else "DELETE", sd, str(dec(p, 2)), str(qq), E, r,
                         str(d["first_seq"]), str(d["last_seq"]), "-1", str(idx), "1" if j == len(lv) - 1 else "0"])
    else:
        rows.append(["A", "TRADE", "BID" if d["side"] == "buy" else "ASK", str(dec(d["px"], 2)),
                     str(dec(d["qty"], 3)), E, r, str(d["tid"]), str(d["tid"]), "-1", str(idx), "1"])
    return rows


def normalize_row_b(r: dict, idx: int) -> list[str]:
    typ = {"SB": "SNAPSHOT_BEGIN", "SL": "SNAPSHOT_LEVEL", "SE": "SNAPSHOT_END", "T": "TRADE"}.get(r["kind"])
    if r["kind"] == "U":
        typ = {"N": "ADD", "C": "MODIFY", "D": "DELETE"}[r["action"]]
    side = {"": "NONE", "1": "BID", "2": "ASK"}[r["side"]]
    px = str(int(r["px_e4"]) // 100) if r["px_e4"] else "0"
    qty = r["size"] if r["size"] and r["kind"] != "SE" else "0"
    prev = r["prev_id"] or "-1"
    return ["B", typ, side, px, qty, r["exch_ns"], r["recv_ns"], r["id"], r["id"], prev, str(idx), "1"]


def test_golden_expected_events_are_the_independent_normalisation():
    lines = (GOLDEN / "venueA.jsonl").read_text().splitlines()
    exp_a = [r.split(",") for r in (GOLDEN / "expected_events_A.csv").read_text().splitlines()]
    got_a = [rw for i, l in enumerate(lines) for rw in normalize_line_a(l, i)]
    assert exp_a[0][0] == "venue" and exp_a[1:] == got_a
    with (GOLDEN / "venueB.csv").open() as fh:
        rows = list(csv.DictReader(fh))
    exp_b = [r.split(",") for r in (GOLDEN / "expected_events_B.csv").read_text().splitlines()]
    assert exp_b[1:] == [normalize_row_b(r, i) for i, r in enumerate(rows)]


def test_golden_contents_and_counters():
    doc = json.loads((GOLDEN / "faults.json").read_text())
    for v in "AB":
        kinds = sorted(f["kind"] for f in doc["faults"] if f["venue"] == v)
        assert kinds == ["crossed", "drop", "duplicate"]
    na = len((GOLDEN / "venueA.jsonl").read_text().splitlines())
    nb = len((GOLDEN / "venueB.csv").read_text().splitlines()) - 1
    assert 30 <= na <= 60 and 30 <= nb <= 100
    cnt = json.loads((GOLDEN / "expected_counters.json").read_text())
    for v in "AB":
        for k, val in doc["expected_counters"][v].items():
            assert cnt[v][k] == val
        lat = cnt[v]["latency"]
        assert lat["count"] == (na if v == "A" else nb)


def test_golden_merge_and_export_independent():
    """Re-derive expected_merged.csv and expected_export.csv from the golden feeds."""
    a = read_a(GOLDEN / "venueA.jsonl")
    b = read_b(GOLDEN / "venueB.csv")
    ev = sorted([(m["r"], 0, i) for i, m in enumerate(a)] + [(m["r"], 1, i) for i, m in enumerate(b)])
    seqs = {0: Seq("A"), 1: Seq("B")}
    snap_acc: dict | None = None
    merged, last = [], None
    for r, v, i in ev:
        sq = seqs[v]
        if v == 0:
            m = a[i]
            if m["kind"] == "snapshot":
                sq.snapshot(m)
            elif m["kind"] == "depth":
                sq.update(m)
        else:
            m = b[i]
            if m["kind"] == "SB":
                snap_acc = {"kind": "snapshot", "seq": m["id"], "levels": []}
            elif m["kind"] == "SL":
                snap_acc["levels"].append((m["side"], m["px"], m["size"]))
            elif m["kind"] == "SE":
                sq.snapshot(snap_acc)
            elif m["kind"] == "U":
                sq.update(m)
        bid = ask = None
        live = 0
        for vid, s in seqs.items():
            if s.state != "LIVE":
                continue
            live |= 1 << vid
            for side in (1, 2):
                if s.book[side]:
                    px = max(s.book[side]) if side == 1 else min(s.book[side])
                    q = s.book[side][px]
                    cur = bid if side == 1 else ask
                    better = cur is None or (px > cur[0] if side == 1 else px < cur[0])
                    if better:
                        cur = [px, q, 1 << vid]
                    elif px == cur[0]:
                        cur = [px, cur[1] + q, cur[2] | (1 << vid)]
                    if side == 1:
                        bid = cur
                    else:
                        ask = cur
        q = tuple((bid or [0, 0, 0]) + (ask or [0, 0, 0]) + [live])
        if q != (last or (0,) * 7):
            merged.append((r,) + q)
            last = q
    exp = [tuple(int(x) for x in l.split(",")) for l in
           (GOLDEN / "expected_merged.csv").read_text().splitlines()[1:]]
    assert merged == exp
    by_us = {}
    for q in merged:
        by_us[q[0] // 1000] = q
    rows = []
    for ts in sorted(by_us):
        q = by_us[ts]
        if q[7] and q[3] and q[6] and q[1] < q[4]:
            rows.append(f"{ts},{q[1] / 100:.2f},{q[4] / 100:.2f},{(q[1] + q[4]) / 200:.3f}")
    exp_rows = (GOLDEN / "expected_export.csv").read_text().splitlines()
    assert exp_rows[0] == "ts_us,bid,ask,mid"
    assert rows == exp_rows[1:]
    ts = [int(x.split(",")[0]) for x in exp_rows[1:]]
    assert all(b > a for a, b in zip(ts, ts[1:]))


def test_golden_is_small():
    total = sum(p.stat().st_size for p in GOLDEN.iterdir())
    assert total < 50_000, total


def test_golden_export_runs_through_pta():
    pytest.importorskip("pta")
    from pta.data.loaders import load_market
    m = load_market(GOLDEN / "expected_export.csv")
    assert (m["ask"] > m["bid"]).all() and m["ts_us"].is_monotonic_increasing


def test_synthetic_label():
    meta = json.loads((GOLDEN / "meta.json").read_text())
    assert meta["synthetic"] is True
    assert "not any real venue" in meta["note"]
