#include "orderBook.hpp"
#include <variant>
#include <string>
#include <optional>
#include <iostream>


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
    for (const auto& i : vector) {
        os << i << " ";
    }
    return os;
}

struct Test {
    struct Modifications {
        Id id;
        std::optional<Price> newPrice;
        std::optional<Quantity> newQuantity;
    };

    struct Failure {
        std::string failureType;
        std::string actualResult;
        std::string expectedResult;

        // Placed directly inside the struct as a friend function
        friend std::ostream& operator<<(std::ostream& os, const Failure& failure) {
            os << "[" << failure.failureType << " mismatch - Expected: " << failure.expectedResult 
               << ", Got: " << failure.actualResult << "]";
            return os;
        }
    };

    using Result = std::variant<bool, std::vector<Failure>>;

    std::vector<Failure> compareFills(
        const std::vector<OrderBook::Fill>& actualFills, 
        const std::vector<OrderBook::Fill>& expectedFills) {
        
        std::vector<Failure> failures;
        
        if (expectedFills.size() == actualFills.size()) {
            for (size_t i = 0; i < expectedFills.size(); ++i) {
                if (actualFills[i].price != expectedFills[i].price) {
                    failures.push_back({"Price", std::to_string(actualFills[i].price), std::to_string(expectedFills[i].price)});
                }
                if (actualFills[i].quantity != expectedFills[i].quantity) {
                    failures.push_back({"Quantity", std::to_string(actualFills[i].quantity), std::to_string(expectedFills[i].quantity)});
                }
                if (actualFills[i].agressorId != expectedFills[i].agressorId) {
                    failures.push_back({"AggressorId", std::to_string(actualFills[i].agressorId), std::to_string(expectedFills[i].agressorId)});
                }
                if (actualFills[i].restingId != expectedFills[i].restingId) {
                    failures.push_back({"RestingId", std::to_string(actualFills[i].restingId), std::to_string(expectedFills[i].restingId)});
                }
            }
        } else {
            failures.push_back({"Size", std::to_string(actualFills.size()), std::to_string(expectedFills.size())});    
        }
        
        return failures;
    }

    std::vector<OrderBook::Fill> submitAndCollect(OrderBook& book, std::vector<Order>& orders){
        std::vector<OrderBook::Fill> actualFills;
        for (auto& order : orders) {
            auto fills = book.submit(order);
            if(fills.has_value()){
                actualFills.insert(actualFills.end(), fills->begin(), fills->end());
            }
        }
        return actualFills;
    }

