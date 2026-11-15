#!/usr/bin/env python3
"""Seeded SYNTHETIC two-venue L2 feed generator for Project 3.

The two feeds ("Venue A" JSON lines, "Venue B" CSV) are modelled on common
conventions of sequence-numbered and chained-id depth feeds. They are NOT the
format of any real venue and must never be described as one. See DESIGN.md
sections 2-5 for the exact formats; this file is the reference producer.

Both venues describe the SAME underlying market: the touch of each venue
follows the mid-price path of pta.data.synthetic.simulate_market (Project 1),
with its own depth, its own activity, its own sequence numbers, its own
latency and (Venue B) its own exchange-clock offset.

Usage:
    python3 tools/gen_feeds.py --seed 7 --hours 0.25 --out DIR [--faults]
    python3 tools/gen_feeds.py --golden --out tests/golden

Outputs (DIR): venueA.jsonl, venueB.csv, truth_A.csv, truth_B.csv, faults.json,
meta.json; with --expected (implied by --golden) also expected_events_A.csv,
expected_events_B.csv, expected_counters.json, expected_merged.csv,
expected_export.csv produced by the reference pipeline in this file.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
import time
from dataclasses import dataclass, replace
from pathlib import Path

import numpy as np
from pta.data.synthetic import MarketConfig, simulate_market

T0_NS = 1_700_000_000_000_000_000      # epoch of the synthetic session (ns)
STEP_NS = 100_000_000                  # pta grid: dt_us = 100_000
TOP_N = 10                             # levels per side in truth checkpoints
BID, ASK = 1, 2                        # internal side codes (match DESIGN Side enum)
LAST_IN_MSG = 1

# Normalised event type names (DESIGN section 1)
SNAP_BEGIN, SNAP_LEVEL, SNAP_END = "SNAPSHOT_BEGIN", "SNAPSHOT_LEVEL", "SNAPSHOT_END"
ADD, MODIFY, SET, DELETE, TRADE = "ADD", "MODIFY", "SET", "DELETE", "TRADE"
SIDE_NAME = {0: "NONE", BID: "BID", ASK: "ASK"}

COUNTER_KEYS = [
    "messages", "events", "parse_errors", "trades", "snapshots", "gaps", "resyncs",
    "duplicates_ignored", "out_of_order", "crossed_detected", "locked_detected",
    "checkpoints_matched", "checkpoints_mismatched", "checkpoints_skipped_stale",
    "snapshots_ignored_late", "snapshot_errors", "buffered_dropped", "buffered_max",
    "buffer_overflows", "add_on_existing", "modify_missing", "delete_missing",
]
FAULT_COUNTER_KEYS = [
    "gaps", "resyncs", "duplicates_ignored", "out_of_order", "crossed_detected",
    "locked_detected", "checkpoints_matched", "checkpoints_mismatched",
    "checkpoints_skipped_stale", "snapshots_ignored_late", "parse_errors",
]


@dataclass(frozen=True)
class VenueParams:
    name: str
    vid: int                     # 0 = A, 1 = B
    touch_delay_ns: tuple[int, int]
    widen_switch_prob: float     # per-step prob of toggling a one-tick widening per side
    noise_rate: float            # Poisson mean of noise actions per step
    pair_prob: float             # prob a noise group carries two actions
    trade_prob: float            # prob of a trade per step
    window: int                  # max levels per side (price window from the touch)
    min_depth: int               # refill below this many levels
    snap_every_steps: int
    snap_phase: int
    lat_base_ns: int
    lat_extra_mean_ns: int
    clock_offset_ns: int         # venue exchange clock minus local clock
    seq_start: int
    trade_id_start: int


def default_params(golden: bool) -> tuple[VenueParams, VenueParams]:
    a = VenueParams("A", 0, (100_000, 2_000_000), 0.0, 2.5, 0.3, 0.35, 20, 8,
                    300, 0, 300_000, 200_000, 0, 1_000_000, 1)
    b = VenueParams("B", 1, (5_000_000, 40_000_000), 0.05, 2.5, 0.0, 0.35, 20, 8,
                    200, 100, 1_200_000, 600_000, 300_000, 5_000_000, 900_000)
    if golden:
        a = replace(a, noise_rate=1.2, trade_prob=0.25, window=3, min_depth=2,
                    snap_every_steps=3, snap_phase=0)
        b = replace(b, noise_rate=1.2, trade_prob=0.2, window=3, min_depth=2,
                    snap_every_steps=3, snap_phase=2, widen_switch_prob=0.1)
    return a, b


# ----------------------------------------------------------------------------
# Oracle book and simulation (venue-agnostic records)
# ----------------------------------------------------------------------------
class OracleBook:
    __slots__ = ("bids", "asks")

    def __init__(self) -> None:
        self.bids: dict[int, int] = {}
        self.asks: dict[int, int] = {}

    def side(self, s: int) -> dict[int, int]:
        return self.bids if s == BID else self.asks

    def get(self, s: int, p: int) -> int:
        return self.side(s).get(p, 0)

    def set(self, s: int, p: int, q: int) -> None:
        d = self.side(s)
        if q == 0:
            d.pop(p, None)
        else:
            d[p] = q

    def levels(self, s: int) -> list[tuple[int, int]]:
        d = self.side(s)
        return sorted(d.items(), reverse=(s == BID))

    def check(self) -> None:
        if self.bids and self.asks and max(self.bids) >= min(self.asks):
            raise AssertionError("oracle book crossed or locked")


class Group:
    """A set of level changes that the venue publishes as one update unit."""

    def __init__(self, book: OracleBook) -> None:
        self.book = book
        self.before: dict[tuple[int, int], int] = {}

    def set(self, s: int, p: int, q: int) -> None:
        k = (s, p)
        if k not in self.before:
            self.before[k] = self.book.get(s, p)
        self.book.set(s, p, q)

    def changes(self) -> list[tuple[int, int, int, int]]:
        out = []
        for (s, p), b in self.before.items():
            a = self.book.get(s, p)
            if a != b:
                out.append((s, p, b, a))
        return out


def _poisson(rnd: random.Random, lam: float) -> int:
    l, k, p = math.exp(-lam), 0, 1.0
    while True:
        p *= rnd.random()
        if p <= l:
            return k
        k += 1


def _qty(rnd: random.Random) -> int:
    return rnd.randint(1, 50) * 100        # lots of 0.001 -> 0.1 .. 5.0 units


def reference_touch(seed: int, hours: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    market, flow = simulate_market(MarketConfig(seed=seed, hours=hours))
    bid = np.rint(market["bid"].to_numpy() * 100).astype(np.int64)
    ask = np.rint(market["ask"].to_numpy() * 100).astype(np.int64)
    assert np.all(ask > bid)
    return bid, ask, flow


def simulate_venue(p: VenueParams, ref_bid: np.ndarray, ref_ask: np.ndarray,
                   flow: np.ndarray, rnd: random.Random) -> list[tuple]:
    """Return records in publication order.

    ('S', t, bids, asks)              snapshot of the full book (<= window levels/side)
    ('G', t, [(side, px, before, after), ...])   one update unit
    ('T', t, aggressor_side, px, qty)  trade (book effect follows as a 'G')
    t is true time in ns relative to T0.
    """
    book = OracleBook()
    recs: list[tuple] = []
    W = p.window
    wb = wa = 0
    geo = math.log(1 - 0.35)

    def target(s: int) -> tuple[int, int]:
        return int(ref_bid[s]) - wb, int(ref_ask[s]) + wa

    # initial book at step 0
    tb, ta = target(0)
    book.set(BID, tb, _qty(rnd))
    book.set(ASK, ta, _qty(rnd))
    for d in range(1, W):
        if rnd.random() < 0.5:
            book.set(BID, tb - d, _qty(rnd))
        if rnd.random() < 0.5:
            book.set(ASK, ta + d, _qty(rnd))

    def snapshot(t: int) -> None:
        recs.append(("S", t, book.levels(BID), book.levels(ASK)))

    def noise(g: Group, tb: int, ta: int) -> None:
        s = BID if rnd.random() < 0.5 else ASK
        d = min(int(math.log(1.0 - rnd.random()) / geo), W - 1)
        px = tb - d if s == BID else ta + d
        cur = book.get(s, px)
        if cur:
            if d == 0 or rnd.random() < 0.55:
                q = _qty(rnd)
                if q == cur:
                    q = cur + 100
                g.set(s, px, q)
            else:
                g.set(s, px, 0)
        else:
            g.set(s, px, _qty(rnd))
        for s2, ref in ((BID, tb), (ASK, ta)):
            side = book.side(s2)
            if len(side) < p.min_depth:
                empty = [d2 for d2 in range(1, W)
                         if (ref - d2 if s2 == BID else ref + d2) not in side]
                if empty:
                    d2 = rnd.choice(empty)
                    g.set(s2, ref - d2 if s2 == BID else ref + d2, _qty(rnd))

    n = len(ref_bid)
    for step in range(n):
        t_step = step * STEP_NS
        if p.widen_switch_prob > 0:
            if rnd.random() < p.widen_switch_prob:
                wb ^= 1
            if rnd.random() < p.widen_switch_prob:
                wa ^= 1
        if step == 0 or step % p.snap_every_steps == p.snap_phase:
            snapshot(t_step)
        tb, ta = target(step)
        t_touch = t_step + rnd.randint(*p.touch_delay_ns)
        g = Group(book)
        for px in [x for x in book.bids if x > tb or x < tb - (W - 1)]:
            g.set(BID, px, 0)
        for px in [x for x in book.asks if x < ta or x > ta + (W - 1)]:
            g.set(ASK, px, 0)
        if tb not in book.bids:
            g.set(BID, tb, _qty(rnd))
        if ta not in book.asks:
            g.set(ASK, ta, _qty(rnd))
        ch = g.changes()
        if ch:
            recs.append(("G", t_touch, ch))
        book.check()
        kinds = ["n"] * _poisson(rnd, p.noise_rate)
        if rnd.random() < p.trade_prob:
            kinds.append("t")
        rnd.shuffle(kinds)
        hi = t_step + STEP_NS - 1_000_000
        times = sorted(rnd.randint(t_touch + 1_000, hi) for _ in kinds)
        for kind, t in zip(kinds, times):
            g = Group(book)
            if kind == "n":
                noise(g, tb, ta)
                if rnd.random() < p.pair_prob:
                    noise(g, tb, ta)
            else:
                buy_p = 0.5 + 0.25 * math.tanh(float(flow[step]))
                if rnd.random() < buy_p:
                    s_book, aggr = ASK, BID
                    px = min(book.asks)
                else:
                    s_book, aggr = BID, ASK
                    px = max(book.bids)
                lvl = book.get(s_book, px)
                q = min(lvl, rnd.randint(1, 20) * 100)
                recs.append(("T", t, aggr, px, q))
                g.set(s_book, px, lvl - q)
            ch = g.changes()
            if ch:
                recs.append(("G", t, ch))
            book.check()
    return recs


# ----------------------------------------------------------------------------
# Venue encoders: records -> structured messages (dicts), one per raw line
# ----------------------------------------------------------------------------
def encode_a(recs: list[tuple], p: VenueParams) -> list[dict]:
    seq, tid, out = p.seq_start, p.trade_id_start - 1, []
    for r in recs:
        if r[0] == "S":
            out.append({"k": "snapshot", "t": r[1], "seq": seq, "b": list(r[2]), "a": list(r[3])})
        elif r[0] == "G":
            b = sorted(((px, a) for s, px, _, a in r[2] if s == BID), reverse=True)
            a_ = sorted((px, a) for s, px, _, a in r[2] if s == ASK)
            fs = seq + 1
            seq += len(b) + len(a_)
            out.append({"k": "depth", "t": r[1], "fs": fs, "ls": seq, "b": b, "a": a_})
        else:
            tid += 1
            out.append({"k": "trade", "t": r[1], "tid": tid, "side": r[2], "px": r[3], "qty": r[4]})
    return out


def encode_b(recs: list[tuple], p: VenueParams, rnd: random.Random) -> list[dict]:
    uid, tid, grp, out = p.seq_start, p.trade_id_start - 1, 0, []
    for r in recs:
        if r[0] == "S":
            grp += 1
            lv = [(BID, px, q) for px, q in r[2]] + [(ASK, px, q) for px, q in r[3]]
            out.append({"k": "SB", "t": r[1], "id": uid, "size": len(lv), "grp": grp})
            for s, px, q in lv:
                out.append({"k": "SL", "t": r[1], "id": uid, "side": s, "px": px, "size": q, "grp": grp})
            out.append({"k": "SE", "t": r[1], "id": uid, "grp": grp})
        elif r[0] == "G":
            def order(c):
                s, px, b, a = c
                cat = 0 if a == 0 else (1 if b > 0 else 2)
                return (cat, s, -px if s == BID else px)
            for s, px, b, a in sorted(r[2], key=order):
                act = "D" if a == 0 else ("C" if b > 0 else "N")
                prev = uid
                uid += rnd.randint(1, 3)
                out.append({"k": "U", "t": r[1], "id": uid, "prev": prev, "act": act,
                            "side": s, "px": px, "size": a})
        else:
            tid += 1
            out.append({"k": "T", "t": r[1], "id": tid, "side": r[2], "px": r[3], "size": r[4]})
    return out


def _is_update(m: dict) -> bool:
    return m["k"] in ("depth", "U")


def _is_snap_start(m: dict) -> bool:
    return m["k"] in ("snapshot", "SB")


def _book_before(msgs: list[dict], upto: int) -> OracleBook:
    """Book from structured clean messages [0, upto) (no sequencing logic needed)."""
    bk = OracleBook()
    for m in msgs[:upto]:
        k = m["k"]
        if k == "snapshot":
            bk = OracleBook()
            for px, q in m["b"]:
                bk.set(BID, px, q)
            for px, q in m["a"]:
                bk.set(ASK, px, q)
        elif k == "depth":
            for px, q in m["b"]:
                bk.set(BID, px, q)
            for px, q in m["a"]:
                bk.set(ASK, px, q)
        elif k == "SB":
            bk = OracleBook()
        elif k == "SL":
            bk.set(m["side"], m["px"], m["size"])
        elif k == "U":
            bk.set(m["side"], m["px"], m["size"])
    return bk


# ----------------------------------------------------------------------------
# Fault injection
# ----------------------------------------------------------------------------
FAULT_KINDS = ("drop", "duplicate", "swap", "crossed")


def inject_faults(msgs: list[dict], venue: str, kinds: tuple[str, ...]) -> tuple[list[dict], list[dict], list[int]]:
    """Return (faulted msgs, manifest entries, clean index of each faulted msg)."""
    starts = [i for i, m in enumerate(msgs) if _is_snap_start(m)]
    intervals = []
    for j in range(len(starts) - 1):
        lo, hi = starts[j], starts[j + 1]
        ups = [i for i in range(lo, hi) if _is_update(msgs[i])]
        # need an adjacent update pair followed by at least one more update
        pairs = [i for i in ups if i + 1 in ups and any(u > i + 1 for u in ups)]
        if pairs:
            intervals.append((j, pairs))
    if len(intervals) < len(kinds) + 1:
        raise SystemExit(f"venue {venue}: not enough snapshot intervals for faults "
                         f"({len(intervals)} eligible); use a longer --hours")
    m = len(intervals)
    chosen = [intervals[(f + 1) * m // (len(kinds) + 1)] for f in range(len(kinds))]
    plan: dict[int, tuple[str, dict]] = {}
    for kind, (_, pairs) in zip(kinds, chosen):
        i = pairs[len(pairs) // 2]
        info: dict = {}
        if kind == "crossed":
            mm = dict(msgs[i])
            if mm["k"] == "depth":
                bk = _book_before(msgs, i + 1)
                bad = min(bk.asks) + 1
                mm["b"] = [(bad, 700)] + list(mm["b"])
            else:
                bk = _book_before(msgs, i)
                bad = min(bk.asks) + 1
                mm.update(act="N", side=BID, px=bad, size=700)
            info = {"msg": mm, "price_ticks": bad}
        plan[i] = (kind, info)

    out: list[dict] = []
    src: list[int] = []
    manifest: list[dict] = []
    i = 0
    while i < len(msgs):
        m = msgs[i]
        if i not in plan:
            out.append(m)
            src.append(i)
            i += 1
            continue
        kind, info = plan[i]
        e: dict = {"kind": kind, "venue": venue, "clean_msg_index": i}
        if m["k"] == "depth":
            e.update(first_seq=m["fs"], last_seq=m["ls"])
        else:
            e.update(id=m["id"], prev_id=m["prev"])
        if kind == "drop":
            e["msg_index"] = len(out)            # first message after the hole
            e["note"] = "message removed; msg_index is the message that now follows the hole"
            i += 1
        elif kind == "duplicate":
            out.append(m)
            src.append(i)
            e["msg_index"] = len(out)            # the copy, right after the original
            out.append(dict(m))
            src.append(i)
            i += 1
        elif kind == "swap":
            e["msg_index"] = len(out)            # pair occupies msg_index, msg_index+1
            e["note"] = "messages clean i and i+1 delivered in reverse order"
            out.append(msgs[i + 1])
            src.append(i + 1)
            out.append(m)
            src.append(i)
            i += 2
        else:  # crossed
            e["msg_index"] = len(out)
            e["side"] = "BID"
            e["price_ticks"] = info["price_ticks"]
            e["note"] = "bid level injected at best ask + 1 tick (A: entry added; B: row replaced)"
            out.append(info["msg"])
            src.append(i)
            i += 1
        manifest.append(e)
    return out, manifest, src


# ----------------------------------------------------------------------------
# Timestamps and serialisation
# ----------------------------------------------------------------------------
def draw_latencies(n: int, p: VenueParams, rnd: random.Random) -> list[int]:
    mean = p.lat_extra_mean_ns
    return [p.lat_base_ns + int(rnd.expovariate(1.0 / mean)) for _ in range(n)]


def assign_recv(msgs: list[dict], src: list[int], lat: list[int], p: VenueParams) -> list[int]:
    """Local receive time per message in file order (strictly increasing, except
    the rows of one Venue B snapshot which share the time of their SB row)."""
    out, prev, prev_grp = [], T0_NS - 1, None
    for m, ci in zip(msgs, src):
        g = m.get("grp")
        if g is not None and g == prev_grp and m["k"] != "SB":
            out.append(prev)
            continue
        r = max(prev + 1, T0_NS + m["t"] + lat[ci])
        out.append(r)
        prev, prev_grp = r, g
    return out


def px_str(t: int) -> str:
    return f"{t // 100}.{t % 100:02d}"


def qty_str(q: int) -> str:
    return f"{q // 1000}.{q % 1000:03d}"


def _lv(levels) -> str:
    return "[" + ",".join(f'["{px_str(px)}","{qty_str(q)}"]' for px, q in levels) + "]"


def serialize_a(msgs: list[dict], recv: list[int], p: VenueParams) -> list[str]:
    lines = []
    for m, r in zip(msgs, recv):
        e_us = (T0_NS + m["t"] + p.clock_offset_ns) // 1000
        k = m["k"]
        if k == "snapshot":
            lines.append(f'{{"r":{r},"e":"snapshot","E":{e_us},"seq":{m["seq"]},'
                         f'"b":{_lv(m["b"])},"a":{_lv(m["a"])}}}')
        elif k == "depth":
            lines.append(f'{{"r":{r},"e":"depth","E":{e_us},"first_seq":{m["fs"]},'
                         f'"last_seq":{m["ls"]},"b":{_lv(m["b"])},"a":{_lv(m["a"])}}}')
        else:
            side = "buy" if m["side"] == BID else "sell"
            lines.append(f'{{"r":{r},"e":"trade","E":{e_us},"tid":{m["tid"]},'
                         f'"px":"{px_str(m["px"])}","qty":"{qty_str(m["qty"])}","side":"{side}"}}')
    return lines


B_HEADER = "recv_ns,kind,exch_ns,id,prev_id,action,side,px_e4,size"


def serialize_b(msgs: list[dict], recv: list[int], p: VenueParams) -> list[str]:
    lines = [B_HEADER]
    for m, r in zip(msgs, recv):
        e = T0_NS + m["t"] + p.clock_offset_ns
        k = m["k"]
        if k == "SB":
            lines.append(f"{r},SB,{e},{m['id']},,,,,{m['size']}")
        elif k == "SL":
            lines.append(f"{r},SL,{e},{m['id']},,,{m['side']},{m['px'] * 100},{m['size']}")
        elif k == "SE":
            lines.append(f"{r},SE,{e},{m['id']},,,,,")
        elif k == "U":
            lines.append(f"{r},U,{e},{m['id']},{m['prev']},{m['act']},{m['side']},{m['px'] * 100},{m['size']}")
        else:
            lines.append(f"{r},T,{e},{m['id']},,,{m['side']},{m['px'] * 100},{m['size']}")
    return lines


# ----------------------------------------------------------------------------
# Reference normaliser + sequencer + merge + export (implements DESIGN.md)
# ----------------------------------------------------------------------------
def _dec(s: str, scale: int) -> int:
    ip, _, fp = s.partition(".")
    if not ip.isdigit() or (fp and not fp.isdigit()) or len(fp) > scale:
        raise ValueError(s)
    return int(ip) * 10 ** scale + int((fp + "0" * scale)[:scale])


def normalize_a(line: str, idx: int) -> list[tuple]:
    d = json.loads(line)
    r, E, e = d["r"], d["E"] * 1000, d["e"]
    ev = []
    if e == "snapshot":
        s = d["seq"]
        lv = [(BID, x) for x in d["b"]] + [(ASK, x) for x in d["a"]]
        ev.append(("A", SNAP_BEGIN, 0, 0, len(lv), E, r, s, s, -1, idx, 0))
        for side, (px, q) in lv:
            ev.append(("A", SNAP_LEVEL, side, _dec(px, 2), _dec(q, 3), E, r, s, s, -1, idx, 0))
        ev.append(("A", SNAP_END, 0, 0, 0, E, r, s, s, -1, idx, LAST_IN_MSG))
    elif e == "depth":
        fs, ls = d["first_seq"], d["last_seq"]
        lv = [(BID, x) for x in d["b"]] + [(ASK, x) for x in d["a"]]
        if not lv:
            raise ValueError("empty depth")
        for j, (side, (px, q)) in enumerate(lv):
            qq = _dec(q, 3)
            ev.append(("A", SET if qq > 0 else DELETE, side, _dec(px, 2), qq, E, r, fs, ls, -1, idx,
                       LAST_IN_MSG if j == len(lv) - 1 else 0))
    elif e == "trade":
        side = {"buy": BID, "sell": ASK}[d["side"]]
        ev.append(("A", TRADE, side, _dec(d["px"], 2), _dec(d["qty"], 3), E, r, d["tid"], d["tid"], -1,
                   idx, LAST_IN_MSG))
    else:
        raise ValueError(e)
    return ev


def normalize_b(line: str, idx: int) -> list[tuple]:
    f = line.split(",")
    if len(f) != 9:
        raise ValueError(line)
    r, kind, e, i = int(f[0]), f[1], int(f[2]), int(f[3])

    def px() -> int:
        v = int(f[7])
        if v % 100:
            raise ValueError("price not on tick")
        return v // 100
    if kind == "SB":
        return [("B", SNAP_BEGIN, 0, 0, int(f[8]), e, r, i, i, -1, idx, LAST_IN_MSG)]
    if kind == "SL":
        return [("B", SNAP_LEVEL, int(f[6]), px(), int(f[8]), e, r, i, i, -1, idx, LAST_IN_MSG)]
    if kind == "SE":
        return [("B", SNAP_END, 0, 0, 0, e, r, i, i, -1, idx, LAST_IN_MSG)]
    if kind == "U":
        t = {"N": ADD, "C": MODIFY, "D": DELETE}[f[5]]
        return [("B", t, int(f[6]), px(), int(f[8]), e, r, i, i, int(f[4]), idx, LAST_IN_MSG)]
    if kind == "T":
        return [("B", TRADE, int(f[6]), px(), int(f[8]), e, r, i, i, -1, idx, LAST_IN_MSG)]
    raise ValueError(kind)


def events_csv(events: list[tuple]) -> list[str]:
    out = ["venue,type,side,price_ticks,qty_lots,exch_ts_ns,local_ts_ns,seq_first,seq_last,prev_seq,msg_index,flags"]
    for e in events:
        out.append(",".join([e[0], e[1], SIDE_NAME[e[2]]] + [str(x) for x in e[3:]]))
    return out


class RefSession:
    """Reference implementation of DESIGN.md section 6 (VenueSession)."""

    def __init__(self, venue: str, max_buffered: int = 1_000_000) -> None:
        self.venue = venue
        self.state = "INIT"
        self.bids: dict[int, int] = {}
        self.asks: dict[int, int] = {}
        self.last = -1
        self.hi = -1
        self.buf: list[tuple[tuple, list[tuple]]] = []
        self.buf_keys: set = set()
        self.max_buffered = max_buffered
        self.c = {k: 0 for k in COUNTER_KEYS}
        self.snap = None              # (seq, count, levels) while assembling
        self.cur_msg = None           # (msg_index, decision)
        self.pending_events: list[tuple] = []
        self.on_before_snapshot = None

    # -- book --
    def _side(self, s: int) -> dict[int, int]:
        return self.bids if s == BID else self.asks

    def _apply(self, e: tuple) -> None:
        t, s, px, q = e[1], e[2], e[3], e[4]
        d = self._side(s)
        if t == SET:
            d[px] = q
        elif t == ADD:
            if px in d:
                self.c["add_on_existing"] += 1
            d[px] = q
        elif t == MODIFY:
            if px not in d:
                self.c["modify_missing"] += 1
            d[px] = q
        elif t == DELETE:
            if px in d:
                del d[px]
            else:
                self.c["delete_missing"] += 1

    def _check_cross(self) -> None:
        if self.bids and self.asks:
            bb, ba = max(self.bids), min(self.asks)
            if bb > ba:
                self.c["crossed_detected"] += 1
                self.state = "STALE"
            elif bb == ba:
                self.c["locked_detected"] += 1
                self.state = "STALE"
        if self.state == "STALE":
            self.hi = max(self.hi, self.last)

    def top(self) -> tuple:
        if self.state != "LIVE":
            return (False, 0, 0, 0, 0)
        bp = max(self.bids) if self.bids else 0
        ap = min(self.asks) if self.asks else 0
        return (True, bp, self.bids.get(bp, 0), ap, self.asks.get(ap, 0))

    def levels(self, s: int, n: int) -> list[tuple[int, int]]:
        d = self._side(s)
        return sorted(d.items(), reverse=(s == BID))[:n]

    # -- sequencing --
    def _key(self, e: tuple):
        return (e[7], e[8]) if self.venue == "A" else (e[8],)

    def _hi_of(self, e: tuple) -> int:
        return e[8]

    def _continuous(self, e: tuple) -> bool:
        if self.venue == "A":
            return e[7] <= self.last + 1 <= e[8]
        return e[9] == self.last

    def _classify(self, e: tuple) -> str:
        seqv = e[8]
        if self.last >= 0 and seqv <= self.last:
            self.c["duplicates_ignored"] += 1
            return "drop"
        if self.state == "LIVE":
            if self._continuous(e):
                return "apply"
            self.c["gaps"] += 1
            self.state = "STALE"
            return "buffer"
        if self._key(e) in self.buf_keys:
            self.c["duplicates_ignored"] += 1
            return "drop"
        if seqv < self.hi:
            self.c["out_of_order"] += 1
        return "buffer"

    def on_event(self, e: tuple) -> bool:
        """Feed one normalised event. Returns True at the end of a raw message."""
        t = e[1]
        self.c["events"] += 1
        last_in = bool(e[11] & LAST_IN_MSG)
        if t in (SNAP_BEGIN, SNAP_LEVEL, SNAP_END):
            self._snap_event(e)
        elif t == TRADE:
            self.c["trades"] += 1
        else:
            if self.snap is not None:
                self.c["snapshot_errors"] += 1
                self.snap = None
            if self.cur_msg is None or self.cur_msg[0] != e[10]:
                self.cur_msg = (e[10], self._classify(e))
                self.pending_events = []
            dec = self.cur_msg[1]
            if dec == "apply":
                self._apply(e)
                if last_in:
                    self.last = e[8]
                    self.hi = max(self.hi, self.last)
                    self._check_cross()
            elif dec == "buffer":
                self.pending_events.append(e)
                if last_in:
                    self._buffer(self._key(e), self.pending_events)
                    self.hi = max(self.hi, e[8])
        if last_in:
            self.c["messages"] += 1
            self.cur_msg = None
        return last_in

    def _buffer(self, key, evs: list[tuple]) -> None:
        if len(self.buf) >= self.max_buffered:
            self.buf.clear()
            self.buf_keys.clear()
            self.c["buffer_overflows"] += 1
        self.buf.append((key, list(evs)))
        self.buf_keys.add(key)
        self.c["buffered_max"] = max(self.c["buffered_max"], len(self.buf))

    def _snap_event(self, e: tuple) -> None:
        t = e[1]
        if t == SNAP_BEGIN:
            if self.snap is not None:
                self.c["snapshot_errors"] += 1
            self.snap = (e[7], e[4], [])
        elif t == SNAP_LEVEL:
            if self.snap is None or self.snap[0] != e[7]:
                self.c["snapshot_errors"] += 1
                self.snap = None
                return
            self.snap[2].append((e[2], e[3], e[4]))
        else:
            sn, self.snap = self.snap, None
            if sn is None or sn[0] != e[7] or sn[1] != len(sn[2]):
                self.c["snapshot_errors"] += 1
                return
            self._on_snapshot(sn[0], sn[2])

    def _load(self, levels) -> None:
        self.bids, self.asks = {}, {}
        for s, px, q in levels:
            self._side(s)[px] = q

    def _on_snapshot(self, S: int, levels) -> None:
        self.c["snapshots"] += 1
        if self.on_before_snapshot is not None:
            self.on_before_snapshot(self, S)
        if self.state == "LIVE":
            if S < self.last:
                self.c["snapshots_ignored_late"] += 1
                return
            if S == self.last:
                img_b = {px: q for s, px, q in levels if s == BID}
                img_a = {px: q for s, px, q in levels if s == ASK}
                if img_b == self.bids and img_a == self.asks:
                    self.c["checkpoints_matched"] += 1
                else:
                    self.c["checkpoints_mismatched"] += 1
                self._load(levels)
                self._check_cross()
                return
            self.c["gaps"] += 1
            self.c["checkpoints_skipped_stale"] += 1
            self._resync(S, levels, count=True)
            return
        if self.state == "STALE":
            self.c["checkpoints_skipped_stale"] += 1
            self._resync(S, levels, count=True)
        else:
            self._resync(S, levels, count=False)

    def _resync(self, S: int, levels, count: bool) -> None:
        self._load(levels)
        self.last = S
        items = sorted(self.buf, key=lambda kv: kv[0])
        rest = None
        for j, (key, evs) in enumerate(items):
            if key[-1] <= self.last:
                self.c["buffered_dropped"] += 1
                continue
            if self._continuous(evs[0]):
                for ev in evs:
                    self._apply(ev)
                self.last = evs[0][8]
            else:
                rest = items[j:]
                break
        if rest is not None:
            self.c["gaps"] += 1
            self.state = "STALE"
            self.bids, self.asks = {}, {}
            self.buf = rest
            self.buf_keys = {k for k, _ in rest}
            return
        self.buf, self.buf_keys = [], set()
        self.state = "LIVE"
        self.hi = self.last
        if count:
            self.c["resyncs"] += 1
        self._check_cross()


def latency_summary(samples: list[int]) -> dict:
    s = sorted(samples)
    n = len(s)
    if n == 0:
        return {"count": 0, "sum_ns": 0, "mean_ns": 0.0, "p50_ns": 0, "p99_ns": 0, "min_ns": 0, "max_ns": 0}

    def rank(pct: int) -> int:
        return s[max((pct * n + 99) // 100 - 1, 0)]
    tot = sum(s)
    return {"count": n, "sum_ns": tot, "mean_ns": round(tot / n, 1), "p50_ns": rank(50),
            "p99_ns": rank(99), "min_ns": s[0], "max_ns": s[-1]}


def reference_pipeline(a_lines: list[str], b_lines: list[str]) -> dict:
    """Run normaliser, sessions, merger and exporter exactly as DESIGN.md specifies."""
    feeds = {"A": [normalize_a(l, i) for i, l in enumerate(a_lines)],
             "B": [normalize_b(l, i) for i, l in enumerate(b_lines[1:])]}
    sess = {v: RefSession(v) for v in "AB"}
    lat = {v: [m[0][6] - m[0][5] for m in feeds[v]] for v in "AB"}
    tops = {v: (False, 0, 0, 0, 0) for v in "AB"}
    order = sorted([(m[0][6], vid, i, v) for vid, v in enumerate("AB") for i, m in enumerate(feeds[v])])
    merged, last_q = [], (0, 0, 0, 0, 0, 0, 0)
    for ts, _, i, v in order:
        for ev in feeds[v][i]:
            sess[v].on_event(ev)
        tops[v] = sess[v].top()
        q = merge_quote(tops)
        if q != last_q:
            merged.append((ts,) + q)
            last_q = q
    for v in "AB":
        sess[v].c["messages"] = len(feeds[v])
    return {"events": feeds, "sessions": sess, "latency": {v: latency_summary(lat[v]) for v in "AB"},
            "merged": merged, "export": export_rows(merged)}


def merge_quote(tops: dict) -> tuple:
    """(bid_px, bid_qty, bid_mask, ask_px, ask_qty, ask_mask, live_mask)."""
    bp = bq = bm = ap = aq = am = live = 0
    for vid, v in enumerate("AB"):
        t = tops[v]
        if not t[0]:
            continue
        bit = 1 << vid
        live |= bit
        if t[1] and t[2]:
            if bm == 0 or t[1] > bp:
                bp, bq, bm = t[1], t[2], bit
            elif t[1] == bp:
                bq += t[2]
                bm |= bit
        if t[3] and t[4]:
            if am == 0 or t[3] < ap:
                ap, aq, am = t[3], t[4], bit
            elif t[3] == ap:
                aq += t[4]
                am |= bit
    return (bp, bq, bm, ap, aq, am, live)


def export_rows(merged: list[tuple]) -> list[str]:
    last_per_us: dict[int, tuple] = {}
    for q in merged:
        last_per_us[q[0] // 1000] = q
    rows = ["ts_us,bid,ask,mid"]
    for ts_us in sorted(last_per_us):
        _, bp, _, bm, ap, _, am, live = last_per_us[ts_us]
        if live and bm and am and bp < ap:
            s5 = (bp + ap) * 5
            rows.append(f"{ts_us},{px_str(bp)},{px_str(ap)},{s5 // 1000}.{s5 % 1000:03d}")
    return rows


MERGED_HEADER = "local_ts_ns,bid_ticks,bid_lots,bid_venues,ask_ticks,ask_lots,ask_venues,live_venues"


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------
def truth_rows(msgs: list[dict], recv: list[int], venue: str) -> list[str]:
    rows = ["checkpoint,seq,msg_index,local_ts_ns,side,level,price_ticks,qty_lots"]
    cp = 0
    for idx, m in enumerate(msgs):
        if m["k"] == "snapshot":
            seq, bids, asks = m["seq"], m["b"], m["a"]
        elif m["k"] == "SB":
            seq = m["id"]
            lv = [x for x in msgs[idx + 1: idx + 1 + m["size"]]]
            bids = [(x["px"], x["size"]) for x in lv if x["side"] == BID]
            asks = [(x["px"], x["size"]) for x in lv if x["side"] == ASK]
        else:
            continue
        for side, lvls in (("BID", bids), ("ASK", asks)):
            for lvl, (px, q) in enumerate(lvls[:TOP_N]):
                rows.append(f"{cp},{seq},{idx},{recv[idx]},{side},{lvl},{px},{q}")
        cp += 1
    return rows


def expected_counters(n_snap: int, kinds: list[str]) -> dict:
    n = {k: kinds.count(k) for k in FAULT_KINDS}
    stale = n["drop"] + n["swap"] + n["crossed"]
    return {"gaps": n["drop"] + n["swap"], "resyncs": stale, "duplicates_ignored": n["duplicate"],
            "out_of_order": n["swap"], "crossed_detected": n["crossed"], "locked_detected": 0,
            "checkpoints_matched": n_snap - 1 - stale, "checkpoints_mismatched": 0,
            "checkpoints_skipped_stale": stale, "snapshots_ignored_late": 0, "parse_errors": 0}


def generate(seed: int, hours: float, out: Path, faults: bool, golden: bool, expected: bool,
             fault_kinds: tuple[str, ...] = FAULT_KINDS, verify: bool = True) -> dict:
    pa, pb = default_params(golden)
    ref_bid, ref_ask, flow = reference_touch(seed, hours)
    out.mkdir(parents=True, exist_ok=True)
    result: dict = {"files": {}}
    lines_by_venue: dict[str, list[str]] = {}
    manifest_all: list[dict] = []
    exp_counts: dict[str, dict] = {}
    meta_v: dict[str, dict] = {}
    for p in (pa, pb):
        rnd = random.Random(seed * 1_000_003 + 17 * (p.vid + 1))
        recs = simulate_venue(p, ref_bid, ref_ask, flow, rnd)
        enc_rnd = random.Random(seed * 1_000_003 + 101 + p.vid)
        msgs = encode_a(recs, p) if p.name == "A" else encode_b(recs, p, enc_rnd)
        lat = draw_latencies(len(msgs), p, random.Random(seed * 1_000_003 + 211 + p.vid))
        clean_msgs, clean_src = msgs, list(range(len(msgs)))
        if faults:
            msgs, manifest, src = inject_faults(clean_msgs, p.name, fault_kinds)
            manifest_all += manifest
            kinds = [m["kind"] for m in manifest]
        else:
            src, kinds = clean_src, []
        recv = assign_recv(msgs, src, lat, p)
        lines = serialize_a(msgs, recv, p) if p.name == "A" else serialize_b(msgs, recv, p)
        lines_by_venue[p.name] = lines
        # truth comes from the clean stream's snapshots == oracle book images; indices refer to the
        # written (possibly faulted) file so map each snapshot to its faulted position.
        truth = truth_rows(msgs, recv, p.name)
        fname = "venueA.jsonl" if p.name == "A" else "venueB.csv"
        (out / fname).write_text("\n".join(lines) + "\n")
        (out / f"truth_{p.name}.csv").write_text("\n".join(truth) + "\n")
        n_snap = sum(1 for m in msgs if _is_snap_start(m))
        exp_counts[p.name] = expected_counters(n_snap, kinds)
        meta_v[p.name] = {
            "file": fname, "lines": len(msgs), "snapshots": n_snap,
            "updates": sum(1 for m in msgs if _is_update(m)),
            "trades": sum(1 for m in msgs if m["k"] in ("trade", "T")),
            "params": {k: v for k, v in p.__dict__.items()},
            "exchange_ts_unit": "us" if p.name == "A" else "ns",
            "true_mean_latency_ns": p.lat_base_ns + p.lat_extra_mean_ns,
            "clock_offset_ns": p.clock_offset_ns,
            "apparent_mean_latency_ns_approx": p.lat_base_ns + p.lat_extra_mean_ns - p.clock_offset_ns,
        }
    faults_doc = {"seed": seed, "faults_enabled": faults, "faults": manifest_all,
                  "expected_counters": exp_counts,
                  "msg_index_rule": "0-based line index in the written file; Venue B excludes the header"}
    (out / "faults.json").write_text(json.dumps(faults_doc, indent=1) + "\n")
    meta = {"generator": "tools/gen_feeds.py", "synthetic": True,
            "note": "Synthetic feeds modelled on common conventions; not any real venue's format.",
            "seed": seed, "hours": hours, "golden": golden, "faults": faults, "t0_ns": T0_NS,
            "step_ns": STEP_NS, "tick": 0.01, "lot": 0.001, "top_n": TOP_N, "venues": meta_v}
    (out / "meta.json").write_text(json.dumps(meta, indent=1) + "\n")

    if verify or expected:
        ref = reference_pipeline(lines_by_venue["A"], lines_by_venue["B"])
        for v in "AB":
            got = {k: ref["sessions"][v].c[k] for k in FAULT_COUNTER_KEYS}
            if got != exp_counts[v]:
                raise AssertionError(f"venue {v}: reference counters {got} != expected {exp_counts[v]}")
        result["reference"] = ref
        if expected:
            for v in "AB":
                evs = [e for m in ref["events"][v] for e in m]
                (out / f"expected_events_{v}.csv").write_text("\n".join(events_csv(evs)) + "\n")
            cnt = {v: dict({k: ref["sessions"][v].c[k] for k in COUNTER_KEYS},
                           final_state=ref["sessions"][v].state, latency=ref["latency"][v]) for v in "AB"}
            mq = ref["merged"]
            cnt["merge"] = {"merged_quotes": len(mq),
                            "merged_crossed": sum(1 for q in mq if q[3] and q[6] and q[1] > q[4]),
                            "merged_locked": sum(1 for q in mq if q[3] and q[6] and q[1] == q[4]),
                            "export_rows": len(ref["export"]) - 1}
            (out / "expected_counters.json").write_text(json.dumps(cnt, indent=1) + "\n")
            flat = ["scope,key,value"]
            for v in "AB":
                flat += [f"{v},{k},{cnt[v][k]}" for k in COUNTER_KEYS]
                flat.append(f"{v},final_state,{cnt[v]['final_state']}")
                flat += [f"{v},latency_{k},{val}" for k, val in cnt[v]["latency"].items()]
            flat += [f"merge,{k},{val}" for k, val in cnt["merge"].items()]
            (out / "expected_counters.csv").write_text("\n".join(flat) + "\n")
            (out / "expected_merged.csv").write_text(
                "\n".join([MERGED_HEADER] + [",".join(str(x) for x in q) for q in mq]) + "\n")
            (out / "expected_export.csv").write_text("\n".join(ref["export"]) + "\n")
    result["meta"] = meta
    result["faults"] = faults_doc
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--hours", type=float, default=0.25)
    ap.add_argument("--out", required=True)
    ap.add_argument("--faults", action="store_true", help="inject drop, duplicate, swap, crossed per venue")
    ap.add_argument("--golden", action="store_true",
                    help="tiny hand-checkable fixture (implies --faults --expected, 1.5 s of market)")
    ap.add_argument("--expected", action="store_true", help="also write reference-pipeline outputs")
    ap.add_argument("--no-verify", action="store_true", help="skip the reference replay self-check")
    a = ap.parse_args(argv)
    t = time.perf_counter()
    if a.golden:
        res = generate(a.seed, 1.5 / 3600, Path(a.out), True, True, True,
                       fault_kinds=("drop", "duplicate", "crossed"))
    else:
        res = generate(a.seed, a.hours, Path(a.out), a.faults, False, a.expected, verify=not a.no_verify)
    dt = time.perf_counter() - t
    v = res["meta"]["venues"]
    print(f"wrote A={v['A']['lines']:,} lines, B={v['B']['lines']:,} rows, "
          f"faults={len(res['faults']['faults'])} to {a.out} in {dt:.1f}s", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
