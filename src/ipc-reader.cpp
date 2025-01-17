#include "ipcator.hpp"
using namespace literals;

int main() {
    std::this_thread::sleep_for(0.3s);  // 等 writer 先创建好消息.

    ShM_Reader rd;
    auto& mul2_add1 = rd.template read<int(int)>(
        std::data(-"/ipcator.target-name"_shm),  // 目标共享内存的路径名.
        (std::size_t&)(-"/ipcator.msg-offset"_shm)[0]  // 消息的偏移量.
    );  // 获取函数.

    std::this_thread::sleep_for(1.3s);  // 这时 writer 进程已经退出了, 当我们仍能读取消息:
    std::cout << "\n[[[ 42 x 2 + 1 = " << mul2_add1(42) << " ]]]\n\n\n";
}
