#pragma once

#include <vector>
#include <iostream>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <string.h>
#include <cmath>
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


// 模板类的模板的实现（即函数体）必须在每一个调用它的 .cpp 文件中都可见
// 所以这些函数的实现必须在 skipList.h 中

template <typename Key, typename Value>
Node<Key, Value>::Node(const Key& key, const Value& value, int level)
    : key_(key), value_(value), level_(level)
{
    forward_ = new Node*[level_ + 1];
    memset(forward_, 0, sizeof(Node<Key, Value> *) * (level_ + 1));
}

template <typename Key, typename Value>
Node<Key, Value>::~Node(){
    delete[] forward_;
}

template <typename Key, typename Value>
Key Node<Key, Value>::getKey() const {
    return key_;
}

template <typename Key, typename Value>
Value Node<Key, Value>::getValue() const {
    return value_;
}

template <typename Key, typename Value>
void Node<Key, Value>::setValue(const Value& value) {
    value_ = value;
}

template <typename Key, typename Value>
void SkipListDump<Key, Value>::insert(const Node<Key, Value>& node) {
    keyDumpVt_.push_back(node.getKey());
    valDumpVt_.push_back(node.getValue());
}

template <typename Key, typename Value>
SkipList<Key, Value>::SkipList(int maxLevel)
    : maxLevel_(maxLevel), curLevel_(0), elementCount_(0)
{
    Key key;
    Value val;
    header_ = new Node<Key, Value>(key, val, maxLevel_);
}

template <typename Key, typename Value>
SkipList<Key, Value>::~SkipList() {
    
    // 递归删除链表
    if(header_->forward_[0]) {
        clear(header_->forward_[0]);    // 最底层的节点是相邻的
    }
    delete header_;
}

template <typename Key, typename Value>
int SkipList<Key, Value>::getRandomLevel() {
    int k = 1;
    while(rand() % 2) {
        k++;
    }

    return k < maxLevel_ ? k : maxLevel_;
}

template <typename Key, typename Value>
Node<Key, Value>* SkipList<Key, Value>::createNode(const Key& key, const Value& value, int level) {
    return new Node<Key, Value>(key, value, level);
}

/*
                           +------------+
                           |  insert 50 |--------
                           +------------+       |
level 4     +-->1+                              |                       100
                 |                             insert
                 |                             +----+
level 3         1+-------->10+                 | 50 |          70       100
                            |                  |    |
                            |                  |    |
level 2         1          10-------->30       | 50 |          70       100
                                       |       |    |
                                       |       |    |
level 1         1    4     10         30       | 50 |          70       100
                                       |       |    |
                                       |       |    |
level 0         1    4   9 10         30-->40  | 50 |  60      70       100
                                               +----+
                                               curNode
*/
// 对外的插入接口，调用insertElementNoBlock来完成插入操作
template <typename Key, typename Value>
int SkipList<Key, Value>::insertElement(const Key& key, const Value& val) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return insertElementNoBlock(key, val);
}


// 向跳表中插入元素，首先从上往下查到每一层 key0 < key 的位置，并将其记录在一个指针数组中
// 待找到了 level0 层的位置之后，往后移动一个指针，就是要插入的位置，然构造一个随机层数的节点，插入到每一层中
template <typename Key, typename Value>
int SkipList<Key, Value>::insertElementNoBlock(const Key& key, const Value& val) {
    Node<Key, Value>* curNode = header_;
    Node<Key, Value>* update[maxLevel_ + 1];    // update[i] 保存了第 i 层中，位于待插入位置左侧最近的那个节点。
    memset(update, 0 , sizeof(Node<Key, Value> *) * (maxLevel_ + 1));

    // 从跳表当前的最高层开始向下遍历
    for(int i = curLevel_; i >= 0; i--) {
        while(curNode->forward_[i] && curNode->forward_[i]->getKey() < key) {
            curNode = curNode->forward_[i];
        }

        update[i] = curNode;    // 记录下每一层中，位于待插入位置左侧最近的那个节点
    }

    curNode = curNode->forward_[0];    // 最底层的节点是相邻的

    if(curNode && curNode->getKey() == key) {   // 插入的key已经存在
        std::cout << "key:" << key << " already exists." << std::endl;
        return 1;
    }

    if(!curNode || curNode->getKey() != key) {
        int radomLevel = getRandomLevel();  // 随机生成一个层数

        if(radomLevel > curLevel_) {
            for(int i = curLevel_ + 1; i <= radomLevel; i++) {
                // 如果新节点的层数大于当前跳表的层数，那么就把 update 数组中，
                // 当前跳表层数以上的元素都指向 header 节点
                update[i] = header_;    
            }

            curLevel_ = radomLevel;    // 更新跳表的层数
        }

        Node<Key, Value>* newNode = createNode(key, val, radomLevel);

        for(int i = 0; i <= radomLevel; i++) {
            newNode->forward_[i] = update[i]->forward_[i];
            update[i]->forward_[i] = newNode;
        }

        std::cout << "Successfully inserted key:" << key << ", value:" << val << std::endl;
        elementCount_++;
    }
    return 0;
}

