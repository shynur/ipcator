#include "ipcator.hpp"

int main() {
  std::println("Page Size = {}", getpagesize());
}

struct Println_at_StartExit {
  Println_at_StartExit() { std::println(stderr, ""); }
  ~Println_at_StartExit() { std::println(""); }
} _;
