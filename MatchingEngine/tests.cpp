#include "orderBook.hpp"

std::ostream& operator<<(std::ostream& os, const OrderBook::Fill& fill) {
    os << "(" << fill.price << "," << fill.quantity << "," << fill.agressorId << "," << fill.restingId << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const OrderBook::ExpectedLevel& state) {
    os << "(" << static_cast<int>(state.side) << "," << state.price << "," << state.quantity << ")";
    return os;
}

template <typename S>
std::ostream& operator<<(std::ostream& os, const std::vector<S>& vector) {
    for (auto i : vector) {
        os << i << " ";
    }
    return os;
}

struct Test {
    bool runReplayTest(const std::string& name,
                       std::vector<Order> sequence,
                       std::vector<OrderBook::Fill> expectedFills,
                       std::vector<OrderBook::ExpectedLevel> expectedState) {
        OrderBook book;
        std::vector<OrderBook::Fill> actualFills;
        for (auto order : sequence) {
            auto fills = book.submit(order);
            actualFills.insert(actualFills.end(), fills.begin(), fills.end());
        }

        bool passedFills = true;
        if (expectedFills.size() == actualFills.size()) {
            for (size_t i = 0; i < expectedFills.size(); ++i) {
                if ((actualFills[i].price != expectedFills[i].price) ||
                    (actualFills[i].quantity != expectedFills[i].quantity)) {
                    passedFills = false;
                    break;
                }
            }
        } else {
            passedFills = false;
        }

        std::vector<int> actualStates;
        for (auto state : expectedState) {
            auto it = book.quantityAt(state.side, state.price);
            actualStates.push_back(it);
        }

        bool passedStates = true;
        if (expectedState.size() == actualStates.size()) {
            int i = 0;
            for (auto state : expectedState) {
                if (actualStates[i] != expectedState[i].quantity) {
                    passedStates = false;
                    break;
                }
                ++i;
            }
        } else {
            passedStates = false;
        }

        if (passedFills) {
            std::cout << "Passed Fills for " << name << "!\n";
        } else {
            std::cout << "FAILED Fills: Expected " << expectedFills << " Got:" << actualFills << "\n";
        }
        if (passedStates) {
            std::cout << "Passed States for " << name << "!\n";
        } else {
            std::cout << "FAILED States: Expected " << expectedState << " Got: " << actualStates << "\n";
        }

        if (passedFills && passedStates) {
            std::cout << name << " Passed Tests ";
            return true;
        }
        std::cout << name << " Failed Tests ";
        return false;
    }

    bool CancelTest(std::vector<Order> sequence,
                    std::vector<Id> ids,
                    std::vector<Id> expectedCancels,
                    std::vector<OrderBook::ExpectedLevel> expectedState) {
        OrderBook book;
        for (auto o : sequence) {
            book.rest(o);
        }

        std::vector<Id> cancelledIds;
        for (auto id : ids) {
            bool removed = book.cancel(id);
            if (!removed) {
                std::cout << "Cancel failed for: " << id << "\n";
            } else {
                cancelledIds.push_back(id);
            }
        }

        bool passedCancelAmount = (cancelledIds.size() == expectedCancels.size());
        bool passedCancelOrder = true;
        if (passedCancelAmount) {
            for (size_t i = 0; i < cancelledIds.size(); ++i) {
                if (cancelledIds[i] != expectedCancels[i]) {
                    passedCancelOrder = false;
                    break;
                }
            }
        } else {
            passedCancelOrder = false;
        }

        std::vector<int> actualStates;
        for (auto state : expectedState) {
            auto it = book.quantityAt(state.side, state.price);
            actualStates.push_back(it);
        }

        bool passedStates = true;
        if (expectedState.size() == actualStates.size()) {
            for (size_t i = 0; i < expectedState.size(); ++i) {
                if (actualStates[i] != expectedState[i].quantity) {
                    passedStates = false;
                    break;
                }
            }
        } else {
            passedStates = false;
        }

        std::cout << (passedCancelAmount ? "Passed Cancel Amount Test\n" : "Failed Cancel Amount Test\n");
        std::cout << (passedCancelOrder ? "Passed Cancel Order Test\n" : "Failed Cancel Order Test\n");
        if (!passedCancelAmount || !passedCancelOrder) {
            std::cout << "FAIL Expected Cancels: " << expectedCancels << " Got: " << cancelledIds << "\n";
        }
        if (!passedStates) {
            std::cout << "FAILED States: Expected " << expectedState << " Got: " << actualStates << "\n";
        } else {
            std::cout << "Passed, got these states: " << actualStates << "\n";
        }

        if (passedCancelAmount && passedCancelOrder && passedStates) {
            std::cout << "Passed Cancel Test\n";
            return true;
        }
        std::cout << "FAILED Cancel Test\n";
        return false;
    }
};

