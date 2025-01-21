#include "ipcator.hpp"
using namespace literals;

#include <cstring>

extern "C" int shared_fn(int n) { return n * 2 + 1; }  // 要传递的函数.

int main() {
    Monotonic_ShM_Buffer buf;  // 共享内存 buffer.
    const auto block = buf.allocate(0x50);  // 向 buffer 申请内存块.
    std::memcpy((char *)block, (char *)shared_fn, 0x50);  // 向内存块写入数据.

    // 查找 block 所在的 POSIX shared memory:
    const auto& target_shm = buf.upstream_resource()->find_arena(block);
    // block 在 POSIX shared memory 中的 偏移量:
    const std::size_t offset = (char *)block - std::data(target_shm);

    // 事先约定的共享内存 vvvvvvvvvvvvvvvv, 用来存放消息的位置区域和偏移量:
    const auto descriptor = "/ipcator.msg_descriptor"_shm[32];
    (std::pair<std::array<char, 24>, std::size_t>&)descriptor[0] = {
        *(std::array<char, 24> *)target_shm.get_name().c_str(),
        offset
    };

    std::this_thread::sleep_for(1s);  // 等待 reader 获取消息.
}
