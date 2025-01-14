#include "ipcator.hpp"
#include <cstring>

using namespace literals;

int shared_fn(int n) {
    return n * 2;
}

int main() {
    Monotonic_ShM_Buffer buf;
    std::memcpy((char *)buf.allocate(1000), (char *)shared_fn, 0x33);

    auto name_passer = "/ipcator-target-name"_shm[247];
    std::strcpy(
        (char *)std::data(name_passer),
        buf.upstream_resource()->last_inserted->get_name().c_str()
    );
    auto notify_reader = "/ipcator-writer-done"_shm[1];

    std::this_thread::sleep_for(100ms);
}

struct S {
    ~S() {
        std::cerr << "析构了";
    }
} _;