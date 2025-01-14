#include "ipcator.hpp"

using namespace literals;

int main() {
    ShM_Reader rd;

    std::this_thread::sleep_for(50ms);
    -"/ipcator-writer-done"_shm;
    auto& mul2 = rd.template read<char>(
        std::string_view{
            (const char *)std::data(-"/ipcator-target-name"_shm),
            247,
        }, 0
    );

    std::cout << "42 x 2 = " << ((int(*)(int))(&mul2))(42) << '\n';
}
