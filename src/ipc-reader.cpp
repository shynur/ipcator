#include "ipcator.hpp"

int main() {
    ShM_Reader rd;

    std::this_thread::sleep_for(50ms);
    -"/ipcator-writer-done"_shm;
    auto& hello = rd.template read<std::array<const char, 100>>(
        std::string_view{
            (const char *)std::data(-"/ipcator-target-name"_shm),
            247,
        }, 0
    );

    for (auto ch : hello | std::views::take_while(std::identity()))
        std::cout << ch;
}
