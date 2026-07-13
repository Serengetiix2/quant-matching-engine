#pragma once
#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <optional>
#include <any>
#include "orderClass.hpp"

using Price = int64_t;
using Quantity = int64_t;
using Id = int64_t;
using orderIterator = std::list<Order>::iterator;

struct level{
    std::list<Order> orders;
    //orderIterator iterator;
};

class OrderBook{
private:
    std::map<Price, level> asks;
    std::map<Price, level> bids;
    std::unordered_map<Id,orderIterator> cancelIndex;
    int64_t nextSeq = 0;

public:

    std::map<Price, level>& getMap(Side side){
        if(side == Side::Buy){
            return bids;
        }else{
            return asks;
        }
    }

    Side opposite(Side side){
        if(side == Side::Buy){
            return Side::Sell;
        }else{
            return Side::Buy;
        }
    }


    bool validate(const Order& o){
        if(contains(o.id)) return false;
        else if(o.quantity <= 0) return false;
        else if(o.type == Type::Limit){
            if(o.price <= 0) return false;
            else return true;
        }
        else return true;
    }

   Order* best(Side s) {
        if (s == Side::Buy) {
            if (!bids.empty()) return &bids.rbegin()->second.orders.front();
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

    
    bool rest(Order o){
        if(!(validate(o))) return false;
            o.seq = nextSeq++;
        orderIterator it;
        if (o.side == Side::Buy) it = restInto(bids, o);
        else                     it = restInto(asks, o);

        cancelIndex[o.id] = it;
        
        return true;
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
    
 
    std::optional<std::vector<Fill>> submit(Order& incoming){
        if(!validate(incoming)) return std::nullopt;
        std::vector<Fill> fills;
        auto& oppositeSide = getMap(opposite(incoming.side));
            while (incoming.quantity > 0 && !oppositeSide.empty()) {
               auto resting = best(opposite(incoming.side));
               bool crosses;
               if (incoming.type == Type::Market){
                crosses = true;
               } else {
                if(incoming.side == Side::Buy){
                    crosses = incoming.price >= resting->price;
                }else{
                    crosses = resting->price >= incoming.price;
                }
               }
                    if (!crosses) break;
                    int64_t tradeQty = std::min(incoming.quantity, resting->quantity);
                    incoming.quantity -= tradeQty;
                    resting->quantity -= tradeQty;
                    fills.emplace_back(resting->price, tradeQty, incoming.id, resting->id);

                    if (resting->quantity == 0) {
                        Id restingId = resting->id;
                        cancel(restingId);
                        //std::println("Remaining Levels" , oppositeSide.size());
                        //std::println("Next Price Level", oppositeSide.begin()->first);
                    }
                

            
        }

        if (incoming.type == Type::Limit && incoming.quantity > 0) {
            rest(incoming);
        }
        return fills;
    }

    struct ExpectedLevel {
        Side side;
        Price price;
        int64_t quantity;
    };

    int64_t quantityAt(Side side, Price price) const{
        int64_t levelQty = 0;
        if(side == Side::Buy){
            if(bids.find(price) != bids.end()){
             auto const& priceLevel = bids.at(price).orders;
             for(auto i : priceLevel){
                levelQty += i.quantity;
             }
            }
            return levelQty;
        }else{
            if(asks.find(price) != asks.end()){
                auto const& priceLevel = asks.at(price).orders;
                for(auto const& i : priceLevel){
                    levelQty += i.quantity;
                }
            }
            return levelQty;
        }
    }

     bool cancel(Id id){
        auto it = cancelIndex.find(id);
        if(it == cancelIndex.end()) return false;
        orderIterator orderIt = it->second;
        const Price p = orderIt-> price;
        const Order& order = *orderIt;
        
        auto& map = getMap(order.side);

           if(map.find(order.price) != map.end()) {
            map.at(order.price).orders.erase(orderIt);
                if(map.at(p).orders.empty()){
                    map.erase(p);
                }
           }
            
        cancelIndex.erase(it);
        return true;
    }

        
    

   bool  modify(Id id, std::optional<Price> newPrice, std::optional<Quantity> newQuantity){
        auto it = cancelIndex.find(id);
        if (it == cancelIndex.end()) return false;
        if(newQuantity == 0){
            cancel(id);
            return true;
        } 
        orderIterator orderIt = it->second;
        Order& order = *orderIt;
        Price currentPrice = orderIt->price;
        Quantity currentQuantity = orderIt->quantity;
        Side currentSide = orderIt->side;

        if(newPrice.has_value() && *newPrice != order.price){
            cancel(id);
            currentPrice = *newPrice;
            if(newQuantity.has_value()){
            currentQuantity = *newQuantity;
            }
            Order replacement{currentSide, Type::Limit, currentPrice, currentQuantity, id, 0};
            submit(replacement);
            return true;
        
        }else if(newQuantity.has_value()){
                if(*newQuantity >  currentQuantity){
                    cancel(id);
                    currentQuantity = *newQuantity;
                    Order replacement{currentSide, Type::Limit, currentPrice, currentQuantity, id, 0};
                    submit(replacement);
                }else if(order.quantity > *newQuantity){
                    order.quantity = *newQuantity;
                }
                return true;
        }else{
            return false;
        }
           

        
   }

    
};