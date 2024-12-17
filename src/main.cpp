#include "tester.hpp"
#include <iostream>


int main() {
    Tester{};
}


static struct Println_onStartExit {
    Println_onStartExit() { std::cerr << std::endl; }
    ~Println_onStartExit() { std::cout << std::endl; }
} _;