int main() {
    Test t;

    std::vector<Order> buyAggressorOrders {
        {Side::Sell, Type::Limit, 102, 100, 1, 0},
        {Side::Sell, Type::Limit, 103, 50, 2, 0},
        {Side::Buy, Type::Limit, 103, 90, 3, 0}
    };
    std::vector<OrderBook::Fill> buyAggressorFills {{102, 90, 3, 1}};
    std::vector<OrderBook::ExpectedLevel> buyAggressorLevels {{Side::Sell, 102, 10}};
    t.runReplayTest("buy aggressor", buyAggressorOrders, buyAggressorFills, buyAggressorLevels);

    std::vector<Order> sellAggressorOrders {
        {Side::Buy, Type::Limit, 103, 100, 1, 0},
        {Side::Buy, Type::Limit, 102, 50, 2, 0},
        {Side::Sell, Type::Limit, 103, 90, 3, 0}
    };
    std::vector<OrderBook::Fill> sellAggressorFills {{103, 90, 3, 1}};
    std::vector<OrderBook::ExpectedLevel> sellAggressorLevels {{Side::Buy, 103, 10}};
    t.runReplayTest("sell aggressor", sellAggressorOrders, sellAggressorFills, sellAggressorLevels);

    std::vector<Order> restRemainderOrders {{Side::Sell, Type::Limit, 100, 100, 1, 0},
                                             {Side::Sell, Type::Limit, 105, 50, 2, 0},
                                             {Side::Buy, Type::Limit, 110, 200, 3, 0}};
    std::vector<OrderBook::Fill> restRemainderFills {{100, 100, 3, 1}, {105, 50, 3, 2}};
    std::vector<OrderBook::ExpectedLevel> restRemainderLevels {{Side::Buy, 110, 50}};
    t.runReplayTest("rest remainder", restRemainderOrders, restRemainderFills, restRemainderLevels);

    std::vector<Order> marketOrderOrders {{Side::Sell, Type::Limit, 100, 100, 1, 0},
                                          {Side::Sell, Type::Limit, 105, 50, 2, 0},
                                          {Side::Buy, Type::Market, 0, 120, 3, 0}};
    std::vector<OrderBook::Fill> marketOrderFills {{100, 100, 3, 1}, {105, 20, 3, 2}};
    std::vector<OrderBook::ExpectedLevel> marketOrderLevels {};
    t.runReplayTest("market order", marketOrderOrders, marketOrderFills, marketOrderLevels);

    std::vector<Order> emptyBookOrders {{Side::Buy, Type::Limit, 110, 50, 1, 0}};
    std::vector<OrderBook::Fill> emptyBookFills {};
    std::vector<OrderBook::ExpectedLevel> emptyBookLevels {{Side::Buy, 110, 50}};
    t.runReplayTest("empty book", emptyBookOrders, emptyBookFills, emptyBookLevels);

    std::vector<Order> cancelSingleOrderSequence {
        {Side::Buy, Type::Limit, 100, 50, 1, 0},
        {Side::Buy, Type::Limit, 101, 25, 2, 0}
    };
    std::vector<Id> cancelSingleOrderIds {1};
    std::vector<Id> cancelSingleOrderExpected {1};
    std::vector<OrderBook::ExpectedLevel> cancelSingleOrderLevels {{Side::Buy, 100, 0}, {Side::Buy, 101, 25}};
    t.CancelTest(cancelSingleOrderSequence, cancelSingleOrderIds, cancelSingleOrderExpected, cancelSingleOrderLevels);

    std::vector<Order> cancelTwoAtSamePriceSequence {
        {Side::Sell, Type::Limit, 100, 20, 1, 0},
        {Side::Sell, Type::Limit, 100, 30, 2, 0}
    };
    std::vector<Id> cancelSharedPriceIds {1};
    std::vector<Id> cancelSharedPriceExpected {1};
    std::vector<OrderBook::ExpectedLevel> cancelSharedPriceLevels {{Side::Sell, 100, 30}};
    t.CancelTest(cancelTwoAtSamePriceSequence, cancelSharedPriceIds, cancelSharedPriceExpected, cancelSharedPriceLevels);

    std::vector<Order> cancelUnknownIdSequence {
        {Side::Sell, Type::Limit, 100, 20, 1, 0}
    };
    std::vector<Id> cancelUnknownIdIds {999};
    std::vector<Id> cancelUnknownIdExpected {};
    std::vector<OrderBook::ExpectedLevel> cancelUnknownIdLevels {{Side::Sell, 100, 20}};
    t.CancelTest(cancelUnknownIdSequence, cancelUnknownIdIds, cancelUnknownIdExpected, cancelUnknownIdLevels);

    std::vector<Order> cancelLastAtPriceSequence {
        {Side::Buy, Type::Limit, 100, 50, 1, 0}
    };
    std::vector<Id> cancelLastAtPriceIds {1};
    std::vector<Id> cancelLastAtPriceExpected {1};
    std::vector<OrderBook::ExpectedLevel> cancelLastAtPriceLevels {};
    t.CancelTest(cancelLastAtPriceSequence, cancelLastAtPriceIds, cancelLastAtPriceExpected, cancelLastAtPriceLevels);

    return 0;
}
