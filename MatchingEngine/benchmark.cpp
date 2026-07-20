#include <tests.cpp>

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void report(const std::string& label, const std::vector<double>& nsPerOpResults) {
    std::cout << "-----------------------------------\n";
    std::cout << label << "\n";
    for (size_t t = 0; t < nsPerOpResults.size(); ++t)
        std::cout << "  Trial " << (t + 1) << ": " << nsPerOpResults[t] << " ns/op\n";
    double median = medianOf(nsPerOpResults);
    std::cout << "  Median: " << median << " ns/op  (" 
              << (1'000'000'000.0 / median) << " ops/sec)\n";
}

// ---- 1. Submit, resting-only (no crossing — pure insertion cost) ----
// Orders are all Buy at low prices / Sell at high prices so nothing ever crosses.
void benchmarkSubmitResting(generator& gen, int iterations, int warmup = 1000, int trials = 5) {
    std::uniform_int_distribution<int> quantityDist(1, 100);
    std::uniform_int_distribution<int> priceDist(1, 100);
    std::vector<double> results;

    for (int t = 0; t < trials; ++t) {
        OrderBook book;
        std::vector<Order> orders;
        orders.reserve(iterations);
        // Buys priced 1-50, sells priced 51-100 — the two sides can never cross.
        for (int i = 0; i < iterations; ++i) {
            bool isBuy = (i % 2 == 0);
            int price = isBuy ? (priceDist(gen.rng) % 50 + 1) : (priceDist(gen.rng) % 50 + 51);
            orders.emplace_back(isBuy ? Side::Buy : Side::Sell, Type::Limit,
                                 price, quantityDist(gen.rng), gen.nextId++, 0);
        }
        for (int i = 0; i < warmup; ++i) { Order o = orders[i]; book.submit(o); }

        auto start = std::chrono::steady_clock::now();
        for (int i = warmup; i < iterations; ++i) { Order o = orders[i]; book.submit(o); }
        auto end = std::chrono::steady_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back(static_cast<double>(ns) / (iterations - warmup));
    }
    report("SUBMIT — resting only (no crossing, insertion cost)", results);
}

// ---- 2. Submit, always crosses (full match-loop cost) ----
// Pre-seed one resting order the aggressor will always match against.
void benchmarkSubmitCrossing(generator& gen, int iterations, int warmup = 1000, int trials = 5) {
    std::vector<double> results;

    for (int t = 0; t < trials; ++t) {
        OrderBook book;
        // Deep resting liquidity on the sell side at price 50, so every buy aggressor at 50 matches immediately.
        for (int i = 0; i < iterations + warmup; ++i) {
            Order resting{Side::Sell, Type::Limit, 50, 1, gen.nextId++, 0};
            book.rest(resting); // rest() directly — no matching, pure seed, doesn't pollute the timed cost
        }

        auto runBatch = [&](int n) {
            for (int i = 0; i < n; ++i) {
                Order aggressor{Side::Buy, Type::Limit, 50, 1, gen.nextId++, 0};
                book.submit(aggressor); // always crosses exactly one resting unit
            }
        };
        runBatch(warmup);

        auto start = std::chrono::steady_clock::now();
        runBatch(iterations);
        auto end = std::chrono::steady_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back(static_cast<double>(ns) / iterations);
    }
    report("SUBMIT — always crosses (full match-loop cost)", results);
}

// ---- 3. Cancel ----
void benchmarkCancel(generator& gen, int iterations, int trials = 5) {
    std::vector<double> results;

    for (int t = 0; t < trials; ++t) {
        OrderBook book;
        std::vector<Id> ids;
        ids.reserve(iterations);
        for (int i = 0; i < iterations; ++i) {
            Order o{Side::Buy, Type::Limit, static_cast<Price>((i % 90) + 1), 1, gen.nextId++, 0};
            book.rest(o);
            ids.push_back(o.id);
        }

        auto start = std::chrono::steady_clock::now();
        for (Id id : ids) book.cancel(id);
        auto end = std::chrono::steady_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back(static_cast<double>(ns) / iterations);
    }
    report("CANCEL", results);
}

// ---- 4. Modify — the two paths cost very differently ----
void benchmarkModifyInPlace(generator& gen, int iterations, int trials = 5) {
    std::vector<double> results;
    for (int t = 0; t < trials; ++t) {
        OrderBook book;
        std::vector<Id> ids;
        for (int i = 0; i < iterations; ++i) {
            Price p = static_cast<Price>((i % 90) + 1);
            Order o{Side::Buy, Type::Limit, p, 100, gen.nextId++, 0};
            book.rest(o);
            ids.push_back(o.id);
        }

        int64_t checksum = 0;

        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < ids.size(); ++i) {
            book.modify(ids[i], std::nullopt, 50);
            checksum += book.getOrderInfo(ids[i])->quantity; // O(1) — reads the ONE mutated order, no level scan
        }
        auto end = std::chrono::steady_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back(static_cast<double>(ns) / iterations);

        std::cout << "  (trial " << (t + 1) << " checksum: " << checksum << ")\n";
    }
    report("MODIFY — in-place (quantity decrease)", results);
}

void benchmarkModifyCancelResubmit(generator& gen, int iterations, int trials = 5) {
    // Price-change: cancel + resubmit, the expensive path.
    std::vector<double> results;
    for (int t = 0; t < trials; ++t) {
        OrderBook book;
        std::vector<Id> ids;
        for (int i = 0; i < iterations; ++i) {
            Order o{Side::Buy, Type::Limit, static_cast<Price>((i % 90) + 1), 100, gen.nextId++, 0};
            book.rest(o);
            ids.push_back(o.id);
        }
        auto start = std::chrono::steady_clock::now();
        for (Id id : ids) book.modify(id, 5, std::nullopt); // price-change — cancel + resubmit
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back(static_cast<double>(ns) / iterations);
    }
    report("MODIFY — cancel+resubmit (price change)", results);
}

// ---- Run everything ----
void runTrueBenchmark(generator& gen, int iterations = 100000) {
    std::cout << "=== Matching Engine Benchmark Suite (submit/cancel/modify isolated, -O3) ===\n";
    benchmarkSubmitResting(gen, iterations);
    benchmarkSubmitCrossing(gen, iterations);
    benchmarkCancel(gen, iterations);
    benchmarkModifyInPlace(gen, iterations);
    benchmarkModifyCancelResubmit(gen, iterations);
    std::cout << "\nNOTE: single-threaded, fixed-size books, no memory-pressure/long-run effects,\n"
              << "      no book-size sweep. This is an informal W9 baseline, not the full W9 suite.\n";
}
