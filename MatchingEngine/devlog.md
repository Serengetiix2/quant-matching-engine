# Dev Log — Matching Engine

*Session-by-session build progress.*

---

## Phase map (where each piece lives)

| Phase | Window | Focus |
|---|---|---|
| W3 | design | Domain trace, structure design, README seed |
| W4 | core | Order type → book skeleton → match loop → submit → cancel |
| W5 | subtle ops | modify, edge cases, validation |
| W6 | testing | property-based invariants, defence |

---

## Status board

| Component | State | Tests |
|---|---|---|
| `Order` type | ✅ done | defended (integer ticks, seq-not-clock, id-vs-seq) |
| Book skeleton (maps, `Level`, `best()`, `rest()`) | ✅ done | covered |
| Cancel index population | ✅ done | covered |
| Match loop | ✅ done | canonical buy/sell, market, rest-remainder, cross-comparison |
| `submit` + replay harness | ✅ done | 6 replay cases |
| `cancel` | ✅ done | 4 cancel cases |
| `modify` | ✅ done | 5 modify cases |
| Edge cases | ✅ done (I) | multi-level sweep, exact-match boundary; self-cross scoped out |
| `validate` / input rejection | ✅ done | 3 validation cases (dup-id, bad-qty, bad-price) |
| Property-based / invariant tests | ⬜ | — |

**Status: W4 core + W5 (modify, edge cases I, validation) all COMPLETE — 23 passing tests. Correct, tested, single-threaded matching engine: submit (limit+market) with input validation, price-time matching, partial fills, cancel, modify (fairness rules), multi-level sweeps, exact-match boundaries. Fixed a critical sell-side crossing bug along the way. W6 (property-based testing) is the only phase remaining before concurrency.**

---

## Session entries

### W5·3 — Validation (COMPLETE)

- **`validate(const Order&) -> bool`**: rejects duplicate id (via `contains`), `quantity <= 0`, and `price <= 0` for limit orders only (market price ignored). `seq` exempt — engine-assigned, never caller input, so nothing to guard there.
- **`rest` converted to `bool`**, guard-clause style: `if (!validate(o)) return false;` at top, `return true;` at bottom. Consistent with `cancel`/`modify`'s existing bool-return pattern — no more silent void operations.
- **`submit` now returns `std::optional<std::vector<Fill>>`**: `nullopt` signals rejection (failed `validate` before any matching attempted); a present vector (possibly empty) signals legitimate matching. First time rejection and legitimate-zero-fills are distinguishable.
- **Critical bug found and fixed:** `submit`'s crossing condition used `incoming.price >= resting.price` for BOTH sides. Correct for a buy aggressor; wrong for a sell — should be `resting.price >= incoming.price`. Invisible in prior tests because the existing sell-aggressor test used equal prices, where both comparisons agree. Only manifests when a sell genuinely undercuts a resting bid (the common real case). Caught by a dedicated test with unequal prices (sell 95 vs resting bid 100) — first run correctly failed with 0 fills instead of 1, confirming the diagnosis before the fix; fix applied, full ~20-test suite rerun to confirm no regression. All green.
- **New test — "Cross Comparison Check":** sell 20@95 against resting buy 50@100. Expects fill at the *resting* price (100, not 95, per the execution-price rule), aggressor 2, resting 1; states 30 remaining bid / 0 ask.
- **`ValidationTest` helper**: takes the book by reference, a vector of orders, and a parallel vector of expected accept/reject outcomes — checks each submission's actual outcome against what was expected, rather than just "did anything fail" (needed because the bad-price case proves rejection AND non-false-positive in the same run).
- **3 validation tests, all passing:**
  - Duplicate id: second order with a reused id rejected, first order's quantity (50) untouched (`quantityAt` confirms).
  - Bad quantity: zero and negative quantity both rejected, book stays empty.
  - Bad price: negative/zero limit prices rejected, market order with price 0 accepted in the same run — proves the guard doesn't false-positive on markets, which are exempt from price validation by design.
- **W5 FULLY COMPLETE** — modify, edge cases I, validation, all done and tested. 23 passing tests.

### W5·2 — Edge cases I

- **Multi-level sweep:** 3 resting levels (25/25/50), one aggressor for 100 sweeping all three. Proves the match loop generalises across repeated iterations, not just a single 2-level partial (which every prior test used).
- **Exact-match boundary:** isolated single-level test — rest 50, aggressor exactly 50. One fill, both sides to zero, level erased. A third order (a small buy resting *after* the match) acts as a live probe: if the sell level had a ghost entry instead of being genuinely erased, this order would produce an unexpected fill instead of resting cleanly. It doesn't — proving erasure, not just emptiness.
- **Self-cross:** reasoned out of scope for v1 — `Order` has no participant/account field, so "same trader on both sides" isn't representable. Documented as a possible future extension (participant model), not a bug or gap.
- **Order-id ownership:** reviewed caller-assigned vs engine-assigned ids. Staying caller-assigned for now (test legibility — hand-authored sequences need predictable ids); revisit engine-assignment at W6 fuzzing / W7+ benchmarking, where uniqueness-by-construction matters more than hand-editing convenience. Duplicate-id guard implemented as W5·3 validation item.
- **Plan review:** most of the originally-listed W5·2 edge cases (thin book, empty side, 2-level partial) turned out already covered by existing W4 tests. Re-scoped to the two cases above, which are the ones existing tests structurally can't catch (loop-generalisation, exact-zero boundary).