    bool runReplayTest(const std::string& name,
                       std::vector<Order>& sequence,
                       const std::vector<OrderBook::Fill>& expectedFills,
                       const std::vector<OrderBook::ExpectedLevel>& expectedState) {
        OrderBook book;
        std::vector<OrderBook::Fill> actualFills = submitAndCollect(book, sequence);

        // Utilize compareFills for the new return format
        auto fillFailures = compareFills(actualFills, expectedFills);
        bool passedFills = fillFailures.empty();

        std::vector<int> actualStates;
        for (const auto& state : expectedState) {
            auto it = book.quantityAt(state.side, state.price);
            actualStates.push_back(it);
        }

        bool passedStates = true;
        if (expectedState.size() == actualStates.size()) {
            int i = 0;
            for (const auto& state : expectedState) {
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
            std::cout << "FAILED Fills for " << name << ": \n  Expected " << expectedFills 
                      << "\n  Got: " << actualFills 
                      << "\n  Failures: " << fillFailures << "\n";
        }
        
        if (passedStates) {
            std::cout << "Passed States for " << name << "!\n";
        } else {
            std::cout << "FAILED States for " << name << ": \n  Expected " << expectedState 
                      << "\n  Got: " << actualStates << "\n";
        }

        if (passedFills && passedStates) {
            std::cout << name << " Passed Tests\n";
            return true;
        }
        
        std::cout << name << " Failed Tests\n";
        return false;
    }

    bool CancelTest(const std::vector<Order>& sequence,
                    const std::vector<Id>& ids,
                    const std::vector<Id>& expectedCancels,
                    const std::vector<OrderBook::ExpectedLevel>& expectedState) {
        OrderBook book;
        for (const auto& o : sequence) {
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
        for (const auto& state : expectedState) {
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
   
    bool modifyTest(
        const std::string& name, 
        std::vector<Order>& setup,
        const std::vector<Modifications>& orderChanges,
        std::vector<Order>& matcherOrders,
        const std::vector<OrderBook::Fill>& expectedFills,
        const std::vector<OrderBook::ExpectedLevel>& expectedState) { // Added expectedState

        OrderBook book;
        for (auto& order : setup) {
            book.rest(order);
        }
        for (auto& change : orderChanges) {
            book.modify(change.id, change.newPrice, change.newQuantity);
        }

        auto actualFills = submitAndCollect(book, matcherOrders);
        auto failures = compareFills(actualFills, expectedFills);
       
        
        for (const auto& state : expectedState) {
            int actualQty = book.quantityAt(state.side, state.price);
            if (actualQty != state.quantity) {
                failures.push_back({
                    "State Qty at Price " + std::to_string(state.price), 
                    std::to_string(actualQty), 
                    std::to_string(state.quantity)
                });
            }
        }

        if (failures.empty()) {
            std::cout << "Passed Modify Test for " << name << "!\n";
            return true;
        } 
        
        std::cout << "FAILED Modify Test for " << name << ":\n"
                  << "  Expected Fills: " << expectedFills << "\n"
                  << "  Got Fills: " << actualFills << "\n"
                  << "  Failures: " << failures << "\n";
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

    std::vector<Order> reduceKeepPosOrders{
        {Side::Sell, Type::Limit, 100, 100, 1, 0},
        {Side::Sell, Type::Limit, 100, 50, 2, 0}
    };
    std::vector<Test::Modifications> reduceKeepPosMods{
        Test::Modifications{1, std::nullopt, 60}
    };
    std::vector<Order> reduceKeepPosMatcher{
        {Side::Buy, Type::Limit, 100, 60, 3, 0}
    };
    std::vector<OrderBook::Fill> reduceKeepPosFills{
        {100, 60, 3, 1}
    };
    std::vector<OrderBook::ExpectedLevel> reduceKeepPosStates{
        {Side::Sell, 100, 50} // Proves A's remaining 40 was cancelled correctly, leaving only B (50)
    };
    t.modifyTest("Reduce Price Keep Position", reduceKeepPosOrders, reduceKeepPosMods, reduceKeepPosMatcher, reduceKeepPosFills, reduceKeepPosStates);


    std::vector<Order> increaseLosePosOrders{
        {Side::Sell, Type::Limit, 100, 60, 1, 0},
        {Side::Sell, Type::Limit, 100, 50, 2, 0}
    };
    std::vector<Test::Modifications> increaseLosePosMods{
        Test::Modifications{1, std::nullopt, 100}
    };
    std::vector<Order> increaseLosePosMatcher{
        {Side::Buy, Type::Limit, 100, 100, 3, 0}
    };
    std::vector<OrderBook::Fill> increaseLosePosFills{
        {100, 50, 3, 2},
        {100, 50, 3, 1}
    };
    std::vector<OrderBook::ExpectedLevel> increaseLosePosStates{
        {Side::Sell, 100, 50} // Proves 50 of A remains
    };
    t.modifyTest("Increase Price Lose Position", increaseLosePosOrders, increaseLosePosMods, increaseLosePosMatcher, increaseLosePosFills, increaseLosePosStates);


    std::vector<Order> priceChangeLosePosOrders{
        {Side::Sell, Type::Limit, 100, 100, 1, 0},
        {Side::Sell, Type::Limit, 101, 100, 2, 0}
    };
    std::vector<Test::Modifications> priceChangeLosePosMods{
        Test::Modifications{1, 101, std::nullopt}
    };
    std::vector<Order> priceChangeLosePosMatcher{
        {Side::Buy, Type::Limit, 101, 150, 3, 0}
    };
    std::vector<OrderBook::Fill> priceChangeLosePosFills{
        {101, 100, 3, 2},
        {101, 50, 3, 1}
    };
    std::vector<OrderBook::ExpectedLevel> priceChangeLosePosStates{
        {Side::Sell, 100, 0}, // Proves no ghost left behind!
        {Side::Sell, 101, 50}
    };
    t.modifyTest("Price Change Loses Position", priceChangeLosePosOrders, priceChangeLosePosMods, priceChangeLosePosMatcher, priceChangeLosePosFills, priceChangeLosePosStates);


    std::vector<Order> modifyToZeroCancelOrders{
        {Side::Sell, Type::Limit, 100, 100, 1, 0}
    };
    std::vector<Test::Modifications> modifyToZeroCancelMods{
        Test::Modifications{1, std::nullopt, 0}
    };
    std::vector<Order> modifyToZeroCancelMatcher{
        {Side::Buy, Type::Limit, 100, 50, 2, 0}
    };
    std::vector<OrderBook::Fill> modifyToZeroCancelFills{};
    std::vector<OrderBook::ExpectedLevel> modifyToZeroCancelStates{
        {Side::Sell, 100, 0} // Verifies book is empty
    };
    t.modifyTest("Modify Qty to Zero Cancels", modifyToZeroCancelOrders, modifyToZeroCancelMods, modifyToZeroCancelMatcher, modifyToZeroCancelFills, modifyToZeroCancelStates);



    std::vector<Order> modifyUnknownIdOrders{
        {Side::Sell, Type::Limit, 100, 100, 1, 0}
    };
    std::vector<Test::Modifications> modifyUnknownIdMods{
        Test::Modifications{999, 105, 50}
    };
    std::vector<Order> modifyUnknownIdMatcher{
        {Side::Buy, Type::Limit, 100, 100, 2, 0}
    };
    std::vector<OrderBook::Fill> modifyUnknownIdFills{
        {100, 100, 2, 1}
    };
    std::vector<OrderBook::ExpectedLevel> modifyUnknownIdStates{
        {Side::Sell, 100, 0}
    };
    t.modifyTest("Modify Unknown ID", modifyUnknownIdOrders, modifyUnknownIdMods, modifyUnknownIdMatcher, modifyUnknownIdFills, modifyUnknownIdStates);



    std::vector<Order> MultiLevelOrders{
        {Side::Sell, Type::Limit, 100, 25, 1, 0},
        {Side::Sell, Type::Limit, 101, 25, 2, 0},
        {Side::Sell, Type::Limit, 102, 50, 3, 0},
        {Side::Buy, Type::Limit, 102, 100, 4, 0}
    };

    std::vector<OrderBook::Fill> MultiLevelFills{
        {100, 25, 4, 1},
        {101, 25, 4, 2},
        {102, 50, 4, 3}
    };

    std::vector<OrderBook::ExpectedLevel> MultiLevelLevels{
        {Side::Sell, 100, 0},
        {Side::Sell, 101, 0},
        {Side::Sell, 102, 0},
        {Side::Buy, 102, 0}

    };
    t.runReplayTest("MultiLevel",MultiLevelOrders, MultiLevelFills, MultiLevelLevels);

    std::vector<Order> ExactMatchOrders{
        {Side::Sell, Type::Limit, 100, 50, 1, 0}, 
        {Side::Buy,  Type::Limit, 100, 50, 2, 0}, 
        {Side::Buy,  Type::Limit, 100, 10, 3, 0}  
    };

    std::vector<OrderBook::Fill> ExactMatchFills{
        {100, 50, 2, 1}
    };

    std::vector<OrderBook::ExpectedLevel> ExactMatchLevels{
        {Side::Sell, 100, 0}, 
        {Side::Buy,  100, 10} 
    };

    t.runReplayTest("Exact Match Boundary", ExactMatchOrders, ExactMatchFills, ExactMatchLevels);

    std::vector<Order> crossCheck{
        {Side::Buy, Type::Limit, 100, 50, 1, 0},
        {Side::Sell, Type::Limit, 95, 20, 2, 0},
    };

    std::vector<OrderBook::Fill> crossCheckFills{
        {100, 20, 2, 1}
    };

    std::vector<OrderBook::ExpectedLevel> crossCheckLevels{
        {Side::Buy, 100, 30},
        {Side::Sell, 95, 0}
    };
    t.runReplayTest("Cross Comparision Check", crossCheck, crossCheckFills, crossCheckLevels);

    /*std::vector<Id> cancelLastAtPriceIds {1};
    std::vector<Id> cancelLastAtPriceExpected {1};
    std::vector<OrderBook::ExpectedLevel> cancelLastAtPriceLevels {};
    t.CancelTest(cancelLastAtPriceSequence, cancelLastAtPriceIds, cancelLastAtPriceExpected, cancelLastAtPriceLevels);*/



    return 0;
}