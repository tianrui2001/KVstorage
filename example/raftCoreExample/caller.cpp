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
        clerk.Put("x", std::to_string(tmp));

        std::string getval = clerk.Get("x");
        std::cout << "Get x: " << getval << std::endl;
    }

    return 0;
}