### W5·1 — Modify

- `modify(Id, std::optional<Price>, std::optional<Quantity>)` — target-state signature; optional fields mean "leave unchanged", handles single or combined changes atomically, `nullopt` self-documents (vs a magic 0).
- **Fairness rule derived:** reduce-quantity keeps queue position (in-place edit — asking for less harms no one); increase-quantity and price-change LOSE position (the added quantity / new level arrived later, can't jump earlier orders) → implemented as cancel + resubmit with same id, fresh seq.
- id preserved across modify (stable identity); seq is fresh on resubmit (that's what puts it at the back). The id-vs-seq split from W4·1 paying off.
- `quantity == 0` → cancel, checked FIRST (before price/quantity routing) so "to zero" always cancels regardless of price change.
- Bugs caught: use-after-free (using the order reference after cancel destroys the node) — fixed with capture-before-cancel (side/price/quantity into value locals, then cancel, then rebuild+submit); forgetting to override the captured field with the new value in each change branch.
- Rejected: limit→market via "price 0" (magic-value overloading, mixes the rests/doesn't-rest boundary) — type-change out of scope for v1.
- All five modify cases green: reduce-keeps-position, increase-loses, price-change-loses, modify-to-zero-cancels, unknown-id-noop. Derived and verified the fairness rule **by consequence** (fill-order in the resulting Fills), not by inspecting internal queue structure.
- Caught and closed a verify-by-consequence blind spot: lose-position tests with a single small matcher can't distinguish "order moved to back" from "order dropped" — fixed by sizing the matcher to sweep through the front order into the tracked one, so both fills emit and their order proves presence + position.
- Extended `modifyTest` with `ExpectedLevel` state assertions to close the ghost-order gap (no duplicate left at the old price level on a price-change).

### W4·5 — Cancel (W4 COMPLETE)

- `cancel(Id) -> bool` on OrderBook: look up id in cancel index → get list iterator → capture price as a value → erase order from its level's list (O(1)) → erase from index → remove level from map if now empty.
- **Safety call (defended under challenge):** capture price as a value before erasing (the list node dies on erase); confirmed all reads of the order reference precede the erase, so no use-after-free. The "don't copy" tenet is about whole Orders/containers, not an 8-byte price — capturing a scalar is free. Kept defensive find-guards for now; strip at benchmarking.
- **Self-describing-order insight:** the cancel index stores only the iterator. Side/price are derived from the order's own fields (the order knows where it belongs), so storing them again would be redundant, mutable-in-two-places state.
- **Harness refactored into a `Test` class**; added `CancelTest` alongside `runReplayTest`. Success signal is `cancel`'s bool return, NOT `contains` (which can't distinguish "cancelled" from "never existed").
- 4 cancel tests: single-order (level emptied, neighbour untouched), two-at-same-price (one cancelled, other + level survive), unknown-id (clean no-op), cancel-last-at-price (level removed). All pass.

### W4·4 — Submit + Replay Harness

- Renamed `match` → `submit` (it matches AND rests the remainder — the full entry point). `rest` stays as the internal placement helper.
- `quantityAt(Side, Price) -> int64_t` — total resting quantity at a price (0 if absent); the state-inspection primitive. Reasoned that total-per-price + `best()` covers all cases; per-order/FIFO inspection deferred to W6 when the invariant needs it.
- `runReplayTest(name, sequence, expectedFills, expectedState)` — runs a scripted order sequence through `submit`, asserts actual fills == expected (count first, then element-by-element on price+quantity) and actual state == expected (via `quantityAt`). Readable failure output via `operator<<` overloads for `Fill`/`ExpectedLevel`/vectors.
- 5 replay cases: buy aggressor, sell aggressor (genuinely distinct, not a duplicate), rest-remainder, market order (remainder dropped), empty-book. All pass.
- Bugs caught by tracing: every-against-every fills comparison (kept last comparison only), self-comparison in states check (compared quantityAt to itself), price/quantity field-order swap in test data, duplicated sell test masquerading as coverage.
- **Deferred:** determinism check (run-twice-identical) → meaningful only at concurrency (W7), noted as conscious deferral. `ExpectedLevel` "checks listed levels, won't catch unlisted extras" — fine for authored sequences.

### W4·3 — Match loop (the hardest single day)

- `std::vector<Fill> match(Order&)` — the matching heart. While incoming has quantity and opposite side non-empty: grab best resting order (mutable, from own maps), check crosses, `tradeQty = min(both quantities)`, reduce both, emit Fill at resting price, pop filled resting orders, remove empty levels. After: rest limit remainder / drop market remainder.
- `crosses` generalised: market always; buy-limit `incoming.price >= resting.price`; sell-limit `resting.price >= incoming.price`.
- `withOppositeSide` lambda-template solves the "bids and asks are different types" problem (comparator makes them distinct types) cleanly.
- **`Fill` struct:** price (resting/execution price), quantity, aggressorId, restingId.
- Also erase from cancel index when a resting order fully fills (no orphaned index entries).
- Bugs caught by comparison-to-plan: `=` vs `==` in the zero check (silent), `min` of prices not quantities (the volume-conservation line), branch structure (loop trapped in wrong side), rest-on-full-fill, spurious "dropped" message on a fully-filled limit.
- Proven on canonical buy (2 fills: 100@102, 20@103; 30 resting) AND sell aggressor (highest-bid-first). Both directions correct.
- **No smart pointers:** the level owns the order; match only mutates + removes. A shared_ptr's atomic refcount would be pure cost on the hot path for a non-problem.

### W4·2 — Book skeleton

- Built `OrderBook`: two side-maps (`bids` with reversed comparator, `asks` default), `Level { std::list<Order> }`, `best(Side)`, `rest(Order)`.
- Cancel index = `unordered_map<Id, list::iterator>` — populated in `rest`.
- **Design call made solo:** `best()` returns `const Order*` (not `std::optional<Order>`) — a handle to the *actual* resting order so the match loop can mutate it in place; `nullptr` signals empty. A copy would have broken matching.
- `contains(Id)` added as a minimal public observer for the private index.
- 7 tests: top-of-book (lowest ask / highest bid), side independence, empty→nullptr, FIFO-within-level (by id), index population.
- **Known deferrals (W9 / tidy):** `rest(Order o)` takes by value → copies; could move into the list. Remove stray `<iostream>` from the header. Raw pointer from `best()` valid only until that order is filled/cancelled — fine for internal callers.

### W4·1 — Order type

- `Order` struct: side, type, price (int64 ticks), quantity, id, seq. Enums `Side`, `Type` as `enum class`.
- Public struct (pure data, no invariants of its own → no encapsulation needed; invariants live in the book).
- Defended cold: integer ticks (float equality unsafe), seq-not-clock (clocks collide/run backwards), id-vs-seq (identity vs temporal ordering).

### W3 — Design on paper

- Traced the canonical matching example by hand (2 fills, 30 resting @103) — the replay-test oracle.
- Derived all four structures from the three requirements (fast best-price, FIFO, fast cancel), each with rejected alternative.
- Discovered the `std::list` stable-node property myself from the cancel requirement.
- Checkpoint: **Go** — continue in C++.

---

## W9 optimization candidates (NOT to act on before benchmarking)

Ideas surfaced during W5 while thinking about FIFO inspection. Recorded so the thinking isn't lost — but these are performance-speculative and must be measured, not assumed. The list-based core is correct and tested; do not rewrite it without (a) the W6 property-test safety net and (b) benchmarks justifying the change.

- **Tombstone-vector vs list levels.** Replace `std::list` levels + iterator-splice cancel with a `std::vector` + `is_cancelled` flag on Order; cancel flips the flag, matching skips tombstones. Trade: O(1) splice-cancel → cache-friendly contiguous storage, but cancelled orders accumulate (needs compaction) and every match branches past dead orders. Which wins depends entirely on workload — a W9 measurement, not a guess. NOTE: the list is currently load-bearing — cancel removes from the *middle* in O(1) via stored iterator; a front/back-only structure can't do that.

- **Raw-pointer cancel index.** Only viable with *stable* storage. Stable with `std::list` (nodes don't move); DANGLES with a vector (reallocation on growth). So this conflicts with the tombstone-vector idea — can't have both. If levels stay list-based, a raw pointer/iterator is already what's used.

- **Map unification (LOW RISK — simplification, not perf gamble).** Make both `bids` and `asks` the same type (default comparator); read best-bid via `rbegin()` (highest) and best-ask via `begin()` (lowest). Removes the `withOppositeSide`/`withGetSide` lambda-templates. DECISION: do this any time between now and end of W6, as its own focused change with all tests green — not bolted onto a feature. This is the one candidate safe to act on pre-benchmark because it's a code simplification with test coverage, not a storage-layout gamble.

---

## Next session

**W6 — property-based testing (the real differentiator).** Define invariants as checkable functions (no crossed book, FIFO preserved within a level, volume conservation, no orphaned cancel-index entries), build a generator for randomised-but-valid order flow, run it fuzzed against all invariants, add a shrinker for failing seeds, then bring the README/DEVLOG to interview-defensible. This is genuinely unbuilt work and the highest-value remaining phase — it'll find bugs the hand-written suite can't imagine (the crossing bug this session is a preview of exactly that kind of thing).