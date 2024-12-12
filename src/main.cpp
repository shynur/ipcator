#include "ipcator.hpp"
#include "tester.hpp"
#include <iostream>


int main() {
    Tester{};
}


static struct Println_at_StartExit {
    Println_at_StartExit() { std::println(stderr, ""); }
    ~Println_at_StartExit() { std::println(""); }
} _;
