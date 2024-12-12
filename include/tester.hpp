#include "ipcator.hpp"


struct Tester {
    Tester() {
        print_sys_info();
        // test_shm();
        // test_UUName();
        // ShM_Resource{};
        Monotonic_ShM_Buffer mono{0};
        std::ignore = mono.allocate(50, 5000);
    }
    void print_sys_info() {
        std::println(stderr, "Page Size = {}", getpagesize());
        // TODO: 打印对齐/缓存行信息.
    }
    void test_shm() {
        Shared_Memory<true> w{"/ipcator123", 256};
        Shared_Memory<false> r{"/ipcator123"};

        for (auto i : std::views::iota(0u, w.size))
            w[i] = std::rand();
        
        println(w.pretty_memory_view());
        println("");
        println(r.pretty_memory_view());
    }
    void test_UUName() {
        println(generate_shm_UUName());
        println(generate_shm_UUName());
    }
    void println(auto arg) {
        std::println(stderr, "{}", arg);
    }
};
