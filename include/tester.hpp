#include "ipcator.hpp"
#include <print>


struct Tester {
    Tester() {
        print_sys_info();
        shared_memory();
        shm_resource();
    }

    /* RAII 绑定共享内存 */
    void shared_memory() {
        // 创建共享内存:
        std::cout << 8848_shm << '\n' << "/ervervver"_shm[2344] << '\n' << +"/ervervver"_shm << '\n';
        Shared_Memory writer{
            generate_shm_UUName(),  // 生成全局唯一名字
            42,  // 要创建的共享内存的大小
        };
        std::cout << "创建了 writer: " << writer << '\n';

        // 写入内容.
        for (auto i : std::views::iota(0) | std::views::take(std::size(writer)))
            writer[i] = i;
        // 但先不读取.

        Shared_Memory<false> reader = writer;
        std::cout << "创建了 reader: " << reader << '\n';
        // reader[0] = 1;  ==>  Error!  不允许赋值.
        // 读取 writer 写入的值:
        std::cout << "reader 读到了:\n" << reader.pretty_memory_view(8, "  ") << '\n';

        auto another_reader = reader;
        std::cout << "另一个 reader: " << another_reader
                  << "\n试一下读取:\n" << another_reader.pretty_memory_view()
                  << '\n';

        assert(reader == another_reader);
    }

    /* 原始的共享内存分配器, 一次性分配一整块共享内存, 管理多块共享内存.  */
    void shm_resource() {
        /* 内部使用红黑树的资源管理器 */
        {
            ShM_Resource resrc_set;
            auto addr1 = resrc_set.allocate(123);
            std::ignore = resrc_set.allocate(300);
            // 假设在 addr1[50] 上有一个对象 obj:
            auto obj = (char *)addr1 + 50;
            // 查询该对象在所在的共享内存:
            auto& shm = resrc_set.find_arena(obj);
            std::cout << "\n对象 " << (void *)obj
                      << " 位于 " << shm
                      << '\n';

            // 打印底层注册表:
            for (auto& shm : resrc_set.get_resources())
                std::cout << "resrc_map 中的: " << shm << '\n';

            // resrc_map.get_resources().clear();  ==>  Error!  默认不可变,
            // 除非是 纯右值 或者 将亡值:
            ShM_Resource{}.get_resources().clear();

            // 自动释放存储...
        }

        /* 内部使用哈希表的资源管理器 */
        {
            ShM_Resource<std::unordered_set> resrc_uoset;
            std::ignore = resrc_uoset.allocate(100);
            std::ignore = resrc_uoset.allocate(200);

            // 查看内部信息, 例如最近一次注册的是哪块共享内存:
            std::cout << "`last_inserted` 字段表示上次插入的共享内存:\n"
                      << resrc_uoset << '\n';

            // `resrc_hash` 底层使用哈希表, 所以没有 `find_arena` 方法.
            // resrc_hash.find_arena(nullptr);  ==>  Error!
        }

        ShM_Resource a, b;
        assert(a != b);
        // 如果等于, 则由 a 分配的内存可由 b 释放.
    }

    void mono_buffer() {}
    void sync_pool() {}
    void unsync_pool() {}
    void print_sys_info() {
        std::cout << "Page Size = " << getpagesize() << '\n';
        // TODO: 打印对齐/缓存行信息.
    }
};
