#include "ipcator.hpp"

int main() {
    ShM_Reader rd;

    +"/ipcator-writer-done"_shm;
    auto& hello = rd.template read<std::array<const char, 100>>(
        std::basic_string_view<unsigned char>{
#if __cplusplus >= 202302L
            +"/ipcator-target-name"_shm
#else
            (+"/ipcator-target-name"_shm).data(), 247
#endif
        }, 0
    );
    //"/ipcator-reader-done"_shm[1];

    for (auto ch : hello | std::views::take_while(std::identity()))
        std::cout << ch;
}
