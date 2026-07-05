#pragma once
#include <map>
#include <unordered_map>
#include <list>
#include <cstdint>
#include <iostream>
#include "orderClass.hpp"

using Price = int64_t;
using Id = int64_t;
using orderIterator = std::list<Order>::iterator;

struct level{
    std::list<Order> orders;
};

class OrderBook{
private:
    std::map<Price, level> asks;
    std::map<Price, level, std::greater<>> bids;
    std::unordered_map<Id,orderIterator> cancelIndex;
    int64_t nextSeq = 0;

public:
   // Return pointer to best order (nullptr if none). Marked const because it does not modify the book.
   const Order* best(Side s) const {
        if (s == Side::Buy) {
            if (!bids.empty()) return &bids.begin()->second.orders.front();
        } else {
            if (!asks.empty()) return &asks.begin()->second.orders.front();
        }
        return nullptr;
    }
    // Returns true if an order with given id is present in the cancel index
    bool contains(Id id) const {
        return cancelIndex.find(id) != cancelIndex.end();
    }
    template <typename BookSide>
        orderIterator restInto(BookSide& book, Order& o){
            auto& lst = book[o.price].orders;
            return lst.insert(lst.end(), o);
    }

    // Accept by value so callers can pass temporaries.
    void rest(Order o){
        o.seq = nextSeq++;
        orderIterator it;
        if (o.side == Side::Buy) it = restInto(bids, o);
        else                     it = restInto(asks, o);

        cancelIndex[o.id] = it;
    }
};