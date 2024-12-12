#include "ipcator.hpp"

int main() {
  std::println("Page Size = {}", getpagesize());
  Shared_Memory<true> w{"/ipcator123", 4096};
  Shared_Memory<false> r{"/ipcator123"};
}

struct Println_at_StartExit {
  Println_at_StartExit() { std::println(stderr, ""); }
  ~Println_at_StartExit() { std::println(""); }
} _;
