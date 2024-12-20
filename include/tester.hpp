#include "ipcator.hpp"


struct Tester {
    Tester() {
        print_sys_info();
        shared_memory();
        shm_resource();
    }

    /**
     * RAII 绑定共享内存.
     */
    void shared_memory() {
        auto writer_1 = 25_shm;  // 创建指定大小的共享内存, 可读可写.
        writer_1[16] = 0x77;  // 设置第 16 个字节.
        Shared_Memory reader_1 = writer_1.get_name();  // 只读地打开刚才的共享内存.
        std::cout << reader_1.pretty_memory_view() << "\n";  // 查看一下内存布局.
        hr(1);

        auto writer_2 = "/will-be-removed-immediately"_shm[5];  // 创建命名的指定大小的共享内存.
        auto reader_2 = +"/will-be-removed-immediately"_shm;  // 只读地打开它.
        writer_2[2] = 0x42;
        Shared_Memory{std::move(writer_2)};  // 移出去, 然后析构.
        std::cout << reader_2.pretty_memory_view() << "\n";
        // ^^^^^ 虽然 writer 没了, 但它的尸体还在, 除非它的 reader 也走了...
        hr(2);

        // 运行时指定名字和大小, 然后创建:
        Shared_Memory writer_3{
            generate_shm_UUName(),  // 生成全局唯一名字
            5 + (unsigned)std::rand() % 5,  // 要创建的共享内存的大小
        };
        Shared_Memory<false> reader_3a = writer_3;
         // "<false>" ^^^^^ 表示不可写入, 通常不需要显式指出, 就比如上面几个例子.
        auto reader_3b = reader_3a;
        // reader 的任何写入操作都会在编译期被阻拦:
        // reader_3a[3] = 0;  // error!
        // reader_3b[3] = 0;  // error!
        auto writer_4 = "/another-shm"_shm[11];
        std::swap(writer_3, writer_4);  // 字面意思, 做了一次交换.
        writer_4[3] = 3;
        std::cout << (int)reader_3b[3] << "\n";
        hr(3);
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
        std::cout << "页表大小 = " << getpagesize() << "\n\n";
    }
    void hr(int i) {
        std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~ "
                  << i
                  <<" ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
    }
};
