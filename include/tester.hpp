#include "ipcator.hpp"


struct Tester {
    Tester() {
        print_sys_info();
        println("");

        test_shm();
        println("");

        test_UUName();
        println("");

        test_sync_pool();
    }
    void test_sync_pool() {
        std::pmr::synchronized_pool_resource pool{new ShM_Resource};
        void *p;
        p = pool.allocate(4096);
        pool.deallocate(p, 4096);
        p = pool.allocate(1024);
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
        
        auto view = w.pretty_memory_view();
        // static_assert(std::is_same_v<decltype(view), std::string>);
        println(view);
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
