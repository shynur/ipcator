#include "ipcator.hpp"
using namespace literals;

int shared_fn(int n) { return 2 * n + 1; }  // 要传递的函数.
int main(int, const char *const av[]) {
    auto shm_allocator = Monotonic_ShM_Buffer/* 或 ShM_Pool<false> 或 ShM_Pool<true> */{};
    const auto size_fn = std::stoul([&] { const auto p = popen(("echo print\\(0x`nm -SC "s + av[0] + " | grep ' shared_fn(int)$' - | awk -F' ' '{print $2}'`\\) | python3").c_str(), "r"); char buf[4]; fgets(buf, sizeof buf, p); return std::string{buf}; }());
    const auto block = (char *)shm_allocator.allocate(size_fn);  // 向 buffer 申请内存块.
    for (const auto i : std::views::iota(0u, size_fn))
        block[i] = ((char *)shared_fn)[i];  // 向内存块写入数据.

    // 查找 block 所在的 POSIX shared memory:
    const auto& target_shm = shm_allocator.upstream_resource()->find_arena(block);
    // block 在 POSIX shared memory 中的 偏移量:
    const auto offset = (char *)block - std::data(target_shm);

    // 事先约定的共享内存, 用来存放消息的位置区域和偏移量:
    const auto descriptor = "/ipcator.msg_descriptor"_shm[32];
    (std::pair<std::array<char, 24>, std::size_t>&)descriptor[0] = {
        *(std::array<char, 24> *)target_shm.get_name().c_str(), offset
    };
    std::this_thread::sleep_for(1s);  // 等待 reader 获取消息.
}
