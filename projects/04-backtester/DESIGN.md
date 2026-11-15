# Project 4 design: event-driven market-making backtester with latency and queue position (C++17 core, Python analysis)

Status: design frozen for implementation. Implement exactly what is written here. Where a
choice is left open it says "implementer's choice". If this text disagrees with Project 3's
`DESIGN.md` or with `tools/gen_feeds.py`, raise it; do not guess.

**Goal (roadmap section 6).** Replace Project 1's toy fill model (Poisson aggressors with
a per-tick decay) with an event-driven simulator over Project 3's normalised book events,
with order-entry and cancel latency and an explicit queue-position model. Then measure how
much those assumptions move PnL, markouts and fill counts.

**Data honesty.** Every input is SYNTHETIC: Project 3's generator, which puts a book on
Project 1's simulated price path. No result from this project describes a real venue.
A backtest is not live PnL. Section 7.6 gives the statements the write-up must make.

Contents

1. Inputs: the file between Project 3 and Project 4, and the book
2. Event loop, time, latency and cancels in flight
3. Queue-position models
4. Strategy interface and the port of Project 1's market maker
5. Outputs
6. Determinism and the fill hash
7. Sensitivity study
8. Module layout, build, public API, CLI, Makefile
9. Test plan
10. Risks, open questions, and what the implementer must know about Project 3

Units (same as Project 3): price in **ticks** (int64, tick = 0.01), quantity in **lots**
(int64, lot = 0.001 instrument units), time in **ns** (int64, Project 3's local capture
clock). No floating point in the event loop, queue models or matching. Floating point is
used only for the fee column, `fill_rate`, and in Python.

Project 1 quantities are instrument **units**. 1 unit = 1000 lots. Project 1 prices are
currency: `px = ticks / 100`.

---

## 1. Inputs

### 1.1 The file between the projects: Project 3's normalised-event CSV

Project 4 reads **one venue's** normalised events in the text form of Project 3
DESIGN section 1. `mdtool normalize` writes exactly this format, so Project 3 needs no
change:

```
mdtool normalize --venue A --in venueA.jsonl --out events_A.csv
```

Header, exactly:

```
venue,type,side,price_ticks,qty_lots,exch_ts_ns,local_ts_ns,seq_first,seq_last,prev_seq,msg_index,flags
```

`venue` A/B; `type` one of `SNAPSHOT_BEGIN SNAPSHOT_LEVEL SNAPSHOT_END ADD MODIFY SET DELETE
TRADE`; `side` `NONE`/`BID`/`ASK`; all other fields are decimal integers (`seq_*`/`prev_seq`
may be `-1`). `\n` line endings. Trade `side` is the **aggressor** (BID = buyer lifted the
ask; ASK = seller hit the bid).

Reader `bt::read_event_csv` (Project 3 has a writer but no reader) is strict. It fails
with exit code 4 on any of these:
- the header differs;
- a row has a field count other than 12;
- an enum is unknown or an integer is malformed (use `md::parse_uint`, plus a leading `-`
  only for the value `-1` in the three sequence fields);
