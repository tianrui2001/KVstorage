#pragma once

#include <vector>
#include <iostream>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

// 节点类，每个节点可以理解成一栋楼，有很多层，层数从 0 开始，一直到 level_
// 这样就可以让 key-value 只存储一次，但是有多层指向下一个节点的指针，每层指向当前层的不同下一个指针
template <typename Key, typename Value>
class Node {
public:
    Node() = default;
    Node(const Key& key, const Value& value, int level);
    ~Node();

    Key getKey() const;
    Value getValue() const;
    void setValue(const Value& value);

public:
    Node** forward_;   // 存储值为 key 的节点在不同层级的下一个节点的指针(一个节点相当于一栋楼)
    int level_;

private:
    Key key_;
    Value value_;
};

// 解决了跳表无法持久化的问题。 当关闭程序，内存释放，指针就全部无效了
// 通过 SkipListDump 类来存储跳表中的 key 和 value 的数据， 这样就可以在程序关闭后将数据持久化到磁盘上， 
// 下次启动程序时再从磁盘上读取数据并重新建立节点和指针
template <typename Key, typename Value>
class SkipListDump {
public:
    friend class boost::serialization::access;

    void insert(const Node<Key, Value>& node);
public:
    template <typename Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar& keyDumpVt_;
        ar& valDumpVt_;
    }

    std::vector<Key> keyDumpVt_;
    std::vector<Value> valDumpVt_;
};


template <typename Key, typename Value>
class SkipList {
public:
    SkipList(int maxLevel);
    ~SkipList();
    int getRandomLevel();
    Node<Key, Value>* createNode(const Key& key, const Value& value, int level);
    int insertElement(const Key& key, const Value& val);
    int insertElementNoBlock(const Key& key, const Value& val);
    bool searchElement(const Key& key, Value& val);
    void deleteElement(Key key);
    void insertSetElement(const Key& key, const Value& val);
    
    std::string dumpFile();
    void loadFile(const std::string& dumpStr);
    void clear(Node<Key, Value>* node);
    void displayList();
    int size() const { return elementCount_; }

private:
    int maxLevel_;
    int curLevel_;
    int elementCount_;
    Node<Key, Value>* header_;
    std::shared_mutex mutex_;
};