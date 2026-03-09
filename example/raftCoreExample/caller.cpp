#include <iostream>
#include "clerk.h"
#include "util.hpp"

int main() {
    Clerk clerk;
    clerk.init("test.conf");
    auto start = now();
    int count = 500;
    int tmp = count;
    while(tmp--) {
        std::string key = "x" + std::to_string(tmp);
        clerk.Put(key, std::to_string(tmp));

        std::string getval = clerk.Get(key);
        std::cout << "Get " << key << ": " << getval << std::endl;
    }

    return 0;
}