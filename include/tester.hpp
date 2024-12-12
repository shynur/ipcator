#include "ipcator.hpp"


struct Tester {
    Tester() {
        // test_shm();
        // test_UUName();
        // ShM_Resource{};
        Monotonic_ShM_Buffer mono{};
        new auto{mono.allocate(24)};
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
