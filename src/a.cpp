#include "ipcator.hpp"
#include <iostream>

int main() {
  Shared_Memory<true> a{"sc232", 33};
  Shared_Memory<false> b{"sc232"};
  for (auto i=0u;i!= 30;i++)
  a[i]=rand();
  std::cerr << std::hash<Shared_Memory<true>>{}(a) << '\n'
            << std::hash<Shared_Memory<false>>{}(b) << '\n';
  std::cerr << (b==b);
}

struct Println_at_StartExit {
  Println_at_StartExit() { std::cerr << std::endl; }
  ~Println_at_StartExit() { std::cout << std::endl; }
} _;