template <typename Key, typename Value>
bool SkipList<Key, Value>::searchElement(const Key& key, Value& val) {
    std::cout << "search_element-----------------" << std::endl;
    std::shared_lock<std::shared_mutex> lock(mutex_);

    Node<Key, Value>* curNode = header_;

    for(int i = curLevel_; i >= 0; i--) {
        while(curNode->forward_[i] && curNode->forward_[i]->getKey() < key) {
            curNode = curNode->forward_[i];
        }
    }

    curNode = curNode->forward_[0];

    if(curNode && curNode->getKey() == key) {
        std::cout << "Found key: " << key << ", value: " << curNode->getValue() << std::endl;
        val = curNode->getValue();
        return true;
    }

    std::cout << "Not Found Key:" << key << std::endl;
    return false;
}

template <typename Key, typename Value>
void SkipList<Key, Value>::deleteElement(Key key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    Node<Key, Value>* curNode = header_;
    Node<Key, Value>* update[maxLevel_ + 1];
    memset(update, 0, sizeof(Node<Key, Value> *) * (maxLevel_ + 1));

    for(int i = curLevel_; i >= 0; i--) {
        while(curNode->forward_[i] && curNode->forward_[i]->getKey() < key) {
            curNode = curNode->forward_[i];
        }

        update[i] = curNode;
    }

    curNode = curNode->forward_[0];

    if(curNode && curNode->getKey() == key) {
        for(int i = 0; i <= curLevel_; i++) {
            if(update[i]->forward_[i] != curNode) {
                // 如果 update[i] 的 forward_[i] 不等于 curNode，说明在第 i 层中，curNode 已经不在链表中了
                break;  
            }

            update[i]->forward_[i] = curNode->forward_[i];
        }

        // 删除没有节点的层
        while (curLevel_ > 0 && !header_->forward_[curLevel_])
        {
            curLevel_--;
        }   

        std::cout << "Successfully deleted key: " << key << std::endl;
        delete curNode;
        elementCount_--;
    }

    return;
}


// 插入节点。保证每个key只出现一次，如果已经存在先删除原来的节点，再插入新的节点
template <typename Key, typename Value>
void SkipList<Key, Value>::insertSetElement(const Key& key, const Value& val) {
    Value oldVal;
    if(searchElement(key, oldVal)) {
        deleteElement(key);
    }

    insertElement(key, val);
}

// 将跳表中的数据持久化到磁盘上，首先把跳表中的数据存储到 SkipListDump 对象中，
// 然后再把 SkipListDump 对象序列化成字符串，最后返回这个字符串
template <typename Key, typename Value>
std::string SkipList<Key, Value>::dumpFile() {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    Node<Key, Value>* curNode = header_->forward_[0];
    SkipListDump<Key, Value> dumpFile;
    while(curNode) {
        dumpFile.insert(*curNode);
        curNode = curNode->forward_[0];
    }

    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << dumpFile;
    return ss.str();
}

// 从磁盘上读取数据并反序列化成 SkipListDump 对象，
// 然后再从 SkipListDump 对象中取出 key 和 value，重新建立节点和指针
template <typename Key, typename Value>
void SkipList<Key, Value>::loadFile(const std::string& dumpStr) {
    if(dumpStr.empty()) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::stringstream ss(dumpStr);
    boost::archive::text_iarchive ia(ss);
    SkipListDump<Key, Value> dumpFile;
    ia >> dumpFile; // 从磁盘上读取数据并反序列化成 SkipListDump 对象
    for(int i = 0; i < dumpFile.keyDumpVt_.size(); i++) {
        insertElementNoBlock(dumpFile.keyDumpVt_[i], dumpFile.valDumpVt_[i]);
    }
}

// 递归删除节点
template <typename Key, typename Value>
void SkipList<Key, Value>::clear(Node<Key, Value>* node) {
    if(node->forward_[0]) {
        clear(node->forward_[0]);
    }

    delete node;
}

template <typename Key, typename Value>
void SkipList<Key, Value>::displayList() {
    std::cout << "\n***** Skip List *****" << std::endl;
    for(int i = 0; i <= curLevel_; i++) {
        Node<Key, Value>* node = header_->forward_[i];
        std::cout << "Level" << i << ":";
        while(node) {
            std::cout << " (" << node->getKey() << ", " << node->getValue() << ")";
            node = node->forward_[i];
        }
        std::cout << std::endl;
    }
}