- the file has more than one venue;
- a message is malformed (see below);
- messages go backwards in time (`local_ts_ns` lower than the previous message's).

**Messages.** A raw message is a maximal run of consecutive events that ends at the first
event with `flags & 1` (kLastInMsg). Every event in a message must have the same
`msg_index` and `local_ts_ns`. A message is a **trade message** if its first event is
`TRADE`, and then every event in it must be `TRADE`. A message mixing TRADE with other
types is malformed. (On Project 3's feeds a trade message is always exactly one event.)
A message's time is its `local_ts_ns`.

**Venue.** The backtester simulates one exchange. The study uses **Venue A**: it has the
lower latency, and every trade on it is followed at once by the book update it caused
(section 3.4). Venue B is accepted but is not used in the study (section 10).

**Feeds.** The study and the integration tests use **clean** feeds (no `--faults`). With
a faulted feed the session goes STALE for some periods. That is handled (section 2.6),
but those results are not used.

### 1.2 The book: reuse Project 3's code

Project 4 **links Project 3's `mdlib`** and uses `md::VenueSession` (which owns an
`md::Book`), `md::Merger`, `md::QuoteExporter`, `md::format_fixed`, `md::parse_uint` and
the `md::Event` types. It does **not** write its own book.

Justification:
1. Project 3's book and session are tested against golden files and the generator's oracle.
   A second book would need its own tests and could drift from them.
2. The session already applies snapshots, sequencing and gap/stale handling. Project 4
   only needs to *observe* level quantities before and after each message (section 3.4).
3. Feeding the session's `top()` into `md::Merger` and `md::QuoteExporter` makes
   Project 4's `market.csv` **byte-identical** to `mdtool export --venue A`. That is a
   free integration check (section 9).

The market book holds **only market orders**. Our simulated orders are phantoms: they are
never inserted into the book, never shown in it, and never change any later market event
(no market impact).

---

## 2. Event loop, time and latency

### 2.1 Simulated time

Simulated time `now_ns` is the timestamp of the event being processed. It never
decreases; assert this in debug builds. The clock is Project 3's **local capture clock**.
The simulated exchange's book at time T is the book built from all market messages with
`local_ts_ns <= T`.

Assumption (stated in the write-up): market-data latency is folded into the clock. The
strategy sees a market message at the instant it is captured. The book an order meets
when it arrives at the exchange is that locally observed book, about 0.5 ms behind the
generator's true time on venue A. The configured latencies are the only delays Project 4
adds.

### 2.2 One time-ordered queue with a fixed tie-break

Each kind of event has a rank. Processing order is ascending `(ts_ns, rank, seq)`.

| rank | kind | source |
|---|---|---|
| 0 | MARKET: one whole raw message | input file, in file order |
| 1 | EXCH_IN: a new-order or cancel arrival at the exchange | strategy actions + latency |
| 2 | REPORT: exchange to strategy (accepted, rejected, filled, canceled, cancel-rejected) | exchange + ack latency |
| 3 | TIMER: a strategy timer | `set_timer` |

`seq` is a global `uint64` counter. Every push into the heap takes the next value, which
gives FIFO order within one `(ts, rank)`. Market messages stay out of the heap: the loop
merges the next file message against the heap top, and the **market wins ties**.

```
i = 0                                   # next market message
last_mkt_ts = ts of the final message
loop:
    hm = heap.empty() ? +inf : heap.top().ts
    if i < n and msgs[i].ts <= hm:  now = msgs[i].ts; process_market(msgs[i]); i += 1
    elif not heap.empty() and (i < n or hm <= last_mkt_ts):
        ev = heap.pop(); now = ev.ts; dispatch(ev)
    else: break                         # market exhausted; later events are never processed
```

Consequences that the tests rely on:
- An action decided at time t with zero latency is processed at t, **after** the market
  message at t and before the next market message (Project 3 venue A `local_ts_ns` is
  strictly increasing).
- A trade at exactly the cancel's arrival time is processed **before** the cancel, so it
  can fill the order.
- A trade at exactly an order's arrival time is processed **before** the order goes live,
  so it cannot fill that order.
- Within one `(ts, rank)`, events run in push order.

### 2.3 Latency model

`LatencyConfig{entry_ns, cancel_ns, ack_ns, jitter_mean_ns, seed}`, all >= 0, default 0.
- **Entry latency**: from the strategy's `send_order` call to the order's arrival at the
  exchange.
- **Cancel latency**: from `send_cancel` to the cancel's arrival.
- **Ack latency**: from an exchange event (accept, reject, fill, cancel, cancel-reject) to
  the report's delivery to the strategy.
- **Jitter (optional, the only randomness)**: each delay adds
  `j = floor(-jitter_mean_ns * log1p(-u))`, with `u = (next() >> 11) * 2^-53` from
  SplitMix64 (section 6). When `jitter_mean_ns == 0`, no draw is made and `j = 0`. Draws
  happen in the order the delays are computed.
- **FIFO channels.** There is one channel strategy→exchange (orders and cancels share it,
  like one order-entry session) and one channel exchange→strategy. Arrival is
  `max(send_ts + base + j, last_arrival_on_that_channel)`, and `last_arrival` is then
  updated. Messages therefore never overtake each other: a cancel sent after an order
  always arrives at or after it, even when `cancel_ns < entry_ns`. Equal arrivals keep
  send order because of `seq`.
- **Strategy decision time** is 0: actions are stamped with the `now_ns` of the callback
  that issued them.

The grid's single "latency L" sets `entry_ns = cancel_ns = ack_ns = L`. One way is L, so a
round trip is 2L.

### 2.4 Exchange processing of arrivals

Each `SimOrder` holds: `id`, `side`, `px`, `qty`, `remaining`, `state`
(`InFlight | Live | Done`), `queue_ahead`, `arrive_ns`, `cancel_send_ns` (-1 if none),
`cancel_arrive_ns`, `crossed_flag`, `filled_lots`, `filled_in_cancel_flight_lots`.

**New-order arrival at t.** All orders are **post-only limit orders** (Project 1's market
maker is passive and has a single maker fee):
1. If the session is not `Live`: the order is `Done`, with REPORT Rejected(BookNotLive).
2. If it would cross or lock the market: Rejected(WouldCross). That means a bid with an
   ask side present and `px >= best_ask`, or an ask with a bid side present and
   `px <= best_bid`.
3. Otherwise it goes `Live` with `queue_ahead = queue_on_arrival(model, book.qty_at(side,
   px))` (section 3) and `crossed_flag = false`, and REPORT Accepted is sent.

Every REPORT is delivered at `report_arrival(t)`.

**Cancel arrival at t** for order X:
- X `Live`: X becomes `Done` (Canceled). REPORT Canceled carries `qty = remaining`.
  `cancels_effective += 1`.
- X `Done` because it was fully filled: REPORT CancelRejected(TooLateToCancel).
  `cancels_too_late += 1`.
- X `Done` for any other reason (rejected, or already canceled): REPORT
  CancelRejected(UnknownOrder). `cancels_unknown += 1`.
- X `InFlight`: impossible because of the FIFO channel; assert.

Then, in every case: if `X.filled_in_cancel_flight_lots > 0`, `cancels_in_flight_filled
+= 1`. That counter therefore counts cancels whose order got at least one fill while the
cancel was travelling, whether the fill was partial or full.

### 2.5 Cancels in flight: exact semantics and what the strategy is told

A cancel sent at `ts_c` takes effect only when it arrives at `ta = cancel_arrival(ts_c)`.
For every fill of X at exchange time `tf`:
- If `ts_c < tf <= ta` (with the tie-break of 2.2, a fill at `tf == ta` comes from a
  market message, which runs before the cancel), the fill **happens**. It is marked
  `cancel_in_flight = 1` and is added to `filled_in_cancel_flight_lots`.
- If `tf <= ts_c`, the fill happened before the decision. The strategy may not know about
  it yet (ack latency), so it is not an in-flight fill.
- After `ta` the order is no longer live and cannot fill.

What the strategy is told, and when (all through the FIFO report channel):
- each fill: REPORT Filled(qty, remaining, exch_ts = tf), delivered at `report_arrival(tf)`;
- the cancel outcome: Canceled(remaining) or CancelRejected(reason), delivered at
  `report_arrival(ta)`.

Fill reports from before `ta` are delivered before the cancel outcome. The strategy keeps
an order in `PendingCancel` until the outcome arrives. Fills keep arriving, and keep
updating its position, until then.

With L = 0, `ts_c == ta` and no market message lies in `(ts_c, ta]`. So
`cancels_in_flight_filled == 0` exactly (tested).

### 2.6 Processing one market message

```
process_market(m):
  was_live = (session.state() == Live)
  if m is a trade message:
      for e in m: session.on_event(e)
          passive = opposite(e.side); credit[passive][e.price_ticks] += e.qty_lots
          match_trade(e)                                  # section 3.3, fills at now
  else:
      L = sorted distinct (side, px) of Live orders
      before[k] = session.book().qty_at(k)
      for e in m: session.on_event(e)
      after[k]  = session.book().qty_at(k)
      for each Live order o at level k (ascending id):
          if was_live and session.state() == Live:
              o.queue_ahead = queue_on_depth(model, o.queue_ahead, before[k], after[k],
                                             explained = min(credit[k], max(before[k]-after[k], 0)))
          else:
              o.queue_ahead = min(o.queue_ahead, after[k])
      trade_credit_unmatched_lots += sum of credit[k] - explained[k] over all credit keys
      credit.clear()
      if cross_fills and session.state() == Live: cross_check()   # section 3.5
  merger.update(venue, session.top(), now); if changed: exporter.on_quote(merger.current())
  market-range tracker update (section 5.1)
  strategy.on_market(view(now), ctx)
```

`explained` is computed per level once and used for every one of our orders at that
level. Credits last for exactly one non-trade message. On venue A that message is the
trade's own book update (section 3.4).

When the session is not Live, the strategy sees `view.live = false`. The market maker
then cancels its working orders. Trades still run `match_trade`, because trades are prints
and do not depend on our book being live.

---

## 3. Queue-position models

### 3.1 Notation and state

For one of our live orders at `(side, px)`:
- `D` = displayed market quantity at `(side, px)` (excluding us; we are phantoms);
- `q` = `queue_ahead`, the market quantity ahead of us at our price;
- `rem` = our remaining quantity.

The invariant `0 <= q <= D` holds after every message, for every model. Market quantity
behind us is `D - q`. `QueueConfig{model, cancel_ahead_ppm}` stores f as an integer in
parts per million (`0 <= ppm <= 1_000_000`). The CLI parses `"0.25"` to 250000 with
integer arithmetic: up to 6 decimals, value in [0, 1].

### 3.2 The three models

| model | queue at arrival | trades at our price | non-trade decrease at our level | assumption |
|---|---|---|---|---|
| **OPTIMISTIC** | `q = 0` | fill `min(rem, V)` | none (q stays 0) | we are always first in line at our price. Every print at or through our price could have been ours |
| **FIFO_BACK** | `q = D` | consumes `q` first, then fills | `q` falls only by what logic forces: if the decrease exceeds the quantity behind us, the excess must have been ahead (`q = min(q, D')`) | price-time priority. We join the back. Cancels come from behind us as far as the numbers allow |
| **FIFO_CANCEL_AHEAD(f)** | `q = D` | as FIFO_BACK | a fraction f of the unexplained decrease is taken from ahead of us, and at least the forced amount | as FIFO_BACK, but cancels are spread through the queue. f is a free parameter that the data cannot identify |

FIFO_CANCEL_AHEAD with f = 0 is **identical** to FIFO_BACK (tested). With f = 1 it is
**not** OPTIMISTIC: we still need trades at our price to fill once `q` reaches 0.

**Why the clamp is in FIFO_BACK.** Suppose D falls from 600 to 300 and only 100 sits
behind us. Then at least 200 of the 300 that left were ahead of us. Keeping `q` above `D`
would be arithmetically impossible, not "conservative". So `q = min(q, D)` is a fact in
every FIFO model, not an assumption. The only assumption in FIFO_BACK is that cancels
come from behind **whenever that is possible**.

### 3.3 Trades (`match_trade`)

Take a trade event with aggressor side `a`, price `p`, volume `V`. The passive side
`s = opposite(a)` is ours if we have orders on it.

- **At** our price: `px == p`.
- **Through** our price: for a bid, `px > p` (the seller accepted less than our bid); for
  an ask, `px < p`.
- Orders on side s with a worse price (bid `px < p`, ask `px > p`) are not touched.

Our affected Live orders are processed in **priority order**: best price first (bids
descending px, asks ascending px), then `arrive_ns`, then `id`. Volume is shared:
`V_1 = V`, and `V_{k+1} = V_k - fill_k`.

```
queue_on_trade(cfg, q, rem, Vk, through) -> {fill, q_after}
  if through:              return {min(rem, Vk), 0}
  if model == OPTIMISTIC:  return {min(rem, Vk), 0}
  c = min(q, Vk)                        # FIFO models: volume ahead of us goes first
  return {min(rem, Vk - c), q - c}
```

Each nonzero fill becomes a `Fill` with kind `TRADE_THROUGH` or `TRADE_AT`, at
`ts = now`, price = **our order price** (passive fill at our limit), and
`queue_ahead_before = q`. A through fill at our price is honest: the counterparty was
willing to trade at a worse price, so it would have hit us first. With no market impact,
the market data afterwards are unchanged.

### 3.4 Depth changes and trade/cancel attribution

Project 3 trades **never** change the book. On venue A of the generator, every trade
message is followed **immediately** by a one-entry depth message. That message sets the
passive touch level to `before - V`, or deletes it when `before == V`. This was checked
on seed 7, 0.25 h, clean feed: 3,198 of 3,198 trades were followed this way. Project 4
therefore attributes depth decreases exactly:
- a trade message adds `V` to `credit[passive side][p]`;
- in the next non-trade message, a decrease `r = before - after > 0` at a level is
  **explained** up to `e = min(credit, r)`. The rest, `u = r - e`, is **unexplained**
  (a cancel, a modify down, or a touch move deleting the level);
- credits not used by that message are added to `trade_credit_unmatched_lots`. This is
  expected to be 0 on clean venue A, and the integration test asserts it.

So on this synthetic feed a cancel and a trade **can** be told apart, by construction of
the generator. On a real feed this attribution is a heuristic (section 10).

```
queue_on_depth(cfg, q, before, after, explained) -> q'
  if model == OPTIMISTIC: return 0
  if after >= before:     return min(q, after)          # increase: new size joins behind us
  r  = before - after
  e  = min(explained, r);  u = r - e
  q1 = min(q, before - e)             # traded volume we had not consumed (order arrived after the print)
  behind = (before - e) - q1
  ppm = (model == FIFO_BACK) ? 0 : cfg.cancel_ahead_ppm
  cut = max((u * ppm) / 1_000_000, u - behind)          # floor; forced amount
  cut = max(0, min(cut, min(u, q1)))
  return q1 - cut                     # guaranteed <= after
```

Cases this covers:
- **Displayed size increases** (`Set`/`Modify` up, or `Add` at our price): new orders
  join behind us, so `q` is unchanged.
- **Trade-explained decrease:** `q` already moved when the trade was processed. `q1`
  only clamps an order that arrived between the print and its book update. Such an order
  saw the pre-trade D, and the traded volume was ahead of it.
- **The price level disappears** (`Delete`, or `after == 0`): `q' = 0`. Everyone left,
  and our order stays live as the only order at that price, now better than the market's
  touch. It fills on the next trade through it (3.3) or by a cross (3.5). If the level is
  re-added later, the new size is behind us (`q` stays 0).
- **Snapshots** (`SnapshotEnd` replaces the book): handled by the same before/after diff
  with `explained = 0`. On clean feeds checkpoints equal the book, so nothing changes.
- **A level absent at arrival** (for example a skewed quote one tick behind): `D = 0` and
  `q = 0`.

Partial quantities: all quantities are integer lots, and a fill can be any 1..rem lots.
Every partial fill is its own row. An order is `Done` (filled) when `rem == 0`.

### 3.5 Crosses (all models, `--cross-fills on` by default)

In the generator, price moves are not printed as trades. The touch moves by deleting old
levels and adding new ones in **one** depth message (Project 3 DESIGN section 4). A
resting bid at the old best bid can therefore find the market's best ask **at or below**
its price. A real matching engine would have executed the incoming ask against our bid,
at our price, with us passive.

Rule (edge-triggered): after each non-trade message while Live, for each Live order:
- `crossing` = for a bid, the ask side is non-empty and `best_ask <= px`; for an ask, the
  bid side is non-empty and `best_bid >= px`.
- If `crossing && !crossed_flag`: fill `min(rem, avail)` at **our price**, with kind
  `CROSS`. Here `avail` is the total displayed opposite quantity at prices that cross us,
  shared among newly crossing orders in priority order as in 3.3. Then set `q = 0`.
- Set `crossed_flag = crossing`.

A remainder that stays crossed does not fill again from the same displayed liquidity. It
fills only by trades (3.3) or by a new cross after the market uncrosses it. This is the
main stale-quote (latency) cost in this market, and it is the same in every queue model.
`--cross-fills off` is an ablation (section 7.5).

### 3.6 Ordering guarantee between models

**Claim.** Take a **fixed, fill-independent order schedule**: the same send times, prices,
quantities and cancel times whatever fills happen. On the same feed, latency and seed, the
cumulative filled quantity of **each order**, at every time, satisfies

`OPTIMISTIC >= FIFO_CA(f2) >= FIFO_CA(f1) >= FIFO_BACK` for `f2 >= f1`.

**Proof sketch.** Write `s = q - filled`. A trade maps `s -> max(s - V, -qty)`. A depth
change maps `q` by a function that is non-decreasing in `q` and non-increasing in f.
Crosses and through-trades add `min(rem, ·)`. OPTIMISTIC is the case `q ≡ 0`. The proof
assumes at most one of our orders on a side at a time, because shared volume (3.3) couples
orders. The tests therefore use schedules with `life < period`.

**When it does not hold.** With the market maker, fills change position, position changes
skew and pulls, and that changes the orders. Fill count, PnL and markouts can then come
out in any order. The tests assert the ordering only for the schedule strategy (section
9). The study reports what the market maker actually does.

---

## 4. Strategy interface and the market maker

### 4.1 Interface

```cpp
struct MarketView {                       // the market book only (no own orders)
    std::int64_t ts_ns = 0;
    bool live = false;                    // session.state() == Live
    bool has_bid = false, has_ask = false;
    std::int64_t bid_px = 0, bid_qty = 0, ask_px = 0, ask_qty = 0;
    const md::Book* book = nullptr;       // full depth, read-only
};
class StrategyContext {
public:
    virtual ~StrategyContext() = default;
    virtual std::int64_t now_ns() const = 0;
    virtual OrderId send_order(md::Side side, std::int64_t px_ticks, std::int64_t qty_lots) = 0; // post-only; id >= 1, increasing
    virtual void send_cancel(OrderId id) = 0;
    virtual void set_timer(std::int64_t at_ns, std::int64_t timer_id) = 0;  // at_ns < now -> now
};
class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void on_market(const MarketView& v, StrategyContext& ctx) = 0;   // after every market message
    virtual void on_report(const Report& r, const MarketView& v, StrategyContext& ctx) = 0;
    virtual void on_timer(std::int64_t timer_id, const MarketView& v, StrategyContext& ctx) {}
};
```

`send_order` requires `qty_lots > 0` and a side of Bid or Ask. Anything else is a
programming error: assert, and return -1 in release builds.

### 4.2 Market maker (`MarketMaker`, port of `pta.strategy.market_maker`)

Project 1's rules, now applied to a real book:

| Project 1 (`MMConfig`) | Project 4 (`MMParams`) | default |
|---|---|---|
| joins best bid/ask | quote at the market's best bid / best ask | — |
| `skew_inv = 20` (units) | `skew_inv_lots` | 20000 |
| `max_inv = 50` (units) | `max_inv_lots` | 50000 |
| one tick behind past skew | `skew_ticks` | 1 |
| `max_lot = 3`, random 1..3 per fill | `quote_qty_lots`, fixed (partial fills vary the size anyway) | 1000 (1 unit) |
| `fee_bps = 0.1` (cost) | `BacktestConfig::fee_bps` (applied in the fills writer) | 0.1 |
| `tick = 0.01` | tick = 1 price unit of the feed | — |

Project 1's Poisson/flow/kappa fill parameters have no counterpart: fills now come from
the book.

Strategy-side state:
- `position_lots`, the position the strategy knows: changed only by Filled reports, +qty
  for a bid fill and -qty for an ask fill;
- per order, `{side, px, remaining, state ∈ {PendingNew, Live, PendingCancel}}`. An order
  is removed on Rejected, on Canceled, on CancelRejected, or on a Filled report with
  `remaining == 0`.

The **working order** of a side is its order in PendingNew or Live. There is at most one
per side.

`decide(v, ctx)` runs at the end of every `on_market` and `on_report`, Bid side first,
then Ask:

```
desired(Bid) = v.live && v.has_bid && position <  max_inv  ?  v.bid_px - (position >  skew_inv ? skew_ticks : 0)  : none
desired(Ask) = v.live && v.has_ask && position > -max_inv  ?  v.ask_px + (position < -skew_inv ? skew_ticks : 0)  : none
for side in (Bid, Ask):
    w = working order of side (or none)
    d = desired(side)
    if d is none:            if w: send_cancel(w); w.state = PendingCancel
    elif w is none:          send_order(side, d, quote_qty_lots)            # state PendingNew
    elif w.px != d:          send_cancel(w); w.state = PendingCancel; send_order(side, d, quote_qty_lots)
```

This means:
- The strategy **re-quotes when the touch moves**: cancel and re-enter in the same
  callback, cancel first in send order, both subject to latency. While the cancel is in
  flight the old and the new order can both fill. That exposure is intended and is
  counted.
- A partially filled working order at the right price is **not** topped up.
- After a Rejected (WouldCross or BookNotLive), the next `decide` sends a new order if one
  is still desired.
- The strategy never looks at its own queue position. Inventory limits use the known
  position, which lags the true position by the ack latency. The summary records the true
  `max_abs_position_lots`.

Comparisons are strict, as in Project 1: skew when `position > skew_inv` (or
`< -skew_inv`), pull the bid when `position >= max_inv`.

### 4.3 Schedule strategy (`ScheduledQuoter`, used for the ordering test)

Its order flow does not depend on fills. On the first `on_market` with `live`, it sets a
timer at `now + period`. On each tick of that timer:
- it sends a bid at `v.bid_px` (if `has_bid`) and an ask at `v.ask_px` (if `has_ask`),
  each `qty_lots`;
- it sets a cancel timer for each at `now + life`;
- it reschedules itself at `now + period`.

The cancel timer cancels its order whatever state it is in. Defaults: `period = 500 ms`,
`life = 400 ms` (shorter than `period`, so at most one order per side is live, as
3.6 requires), `qty_lots = 1000`. Timer ids: even ids are the schedule tick, and odd id
`2*order_id + 1` cancels that order.

---

## 5. Outputs

`bt_run --out DIR` writes five files. All use `\n` line endings, the final line is
terminated, and all are byte-deterministic (section 6).

### 5.1 `market.csv`: Project 1 market schema

Header `ts_us,bid,ask,mid`. It is produced by `md::QuoteExporter` from
`md::Merger::update(venue, session.top(), now)` after every market message, then
`finish()`. It is identical to `mdtool export --venue A` on the same feed (section 9).

A small `MarketRange` tracker applies the same collapse-then-filter rule (Project 3
section 9) to record `first_ts_us` and `last_ts_us` of the rows written.

### 5.2 `fills.csv`: Project 1 fills schema

Header `ts_us,side,px,qty,fee`. One row per fill, in generation order. Generation order
is non-decreasing in time.

| column | value |
|---|---|
| `ts_us` | `fill.ts_ns / 1000` (exchange fill time on the local clock; not the report time) |
| `side` | `1` if our bid filled (we bought), `-1` if our ask filled |
| `px` | `md::format_fixed(px_ticks, 2)` |
| `qty` | `md::format_fixed(qty_lots, 3)` (units; Project 1 `load_fills` reads float qty) |
| `fee` | `printf("%.17g", ((fee_bps * 1e-4) * (px_ticks / 100.0)) * (qty_lots / 1000.0))`, evaluated in exactly this order in `double`; positive = cost |

Fills with `ts_us` outside `[first_ts_us, last_ts_us]` of `market.csv` would fail
`pta.data.loaders.validate`. They are left out of `fills.csv` (but kept in
`fills_detail.csv`) and counted in `fills_outside_market`. On a clean feed this is
expected to be 0: every fill comes from a trade followed by a touch update, or from a
message that itself changes the top. The integration test asserts 0.

So `pta report --fills fills.csv --market market.csv --out R` and
`rmon run --market market.csv --fills fills.csv --out R` work unchanged.

### 5.3 `fills_detail.csv`

Header
`ts_ns,order_id,side,px_ticks,qty_lots,kind,queue_ahead_before,cancel_in_flight`. `side` is
`BID`/`ASK`, `kind` is `TRADE_AT|TRADE_THROUGH|CROSS`, and `cancel_in_flight` is 0/1. It
has the same rows as the fills vector, including fills outside the market range.

### 5.4 `orders.csv`

Header
`order_id,side,px_ticks,qty_lots,send_ns,arrive_ns,status,reject_reason,queue_ahead_at_arrival,cancel_send_ns,cancel_arrive_ns,filled_lots,filled_in_cancel_flight_lots`.
One row per order, ascending id.
- `status` is one of `FILLED CANCELED REJECTED OPEN` (OPEN means still live or in flight
  at the end).
- `reject_reason` is one of `NONE WOULD_CROSS BOOK_NOT_LIVE`.
- Fields that do not apply are `-1`.

### 5.5 `summary.csv`: flat run summary

Header `key,value`, then exactly these keys in this order. Integers are plain, `fill_rate`
is `%.6f`, and enums are lower-case strings:

```
model, cancel_ahead_ppm, strategy, cross_fills, entry_latency_ns, cancel_latency_ns,
ack_latency_ns, latency_jitter_mean_ns, seed, fee_bps (%.17g), venue,
input_events, input_messages, input_trades, session_final_state, session_gaps,
session_resyncs, market_rows, market_first_ts_us, market_last_ts_us,
orders_sent, orders_accepted, orders_rejected_would_cross, orders_rejected_not_live,
cancels_sent, cancels_effective, cancels_too_late, cancels_unknown,
cancels_in_flight_filled, fills, fills_trade_at, fills_trade_through, fills_cross,
filled_lots, orders_with_fill, fill_rate, final_position_lots, max_abs_position_lots,
orders_open_at_end, trade_credit_unmatched_lots, fills_outside_market, fills_fnv1a64
```

- `fills` counts rows in `fills.csv`. The `fills_*` kind counters count all fills.
- `fill_rate = orders_with_fill / orders_accepted` (0 when there are no accepted orders).
- `final_position_lots` and `max_abs_position_lots` are the true exchange-side position.
- Wall time is **not** in the summary, because it would break byte determinism. `bt_run`
  prints it on stderr.

---

## 6. Determinism and the fill hash

- **Randomness.** The base models (all three queue models, fixed latencies, both
  strategies) have **none**. Output depends only on the input file and the configuration.
  The only random component is latency jitter (`--latency-jitter-us > 0`), drawn from
  SplitMix64 seeded with `--seed` (default 1):
  `z = (state += 0x9E3779B97F4A7C15); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EB; return z ^ (z >> 31);`
  Do not use `std::*_distribution`, whose output is implementation-defined.
- **Stable ordering.** The event key is `(ts_ns, rank, seq)` (section 2.2). Order
  iteration is always by explicit priority keys or ascending id, never over unordered
  containers. There are no pointers in sort keys and no wall-clock reads in the loop.
- **Fill hash.** It is FNV-1a 64 over the exact bytes of `fills.csv`, header included:
  offset basis `14695981039346656037`, prime `1099511628211`, xor each byte then multiply
  mod 2^64. It is printed as 16 lower-case hex digits in `summary.csv` (`fills_fnv1a64`).
  Python recomputes it in tests from the file to check it.
- **Guarantees, all tested.**
  - Same file and configuration give byte-identical `fills.csv`, the same hash, and
    byte-identical `summary.csv`, `orders.csv` and `market.csv`.
  - With jitter 0, `--seed 1` and `--seed 2` give the same hash.
  - With jitter > 0, the same seed gives the same hash and a different seed gives a
    different hash.
  - The guarantee covers one build on one machine. The fee column uses `double` and
    `printf %.17g`, so a different libc or compiler might print it differently. All
    integer columns are portable.

---

## 7. Sensitivity study

### 7.1 Grid

| axis | values |
|---|---|
| latency L (one way, entry = cancel = ack) | 0, 1, 5, 20 ms |
| queue model | OPTIMISTIC, FIFO_BACK, FIFO_CANCEL_AHEAD f = 0.25, FIFO_CANCEL_AHEAD f = 0.5 |
| market seed | 7, 8, 9 (generator `--seed`; clean; `--hours 1.0`; venue A) |

That is 16 cells per seed, 48 runs in total. Fixed settings: `strategy = mm` with
`MMParams` defaults, `fee_bps = 0.1`, `cross_fills = on`, jitter 0.

### 7.2 Metrics per cell

The harness computes them with `pta.report.analyze(load_fills, load_market)` plus the
run's `summary.csv`.

| metric | source |
|---|---|
| `total_pnl` | `totals["total"]` |
| `spread_capture` | `totals["spread_capture"]` |
| `inventory_pnl` | `totals["inventory"]` |
| `fees` | `totals["fees"]` |
| `markout_1s_bps` | `markouts` row with `horizon_us == 1_000_000`, column `avg_bps` (qty-weighted) |
| `fills` | `summary.fills` |
| `filled_qty` | `summary.filled_lots / 1000` |
| `fill_rate` | `summary.fill_rate` |
| `cancels_in_flight_filled`, `fills_cross`, `fills_trade_through`, `orders_accepted`, `max_abs_position` (units) | `summary.csv` |
| `fills_fnv1a64`, `run_s` (wall time of `bt_run`) | — |

For a cell with fewer than 2 fills, the pta metrics are written as NaN with
`note = "too_few_fills"`, so a pta error does not abort the study.

Note on attribution: `pnl.mid_asof` uses the market row at the fill's own `ts_us`. For a
CROSS fill that row is already the moved market, so a stale quote's loss shows up in
**spread capture** (negative edge against the post-move mid), not in inventory. The
write-up must say so.

### 7.3 `python/sensitivity.py`

```
python3 python/sensitivity.py --bt-run PATH --mdtool PATH --out DIR
    [--gen-feeds ../03-market-data/tools/gen_feeds.py] [--seeds 7,8,9] [--hours 1.0]
    [--latencies-ms 0,1,5,20]
    [--models optimistic,fifo_back,fifo_cancel_ahead:0.25,fifo_cancel_ahead:0.5]
    [--ablate-cross] [--jobs 1] [--quick]
```

`--quick` = seeds 7, hours 0.02, latencies 0,5, models optimistic,fifo_back. It is used
by the smoke test.

Steps:
1. For each seed, generate `DIR/work/feed_<seed>/` with
   `gen_feeds.py --seed S --hours H --no-verify`. Cache by comparing `meta.json` `seed` and
   `hours`. Then run `mdtool normalize --venue A` to produce `events_A.csv`.
2. For each cell, run `bt_run --events events_A.csv --out DIR/runs/s<seed>_L<ms>_<label>
   --strategy mm --latency-us <ms*1000> --model M [--cancel-ahead F]`. A non-zero exit
   aborts with a message naming the cell.
3. Collect the metrics (7.2) into `DIR/sensitivity.csv`. It is long format, one row per
   (seed, cell), with columns `seed,hours,latency_ms,model,cancel_ahead,label,total_pnl,
   spread_capture,inventory_pnl,fees,markout_1s_bps,fills,filled_qty,fill_rate,
   orders_accepted,cancels_in_flight_filled,fills_cross,fills_trade_through,
   max_abs_position,fills_fnv1a64,run_s,note`. Labels: `OPTIMISTIC`, `FIFO_BACK`,
   `FIFO_CA(0.25)`, `FIFO_CA(0.5)` (with `--ablate-cross`, a suffix ` no-cross`).
4. Write `DIR/sensitivity.md` (7.4) and `DIR/sensitivity.png` (7.4).

Public functions:
- `parse_models(s) -> list[tuple[str, float | None]]`
- `ensure_feed(seed, hours, work_dir, gen_feeds, mdtool) -> Path`
- `run_cell(bt_run, events, seed, latency_ms, model, f, out_dir, extra_args=()) -> Path`
- `cell_metrics(run_dir) -> dict`
- `write_markdown(df, path)`
- `write_chart(df, path)`
- `main(argv=None) -> int`

pta is imported (`pta.data.loaders`, `pta.report`). It requires Projects 1 and 2 to be
installed or on `PYTHONPATH`; the Makefile sets the path.

### 7.4 Table and chart format

`sensitivity.md` has:
1. A parameter block: seeds, hours, venue, strategy defaults, fee, cross fills, and the
   generator command.
2. **Table 1, total PnL**, with rows = the 4 queue models and columns = L in ms. Each cell
   is `mean [min, max]` over seeds, formatted `%.2f`.
3. Tables 2-4 in the same layout: **1 s markout (bps)** (`%.3f`), **fill count**
   (integer mean), **fill rate** (`%.3f`).
4. **Table 5**, the inflation table: for each L, `OPTIMISTIC - FIFO_BACK` and
   `FIFO_CA(0.5) - FIFO_BACK` for total PnL and fill count (seed means).
5. A long table with every metric (seed means).
6. The verbatim statement in 7.6.

`sensitivity.png`, matplotlib Agg, `figsize=(10, 7)`, dpi 130, is a 2×2 grid of small
multiples:
- (a) total PnL, (b) 1 s markout bps, (c) fill count, (d) spread capture.
- The x axis is latency as **categorical** positions 0..3, labelled `0 1 5 20 ms`.
- Each model is one line with markers. Its seed range (min to max) is drawn as error bars
  or a light band.
- Panels (a), (b) and (d) have a horizontal zero line.
- The same colour per model in every panel, and one shared legend.
- The title states "synthetic market, not live PnL".

The implementer should follow the repo's dataviz guidance for the colours.

### 7.5 Run-time budget

- `bt_run` on a 1 h venue A feed (about 135k messages, 150k events, 15 MB CSV): **at most
  2 s** in Release.
- `pta.report.analyze` per cell: at most 5 s.
- Feed generation: about 6 s per seed (measured: 0.25 h took 1.5 s).
- `make sensitivity` in total: **at most 10 minutes on one core**, target about 5.
  `--jobs N` runs cells in parallel and does not change any output.
- `--quick` (the smoke test): at most 30 s.
- `--ablate-cross` adds the 4 models × 4 latencies with `--cross-fills off` for seed 7
  only. It is optional and reported in a separate section.

### 7.6 How to state the result (verbatim in `sensitivity.md` and in `docs/writeup.md`)

> These numbers come from a synthetic market (Project 3's generator on Project 1's price
> path) and a backtester with no market impact. Their sizes are properties of that
> generator: its trade rate, its random re-sizing of book levels, and its touch moves that
> delete and re-add levels without printing trades. They do not transfer to any real
> venue. The direction of the effect (optimistic fill assumptions raise fill counts and
> backtest PnL) is the finding; the magnitudes are not. A backtest is not live PnL: it
> cannot reproduce real queue dynamics, how other participants react to our orders, or
> real latency distributions. Senior HFT roles ask for a live track record, and no project
> here substitutes for one.

The write-up quantifies from Table 5 ("on this simulator, at L = 1 ms, OPTIMISTIC reports
X vs FIFO_BACK Y"), using only numbers copied from the generated tables. It must also
state:
- the local-clock assumption (2.1);
- the cross rule (3.5) and its ablation;
- that f is not identifiable from the data;
- that the market maker's results need not follow the model ordering (3.6).

---

## 8. Module layout, build, API, CLI

### 8.1 Files

```
projects/04-backtester/
  CMakeLists.txt  Makefile  DESIGN.md  README.md
  include/bt/types.hpp            OrderId, QueueModel, QueueConfig, ReportType, RejectReason, FillKind,
                                  Report, Fill, OrderRecord, to_string/parse helpers, parse_fraction_ppm
  include/bt/event_csv.hpp        read_event_csv, Message, group_messages
  include/bt/queue.hpp            queue_on_arrival, queue_on_trade, queue_on_depth
  include/bt/latency.hpp          SplitMix64, LatencyConfig, LatencyModel
  include/bt/event_queue.hpp      SimEventKind, SimEvent, EventQueue
  include/bt/strategy.hpp         MarketView, StrategyContext, Strategy
  include/bt/strategy_mm.hpp      MMParams, MarketMaker
  include/bt/strategy_schedule.hpp ScheduleParams, ScheduledQuoter
  include/bt/backtest.hpp         BacktestConfig, RunSummary, BacktestResult, run_backtest
  include/bt/output.hpp           fnv1a64, write_fills_csv, write_fills_detail_csv, write_orders_csv, write_summary_csv, MarketRange
  src/event_csv.cpp queue.cpp latency.cpp event_queue.cpp strategy_mm.cpp strategy_schedule.cpp
      backtest.cpp output.cpp types.cpp
  src/bt_run.cpp                  main()
  tests/harness.hpp               minimal TEST/CHECK/CHECK_EQ registry (same style as Project 3; no gtest)
  tests/feed_builder.hpp          helpers to build md::Event messages in code (snapshot, set, del, trade)
  tests/scripted.hpp              ScriptedStrategy: timed send/cancel actions, records every report and its delivery ts
  tests/test_main.cpp test_event_csv.cpp test_queue.cpp test_latency.cpp test_exchange.cpp
      test_strategy_mm.cpp test_backtest.cpp test_determinism.cpp
  python/sensitivity.py  python/test_integration.py  python/test_sensitivity.py
  docs/writeup.md                 week 15 (section 7.6)
```

### 8.2 CMake

- `cmake_minimum_required(VERSION 3.20)`, `project(bt LANGUAGES CXX)`.
- C++17, required, no extensions. The default build type is Release when unset.
- Options: `BT_WERROR` (ON), `BT_SANITIZE` (OFF:
  `-fsanitize=address,undefined -fno-omit-frame-pointer` on compile and link).
- Before adding Project 3:
  `set(MD_WERROR ${BT_WERROR} CACHE BOOL "" FORCE)` and
  `set(MD_SANITIZE ${BT_SANITIZE} CACHE BOOL "" FORCE)`, then
  `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../03-market-data ${CMAKE_BINARY_DIR}/md EXCLUDE_FROM_ALL)`.
- Warnings on every bt target: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
  -Wsign-conversion` (`-Werror` when `BT_WERROR`). These match Project 3.
- Targets:
  - `bt_lib`: STATIC, `src/*.cpp` except `bt_run.cpp`, public include `include/`, links
    `mdlib` PUBLIC;
  - `bt_run`: `src/bt_run.cpp`;
  - `bt_tests`: `tests/*.cpp`, compile definition
    `BT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"`.
  - Both `bt_run` and `bt_tests` link `bt_lib`.
- `enable_testing()`; `add_test(NAME bt_tests COMMAND bt_tests)`.
- Zero warnings on g++ 13.

Project 3's own `add_test` calls also register inside this tree. Always run
`ctest -R '^bt_'`.

### 8.3 Public API (namespace `bt`; signatures are binding)

```cpp
// types.hpp
using OrderId = std::int64_t;
using md::Side;
enum class QueueModel : std::uint8_t { Optimistic, FifoBack, FifoCancelAhead };
constexpr std::int64_t kPpm = 1'000'000;
struct QueueConfig { QueueModel model = QueueModel::FifoBack; std::int64_t cancel_ahead_ppm = 0; };
enum class ReportType : std::uint8_t { Accepted, Rejected, Filled, Canceled, CancelRejected };
enum class RejectReason : std::uint8_t { None, WouldCross, BookNotLive, TooLateToCancel, UnknownOrder };
enum class FillKind : std::uint8_t { TradeAt, TradeThrough, Cross };
struct Report {
    ReportType type = ReportType::Accepted; OrderId id = 0; Side side = Side::None;
    std::int64_t px_ticks = 0;
    std::int64_t qty_lots = 0;        // Filled: fill qty; Canceled: canceled qty; else 0
    std::int64_t remaining_lots = 0;  // after this event
    std::int64_t exch_ts_ns = 0;      // when it happened at the exchange
    std::int64_t ts_ns = 0;           // when delivered to the strategy
    RejectReason reason = RejectReason::None;
    FillKind fill_kind = FillKind::TradeAt;
};
struct Fill { std::int64_t ts_ns = 0; OrderId id = 0; Side side = Side::None;
              std::int64_t px_ticks = 0, qty_lots = 0; FillKind kind = FillKind::TradeAt;
              std::int64_t queue_ahead_before = 0; bool cancel_in_flight = false; };
enum class OrderStatus : std::uint8_t { Open, Filled, Canceled, Rejected };
struct OrderRecord { OrderId id = 0; Side side = Side::None; std::int64_t px_ticks = 0, qty_lots = 0,
    send_ns = 0, arrive_ns = -1; OrderStatus status = OrderStatus::Open; RejectReason reject = RejectReason::None;
    std::int64_t queue_ahead_at_arrival = -1, cancel_send_ns = -1, cancel_arrive_ns = -1,
    filled_lots = 0, filled_in_cancel_flight_lots = 0; };
const char* to_string(QueueModel);  // "optimistic" "fifo_back" "fifo_cancel_ahead"
bool parse_queue_model(std::string_view s, QueueModel& out);
bool parse_fraction_ppm(std::string_view s, std::int64_t& ppm);   // "0" "0.25" "1" "1.000000"; [0,1]; <= 6 decimals
const char* to_string(ReportType); const char* to_string(RejectReason);
const char* to_string(FillKind);    // "TRADE_AT" "TRADE_THROUGH" "CROSS"
const char* to_string(OrderStatus);

// event_csv.hpp
bool read_event_csv(const std::string& path, std::vector<md::Event>& out, std::string& err);
bool parse_event_csv_line(std::string_view line, md::Event& out, std::string& err);
struct Message { std::size_t begin = 0, end = 0; std::int64_t ts_ns = 0; bool is_trade = false; };
// Groups events into messages and validates section 1.1 rules; false + err on violation.
bool group_messages(const std::vector<md::Event>& ev, std::vector<Message>& out, std::string& err);

// queue.hpp (pure; section 3)
std::int64_t queue_on_arrival(const QueueConfig& c, std::int64_t displayed);
struct TradeResult { std::int64_t fill = 0; std::int64_t queue_after = 0; };
TradeResult queue_on_trade(const QueueConfig& c, std::int64_t queue_ahead, std::int64_t remaining,
                           std::int64_t volume, bool through);
std::int64_t queue_on_depth(const QueueConfig& c, std::int64_t queue_ahead, std::int64_t displayed_before,
                            std::int64_t displayed_after, std::int64_t explained);

// latency.hpp
class SplitMix64 { public: explicit SplitMix64(std::uint64_t seed); std::uint64_t next(); double uniform01(); };
struct LatencyConfig { std::int64_t entry_ns = 0, cancel_ns = 0, ack_ns = 0, jitter_mean_ns = 0; std::uint64_t seed = 1; };
class LatencyModel {
public:
    explicit LatencyModel(const LatencyConfig& c);
    std::int64_t entry_arrival(std::int64_t send_ns);    // FIFO channel to exchange (shared with cancels)
    std::int64_t cancel_arrival(std::int64_t send_ns);
    std::int64_t report_arrival(std::int64_t exch_ns);   // FIFO channel to strategy
};

// event_queue.hpp
enum class SimEventKind : std::uint8_t { ExchangeIn = 1, Report = 2, Timer = 3 };  // rank; market = 0 outside the heap
enum class InKind : std::uint8_t { NewOrder, Cancel };
struct SimEvent { std::int64_t ts_ns = 0; SimEventKind kind = SimEventKind::Timer; std::uint64_t seq = 0;
                  InKind in_kind = InKind::NewOrder; OrderId id = 0; Report report{}; std::int64_t timer_id = 0; };
class EventQueue {   // min-heap on (ts_ns, kind, seq); push assigns seq
public:
    void push(SimEvent e); bool empty() const; const SimEvent& top() const; SimEvent pop(); std::size_t size() const;
};

// strategy.hpp: section 4.1.  strategy_mm.hpp:
struct MMParams { std::int64_t quote_qty_lots = 1000, skew_inv_lots = 20000, max_inv_lots = 50000, skew_ticks = 1; };
class MarketMaker : public Strategy { public: explicit MarketMaker(MMParams p = {});
    std::int64_t position_lots() const; /* overrides */ };
// strategy_schedule.hpp:
struct ScheduleParams { std::int64_t period_ns = 500'000'000, life_ns = 400'000'000, qty_lots = 1000; };
class ScheduledQuoter : public Strategy { public: explicit ScheduledQuoter(ScheduleParams p = {}); /* overrides */ };

// backtest.hpp
struct BacktestConfig { QueueConfig queue{}; LatencyConfig latency{}; bool cross_fills = true; double fee_bps = 0.1; };
struct RunSummary { /* one int64/double/string member per summary.csv key, same names (section 5.5) */ };
struct BacktestResult { std::vector<Fill> fills; std::vector<OrderRecord> orders; RunSummary summary;
                        std::int64_t market_first_ts_us = 0, market_last_ts_us = 0; };
// events must pass group_messages (else returns false + err). market_out may be null; when set,
// receives market.csv (section 5.1). summary.fills_fnv1a64 and fills counters are filled by write step,
// so run_backtest leaves them 0; write_outputs completes them.
bool run_backtest(const std::vector<md::Event>& events, Strategy& strategy, const BacktestConfig& cfg,
                  std::ostream* market_out, BacktestResult& out, std::string& err);

// output.hpp
std::uint64_t fnv1a64(std::string_view bytes);
std::string hex16(std::uint64_t h);
// Writes fills.csv content (section 5.2) to os, dropping fills outside [first,last] us; returns rows written
// and adds dropped count to *outside.
std::int64_t write_fills_csv(std::ostream& os, const std::vector<Fill>& fills, double fee_bps,
                             std::int64_t first_ts_us, std::int64_t last_ts_us, std::int64_t* outside);
void write_fills_detail_csv(std::ostream& os, const std::vector<Fill>& fills);
void write_orders_csv(std::ostream& os, const std::vector<OrderRecord>& orders);
void write_summary_csv(std::ostream& os, const RunSummary& s);
// Writes all five files into dir (creating it), completing summary fills/hash fields. false + err on I/O error.
bool write_outputs(const std::string& dir, BacktestResult& r, const BacktestConfig& cfg,
                   const std::string& market_csv, std::string& err);
```

Private members and the exchange's internal structure are the implementer's choice.
Recommended: a `SimExchange` class inside `backtest.cpp` with orders in a
`std::vector<SimOrder>` indexed by `id - 1`, and per side a sorted vector of live ids for
priority iteration.

### 8.4 `bt_run` CLI

```
bt_run --events PATH --out DIR
       [--model optimistic|fifo_back|fifo_cancel_ahead]   (default fifo_back)
       [--cancel-ahead F]          required iff model = fifo_cancel_ahead; F in [0,1]
       [--latency-us N]            sets entry, cancel and ack (default 0)
       [--entry-latency-us N] [--cancel-latency-us N] [--ack-latency-us N]   override --latency-us
       [--latency-jitter-us N] [--seed N]                (defaults 0, 1)
       [--strategy mm|schedule]                          (default mm)
       [--quote-qty-lots N] [--skew-inv-lots N] [--max-inv-lots N] [--skew-ticks N]
       [--schedule-period-us N] [--schedule-life-us N]
       [--fee-bps X]                                     (default 0.1)
       [--cross-fills on|off]                            (default on)
```

Microsecond arguments are multiplied by 1000 into ns.

Exit codes:
- 0: ok;
- 2: usage error (unknown flag, missing value, bad number, `--cancel-ahead` given without
  `fifo_cancel_ahead` or missing with it);
- 3: I/O error (cannot read the events, cannot write DIR);
- 4: invalid input (section 1.1);
- 5: consistency failure (`fills_outside_market > 0`). The files are still written.

stdout: nothing. stderr: one line,
`bt_run: <fills> fills, <orders_sent> orders, hash <hex16>, <wall_ms> ms`.

### 8.5 Project Makefile (`projects/04-backtester/Makefile`)

```make
PY    ?= python3
BUILD ?= build
ASAN  ?= build-san
export PYTHONPATH := $(CURDIR)/../01-post-trade-analytics/src:$(CURDIR)/../02-risk-monitor/src:$(PYTHONPATH)

build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j --target bt_run bt_tests mdtool
test: build
	ctest --test-dir $(BUILD) -R '^bt_' --output-on-failure
	BT_BUILD_DIR=$(BUILD) $(PY) -m pytest -q -p no:cacheprovider python
test-sanitize:
	cmake -S . -B $(ASAN) -DCMAKE_BUILD_TYPE=Debug -DBT_SANITIZE=ON
	cmake --build $(ASAN) -j --target bt_run bt_tests
	ctest --test-dir $(ASAN) -R '^bt_' --output-on-failure
sensitivity: build
	$(PY) python/sensitivity.py --bt-run $(BUILD)/bt_run --mdtool $(BUILD)/md/mdtool --out out/sensitivity
clean:
	rm -rf $(BUILD) $(ASAN) out
.PHONY: build test test-sanitize sensitivity clean
```

The root `Makefile` already calls `make -C projects/04-backtester test`. `mdtool` is built
as `$(BUILD)/md/mdtool` from the subdirectory.

---

## 9. Test plan

All C++ tests build their feeds in code (`tests/feed_builder.hpp`), so no Python is needed.
Times are written in ms for readability; tests use ns. The feeds are venue A style: one
snapshot message, then `SET`/`DELETE` messages and one-event `TRADE` messages.

### 9.1 Unit tests (`bt_tests`)

1. **event_csv**:
   - a round trip of every event type (rows written with the Project 3 header); the
     golden `../03-market-data/tests/golden/expected_events_A.csv` parses and groups into
     44 messages;
   - negative cases: bad header, 11 fields, unknown type/side, a mixed trade message, a
     time going backwards, two venues, `msg_index` changing inside a message.
2. **queue pure functions**:
   - arrival: OPT `0`, FIFO `D`.
   - trade: FIFO `q=500,rem=200,V=400` gives `{0,100}`; `q=100,V=150` gives `{50,0}`;
     through gives `{min(rem,V),0}`.
   - depth:
     - `(q=500, 800->600, e=0)`: BACK 500, f=0.25 450, f=0.5 400, f=1 300;
     - forced: `(q=500, 600->300, e=0, f=0)` gives 300;
     - explained: `(q=200, 500->200, e=300)` gives 200;
     - arrived after the print: `(q=500, 500->200, e=300)` gives 200;
     - increase: `(q=500, 500->900)` gives 500;
     - delete: `(q=300, 300->0)` gives 0.
   - Property test (10k SplitMix-seeded random cases): `0 <= q' <= after`; q' is
     non-increasing in f and non-decreasing in q; f=0 equals FIFO_BACK.
3. **latency**:
   - the SplitMix64 first 3 outputs for seed 1 equal 0x910a2dec89025cc1, 0xbeeb8da1658eec67, 0xf893a2eefb32555e;
   - FIFO channel: entry 5 ms and cancel 1 ms, order sent at 10 and cancel at 11, both
     arrive at 15, order first;
   - jitter 0 makes no draws;
   - report channel monotone.
4. **event queue**: `(ts, rank, seq)` order. At equal ts: ExchangeIn < Report < Timer;
   within one kind, push order.

### 9.2 Exchange and loop tests (`ScriptedStrategy`, hand-built feeds)

**S1, queue advancement in every model** (L = 0; the cumulative ordering of 3.6 is
checked at t = 5).

Feed:
- t0: snapshot, bids 100:500 and 99:300, ask 101:400.
- The script sends a bid at 100 for 200 lots at t1.
- t2 SET bid 100→800 · t3 SET 100→600 · t4 TRADE sell 100×400 · t4.1 SET 100→200 ·
  t5 TRADE sell 100×150 · t5.1 SET 100→50 · t6 DELETE bid 100 · t7 TRADE sell 99×300 ·
  t7.1 DELETE bid 99.

| model | q after t1 / t2 / t3 / t4 / t5 / t6 | fills (ts: lots, kind) |
|---|---|---|
| OPTIMISTIC | 0 throughout | t4: 200 AT |
| FIFO_BACK | 500/500/500/100/0/0 | t5: 50 AT; t7: 150 THROUGH |
| FIFO_CA(0.25) | 500/500/450/50/0/0 | t5: 100 AT; t7: 100 THROUGH |
| FIFO_CA(0.5) | 500/500/400/0/0/0 | t5: 150 AT; t7: 50 THROUGH |

Also check: FIFO_CA(0) gives fills identical to FIFO_BACK; `trade_credit_unmatched_lots
== 0`; at t5 the cumulative fills satisfy OPT 200 >= 150 >= 100 >= 50.

**S2, partial fills and level re-add** (FIFO_BACK).
- Bid 100 for 200 lots arrives at D=100 (q=100).
- TRADE 100×60 then SET 100→40: q=40, no fill.
- DELETE 100: q=0.
- SET 100→300 (re-add): q stays 0.
- TRADE 100×120: fill 120 AT (partial; remaining 80). SET 100→180.
- TRADE 100×500: fill 80; status FILLED.
- Expect two fill rows; `orders.csv` `filled_lots=200`.

**S3, cancel in flight** (OPTIMISTIC, entry = cancel = ack = 5 ms).
- Snapshot at t0: bid 100:500, ask 101:500.
- The script sends a bid at 100 for 1000 lots at t1 (arrives t6, Accepted delivered t11)
  and a cancel at t10 (arrives t15).

| case | trade (sell at 100) | expected |
|---|---|---|
| a | 300 at t12 | fill 300 at t12 with `cancel_in_flight=1`; Filled delivered at t17; Canceled(700) at t20; `cancels_in_flight_filled=1`, `cancels_effective=1` |
| b | 300 at t16 | no fill; Canceled(1000) delivered at t20; `cancels_in_flight_filled=0` |
| c | 300 at exactly t15 | fill 300 (market before cancel at equal ts); Canceled(700) |
| d | 1500 at t12 | fill 1000; CancelRejected(TooLateToCancel) at t20; `cancels_too_late=1`, `cancels_in_flight_filled=1` |
| e | as (a) but FIFO_BACK | q=500 at arrival; no fill; Canceled(1000) |

In each case the strategy's report sequence and delivery times are checked exactly.

**S4, latency ordering.**
- (a) L = 0: a timer at t1 sends a bid at 100; a TRADE sell 100 also at t1. No fill
  (market first), and the order is Live at t1.
- (b) OPTIMISTIC, entry 5 ms: an order sent at t0 with a trade at t5 gets no fill (arrival after the
  market); the same order with the trade at t5.000001 gets a fill.
- (c) Report timing: a fill at t20 with ack 3 ms is delivered at t23. Two exchange events
  at the same ts are delivered in exchange order.
- (d) L = 0 run of S3-style traffic: `cancels_in_flight_filled == 0`.

**S5, cross and post-only.**
- Snapshot bid 100:500, ask 101:400. Our ask at 101 for 200 lots (q=400).
- A depth message {SET bid 101:300, DELETE ask 101, SET ask 102:500} gives a CROSS fill of
  200 at 101 (sell), in every model.
- A resting ask at 102 for 500 lots (q = 500). A message {SET bid 102:300, DELETE ask
  102, SET ask 103:400} gives a CROSS fill of 300 (partial; remaining 200). The next
  message, SET bid 102:350, keeps it crossed, so there is no second fill (edge trigger).
  After DELETE bid 102 (uncrossed) and then SET bid 102:100 (crossed again), it fills
  100.
- A bid sent at 101 while the best ask is 101: Rejected(WouldCross).
- An order arriving before the first snapshot: Rejected(BookNotLive).
- `--cross-fills off`: no CROSS fills.

**S6, several of our orders at one price** (OPTIMISTIC).
- Two bids at 100 (200 lots each, arrivals t1 and t2). TRADE sell 100×300 fills 200 for
  the first and 100 for the second.
- FIFO_BACK variant: bid 100 shows 100 lots at both arrivals (q = 100 each). TRADE sell
  100×400: the first order has c = 100 and fills 200; the second sees V = 200, has
  c = 100, and fills 100.

**S7, market maker rules** (`MarketMaker` with a fake context, no loop):
- flat: it joins both sides at the touch;
- `position = 20001` lots: bid at best−1;
- `position = 50000`: no bid, and a working bid is canceled;
- the touch moves up a tick: cancel and new order in that send order;
- a PendingNew order at the right price is not duplicated;
- after a Rejected it sends again;
- a partially filled order is not topped up;
- `view.live = false`: cancel all.

**S8, stale session.** Golden-like hand feed with a sequence gap: after the gap the
strategy sees `live=false` and cancels; arrivals while STALE are BookNotLive; queue values
are only clamped.

### 9.3 Determinism (`test_determinism.cpp`, in-code random feed)

`feed_builder::random_feed(seed, n_steps)` builds a legal venue A style book walk with
SplitMix64:
- touch moves that delete and add levels in one message;
- random SET/DELETE near the touch;
- trades at the touch, each followed by its reduction message.

On that feed:
- Two `run_backtest` runs with MarketMaker, L = 5 ms and FIFO_CA(0.5) give identical
  `fills.csv` bytes and hashes.
- Jitter 0: seeds 1 and 2 give equal hashes.
- Jitter 200 µs: seeds 1 and 2 give different hashes (the feed must yield at least 50
  fills; assert this precondition), while seed 1 twice gives equal hashes.
- Ordering property (3.6) with ScheduledQuoter (default `life < period`) at
  L ∈ {0, 5 ms}: for every order id, filled lots satisfy
  OPT >= CA(0.5) >= CA(0.25) >= BACK, and the order id sets are identical across
  models.

### 9.4 Python (`python3 -m pytest -q -p no:cacheprovider python`)

Tests find the build through `BT_BUILD_DIR` (default `build`): `bt_run` at
`$BT_BUILD_DIR/bt_run` and `mdtool` at `$BT_BUILD_DIR/md/mdtool`. They skip with a clear
message if either is missing.

`test_integration.py`, using a module-scoped fixture: `gen_feeds.py --seed 7 --hours 0.05`
(clean) into a tmp dir, then `mdtool normalize --venue A`.
1. **Project 1 and Project 2 work unchanged.**
   - `bt_run --strategy mm --latency-us 1000 --model fifo_cancel_ahead --cancel-ahead 0.5`
     exits 0 and has fills > 0.
   - `python -m pta.cli report --fills fills.csv --market market.csv --out R` exits 0 and
     writes `R/report.md`.
   - `pta.report.analyze` totals satisfy `total == spread_capture + inventory - fees`
     within 1e-9.
   - `python -m rmon.cli run --market market.csv --fills fills.csv --out R2` exits 0 and
     writes `snapshot.json`. Alerts are allowed, because rmon's limits are calibrated on
     Project 1's model.
2. **Market identity:** `market.csv` equals, byte for byte, the output of
   `mdtool export --a venueA.jsonl --venue A --out X`.
3. **Consistency:** `fills_outside_market == 0` and `trade_credit_unmatched_lots == 0`.
   Python's FNV-1a of `fills.csv` equals `fills_fnv1a64`.
4. **Determinism:**
   - two identical runs give identical `fills.csv`, `summary.csv` and `orders.csv`;
   - `--seed 1` and `--seed 2` without jitter give the same hash;
   - with `--latency-jitter-us 100` they give different hashes and the same seed gives the
     same hash.
5. **Ordering on the generated feed:** `--strategy schedule` at L ∈ {0, 5000 µs} for the 4
   models. Join `orders.csv` by `order_id` (identical id sets, asserted) and check
   per-order `filled_lots` OPT >= CA(0.5) >= CA(0.25) >= BACK. Also check that CA(0) is
   identical to BACK.
6. **Market maker sanity:** at L = 0, `cancels_in_flight_filled == 0`. At L = 20 ms,
   `cancels_in_flight_filled > 0`, and `fills_cross > 0` in every model.
   - The market-maker fill-count ordering is **reported, not asserted** (3.6).
   - The test prints the four counts so a reviewer can see them.

`test_sensitivity.py`: `sensitivity.py --quick` into a tmp dir finishes in under 60 s.
`sensitivity.csv` has the exact columns of 7.3 and 4 rows, `sensitivity.md` contains the
statement of 7.6 verbatim, and `sensitivity.png` exists and is larger than 10 KB.

### 9.5 Build checks

The Release build with `BT_WERROR=ON` has zero warnings, and `make test-sanitize` passes.

---

## 10. Risks, open questions, and what to know about Project 3

**Project 3 facts the implementer must rely on:**
1. **Project 3 is implemented** (when this design was written it was not). Project 4 uses
   its `mdlib` (Book, VenueSession, Merger, QuoteExporter, fixed formatting) and
   `mdtool normalize/export`, pulled in with `add_subdirectory(../03-market-data ... EXCLUDE_FROM_ALL)`.
   - Project 3's API in its DESIGN section 11 is binding.
   - If Project 3's CMake target names or options (`mdlib`, `mdtool`, `MD_WERROR`,
     `MD_SANITIZE`) change, update 8.2.
