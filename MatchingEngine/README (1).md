# Matching Engine

*A correct, single-threaded limit order book matching engine written in modern C++ using price-time priority.*

---

## What it does

This project implements the core component of an electronic exchange: a limit order book. The book stores buy and sell orders, matches incoming orders against resting orders on the opposite side, and maintains the remaining orders that are waiting to trade.

Matching follows **price-time priority**. Better prices always execute first, and when multiple orders exist at the same price, the oldest order is matched first (FIFO). When a trade occurs, it executes at the **resting order's price**, matching the behaviour of real-world exchanges.

Current supported operations:

- Submit limit orders
- Submit market orders
- Cancel existing orders
- Modify existing orders
- Partial fills across multiple price levels

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

Prices are stored as integer ticks instead of floating-point numbers because floating-point values cannot represent many decimal values exactly. Matching depends on exact price comparisons, so integer arithmetic avoids rounding errors.

Remaining quantity is stored rather than the original quantity because matching continuously reduces the outstanding size of an order.

A monotonically increasing sequence number is also used as the order ID in v1. This uniquely identifies an order while simultaneously recording arrival order, allowing FIFO ordering without relying on timestamps.

**Rejected**

Using floating-point prices introduces rounding issues that can incorrectly determine whether two prices are equal.

Using timestamps for ordering was rejected because multiple orders may arrive within the same clock tick and system clocks are not guaranteed to be perfectly monotonic.

---

## Order Book (per side)

**Choice**

Each side of the book uses an ordered map from price to price level.

```
Price
  │
  ▼
FIFO queue of orders
```

**Why**

The matching engine constantly needs access to the best available price.

Using an ordered map keeps price levels automatically sorted, allowing efficient insertion while making the best bid and best ask immediately available.

This closely matches the access pattern of a matching engine, where the best price is accessed far more frequently than deeper levels.

**Rejected**

A hash map provides average **O(1)** lookup for individual prices but does not preserve ordering. Finding the next best price after a level becomes empty would require additional bookkeeping, making the overall design more complicated.

---

## Price Level

**Choice**

Each price level stores its orders in FIFO order.

New orders are added to the back of the queue, while matching always removes or partially fills the order at the front.

**Why**

Price-time priority requires that among orders at the same price, the earliest submitted order executes first.

A FIFO structure naturally enforces this behaviour while allowing efficient insertion and removal.

**Rejected**

Alternative container layouts were considered, but stable node addresses become important once an order index is introduced for O(1) cancellation. A container with stable elements simplifies the cancel mechanism considerably.

---

## Cancel Index

**Choice**

A hash map stores:

```
Order ID → Location in the order book
```

The index stores non-owning references to orders rather than owning the orders themselves.

**Why**

Cancelling an order should not require searching the entire order book.

The index allows an order to be located in constant time before being removed from its price level.

Because ownership remains with the order book, the index only needs to reference existing orders without managing their lifetime.

**Rejected**

Using `std::shared_ptr` was rejected because the cancel index does not own orders. Shared ownership would introduce unnecessary reference-counting overhead on the engine's hot path while complicating memory management.

---

## Testing

Testing will be expanded throughout development.

Planned testing includes:

- replay tests with known order sequences
- verification of expected fills
- verification of final book state
- property-based testing using randomly generated order flow
- invariants such as:
  - no crossed book
  - FIFO preserved within each price level
  - volume conservation
  - no orphaned cancel-index entries

---

## Scope (v1)

### Included

- Single-threaded matching engine
- Limit orders
- Market orders
- Cancel orders
- Modify orders
- Price-time priority
- Correct order matching
- Unit and replay testing

### Out of Scope

These are intentionally deferred until later phases:

- Multi-threaded matching
- Performance benchmarking
- Lock-free data structures
- Persistence
- Risk management
- Margin calculations
- Pricing models
- Derivatives support

---

## Future Work

Planned improvements include:

- Single-writer concurrent architecture
- Benchmarking with realistic synthetic order flow
- Memory pools and allocator optimisation
- Alternative order book data structures
- Latency and throughput analysis
- Property-based fuzz testing
- Performance profiling and optimisation