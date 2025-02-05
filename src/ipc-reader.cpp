#include "ipcator.hpp"
ShM_Reader rd;

int main() {
    std::this_thread::sleep_for(0.3s);  // 等 writer 先创建好消息.
    const auto& [name, offset] = rd.template read<
                                     std::pair<std::array<char, 24>, std::size_t>
                                 >("/ipcator.msg_descriptor", 0);
    const auto& mul2_add1 = rd.template read<int(int)>(name.data(), offset);

    std::this_thread::sleep_for(1.3s);  // 这时 writer 进程已经退出了, 但我们仍能读取消息:
    std::cout << "\n[[[ 42 x 2 + 1 = " << mul2_add1(42) << " ]]]\n\n\n";
}