2. **Trades never change the book.** The book effect arrives as the very next venue A
   message. Section 3.4 depends on this adjacency. It holds in the generator because each
   trade record is appended immediately before its group and A receive times increase
   strictly in file order. It is not a contract in Project 3's DESIGN. Ask Project 3 to
   add one sentence making it one, or keep the `trade_credit_unmatched_lots == 0` check as
   the guard.
3. **In the synthetic feed, cancels and trades are exactly distinguishable** (verified
   3,198 of 3,198 on seed 7). The generator also never sweeps more than one level, and
   trade size never exceeds the touch size. On real feeds the attribution would be a
   heuristic (matching by time and size), and FIFO results would carry that error.
4. **Level sizes are re-drawn, not queued.**
   - The generator's "modify" sets a level to a fresh uniform random size (0.1-5 units).
     A touch noise action always modifies, never deletes.
   - On seed 7 over 0.25 h, there were about 7.6k non-trade decreases at the touch against
     3.2k trades.
   - Most queue movement in the FIFO models therefore comes from the forced clamp
     (3.2), not from trades. The cancel-ahead fraction f is not identifiable from these
     data, since the generator has no notion of queue order. The f results show
     sensitivity, not an estimate.
5. **Touch moves delete and re-add levels in one message, and they are frequent**
   (about 5.8 touch-price changes per second on seed 7, including Project 1's 15 %
   one-tick spread widenings). No latency can dodge a move that arrives as one message,
   so stale-quote losses appear even at L = 0 through the cross rule. Latency then adds
   time spent at the wrong price, plus in-flight double exposure. The cross rule (3.5) is
   a modelling decision with a large effect: hence the `--ablate-cross` ablation.
