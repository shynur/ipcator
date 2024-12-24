#include "ipcator.hpp"
#include <thread>

int main() {
    std::this_thread::sleep_for(200ms);

    ShM_Reader rd;
    auto& hello = rd.template read<std::array<const char, 100>>(
        std::basic_string_view<unsigned char>{ +"/ipcator-ipc-name"_shm }, 0
    );
    for (auto ch : hello | std::views::take_while(std::identity()))
        std::cout << ch;
}
