#pragma once

#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <vector>

template <typename KeyType, typename ValType>
class ThreadSafeMap {
private:
    std::unordered_map<KeyType, ValType> map_;
    mutable std::shared_mutex mutex_;

public:
    void insert(const KeyType &key, const ValType &val){
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_[key] = val;
    }

    bool remove(const KeyType &key){
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if(it != map_.end()){
            map_.erase(it);
            return true;
        }
        return false;
    }

    bool get(const KeyType &key, ValType &value) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if(it != map_.end()){
            value = it->second;
            return true;
        }
        return false;
    }
    int getSize(){
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.size();
    }
};

