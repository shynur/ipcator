#include "ipcator.hpp"
#include <cstring>

int main() {
    Monotonic_ShM_Buffer buf;
#if __GNUC__ >= 11  // P1009R2
    new (buf.allocate(100)) char[]{"Hello, IPCator!"};
#else
    std::strcpy((char *)buf.allocate(100), "Hello, IPCator!");
#endif

    auto name_passer = "/ipcator-target-name"_shm[247];
    std::strcpy(
        (char *)std::data(name_passer),
        buf.upstream_resource()->last_inserted->get_name().c_str()
    );
    auto notify_reader = "/ipcator-writer-done"_shm[1];

    std::this_thread::sleep_for(100ms);
}
