#include "ipcator.hpp"
using namespace literals;

int main() {
    std::this_thread::sleep_for(0.3s);  // 等 writer 先创建好消息.
    const auto descriptor = -"/ipcator.msg_descriptor"_shm;
    const auto& [name, offset] = (std::pair<std::array<char, 24>, std::size_t>&)descriptor[0];

    ShM_Reader rd;
    const auto& mul2_add1 = rd.template read<int(int)>(name.data(), offset);

    std::this_thread::sleep_for(1.3s);  // 这时 writer 进程已经退出了, 当我们仍能读取消息:
    std::cout << "\n[[[ 42 x 2 + 1 = " << mul2_add1(42) << " ]]]\n\n\n";
}
