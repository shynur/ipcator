#include "ipcator.hpp"
using namespace literals;

#include <cstring>

int main() {
    Monotonic_ShM_Buffer buf;  // 共享内存 buffer.
    auto block = buf.allocate(6);  // 向 buffer 申请内存块.
    std::memcpy((char *)block, "Hello!", 6);  // 向内存块写入数据.

    // 这是 block 所在的 POSIX shared memory:
    auto& target_shm = buf.upstream_resource()->find_arena(block);

    // 事先约定的共享内存 vvvvvvvvvvvvvvvv, 用来存放 target_shm 的路径名:
    auto name_passer = "/ipcator.target-name"_shm[248];
    // 将 target_shm 的路径名拷贝到 name_passer 中:
    std::strcpy(std::data(name_passer), target_shm.get_name().c_str());

    auto offset_passer = "/ipcator.msg-offset"_shm[sizeof(std::size_t)];
    (std::size_t&)offset_passer[0] = (char *)block - std::data(target_shm);

    std::this_thread::sleep_for(1s);  // 等待 reader 获取消息.
}
