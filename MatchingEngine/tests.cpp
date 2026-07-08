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
std::ostream& operator<<(std::ostream& os,
                    const std::vector<S>& vector) {
  
    // Printing all the elements using <<
    for (auto i : vector) 
        os << i << " ";
    return os;
}

std::ostream& operator<<(std::ostream& os,
                    const std::vector<OrderBook::ExpectedLevel>& vector) {
  
    // Printing all the elements using <<
    for (auto i : vector) 
        os << i << " ";
    return os;
}



bool runReplayTest(
    const std::string& name, 
    std::vector<Order> sequence, 
    std::vector<OrderBook::Fill> expectedFills, 
    std::vector<OrderBook::ExpectedLevel> expectedState 
){
    OrderBook book;
    std::vector<OrderBook::Fill> actualFills;
    for(auto order : sequence){
        auto fills = book.submit(order);
        actualFills.insert(actualFills.end(), fills.begin(), fills.end());
        
    }
    bool passedFills = true;
    if(expectedFills.size() == actualFills.size()){
    for (size_t i = 0; i < expectedFills.size(); ++i){
            if ((actualFills[i].price != expectedFills[i].price) || (actualFills[i].quantity != expectedFills[i].quantity)){
                passedFills = false;
                break;
            }
    }
}else{
    passedFills = false;
}
    std::vector<int> actualStates;
    for(auto state : expectedState){
        auto it = book.quantityAt(state.side, state.price);
        actualStates.push_back(it);
    }
    
    bool passedStates = true;
    if(expectedState.size() == actualStates.size()){
    int i = 0;
    for(auto state : expectedState){
        if(actualStates[i] != expectedState[i].quantity){
            passedStates = false;
            break;
        }
        ++i;
    }
}else{
    passedStates = false;
}

    if(passedFills) std::cout<< "Passed Fills for " << name << "! " << "\n";
    else{
        std::cout << "FAILED Fills: Expected " << expectedFills << " Got:" << actualFills << "\n";
    } 
    if(passedStates) std::cout<< "Passed States for " << name << "! " << "\n";
    else{
        std::cout << "FAILED States: Expected " << expectedState << " Got: " << actualStates << "\n";
    } 
    if(passedFills && passedStates){
        std::cout<< name << " Passed Tests ";
        return true;
    } 
    else {
        std::cout<< name << " Failed Tests ";
        return false;
    }


}



int main(){
    std::vector<Order> buyAggressorOrders {
        {Side::Sell, Type::Limit, 102, 100, 1, 0},
        {Side::Sell, Type::Limit, 103, 50, 2, 0},
        {Side::Buy, Type::Limit, 103, 90, 3, 0}
    };
    std::vector<OrderBook::Fill> buyAggressorFills {{102, 90, 3, 1}};
    std::vector<OrderBook::ExpectedLevel> buyAggressorLevels {{Side::Sell, 102, 10}};
    runReplayTest("buy aggressor", buyAggressorOrders, buyAggressorFills, buyAggressorLevels);

    std::vector<Order> sellAggressorOrders {
        {Side::Buy, Type::Limit, 103, 100, 1, 0},
        {Side::Buy, Type::Limit, 102, 50, 2, 0},
        {Side::Sell, Type::Limit, 103, 90, 3, 0}
    };
    std::vector<OrderBook::Fill> sellAggressorFills {{103, 90, 3, 1}};
    std::vector<OrderBook::ExpectedLevel> sellAggressorLevels {{Side::Buy, 103, 10}};
    runReplayTest("sell aggressor", sellAggressorOrders, sellAggressorFills, sellAggressorLevels);

    std::vector<Order> restRemainderOrders {{Side::Sell, Type::Limit, 100, 100, 1, 0},
                                             {Side::Sell, Type::Limit, 105, 50, 2, 0},
                                             {Side::Buy, Type::Limit, 110, 200, 3, 0}};
    std::vector<OrderBook::Fill> restRemainderFills {{100, 100, 3, 1},{105, 50, 3, 2}};
    std::vector<OrderBook::ExpectedLevel> restRemainderLevels {{Side::Buy, 110, 50}};
    runReplayTest("rest remainder", restRemainderOrders, restRemainderFills, restRemainderLevels);

    std::vector<Order> marketOrderOrders {{Side::Sell, Type::Limit, 100, 100, 1, 0},
                                          {Side::Sell, Type::Limit, 105, 50, 2, 0},
                                          {Side::Buy, Type::Market, 0, 120, 3, 0}};
    std::vector<OrderBook::Fill> marketOrderFills {{100, 100, 3, 1},{105, 20, 3, 2}};
    std::vector<OrderBook::ExpectedLevel> marketOrderLevels {};
    runReplayTest("market order", marketOrderOrders, marketOrderFills, marketOrderLevels);

    std::vector<Order> emptyBookOrders {{Side::Buy, Type::Limit, 110, 50, 1, 0}};
    std::vector<OrderBook::Fill> emptyBookFills {};
    std::vector<OrderBook::ExpectedLevel> emptyBookLevels {{Side::Buy, 110, 50}};
    runReplayTest("empty book", emptyBookOrders, emptyBookFills, emptyBookLevels);
}