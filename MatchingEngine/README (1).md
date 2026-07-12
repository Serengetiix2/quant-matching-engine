# Matching Engine

*A correct, single-threaded limit order book matching engine written in modern C++ using price-time priority.*

---

## What it does

This project implements the core component of an electronic exchange: a limit order book. The book stores buy and sell orders, matches incoming orders against resting orders on the opposite side, and maintains the remaining orders that are waiting to trade.

Matching follows **price-time priority**. Better prices always execute first, and when multiple orders exist at the same price, the oldest order is matched first (FIFO). When a trade occurs, it executes at the **resting order's price**, matching the behaviour of real-world exchanges.

### Currently implemented and tested

- Submit limit orders
- Submit market orders
- Order matching with price-time priority
- Partial fills across multiple price levels
- Cancel existing orders (O(1) by id, with automatic level cleanup)

### Planned / in progress

- Modify existing orders (with price-time priority rules)
- Edge-case hardening (validation, thin-book behaviour)
- Property-based / invariant testing

See `DEVLOG.md` for detailed build progress.

---

## Build & Run

```bash
mkdir build
cd build

cmake ..
cmake --build .

./matching-engine
```

---

# Design Decisions

The goal of this project is correctness first. Every design choice favours maintaining the matching rules while keeping operations efficient.

## The `Order` type

**Choice**

Each order stores:

- unique ID / sequence number
- side (`Buy` or `Sell`)
- order type (`Limit` or `Market`)
- price (stored as integer ticks)
- remaining quantity

**Why**

Prices are stored as integer ticks instead of floating-point numbers because floating-point values cannot represent many decimal values exactly, and equality comparison between two doubles that *should* be equal can silently fail. Matching depends on exact price comparison, so integer arithmetic removes that entire class of error.

Remaining quantity is stored rather than the original quantity because matching continuously reduces the outstanding size of an order.

A monotonically increasing sequence number is also used as the order ID in v1. This uniquely identifies an order while simultaneously recording arrival order, allowing FIFO ordering without relying on timestamps.

**Rejected**

Floating-point prices were rejected because rounding and unsafe equality comparison can incorrectly determine whether two prices match.

Timestamps for ordering were rejected because multiple orders may arrive within the same clock resolution, and system clocks are not guaranteed to be monotonic (they can be adjusted backwards). A monotonic counter guarantees a unique, gap-free, always-increasing arrival order by construction.

---

## Order Book (per side)

**Choice**

Each side of the book uses an ordered map from price to price level. The two sides are kept as separate maps, with the bid side using a reversed comparator so that the best price sits at the front of each map.

```
Price
  │
  ▼
FIFO queue of orders
```

**Why**

The matching engine constantly needs access to the best available price. An ordered map keeps price levels automatically sorted, allowing efficient insertion while making the best bid and best ask immediately available. This closely matches the access pattern of a matching engine, where the best price is accessed far more frequently than deeper levels.

**Rejected**

A hash map provides average **O(1)** lookup for an individual price but does not preserve ordering. Finding the next best price after a level becomes empty would require scanning all levels (O(n)) or extra bookkeeping, which is worse for the operation that matters most.

---

## Price Level

**Choice**

Each price level stores its orders in a FIFO list. New orders are added to the back of the queue, while matching always removes or partially fills the order at the front. The level does not store its own price — the price is the map key, so duplicating it would risk the two disagreeing.

**Why**

Price-time priority requires that among orders at the same price, the earliest submitted order executes first. A FIFO structure naturally enforces this behaviour.

A linked list specifically was chosen because its node addresses are **stable** — a handle to an order stays valid as other orders are inserted or removed around it. This is what makes O(1) cancellation possible: the cancel index can hold a stable iterator directly to an order's node.

**Rejected**

A contiguous container (e.g. `std::deque`) offers better cache behaviour, but it can relocate its elements when it grows, invalidating any stored handle into it. That conflicts with the cancel index, which needs a stable reference. The cache-locality trade-off is revisited in the benchmarking phase, not now.

---

## Cancel Index

**Choice**

A hash map stores:

```
Order ID → stable iterator into a price level's list
```

The index stores non-owning handles rather than owning the orders themselves.

**Why**

Cancelling an order should not require searching the entire order book. The index locates an order in constant time, then removal from its price level is also constant time via the stored iterator.

The index stores only the iterator — an order's side and price are read from the order's own fields when needed, since the order is self-describing and knows where it belongs. Storing them again in the index would be redundant, mutable-in-two-places state. Because ownership remains with the order book, the index only references orders without managing their lifetime.

**Rejected**

Using `std::shared_ptr` was rejected because the cancel index does not own orders. Shared ownership would add atomic reference-counting overhead on the engine's hot path and complicate the memory model, for no benefit — the level already owns the order, and the index only needs to find it.

---

## Market Order Remainder Policy

**Choice**

When a market order cannot be fully filled — either because the opposite side becomes empty mid-match, or because the book was already too thin — the unfilled remainder is dropped. No leftover portion is rested in the book, and no rejection is signalled for the partial fill.

**Why**

A market order expresses an intent to take liquidity now at whatever the current market offers. It has no price attached, so if it were rested, it would need to be assigned one — and any choice would misrepresent the sender's intent. Dropping the remainder is the simplest behaviour consistent with what a market order is for: fill what the book can provide immediately, and discard what it cannot. This is also the behaviour of an "immediate-or-cancel" (IOC) order at real venues, and is the default assumption for a market order at most exchanges.

**Rejected**

Rejecting the entire order on any shortfall was rejected because it discards fills that were already valid and successfully executed. Partial fills are a normal outcome of hitting a thin book; the correct response is to keep them, not to undo them.
Resting the remainder as a limit order was rejected because a market order has no price to rest at. Assigning one arbitrarily (e.g. the last-traded price, or the far-touch) invents information the sender never provided and is a common source of unintended risk in real systems.

## Testing

Testing is written alongside each operation, not deferred. A reusable test harness runs scripted order sequences and asserts both the exact fills produced and the exact final book state.

**Current: 9 passing tests.**

- **Replay tests (5)** — buy aggressor, sell aggressor, rest-remainder, market order (remainder dropped), empty-book submit. Each asserts exact fills and final book state.
- **Cancel tests (4)** — single order (level emptied, neighbour untouched), two orders at the same price (one cancelled, other survives), unknown id (clean no-op), cancel last order at a price (level removed).

**Planned:**

- Property-based testing using randomly generated valid order flow
- Invariants asserted after every operation:
  - no crossed book (best bid < best ask)
  - FIFO preserved within each price level
  - volume conservation
  - no orphaned cancel-index entries
- Determinism check (identical output across repeated runs) — becomes meaningful at the concurrency phase

---

## Scope (v1)

### Included

- Single-threaded matching engine
- Limit and market orders
- Cancel and modify
- Price-time priority
- Replay and property-based testing

### Out of Scope

Intentionally deferred to later phases:

- Multi-threaded / concurrent matching
- Performance benchmarking and profiling
- Lock-free data structures
- Persistence / event log
- Risk, margin, pricing models, derivatives

---

## Future Work

- Single-writer concurrent architecture
- Benchmarking with realistic synthetic order flow
- Memory pools and allocator optimisation
- Alternative order book layouts (cache-friendly structures)
- Latency and throughput analysis (p50 / p99 percentiles)