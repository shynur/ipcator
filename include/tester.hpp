#include "ipcator.hpp"
#include <vector>
#include <iostream>
#include <print>
#include <thread>


struct Tester {
    Tester() {
        print_sys_info();
        shared_memory();
        shm_resource();
    }

    /* RAII 绑定共享内存 */
    void shared_memory() {
        // 创建共享内存:
        Shared_Memory writer{
            generate_shm_UUName(),  // 生成全局唯一名字
            42,  // 要创建的共享内存的大小
        };
        std::println("创建了 writer: {}\n", writer);

        // 写入内容.
        for (auto i : std::views::iota(0) | std::views::take(std::size(writer)))
            writer[i] = i;
        // 但先不读取.

        Shared_Memory reader{writer.name};
        std::println("创建了 reader: {}\n", reader);
        // reader[0] = 1;  ==>  Error!  不允许赋值.
        // 读取 writer 写入的值:
        std::println("reader 读到了:\n{}\n", reader.pretty_memory_view(8, "  "));

        auto another_reader = reader;
        std::println(
            "另一个 reader: {}\n试一下读取:\n{}\n",
            another_reader, another_reader.pretty_memory_view()
        );
    }

    /* 原始的共享内存分配器, 一次性分配一整块共享内存, 管理多块共享内存.  */
    void shm_resource() {
        /* 内部使用红黑树的资源管理器 */
        {
            ShM_Resource resrc_map;
            auto addr1 = resrc_map.allocate(123);
            auto addr2 = resrc_map.allocate(300);
            // 假设在 addr1[50] 上有一个对象 obj:
            auto obj = addr1 + 50;
            // 查询该对象在所在的共享内存:
            auto shm = resrc_map.find_arena(obj);
            std::println("\n对象 {} 位于 {}\n", (void *)obj, *shm);

            // 打印底层注册表:
            for (auto& [_, shm] : resrc_map.get_resources())
                std::println("resrc_map 中的: {}\n", *shm);

            // resrc_map.get_resources().clear();  ==>  Error!  默认不可变,
            // 除非是 纯右值 或者 将亡值:
            ShM_Resource{}.get_resources().clear();

            // 自动释放存储...
        }

        /* 内部使用哈希表的资源管理器 */
        {
            ShM_Resource<std::unordered_map> resrc_hash;
        }
    }

    void test_pool() {
        for (auto _ : std::views::iota(0, 8))
            std::jthread{
                []{
                    Monotonic_ShM_Buffer p;
                    for (auto _ : std::views::iota(0, 100))
                        std::ignore = p.allocate(std::rand() % 1024);
                }
            };
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
    void print_sys_info() {
        std::println(stderr, "Page Size = {}", getpagesize());
        // TODO: 打印对齐/缓存行信息.
    }
};
