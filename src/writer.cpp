#include "ipcator.hpp"
#include <thread>
#include <cstring>
auto name_passer = "/ipcator-ipc-name"_shm[256];

int main() {
    Monotonic_ShM_Buffer buf;
    new (buf.allocate(100)) char[]{"Hello, IPCator!"};

    std::strcpy(
        new (name_passer.data()) char[256],
        buf.upstream_resource()->last_inserted->get_name().c_str()
    );
    std::this_thread::sleep_for(1000ms);
}
