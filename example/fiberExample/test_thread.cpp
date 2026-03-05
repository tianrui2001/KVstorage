#include <vector>
#include <iostream>

#include "monsoon.hpp"

void func() {
    std::cout << "Thread name:" << monsoon::Thread::GetName() << 
            ", id:" << monsoon::Thread::GetThis()->GetTid() << std::endl;
}

int main() {
    std::vector<monsoon::Thread::ThreadPtr> tpool;
    for(int i=0; i<5; i++) {
        monsoon::Thread::ThreadPtr t(new monsoon::Thread(func, "name_" + std::to_string(i)));
        tpool.push_back(t);
    }

    for(auto& t : tpool) {
        t->join();
    }

    std::cout << "-----thread_test end-----" << std::endl;
}