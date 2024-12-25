#include "ipcator.hpp"
#include <cstring>

int main() {
    Monotonic_ShM_Buffer buf;
    new (buf.allocate(100)) char[]{"Hello, IPCator!"};

    auto name_passer = "/ipcator-target-name"_shm[247];
    std::strcpy(
        (char *)std::data(name_passer),
        buf.upstream_resource()->last_inserted->get_name().c_str()
    );
    auto notify_reader = "/ipcator-writer-done"_shm[1];

    +"/ipcator-reader-done"_shm;
}