6. **Venue B is not used.**
   - Its touch moves are applied 5-40 ms into the step.
   - It is sometimes one tick wider.
   - It splits one change set into several one-row messages (deletes, then changes, then
     new levels), so the strategy would react to transient tops.
   - Running on B works, but its results mix these artefacts with latency effects.
7. **The golden fixture has faults** (drop, duplicate, crossed). Use it only for
   CSV-reader tests, never for fill tests.
8. **The export filter drops rows while stale or crossed,** which is why
   `fills_outside_market` exists. On clean feeds it is expected to be 0.
9. **Project 3 has no event-CSV reader.** Project 4 implements `parse_event_csv_line`;
   enum names come from Project 3 DESIGN section 1.

**Open questions (decided here, revisit only with evidence):**
- **Quote size: 1000 lots (1 unit),** against Project 1's random 1-3 units. Larger
  quotes give more partial fills and larger rmon position swings. rmon's limits (warn 60
  units) may fire in OPTIMISTIC cells. That is acceptable, and the integration test
  allows alerts.
- **Post-only rejection, rather than letting a crossing order take liquidity.** Taking
  would need a taker fee and is not part of Project 1's model. As a result, with high
  latency some re-quotes are rejected, which the summary counts.
- **No modelling of exchange matching delay or of the market reacting to our orders.**
  The local-clock simplification (2.1) slightly understates the effective latency, by
  venue A's market-data latency (about 0.5 ms mean).
- **Pta marking.** A CROSS fill's loss lands in spread capture (7.2). If reviewers prefer
  it in inventory, the fix belongs in the write-up's explanation, not in the fills
  (`ts_us` must stay the exchange time).
- **Comparison with Project 1's toy model.** Running `pta simulate` on the same reference
  path is not like-for-like: different market data, and fills linked to flow in Project 1
  but to the book here. It may appear in the write-up as context, with that caveat, but it
  is not a cell of the grid.

**Exit-criteria mapping (roadmap section 6):**

| criterion | where it is met |
|---|---|
| deterministic replays | section 6; tests in 9.3 and 9.4 item 4 |
| unit tests for queue advancement and cancels in flight | 9.1 item 2; 9.2 S1, S2, S3 |
| sensitivity table and chart | section 7 (`sensitivity.md` and `sensitivity.png`) |
| not-live-PnL statement | 7.6, verbatim in `sensitivity.md` and `docs/writeup.md` |
