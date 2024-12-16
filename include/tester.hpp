#include "ipcator.hpp"
#include <vector>
#include <iostream>
#include <tr2/type_traits>
#include <print>


struct Tester {
    Tester() {
        print_sys_info(), println("");
        test_shm(),println("");
        test_UUName(),println("");
        test_sync_pool(),println("");
        test_const(),println("");
        test_fmt(),println("");
        std::clog << typeid(
            typename std::tr2::direct_bases<ShM_Pool<true>>::type::first::type
        ).name();
    }
    void test_fmt() {
        Shared_Memory a{"/wq", 2};
        Shared_Memory<false> b{"/wq"}, c{"/wq"};
        std::println("{}", a);
        std::clog << b << ' ' << c << '\n';
    }
    void test_const() {
        Shared_Memory<true> w{"/ipcator123", 256};
        static_assert( std::is_same_v<decltype(w[0]), volatile std::uint8_t&> );
        static_assert( std::is_same_v<decltype(w.begin()), volatile std::uint8_t *> );
        static_assert( std::is_same_v<decltype(w.cbegin()), const volatile std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(w.cbegin()), const std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(w.cbegin()), volatile std::uint8_t *> );

        const auto& cw{w};
        static_assert( std::is_same_v<decltype(cw[0]), const volatile std::uint8_t&> );
        static_assert( std::is_same_v<decltype(cw.begin()), const volatile std::uint8_t *> );
        static_assert( std::is_same_v<decltype(cw.cbegin()), const volatile std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(cw.cbegin()), const std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(cw.cbegin()), volatile std::uint8_t *> );

        Shared_Memory<false> r{"/ipcator123"};
        static_assert( std::is_same_v<decltype(r[0]), const volatile std::uint8_t&> );
        static_assert( std::is_same_v<decltype(r.begin()), const volatile std::uint8_t *> );
        static_assert( std::is_same_v<decltype(r.cbegin()), const volatile std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(r.cbegin()), const std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(r.cbegin()), volatile std::uint8_t *> );

        const auto& cr{r};
        static_assert( std::is_same_v<decltype(cr[0]), const volatile std::uint8_t&> );
        static_assert( std::is_same_v<decltype(cr.begin()), const volatile std::uint8_t *> );
        static_assert( std::is_same_v<decltype(cr.cbegin()), const volatile std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(cr.cbegin()), const std::uint8_t *> );
        static_assert( !std::is_same_v<decltype(cr.cbegin()), volatile std::uint8_t *> );
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
        Shared_Memory<true> w{"/ipcator123", 233};
        Shared_Memory<false> r{"/ipcator123"};

        for (auto i : std::views::iota(0u, std::size(w)))
            w[i] = std::rand();

        auto view = w.pretty_memory_view();
        static_assert( std::is_same_v<decltype(view), std::string> );
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
    void scratch() {
    }
    void list_shm() {
        std::system("ls -al /dev/shm; echo");
    }
};
