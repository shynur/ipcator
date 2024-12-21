#include "ipcator.hpp"
#include <chrono>
#ifdef __cpp_lib_print
#include <print>
#endif


using std::swap;

struct Print_Fences {
    const std::string f;
    Print_Fences(const char *f);
    ~Print_Fences();
    void hr() const;
};


struct Tester {
    Tester() {
        std::setbuf(stdout, 0);
        print_sys_info();
        shared_memory();
        // shm_resource();
    }
    void shm_1() {
        Print_Fences pf = __func__;

        // 创建指定大小 (25 bytes) 的共享内存, 可读可写,
        auto writer = 25_shm;
        // 内核需要一个名字指代共享内存, 此处由 ipcator 自行取名, 确保唯一.

        writer[16] = 0x77;  // 设置第 16 个 byte.

        // 只读地打开上面的共享内存:
        Shared_Memory reader = writer.get_name();
        // 查看一下内存布局:
        std::cout << reader.pretty_memory_view();

        pf.hr();
    }
    void shm_2() {
        Print_Fences pf = __func__;

        // 创建命名的指定大小的共享内存:
        auto writer = "/will-be-removed-immediately"_shm[5];
        // 只读地打开它:
        auto reader = +"/will-be-removed-immediately"_shm;

        writer[2] = 0x42;

        // 将 writer 的所有权移交给临时值:
        Shared_Memory{std::move(writer)};
        // 临时值消亡了, 从而触发 GC.

        std::cout << reader.pretty_memory_view();
        // 虽然 writer 没了, 但它的尸体还在,
        // 除非它的 reader 也走了, 否则仍能读取.

        pf.hr();
    }
    void shm_3() {
        Print_Fences pf = __func__;

        // 通过函数指定名字和大小, 然后创建:
        Shared_Memory writer{
            generate_shm_UUName(),  // 生成全局唯一名字
            5 + (unsigned)std::rand() % 5,  // 要创建的共享内存的大小
        };
        Shared_Memory<false> reader_a = writer;
        //            ^^^^^ 表示只读, 通常不需要显式指出.
        // 将同一个共享内存对象映射到不同的地址:
        auto reader_b = reader_a;

        // reader 的任何写入操作都会在*编译期*被阻拦 (无论构造时是否显式指出 read-only):
        // reader_a[3] = 0;  // error!
        // reader_b[3] = 0;  // error!
        writer[3] = 3;

        // 读取前五个 byte:
        for (auto byte : reader_b | std::views::take(5))
            std::cout << (int)byte << ' ';
        pf.hr();
    }
    void shm_4() {
        Print_Fences pf = __func__;

        auto writer_a = "/one-more-shm"_shm[7],
             writer_b = "/yet-another-one"_shm[11];

        Shared_Memory reader = std::string{"/one-more-shm"};

        swap(writer_a, writer_b);  // 交换所有权.
        writer_b[5] = 5;

        std::cout << "读取 writer_b 在原 writer_a 持有的内存上写入的 byte: "
                  << (int)reader[5] << "\n";

        writer_b = std::move(writer_a);
        // writer_b 原本持有的共享内存所有权被 writer_a
        // 移交的所有权挤占掉了, 只能被丢弃并触发 GC.
        writer_b[5] = 1;
        std::cout << "再读刚刚那个 byte, 发现并没有改变: "
                  << (int)reader[5];
        pf.hr();
    }
    void shm_5() {
        Print_Fences pf = __func__;

        auto writer = "/one-shared-memory"_shm[10];
        std::cout << "起始地址: " << writer.data() << ' '
                  << "或 " << (void *)&writer[0] << ", "
                  << "长度为: " << std::size(writer) << '\n';

        auto reader = +"/one-shared-memory"_shm;
        std::cout << (writer == reader ? "writer 和 reader 指向同一个共享内存对象" : "并不") << ".\n";

#ifdef __cpp_lib_print
        std::println("writer 的 JSON 表示: {}", writer);
#else
        std::cout << "writer 的 JSON 表示: " << writer << '\n';
#endif

#if __cplusplus >= 202302L
        for (auto [i, byte] : writer[2, 8] | std::views::enumerate)
            byte = i+1;
        std::cout << reader.pretty_memory_view();
#endif

        const auto& oops_icantwrite = writer;
        // const 时权限和 reader 一样:
        // oops_icantwrite[0] = 0;  // error!
        auto& icanwrite = const_cast<Shared_Memory<true>&>(oops_icantwrite);
        // 只要不改变所有权, 由 ipcator 保证使用 const_cast 是合法有定义的行为.
        icanwrite[0] = 0;  // 成功修改.

        pf.hr();
    }
    void shm_benchmark(const unsigned times) {
        Print_Fences pf = __func__;

        const auto start = std::chrono::high_resolution_clock::now();
        for (auto _ : std::views::iota(0) | std::views::take(times)) {
            auto&& writer = 8848_shm;
            Shared_Memory<false> reader_a = writer;
            // auto reader_b = reader_a;
        }
        const auto end = std::chrono::high_resolution_clock::now();

        std::cout << "平均耗时: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(end - start) / (double)times
                  << '\n';
    }
    /**
     * RAII 绑定共享内存的示例.
     */
    void shared_memory() {
        shm_1();
        shm_2();
        shm_3();
        shm_4();
        shm_5();
        shm_benchmark(1'0000);
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
};


Print_Fences::Print_Fences(const char *f): f{f} {
    std::cout << "\n\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> "
              << f
              << " >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n";
}
Print_Fences::~Print_Fences() {
    std::cout << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< "
              << f
              << " <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n\n";
}
void Print_Fences::hr() const {
    std::cout << "\n~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ "
                 "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n";
}
