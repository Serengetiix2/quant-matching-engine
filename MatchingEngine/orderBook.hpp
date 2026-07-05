#pragma once
#include <map>
#include <unordered_map>
#include <list>
#include <cstdint>
#include <iostream>
#include <algorithm>
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
   const Order* best(Side s) const {
        if (s == Side::Buy) {
            if (!bids.empty()) return &bids.begin()->second.orders.front();
        } else {
            if (!asks.empty()) return &asks.begin()->second.orders.front();
        }
        return nullptr;
    }
   
    bool contains(Id id) const {
        return cancelIndex.find(id) != cancelIndex.end();
    }
    template <typename BookSide>
        orderIterator restInto(BookSide& book, Order& o){
            auto& lst = book[o.price].orders;
            return lst.insert(lst.end(), o);
    }

    
    void rest(Order o){
        o.seq = nextSeq++;
        orderIterator it;
        if (o.side == Side::Buy) it = restInto(bids, o);
        else                     it = restInto(asks, o);

        cancelIndex[o.id] = it;
    }

    struct Fill{
        int64_t price; //resting order price
        int64_t quantity;
        int64_t agressorId;
        int64_t restingId;

        Fill(int64_t price, int64_t quantity, int64_t agressorId, int64_t restingId)
        : price(price), 
        quantity(quantity), 
        agressorId(agressorId), 
        restingId(restingId) {}
            
    
    };
    template <typename F>
    auto withOppositeSide(Order& incoming, F&& fn) {
        if (incoming.side == Side::Buy) {
            return fn(asks);
        } else {
            return fn(bids);
        }
    }

    std::vector<Fill> match(Order& incoming){
        std::vector<Fill> fills;
        withOppositeSide(incoming, [&](auto& oppositeSide) {
            while (incoming.quantity > 0 && !oppositeSide.empty()) {
               Order& resting = oppositeSide.begin()->second.orders.front();
                        if (incoming.price >= resting.price) {
                    int64_t tradeQty = std::min(incoming.quantity, resting.quantity);
                    incoming.quantity -= tradeQty;
                    resting.quantity -= tradeQty;
                    fills.emplace_back(resting.price, tradeQty, incoming.id, resting.id);

                    if (resting.quantity == 0) {
                        oppositeSide.begin()->second.orders.pop_front();
                        cancelIndex.erase(resting.id);
                        if (oppositeSide.begin()->second.orders.empty()) {
                            oppositeSide.erase(oppositeSide.begin());
                        }
                    }
                } else if (incoming.type == Type::Limit) {
                    break;
                }
            }
        });

        if (incoming.type == Type::Limit) {
            rest(incoming);
        } else {
            std::cout << " order " << incoming.id << " has been dropped" << "\n";
        }
        return fills;
    }
};