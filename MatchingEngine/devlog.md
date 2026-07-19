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
| Map unification (pre-W6 cleanup) | ✅ done | full 23-test suite green |
| Property-based / invariant tests (W6·1–2) | ✅ done | 100k-op fuzzed run, 0 violations |
| Shrinker (W6·3) | ⬜ | not yet built — nothing organic to shrink |
| Defence pass (W6·4) | ⬜ | — |

**Status: W4 core + W5 (modify, edge cases I, validation) + map unification all COMPLETE — 23 hand-written passing tests. W6·1–2 COMPLETE: property-based fuzzer built and run — 100,000 operations (weighted toward modify), 3 active invariants checked after every single operation, ZERO violations. Correct, tested, single-threaded matching engine, now stress-tested at volume. W6·3 (shrinker) and W6·4 (defence pass) remain before concurrency.**

---

## Session entries

### W6·1–2 — Property-based invariant testing + fuzzer (COMPLETE)

**Invariants — precisely defined before coding, each derived through explicit reasoning (not just named):**
- **No crossed book:** `best(Buy)->price < best(Sell)->price` whenever both sides are non-empty; vacuously satisfied if either is empty. Implemented as `checkNoCrossedBook() const -> optional<vector<Order>>` — `nullopt` = clean, a populated vector (the two crossing orders) = violation. Required adding a `const` overload of `best()` (the existing one returns non-const `Order*` for the match loop's in-place mutation; the checker needed a read-only path).
- **Volume conservation:** for every trade, the aggressor's quantity decrease must equal the resting order's quantity decrease (the `min(quantities)` line's guarantee, checked after the fact). Lives in the fuzzer/harness against returned `Fill`s — not a book method, since it only needs public `Fill` data.
- **No orphaned cancel-index entries:** every `(id, iterator)` in `cancelIndex` must dereference to an order whose own `.id` matches the key. `checkNoOrphans() const -> optional<vector<Order>>`, same nullopt/violation-list shape.
- **FIFO preserved:** within any level's list, consecutive orders must have non-decreasing `seq`. **Proven structurally impossible to violate** by tracing every insertion path in the actual code (not assumed): `restInto` only appends at `end()`; the match loop only pops from the front; `modify`'s reduce-path only mutates a field in place, never re-inserts; `modify`'s increase/price-change paths go through `cancel` (pure removal) then `submit`→`restInto` (fresh seq, appended at end). No path can reorder a list. Because of this, `checkFIFO` was initially planned to be dropped from the per-operation hot path (O(book size) per call vs. crossed-book/orphans' cheaper cost) — ultimately built anyway (single forward pass, track previous `seq`, flag any decrease) since it was cheap to add and useful as regression insurance against future changes silently invalidating the guarantee (exactly what happened with the map-unification's begin/rbegin assumption a session prior).

**Placement discipline — a real design correction made mid-session:** initially reached for folding FIFO/orphan checks into `validate`. Caught and reversed: `validate` is a *gatekeeping* question (should this new order be admitted) with no view of the whole book; these are *whole-book structural* invariants needing access to all levels/the whole index. Landed on standalone `const`, pure-observation methods on `OrderBook` — explicitly **detection, not enforcement**: they watch and report, never intervene or auto-fix, since acting on a violation would hide the very bug the check exists to expose.

**`getOrderInfo(Id) const -> optional<Order>`:** added to let the generator read a resting order's current price/quantity safely for percentage-based modify generation — returns an owned copy (not an iterator/pointer), so it carries zero dangling risk regardless of what happens to the book afterward. Explicitly rejected an earlier idea of the generator holding its own `id→iterator` map (would have duplicated `cancelIndex` and reintroduced the exact dangling-iterator fragility fixed in the map-unification session — two independent structures holding handles into the same live memory).

**Generator design (`Generator` struct — `rng`, `restingIds` (plain `Id`s, no iterators), `nextId`):**
- Weighted operation mix, modify heaviest (highest-risk, most branching logic) — designed as: ~50% modify, ~30% submit, ~20% cancel; force submit if `restingIds` is empty.
- Type weighted ~90% Limit / 10% Market (only limits rest, so this is what keeps `restingIds` populated with real material for cancel/modify to target — an all-market flow would starve the interesting paths).
- Narrow price band (1–100) — deliberately maximises crossing/matching pressure, pointing fuzz effort at the invariants with actual historical bugs (crossed-book, volume conservation) rather than FIFO, which was independently proven structurally safe by this point.
- Modify's new price/quantity: percentage-of-current-value via `getOrderInfo`, scaled to a **max ~20% change per call** (divisor of 100, not 10 — the original /10 scaling allowed up to 3x per modify, which compounded across repeated modifies on the same order into absurd price drift, e.g. one order observed reaching price 15,918 after six consecutive modifies in an early test run).
- One operation generated and executed against the real book per step (not planned blind in advance) — resolves generator/book state drift, since a tracked "resting" order can get fully matched away by an unrelated later operation; checking the real book's state after each step keeps `restingIds` accurate.

**Bugs caught during generator construction (before any fuzzing even started):**
- Type-weighting was inverted **twice** across two different code paths: first pass used `== 0` out of a `(0,9)` range (9-in-10 → Market, the opposite of intended); after a partial fix, one of the two submit branches (the empty-book bootstrap path) was corrected while the other — the one that actually fires on effectively every iteration once the book has any resting order — was left on the old broken logic. Caught by reading the fuzz log's actual output ratio rather than trusting the code by inspection.
- `checkNoCrossedBook`'s truthiness direction double-checked explicitly (optional is truthy when it *holds a value* — i.e. when a violation was found) since it reads easily backwards; confirmed correct, documented with a comment to prevent a future accidental "fix."

**The run:** 100,000 operations, `-O3`, 0.39s wall time. **Zero invariant violations** — crossed-book, orphans, and FIFO all held for the entire run (confirmed via `./tests > run_output.txt && grep -c "DETECTED" run_output.txt` → `0`, not just eyeballing the log tail). This is the first genuinely volume-tested confidence in the engine's core correctness, beyond the 23 hand-authored scenarios.

**Process note:** two separate false-positive "clean" runs occurred before this one — first, a run where the invariant checks were accidentally commented out (measuring generation speed only, not correctness); second, a run where the type-weighting fix hadn't actually landed in the branch that mattered, so the "clean" pass was against unrepresentative (mostly-market) flow. Both caught by insisting on checking actual log output/ratios rather than trusting that a fix compiled correctly. Worth remembering: a fast, quiet test run is not evidence of correctness unless you've confirmed what was actually being exercised.

---

### Map unification (pre-W6 cleanup — COMPLETE)

- **Motivation:** `bids` used `std::greater<>` and `asks` used the default comparator specifically so `begin()` always meant "best" on both sides — which is why `withOppositeSide`/`withGetSide` had to be *templates* (the two maps were different C++ types). Unified both maps to the default comparator, replacing the templates with three small named helpers: `getMap(Side) -> map&` (the map for a given side, no flipping), `opposite(Side) -> Side` (the enum flip, used explicitly at call sites — e.g. `submit` composes `getMap(opposite(incoming.side))` rather than hiding the flip inside a helper), and `best(Side)` (already existed since W4·2, updated to `bids.rbegin()` / `asks.begin()` now that "best" isn't automatically at `begin()` for both sides).
- **Design call:** kept `rest()` as its own simple if/else rather than routing it through `getMap` — it wasn't duplicating error-prone logic, so adding indirection there would have been simplification for its own sake. Judged case-by-case rather than mechanically applying the new helpers everywhere.
- **Rejected:** reusing `validate()` for post-match cleanup in `submit`'s loop — `validate` is a gatekeeping question ("should this new order be admitted"), cleanup is a different question ("this existing order hit zero, remove it correctly"). Different category, `cancel()` is the right existing tool.
- **Rejected (explicitly, mid-session):** doing a broader cleanup/enhancement pass while at it, on the reasoning "I keep noticing more things, my code quality is improving." Recognised this as the normal experience of touching load-bearing code (re-examination surfaces things every time, doesn't mean growing debt) rather than evidence of a real backlog — and that W6's fuzzer is the disciplined tool for systematically finding exactly this class of thing, not more ad-hoc manual review. Kept the change bounded to the map unification only.
- **Bug 1 — found during the refactor:** `submit`'s post-fill cleanup block still did `oppositeSide.begin()->second.orders.pop_front()` unconditionally. This was fine when the comparator trick made `begin()` mean "best" on both sides — now that both maps share one comparator, `begin()` only means "best" for asks; for bids, best is `rbegin()`. The match itself (via `best()`) correctly read from the right end, but cleanup after a fill was popping/erasing from the *wrong* end whenever the aggressor was a sell (matching against bids). Manifested as failed rest-remainder/market/price-change-modify tests with corrupted state and, eventually, a crash from cumulative map corruption.
  - **Fix:** replaced the inline `begin()`-based pop/erase with a call to `cancel(restingId)` — side-agnostic, works from the order's own fields via the existing index, no begin/rbegin assumption. Capture `resting->id` into a local *before* calling `cancel` (same capture-before-cancel discipline as W5·1's modify), and ensure `fills.emplace_back(...)` runs *before* the cancel call, not after — reading `resting->price`/`resting->id` after `cancel` erases the node is a use-after-free (this was the source of the garbage Fill values and the trace-trap crash seen mid-session).
- **Bug 2 — found immediately after fixing Bug 1:** `cancel`'s internal side-lookup was written as `auto map = getMap(order.side);` — missing the `&`, silently copying the entire map. `cancel` was operating on a throwaway copy, so the real `asks`/`bids` was never actually mutated — explaining why the loop's next iteration kept finding a stale/corrupted view of the book even after Bug 1's fix. Same *class* of mistake as two earlier copy-vs-reference bugs from the same refactor session (the original `getSide`/`getOpposite` drafts also returned by value before being corrected) — worth flagging as a recurring failure mode: always double-check `auto&` vs `auto` at every call site of a function that returns a reference.
- **Full 23-test suite green** after both fixes. No regressions.

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

Ideas surfaced during W5 while thinking about FIFO inspection. Recorded so the thinking isn't lost — but these are performance-speculative and must be measured, not assumed. The list-based core is correct and tested; do not rewrite it without benchmarks justifying the change.

- **Tombstone-vector vs list levels.** Replace `std::list` levels + iterator-splice cancel with a `std::vector` + `is_cancelled` flag on Order; cancel flips the flag, matching skips tombstones. Trade: O(1) splice-cancel → cache-friendly contiguous storage, but cancelled orders accumulate (needs compaction) and every match branches past dead orders. Which wins depends entirely on workload — a W9 measurement, not a guess. NOTE: the list is currently load-bearing — cancel removes from the *middle* in O(1) via stored iterator; a front/back-only structure can't do that.

- **Raw-pointer cancel index.** Only viable with *stable* storage. Stable with `std::list` (nodes don't move); DANGLES with a vector (reallocation on growth). So this conflicts with the tombstone-vector idea — can't have both. If levels stay list-based, a raw pointer/iterator is already what's used.

- ~~**Map unification.**~~ **DONE** — see "Map unification (pre-W6 cleanup)" above.

- **Engine-assigned order ids.** Currently caller-assigned (kept for hand-written test legibility). Revisit if/when fuzzing or benchmarking needs uniqueness-by-construction rather than a duplicate-id validation guard.

---

## Next session

**W6·3 — Shrinker, then W6·4 — Defence pass.** The fuzzer found zero organic violations (100k ops, clean), so the shrinker has nothing real to reduce yet — either deliberately inject a known bug (e.g. temporarily revert the crossing-condition fix) to prove the shrinker can take a long failing sequence down to a minimal reproducer, or deprioritise this in favour of the defence pass given time constraints. W6·4: bring the README to fully interview-defensible — every structural decision answers what/why/rejected/how-you'd-know-if-wrong/what-you'd-do-differently. This session's 100k-operation, zero-violation fuzz result is strong, quotable material to fold in. After W6, the engine moves to W7 — concurrency.