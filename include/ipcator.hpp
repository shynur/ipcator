/* Copyright (C) 2024-2025  谢骐.  All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, version 2.0.  If a copy of the MPL was not distributed with
 * this file, You can obtain one at <http://mozilla.org/MPL/2.0/>.  */

/**
 * @mainpage
 * @brief 用于 基于共享内存 的 IPC 的基础设施.
 * @note 有关 POSIX 共享内存的简单介绍: <br />
 *       共享内存由临时文件映射而来.  若干进程将相同的目标文件映射到 RAM 中就能实现
 *       内存共享.  一个目标文件可以被同一个进程映射多次, 被映射后的起始地址一般不同.
 *       <br />
 * @details 自顶向下, 包含 3 个共享内存分配器 **适配器**: `Monotonic_ShM_Buffer`,
 *          `ShM_Pool<true>`, `ShM_Pool<false>`.  <br />
 *          它们将 2 个 POSIX shared memory 分配器 (即一次性分配若干连续的📄页面而
 *          不对其作任何切分, 这些📄页面由 kernel 分配) —— `ShM_Resource<std::set>`
 *          和 `ShM_Resource<std::unordered_set>` —— 的其中之一作为⬆️游, 实施特定的
 *          分配算法.  <br />
 *          `ShM_Resource` 拥有若干 `Shared_Memory<true>`, `Shared_Memory` 即是对 POSIX
 *          shared memory 的抽象.  <br />
 *          读取器有 `ShM_Reader`.  工具函数/类/概念有 `ceil_to_page_size(std::size_t)`,
 *          `generate_shm_UUName()`, [namespace literals](./namespaceliterals.html),
 *          [concepts](./concepts.html).
 * @note 关于 POSIX shared memory 生命周期的介绍: <br />
 *       我们使用 `Shared_Memory` 实例对 POSIX shared memory 进行引用计数, 这个计数是跨
 *       进程的, 并且和 `Shared_Memory` 的生命周期相关联, 一个实例对应 **1** 个计数.
 *       **仅当** POSIX shared memory 的计数为 0 的时候, 它占用的物理内存才被真正释放.
 *       <br />
 *       `ShM_Resource`, `Monotonic_ShM_Buffer`, 和 `ShM_Pool` 都持有若干个 `Shared_Memory`
 *       实例, 在这些分配器析构之前, 它们分配的共享内存块绝对能被访问; 在它们析构之后, 或
 *       调用了 `release()` 方法 (如有) 之后, 那些共享内存块是否仍驻留在物理内存中 视情况
 *       而定, 原因如上.  <br />
 *       `Shared_Memory` \[*creat*=false\] 是对 POSIX shared memory 的读抽象.  读取消息
 *       通常使用 `ShM_Reader` 实例, 它自动执行目标共享内存的映射以及读取, 并在读取后保留
 *       该映射以备后续再次读取, 也就是说它维护一个 `Shared_Memory` \[*creat*=false\] cache.
 *       因此 `ShM_Reader` 对 POSIX shared memory 的引用计数也有贡献, 且保证单个实例对同
 *       一片 POSIX shared memory 最多增加 **1** 个引用计数.  当 `ShM_Reader` 析构时, 释放
 *       所有资源 (所以也会将缓存过的 POSIX shared memory 的引用计数减一).
 * @warning 要构建 release 版本, 请在文件范围内定义以下宏, 否则性能会非常差:
 *          - `NDEBUG`: 删除诸多非必要的校验措施;
 *          - `IPCATOR_OFAST`: 开启额外优化.  可能会导致观测到 API 的行为发生变化, 但此类
 *            变化通常无关紧要 (例如, 不判断 allocation 的 alignment 参数是否能被满足, 因为
 *            基本不可能不满足).
 * @note 定义 `IPCATOR_LOG` 宏可以打开日志.  调试用.
 * @note 定义 `IPCATOR_NAMESPACE` 宏可以将该文件内的所有 API 放到指定的命名空间.
 */
/* clang-format off */

#pragma once
#include <version>
#include <algorithm>  // ranges::fold_left
# if __has_include(<experimental/algorithm>)
#   include <experimental/algorithm>  // experimental::sample
# endif
#include <atomic>  // atomic_uint, memory_order_relaxed
#include <cassert>
#include <cerrno>  // EPERM, errno
#include <chrono>
#include <climits>  // NAME_MAX
#include <concepts>  // {,unsigned_}integral, convertible_to, copy_constructible, same_as, movable
#include <cstddef>  // size_t
# if __has_include(<format>)
#   include <format>  // format, formatter, format_error, vformat{_to,}, make_format_args
# elif __has_include(<experimental/format>)
#   include <experimental/format>
    namespace std {
        using experimental::format,
              experimental::formatter, experimental::format_error,
              experimental::vformat, experimental::vformat_to,
              experimental::make_format_args;
    }
# elif __has_include("fmt/format.h")
#   include "fmt/format.h"
#   if FMT_VERSION < 10'00'00L
#       error "你的 `libfmt' 版本太低了"
#   else
        namespace std {
            using ::fmt::format,
                  ::fmt::formatter, ::fmt::format_error,
                  ::fmt::vformat, ::fmt::vformat_to,
                  ::fmt::make_format_args;
        }
#   endif
# else
#   error "你需要首先升级编译器和标准库以获得完整的 C++20 支持, 或安装 C++20 <format> 的替代品 <https://github.com/fmtlib/fmt>"
# endif
#include <cstdint>  // uintptr_t
#include <filesystem>  // filesystem::filesystem_error
#include <functional>  // bind{_back,}, bit_or, plus
#include <future>  // async, future_status::ready
#include <iostream>  // clog
#include <iterator>  // size, {,c}{begin,end}, data, empty, back_inserter
#include <memory>  // shared_ptr
#include <memory_resource>  // pmr::{memory_resource,monotonic_buffer_resource,{,un}synchronized_pool_resource,pool_options}
#include <new>  // bad_alloc
#include <ostream>  // ostream
#include <ranges>  // ranges::find_if, views::{chunk,transform,join_with,iota}
#include <set>
# if __has_include(<source_location>)
#   include <source_location>  // source_location::current
# elif __has_include(<experimental/source_location>)
#   include <experimental/source_location>
    namespace std { using typename experimental::source_location; }
# else
#   warning "连 <source_location> 都没有, 虽然我给你打了个补丁, 但还是建议你退群"
    namespace std {
        struct source_location {
            static consteval auto current() noexcept {
                return source_location{};
            }
            constexpr auto function_name() const noexcept {
                return "某函数";
            }
        };
    }
# endif
#include <span>
#include <stdexcept>  // invalid_argument
#include <string>
#include <string_view>
#include <system_error>  // make_error_code, errc::no_such_file_or_directory
#include <thread>  // this_thread::{sleep_for,yield}
#include <tuple>  // ignore
#include <type_traits>  // conditional_t, is_const{_v,}, remove_reference{_t,}, is_same_v, decay_t, disjunction, is_lvalue_reference
#include <unordered_set>
# include <utility>  // as_const, move, swap, unreachable, hash, exchange
# ifndef __cpp_lib_unreachable
    namespace std {
        [[noreturn]] inline void unreachable() {
#  if defined _MSC_VER && !defined __clang__
            __assume(false);
#  else
            __builtin_unreachable();
#  endif
        }
    }
# endif
#include <variant>  // monostate
#include <fcntl.h>  // O_{CREAT,RDWR,RDONLY,EXCL}, open
#include <sys/mman.h>  // m{,un}map, shm_{open,unlink}, PROT_{WRITE,READ,EXEC}, MAP_{SHARED,FAILED,NORESERVE}
#include <sys/stat.h>  // fstat, struct stat, fchmod
#include <unistd.h>  // close, ftruncate, getpagesize


#ifdef __clang__
# pragma clang diagnostic ignored "-Wc++2a-extensions"
# pragma clang diagnostic ignored "-Wc++2b-extensions"
# pragma clang diagnostic ignored "-Wc++2c-extensions"
# pragma clang diagnostic ignored "-Wc++23-attribute-extensions"
# pragma clang diagnostic ignored "-Wc++26-extensions"
# pragma clang diagnostic ignored "-Wunknown-attributes"
#elif defined __GNUG__
# pragma GCC diagnostic ignored "-Wc++23-extensions"
# pragma GCC diagnostic ignored "-Wc++26-extensions"
# if 12 <= __GNUC__
#   pragma GCC diagnostic ignored_attributes "clang::"
# endif
#endif


#ifdef IPCATOR_NAMESPACE
# define IPCATOR_OPEN_NAMESPACE  namespace IPCATOR_NAMESPACE {
# define IPCATOR_CLOSE_NAMESPACE }
#else
# define IPCATOR_OPEN_NAMESPACE
# define IPCATOR_CLOSE_NAMESPACE
#endif
IPCATOR_OPEN_NAMESPACE


using namespace std::literals;
#ifndef __cpp_size_t_suffix
#   ifdef IPCATOR_USED_BY_SEER_RBK
#       pragma clang diagnostic push
#       pragma clang diagnostic ignored "-Wuser-defined-literals"
#   endif
    consteval auto operator "" uz(unsigned long long integer) -> std::size_t {
        return integer;
    }
#   ifdef IPCATOR_USED_BY_SEER_RBK
#       pragma clang diagnostic pop
#   endif
#endif


/* 对 POSIX API 的复刻, 但参数的类型更多样.  */
namespace POSIX {
    // 注意: POSIX API 不使用异常!

    inline auto close(const decltype(::open("", {})) *const fd) noexcept {
#ifdef IPCATOR_LOG
        std::clog << "调用了 `"s +
# if defined __GNUG__ || defined __clang__
                     __PRETTY_FUNCTION__
# else
                     __func__
# endif
                     + "` (手写的 POSIX close 的重载版本).\n";
#endif
        return ::close(*fd);
    }
}


inline namespace utils {
    /**
     * @brief 将数字向上取整, 成为📄页面大小 (通常是 4096) 的整数倍.
     * @details 用该返回值设置 shared memory 的大小, 可以提高内存空间♻️利用率.
     * @note example:
     * ```
     * assert( ceil_to_page_size(0) == 0 );
     * std::cout << ceil_to_page_size(1);
     * ```
     */
    inline auto ceil_to_page_size [[gnu::const]] (
        const std::size_t min_length
    ) noexcept -> std::size_t {
        const auto current_num_pages = min_length / ::getpagesize();
        const bool need_one_more_page = min_length % ::getpagesize();
        return (current_num_pages + need_one_more_page) * ::getpagesize();
    }
}


/**
 * @brief 对由目标文件映射而来的 POSIX shared memory 的抽象.
 * @note 文档约定:
 *       称 `Shared_Memory` **[*creat*=true]**  实例为 creator,
 *          `Shared_Memory` **[*creat*=false]** 实例为 accessor.
 * @tparam creat 是否新建文件以供映射.
 * @tparam writable 是否允许在映射的区域写数据.
 */
template <bool creat, auto writable=creat>
class Shared_Memory: public std::span<
    std::conditional_t<
        writable,
        char, const char
    >
> {
        using span = std::span<
            std::conditional_t<
                writable,
                char, const char
            >
        >;
        std::string name;
    public:
        /**
         * @brief 创建 shared memory 并映射, 可供其它进程打开以读写.
         * @param name 这是目标文件名.  POSIX 要求的格式是 `/path-to-shm`.
         *             建议使用 `generate_shm_UUName()` 自动生成该名字.
         * @param size 目标文件的大小, 亦即 shared memory 的长度.  建议使用
         *             `ceil_to_page_size(std::size_t)` 自动生成.
         * @note Shared memory 的长度是固定的, 一旦创建, 无法再改变.
         * @warning POSIX 规定 `size` 不可为 0.
         * @details 根据 `name` 创建一个临时文件, 并将其映射到进程自身的
         *          RAM 中.  临时文件的文件描述符在构造函数返回前就会被删除.
         * @warning `name` 不能和已有 POSIX shared memory 重复, 否则会崩溃.
         * @note example (该 constructor 会推导类的模板实参):
         * ```
         * Shared_Memory shm{"/ipcator.Shared_Memory-creator", 1234};
         * static_assert( std::is_same_v<decltype(shm), Shared_Memory<true, true>> );
         * ```
         */
        Shared_Memory(
            const std::string
#ifdef IPCATOR_OFAST
                             &
#endif
                               name, const std::size_t size
        ) requires(creat): span{
            Shared_Memory::map_shm(name, size),
            size,
        }, name{name} {
#ifdef IPCATOR_LOG
                std::clog << std::format("创建了 Shared_Memory: \033[32m{}\033[0m", *this) + '\n';
#endif
        }
        /**
         * @brief 打开📂目标文件, 将其映射到 RAM 中.
         * @param name 目标文件的路径名.  这个路径通常是事先约定的, 或者
         *             从其它实例的 `Shared_Memory::get_name()` 方法获取.
         * @details 目标文件的描述符在构造函数返回前就会被删除.
         * @note 若目标文件不存在, 则每隔 20ms 查询一次, 持续至多 1s.
         * @exception 若 1s 后目标文件仍未创建, 抛出
         *            `std::filesystem::filesystem_error` (no such file
         *            or directory).
         * @note example (该 constructor 会推导类的模板实参):
         * ```
         * Shared_Memory creator{"/ipcator.1", 1};
         * Shared_Memory accessor{"/ipcator.1"};
         * static_assert( std::is_same_v<decltype(accessor), Shared_Memory<false, false>> );
         * assert( std::size(accessor) == 1 );
         * ```
         */
        Shared_Memory(
            const std::string
#ifdef IPCATOR_OFAST
                             &
#endif
                               name
        ) noexcept(noexcept(Shared_Memory::map_shm(""s))) requires(!creat)
        : span{
            [&]() -> span {
                const auto [addr, length] = Shared_Memory::map_shm(name);
                return {addr, length};
            }()
        }, name{name} {
#ifdef IPCATOR_LOG
                std::clog << std::format("创建了 Shared_Memory: \033[32m{}\033[0m\n", *this) + '\n';
#endif
        }
        /**
         * @brief 实现移动语义.
         * @note example:
         * ```
         * Shared_Memory a{"/ipcator.move", 1};
         * auto b{std::move(a)};
         * assert( std::data(a) == nullptr );
         * assert( std::data(b) && std::size(b) == 1 );
         * ```
         */
        Shared_Memory(Shared_Memory&& other) noexcept
        : span{
            // Self 的 destructor 靠 `span` 是否为空来
            // 判断是否持有所有权, 所以此处需要强制置空.
            std::exchange<span>(other, {})
        }, name{std::move(other.name)} {}
        /**
         * @brief 实现交换语义.
         */
        friend void swap(Shared_Memory& a, decltype(a) b) noexcept {
            std::swap<span>(a, b);
            std::swap(a.name, b.name);
        }
        /**
         * @brief 实现赋值语义.
         * @note example:
         * ```
         * auto a = Shared_Memory{"/ipcator.assign-1", 3};
         * a = {"/ipcator.assign-2", 5};
         * assert(
         *     a.get_name() == "/ipcator.assign-2" && std::size(a) == 5
         * );
         * ```
         */
        auto& operator=(Shared_Memory other) {
            swap(*this, other);
            return *this;
        }
        /**
         * @brief 将 shared memory **unmap**.  对于 creator, 还会阻止对关联的目标
         *        文件的新的映射.
         * @details 如果是 creator 析构了, 其它 accessors 仍可访问对应的 POSIX
         *          shared memory, 但新的 accessor 的构造将导致进程 crash.  当余下
         *          的 accessors 都析构掉之后, 目标文件的引用计数归零, 将被释放.
         * @note example (creator 析构后仍然可读写):
         * ```
         * auto creator = new Shared_Memory{"/ipcator.1", 1};
         * auto accessor = Shared_Memory<false, true>{creator->get_name()};
         * auto reader = Shared_Memory{creator->get_name()};
         * (*creator)[0] = 42;
         * assert( accessor[0] == 42 && reader[0] == 42 );
         * delete creator;
         * accessor[0] = 77;
         * assert( reader[0] == 77 );
         * ```
         */
        ~Shared_Memory() noexcept {
            if (std::data(*this) == nullptr)
                return;

            // 🚫 Writer 将要拒绝任何新的连接请求:
            if constexpr (creat)
                ::shm_unlink(this->name.c_str());
                // 此后的 ‘shm_open’ 调用都将失败.
                // 当所有 shm 都被 ‘munmap’ed 后, 共享内存将被 deallocate.

            ::munmap(
                const_cast<char *>(std::data(*this)),
                std::size(*this)
            );

#ifdef IPCATOR_LOG
                std::clog << std::format("析构了 Shared_Memory: \033[31m{}\033[0m", *this) + '\n';
#endif
        }

        /**
         * @brief 所关联的目标文件的路径名.
         * @note example:
         * ```
         * auto a = Shared_Memory{"/ipcator.name", 1};
         * assert( a.get_name() == "/ipcator.name" );
         * ```
         */
        auto& get_name() const { return this->name; }

#if __has_cpp_attribute(nodiscard)
        [[nodiscard]]
#endif
        static auto map_shm(const std::string& name, const std::unsigned_integral auto... size)
            noexcept(false)  // 创建时可能文件已存在; 打开时可能报 “no such file” 错误.
            requires(sizeof...(size) == creat)
        {
            assert(
                name.length() <= NAME_MAX
                // 实际上 POSIX 没有规定 name 的长度上限, 但我认为需要一个保守值.
            );

            using POSIX::close;
            const
#if 16 <= __clang_major__ && __clang_major__ <= 21  // <https://github.com/llvm/llvm-project/issues/129631>
                  decltype(::open("", {}))
#else
                  auto
#endif
                       fd [[gnu::cleanup(close)]] = [&](const auto do_open) {
                std::future opening = std::async([&] {
                    while (true)
                        if (const auto fd = do_open(); fd != -1)
                            return fd;
                        else
                            std::this_thread::sleep_for(20ms);
                });
                if (opening.wait_for(1s) == std::future_status::ready)
                    [[likely]] return opening.get();
                else
                    throw std::filesystem::filesystem_error{
                        // 不要加句号:
                        creat ? "重名的 共享内存对象 已存在, 等待它被删除... creator 等待超时"
                              : "共享内存对象 仍未被创建, 导致 accessor 等待超时",
                        std::format(
#ifdef __linux__
                            "/dev/shm/"
#elif defined __FreeBSD__ || defined __APPLE__
                            "/var/run/shm/"
#elif defined __NetBSD__
                            "/var/shm/"
#endif
                            "{}", name
                        ),
                        std::make_error_code(
                            creat ? std::errc::file_exists
                                  : std::errc::no_such_file_or_directory
                        )
                    };
            }(std::bind(
                ::shm_open,
                name.c_str(),
                (creat ? O_CREAT|O_EXCL : 0) | (writable ? O_RDWR : O_RDONLY),
                0777
            ));
#if __has_cpp_attribute(assume)
            [[assume(fd != -1)]];
#endif
#ifdef IPCATOR_USED_BY_SEER_RBK
            ::fchmod(fd, 0777);
#endif

            if constexpr (creat) {
                // 设置 shm obj 的大小:
                const auto result_resize [[maybe_unused]] = ::ftruncate(
                    fd,
                    size...
#ifdef __cpp_pack_indexing
                           [0]
#endif
                );
                assert(result_resize != -1);
            }

            return [
                fd, size=[&] {
                    if constexpr (creat)
                        return
#ifdef __cpp_pack_indexing
                            size...[0]
#else
                            [](auto size, ...) { return size; }(size...)
#endif
                        ;
                    else
                        // 等到 creator resize 完 shm obj:
                        for (struct ::stat shm; true; std::this_thread::yield())
                            if (::fstat(fd, &shm); shm.st_size)
                                [[likely]] return shm.st_size + 0uz;
                }()
            ] {
                assert(size);
#if __has_cpp_attribute(assume)
                [[assume(size)]];  // POSIX mmap 要求.
#endif
                const auto area_addr = [&] {
#ifdef IPCATOR_OFAST
                    static constinit auto failed_because_of_exec = false;
#endif
                    const auto mmap_executable = [&](bool use_prot_exec) {
                        return ::mmap(
                            nullptr, size,
                            PROT_READ | (writable ? PROT_WRITE : 0) | (use_prot_exec ? PROT_EXEC : 0),
                            MAP_SHARED | (!writable ? MAP_NORESERVE : 0),
                            fd, 0
                        );
                    };

                    auto addr = mmap_executable(
#ifndef IPCATOR_OFAST
                        true
#else
                        failed_because_of_exec ? false : true
#endif
                    );
                    if (addr == MAP_FAILED && errno == EPERM)
#ifdef IPCATOR_OFAST
                        [[unlikely]]  // 因为只会设置这么一次:
                        failed_because_of_exec = true,
#endif
#ifdef IPCATOR_LOG
                        std::clog << "Failed to map shm as PROT_EXEC.\n",
#endif
                        addr = mmap_executable(false);

                    assert(addr != MAP_FAILED);
                    return (char *)addr;
                }();
#if !__has_cpp_attribute(gnu::cleanup)
# ifdef IPCATOR_LOG
                std::clog << "调用了 POSIX close.\n";
# endif
                ::close(fd);  // 映射完立即关闭, 对后续操作🈚影响.
#endif

                if constexpr (creat)
                    return area_addr;
                else {
                    const struct {
                        std::conditional_t<
                            writable,
                            char, const char
                        > *const addr;
                        const std::size_t length;
                    } area{area_addr, size};
                    return area;
                }
            }();
        }

        /**
         * @brief 🖨️打印内存布局到一个字符串.  调试用.
         * @details 一个造型是多行多列的矩阵, 每个元素
         *          用 16 进制表示对应的 byte.
         * @param num_col 列数
         * @param space 每个 byte 之间的填充字符串
         */
        auto pretty_memory_view [[gnu::cold]] (
            const std::size_t num_col = 16, const std::string_view space = " "
        ) const {
#if defined __cpp_lib_ranges_fold  \
    && defined __cpp_lib_ranges_chunk  \
    && defined __cpp_lib_ranges_join_with  \
    && defined __cpp_lib_bind_back
            return std::ranges::fold_left(
                this->area
                | std::views::chunk(num_col)
                | std::views::transform(
                    std::bind_back(
                        std::bit_or<>{},
                        std::views::transform([](auto& B) static {
                            return std::format("{:02X}", B);
                        })
                        | std::views::join_with(space)
                    )
                )
                | std::views::join_with('\n'),
                ""s, std::plus<>{}
            );
#else
            std::vector<std::vector<std::string>> lines;
            std::vector<std::string> line;
            for (const auto& B : this->area) {
                line.push_back(std::format("{:02X}", B));
                line.push_back(std::string{space});
                if (line.size() / 2 == num_col) {
                    line.back() = '\n';
                    lines.push_back(std::exchange(line, {}));
                }
            }
            lines.push_back(line);
            std::string view;
            for (const auto& line : lines) {
                for (const auto& e : line)
                    view += e;
            }
            view.pop_back();
            return view;
#endif
        }

        /**
         * @brief 将 self 以类似 JSON 的格式输出.
         * @note 也可用 `std::println("{}", self)` 打印 (since C++23).
         * @note example:
         * ```
         * std::cout << Shared_Memory{"/ipcator.print", 10} << '\n';
         * ```
         */
        friend auto operator<<[[gnu::cold]](std::ostream& out, const Shared_Memory& shm)
        -> decltype(auto) {
            return out << std::format("{}", shm);
        }
};
Shared_Memory(
    std::convertible_to<std::string> auto, std::integral auto
) -> Shared_Memory<true>;
Shared_Memory(
    std::convertible_to<std::string> auto
) -> Shared_Memory<false>;

static_assert(
    !std::copy_constructible<Shared_Memory<true>>
    && !std::copy_constructible<Shared_Memory<true, false>>
    && !std::copy_constructible<Shared_Memory<false>>
    && !std::copy_constructible<Shared_Memory<false, true>>
);


IPCATOR_CLOSE_NAMESPACE
template <auto creat, auto writable>
struct std::formatter<
#ifdef IPCATOR_NAMESPACE
        IPCATOR_NAMESPACE::
#endif
        Shared_Memory<creat, writable>
    > {
    constexpr auto parse(const auto& parser) {
        if (const auto p = parser.begin(); p != parser.end() && *p != '}')
            throw std::format_error("不支持任何格式化动词.");
        else
            return p;
    }
    auto format(const auto& shm, auto& context) const {
        constexpr auto obj_constructor = []() consteval {
            if (creat)
                if (writable)
                    return "Shared_Memory<creat=true,writable=true>";
                else
                    return "Shared_Memory<creat=true,writable=false>";
            else
                if (writable)
                    return "Shared_Memory<creat=false,writable=true>";
                else
                    return "Shared_Memory<creat=false,writable=false>";
        }();
        const auto addr = (const void *)std::data(shm);
        const auto length = std::size(shm);
        const auto name = shm.get_name();
        return std::vformat_to(
            context.out(),
            R":({{
    "area": {{ "&addr": {}, "|length|": {} }},
    "name": "{}",
    "constructor()": "{}"
}}):",
            std::make_format_args(
                addr, length,
                name,
                obj_constructor
            )
        );
    }
};
IPCATOR_OPEN_NAMESPACE


namespace literals {
    /**
     * @brief 创建 `Shared_Memory` 实例的快捷方式.
     * @details
     * - 创建指定大小的命名的共享内存, 以读写模式映射: `"/filename"_shm[size]`;
     * - 不创建, 只将目标文件以读写模式映射至本地: `+"/filename"_shm`;
     * - 不创建, 只将目标文件以只读模式映射至本地: `-"/filename"_shm`.
     * @note example:
     * ```
     * using namespace literals;
     * auto creator = "/ipcator.1"_shm[123];
     * creator[5] = 5;
     * auto accessor = +"/ipcator.1"_shm;
     * assert( accessor[5] == 5 );
     * auto reader = -"/ipcator.1"_shm;
     * accessor[9] = 9;
     * assert( reader[9] == 9 );
     * ```
     */
    inline auto operator""_shm[[gnu::cold]](const char *const name, [[maybe_unused]] std::size_t) {
        struct ShM_Constructor_Proxy {
            const char *const name;
            auto operator[](const std::size_t size) const {
                return Shared_Memory{name, size};
            }
            auto operator+() const {
                return Shared_Memory<false, true>{name};
            }
            auto operator-() const {
                return Shared_Memory{name};
            }
        };
        return ShM_Constructor_Proxy{name};
    }
}


inline namespace utils {
    /**
     * @brief 创建一个 **全局唯一** 的 POSIX shared memory
     *        名字, 不知道该给共享内存起什么名字时就用它.
     * @see Shared_Memory::Shared_Memory(std::string, std::size_t)
     * @note 格式为 `/固定前缀-进程专属的标识符-原子自增的计数字段`.
     * @details 返回的名字的长度为 (31-8=23).  你可以将它转换成包含
     *          NULL 字符的 c_str, 此时占用 24 bytes.  在传递消息时
     *          需要告知接收方该消息所在的 POSIX shared memory 的
     *          名字和消息在该 shared memory 中的偏移量, 偏移量通常
     *          是 `std::size_t` 类型, 因此加起来刚好 32 bytes.
     * @note example:
     * ```
     * auto name = generate_shm_UUName();
     * assert( name.length() + 1 == 24 );  // 计算时包括 NULL 字符.
     * assert( name.front() == '/' );
     * std::cout << name << '\n';
     * ```
     */
    inline auto generate_shm_UUName() noexcept {
        constexpr auto len_name = 31uz - sizeof(std::size_t);

        constexpr auto prefix = "ipcator";

        // 在 shm obj 的名字中包含一个顺序递增的计数字段:
        constinit static std::atomic_uint cnt;
        const auto suffix = std::format(
            "{:06}",
            1 + cnt.fetch_add(1, std::memory_order_relaxed)
        );

        constexpr auto available_chars = "0123456789"
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                         "abcdefghijklmnopqrstuvwxyz"sv;
        // 由于 (取名 + 构造 shm) 不是原子的, 可能在构造 shm obj 时
        // 和已有的 shm 的名字重合, 或者同时多个进程同时创建了同名 shm.
        // 所以生成的名字必须足够长 (取决于 `infix`), 📉降低碰撞率.
        static const auto infix = [&] {
            std::string infix;
            std::experimental::sample(
                std::cbegin(available_chars), std::cend(available_chars),
                std::back_inserter(infix),
                len_name - ("/"s + prefix + '.' + "" + '.' + suffix).length()
            );
            return infix;
        }();
        assert(infix.length() >= 7);

        auto&& full_name = "/"s + prefix + '.' + infix + '.' + suffix;
        assert(full_name.length() == len_name);
        return full_name;
    }
}


#ifndef IPCATOR_LOG
# define IPCATOR_LOG_ALLO_OR_DEALLOC(color)  (void())
#else
# define IPCATOR_LOG_ALLO_OR_DEALLOC(color)  (  \
    std::clog <<  \
        std::source_location::current().function_name() + "\n"s  \
        + std::vformat(  \
            (color == "green"sv ? "\033[32m" : "\033[31m")  \
            + "\tsize={}, &area={}, alignment={}\033[0m"s,  \
            std::make_format_args(size, (const void *const&)area, alignment)  \
        ) + '\n'  \
)
#endif


/**
 * @brief Allocator: 给⬇️游分配 POSIX shared memory.
 *       本质上是一系列 `Shared_Memory<true>` 的集合.
 * @note 该类的实例持有 `Shared_Memory<true>` 的所有权.
 * @tparam set_t 存储 `Shared_Memory<true>` 的集合类型.
 *         不同的 set_t 决定了
 *         - allocation 的速度.  因为需要向 set_t 实例
 *           中插入 `Shared_Memory<true>`.
 *         - 当给出任意指针时, 找出它指向的对象位于哪片
 *           POSIX shared memory 区间时的速度.
 */
template <template <typename... T> class set_t = std::set>
class ShM_Resource: public std::pmr::memory_resource {
    public:
        /**
         * @cond
         * 请 Doxygen 忽略该变量, 因为它总是显示初始值 (而我不想这样).
         */
        static constexpr bool using_ordered_set = []() consteval {
            if constexpr (requires {
                requires std::same_as<set_t<int>, std::set<int>>;
            })
                return true;
            else if constexpr (std::is_same_v<set_t<int>, std::unordered_set<int>>)
                return false;
            else {
#if !__GNUG__ || __GNUC__ >= 13  // P2593R1
                static_assert(false, "只接受 ‘std::{,unordered_}set’ 作为注册表格式.");
#else
                std::unreachable();
#endif
            }
        }();  /// @endcond
    private:
        struct ShM_As_Addr {
            using is_transparent = int;

            static auto get_addr(const auto& shm_or_ptr)
            -> const void * {
                if constexpr (std::is_same_v<
                    std::decay_t<decltype(shm_or_ptr)>,
                    const void *
                >)
                    return shm_or_ptr;
                else
                    return std::data(shm_or_ptr);
            }

            /* As A Comparator */
#ifdef __cpp_static_call_operator
            static
#endif
            bool operator()[[gnu::cold]](const auto& a, const auto& b)
#ifndef __cpp_static_call_operator
            const
#endif
            noexcept {
                const auto pa = get_addr(a), pb = get_addr(b);

                if constexpr (using_ordered_set)
                    return pa < pb;
                else
                    return pa == pb;
            }
            /* As A Hasher */
#ifdef __cpp_static_call_operator
            static
#endif
            auto operator()(const auto& shm)
#ifndef __cpp_static_call_operator
            const
#endif
            noexcept -> std::size_t {
                const auto addr = get_addr(shm);
                return std::hash<std::decay_t<decltype(addr)>>{}(addr);
            }
        };
        std::conditional_t<
            using_ordered_set,
            set_t<Shared_Memory<true>, ShM_As_Addr>,
            set_t<Shared_Memory<true>, ShM_As_Addr, ShM_As_Addr>
        > resources;
    protected:
#ifdef IPCATOR_IS_BEING_DOXYGENING  // stupid doxygen
        /**
         * @brief 分配 POSIX shared memory.
         * @param alignment 对齐要求.
         * @details 新建 `Shared_Memory<true>`, 不作任何切分,
         *          因此这是粒度最粗的分配器.
         * @return `Shared_Memory<true>` 的 `std::data` 值.
         * @note example:
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto _ = allocator.allocate(12); _ = allocator.allocate(34, 8);
         *      allocator = ShM_Resource<std::unordered_set>{};
         *      _ = allocator.allocate(56), _ = allocator.allocate(78, 16);
         * ```
         */
        void *allocate(
            std::size_t size, std::size_t alignment = alignof(std::max_align_t)
        );
        /**
         * @brief 析构对应的 `Shared_Memory<true>`.
         * @param area 与 deallocation 对应的那次 allocation 的返回值.
         * @param size 若定义了 `NDEBUG` 宏, 传入任意值即可; 否则, 表示
         *             POSIX shared memory 的大小, 必须与请求分配 (`allocate`)
         *             时的大小 (`size`) 一致.
         * @note example:
         * ```
         * auto allocator_1 = ShM_Resource<std::set>{};
         * allocator_1.deallocate(
         *     allocator_1.allocate(111), 111
         * );
         * auto allocator_2 = ShM_Resource<std::unordered_set>{};
         * allocator_2.deallocate(
         *     allocator_2.allocate(222), 222
         * );
         * ```
         */
        void deallocate(void *area, std::size_t size);
#endif
#ifdef IPCATOR_OFAST
        [[gnu::assume_aligned(4096)]]
#endif
        void *do_allocate [[using gnu: returns_nonnull, alloc_size(2)]] (
            const std::size_t size, const std::size_t alignment
        ) noexcept
#ifndef IPCATOR_OFAST
                  (false)
#endif
        [[clang::lifetimebound]] override {
            if (alignment > ::getpagesize() + 0u) [[unlikely]] {
                struct TooLargeAlignment: std::bad_alloc {
                    const std::string message;
                    TooLargeAlignment(const std::size_t demanded_alignment)
                    : message{
                        std::format(
                            "请求分配的字节数组要求按 {} 对齐, 超出了页表大小 (即 {}).",
                            demanded_alignment,
                            ::getpagesize()
                        )
                    } {}
                    const char *what() const noexcept override {
                        return this->message.c_str();
                    }
                };
#ifndef IPCATOR_OFAST
                throw TooLargeAlignment{alignment};
#endif
            }

            const auto [inserted, ok] = this->resources.emplace(
                generate_shm_UUName(),
                size
            );
            assert(ok);
#if __has_cpp_attribute(assume)
            [[assume(ok)]];
#endif
            if constexpr (!using_ordered_set)
                this->last_inserted = std::to_address(
#if _GLIBCXX_RELEASE == 10  // GCC 的 bug, 见 ipcator#2.
                    &*
#endif
                    inserted
                );

            const auto area = std::data(*inserted);
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }
        [[gnu::nonnull(2)]]  // 不用 `nonnull_if_nonzero` 是因为 size 不可能为 0.
        void do_deallocate(
            void *const area [[clang::noescape]],
            const std::size_t size [[maybe_unused]],
            const std::size_t alignment [[maybe_unused]]
        )
#ifdef IPCATOR_OFAST
          noexcept
#endif
          override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");

            // 标准要求 allocation 与 deallocation 的 ‘alignment’ 要匹配, 否则是 undefined
            // behavior.  我们没有记录 allocation 的 ‘alignment’ 值是多少, 但肯定不比📄页面大.
            assert(alignment <= ::getpagesize() + 0u);

            const auto whatcanisay_shm_out = std::move(
                this->resources
#ifdef __cpp_lib_associative_heterogeneous_erasure
                .template extract<const void *>(area)
#else
                .extract(
                    std::find_if(
                        this->resources.cbegin(), this->resources.cend(),
                        [area=(const void *)area](const auto& shm) noexcept {
                            if constexpr (using_ordered_set)
                                return !ShM_As_Addr{}(shm, area) == !ShM_As_Addr{}(area, shm);
                            else
                                return ShM_As_Addr{}(shm) == ShM_As_Addr{}(area)
                                       && ShM_As_Addr{}(shm, area);
                        }
                    )
                )
#endif
                .value()
            );
            // 标准要求 allocation 与 deallocation 的 ‘size’ 要匹配, 否则是 undefined
            // behavior.  我们没有记录 allocation 的 ‘size’ 值是多少, 但肯定在此范围.
            assert(
                size <= std::size(whatcanisay_shm_out)
                && std::size(whatcanisay_shm_out) <= ceil_to_page_size(size)
            );
        }
        bool do_is_equal [[gnu::cold]] (
            const std::pmr::memory_resource& other
        ) const noexcept override {
            if (const auto that = dynamic_cast<decltype(this)>(&other))
                return &this->resources == &that->resources;
            else
                return false;
        }
    public:
        /**
         * @brief 构造函数.
         */
        ShM_Resource() noexcept = default;
        /**
         * @brief 实现移动语义.
         */
        ShM_Resource(ShM_Resource&& other) noexcept
        : resources{std::move(other.resources)} {
            if constexpr (!using_ordered_set)
                this->last_inserted = std::move(other.last_inserted);
        }
        /**
         * @brief 实现交换语义.
         */
        friend void swap(ShM_Resource& a, decltype(a) b) noexcept {
            std::swap(a.resources, b.resources);

            if constexpr (!using_ordered_set)
                std::swap(a.last_inserted, b.last_inserted);
        }
        /**
         * @brief 实现赋值语义.
         * @details 等号左侧的分配器的资源会被释放.
         * @note example (强制回收所有来自该分配器的共享内存):
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto _ = allocator.allocate(1);
         *      _ = allocator.allocate(2);
         *      _ = allocator.allocate(3);
         * assert( std::size(allocator.get_resources())== 3 );
         * allocator = {};
         * assert( std::size(allocator.get_resources())== 0 );
         * ```
         */
        auto& operator=(ShM_Resource other) {
            swap(*this, other);
            return *this;
        }
        ~ShM_Resource() override {
#ifdef IPCATOR_LOG  // 显式删除以触发日志输出.
                while (!std::empty(this->resources)) {
                    auto& area = *std::cbegin(this->resources);
                    this->deallocate(
                        (void *)std::data(area), std::size(area)
                    );
                }
#endif
        }

        /**
         * @brief 获取 `Shared_Memory<true>` 的集合的引用.
         * @details 它包含了所有已分配而未回收的 `Shared_Memory<true>`.
         * @note example:
         * ```
         * auto allocator = ShM_Resource<std::unordered_set>{};
         * auto addr = (char *)allocator.allocate(sizeof(int));
         * const Shared_Memory<true> *pshm;
         * for (auto& shm : allocator.get_resources())
         *     if (std::data(shm) <= addr && addr < std::data(shm) + std::size(shm)) {
         *         pshm = &shm;
         *         break;
         *     }
         * assert( pshm == &allocator.find_arena(addr) );
         * ```
         */
        auto get_resources [[gnu::cold]] (
#ifndef __cpp_explicit_this_parameter
        ) const& -> auto& {
            return this->resources;
        }
        auto& get_resources() const&& {
            return this->resources;
        }
        auto get_resources() && {
            return std::move(this->resources);
        }
#else
            this auto&& self
        ) -> decltype(auto) {
            if constexpr (
                std::disjunction<
                    std::is_lvalue_reference<decltype(self)>,
                    std::is_const<typename std::remove_reference<decltype(self)>::type>
                >::value
            )
                return std::as_const(self.resources);
            else
                return std::move(self.resources);
        }
#endif
        /**
         * @details 允许 `ShM_Resource<std::set>` 从
         *          `ShM_Resource<std::unordered_set>`
         *          移动构造.
         * @note example:
         * ```
         * ShM_Resource<std::unordered_set> a;
         * auto _ = a.allocate(1);
         * assert( std::size(a.get_resources()) == 1 );
         * ShM_Resource<std::set> b{std::move(a)};
         * assert(
         *     std::size(a.get_resources()) == 0
         *     && std::size(b.get_resources()) == 1
         * );
         * ```
         */
        ShM_Resource(ShM_Resource<std::unordered_set>&& other) requires(using_ordered_set)
        : resources{[
            other_resources=std::move(other).get_resources(),
#pragma clang diagnostic push
#if 16 <= __clang_major__ && __clang_major__ <= 21
# pragma clang diagnostic ignored "-Wunused-lambda-capture"
#endif
            this
#pragma clang diagnostic pop
        ]() mutable {
            decltype(this->resources) resources;

            while (!std::empty(other_resources))
                resources.insert(std::move(
                    other_resources
                    .extract(std::cbegin(other_resources))
                    .value()
            ));

            return resources;
        }()} {}

        /**
         * @brief 将 self 以类似 JSON 的格式输出.
         * @note 也可用 `std::println("{}", self)` 打印 (since C++23).
         * @note example:
         * ```
         * auto a = ShM_Resource<std::set>{};
         * auto _ = a.allocate(1);
         * auto b = ShM_Resource<std::unordered_set>{};
         * _ = b.allocate(2), _ = b.allocate(2);
         * std::cout << a << '\n'
         *           << b << '\n';
         * ```
         */
        friend auto operator<<[[gnu::cold]](std::ostream& out, const ShM_Resource& resrc)
        -> decltype(auto) {
            return out << std::format("{}", resrc);
        }

        /**
         * @brief 查询给定对象位于哪个 POSIX shared memory.
         * @param obj 被查询的对象的指针 (可以是 `void *`).
         * @return 对象所在的 `Shared_Memory<true>` 的引用.
         * @note - 当类的模板参数 `set_t` 是 `std::set` 时,
         *         查找时间 O(log N).
         *       - 是 `std::unordered_set` 时, 如果 `obj` 是
         *         最近一次 allocation 的内存块中的某个对象
         *         的指针, 则时间为 O(1); 否则为 O(N).
         * @exception 查找失败说明 ‘obj’ 不在此实例分配的
         *            内存块上, 抛 `std::invalid_argument`.
         * @note example:
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto area = (char *)allocator.allocate(100);
         * int& i = (int&)area[8],
         *    & j = (int&)area[8 + sizeof(int)],
         *    & k = (int&)area[8 + 2 * sizeof(int)];
         * assert(
         *     allocator.find_arena(&i).get_name() == allocator.find_arena(&j).get_name()
         *     && allocator.find_arena(&j).get_name() == allocator.find_arena(&k).get_name()
         * );  // 都在同一片 POSIX shared memory 区域.
         * ```
         */
        const auto& find_arena [[gnu::hot]] (const auto *const obj) const noexcept(false) {
            const auto obj_in_shm = [&](const auto& shm) {
                return std::to_address(std::cbegin(shm)) <= (const char *)obj
                       && (const char *)(std::uintptr_t(obj)+1) <= std::to_address(std::cend(shm));
                       //                              ^^^^^^^ 校验 obj 的宽度不会超出 shm 尾端.
            };

            if constexpr (using_ordered_set) {
                if (auto next_shm = this->resources.upper_bound((const void *)obj), shm = decltype(next_shm){};
                    next_shm != std::cbegin(this->resources) && obj_in_shm(*(shm = --next_shm)))
                    return *shm;
            } else {
                if (obj_in_shm(*this->last_inserted))
                    return *this->last_inserted;
                if (const auto target = std::ranges::find_if(this->resources, obj_in_shm);
                    target != std::cend(this->resources))
                    return *target;
            }
            throw std::invalid_argument{"传入的 ‘obj’ 并不位于任何由该实例所分配的共享内存块上"};
        }
        private:
            friend struct std::formatter<ShM_Resource>;
            std::conditional_t<
                !using_ordered_set,
                const Shared_Memory<true> *, std::monostate
            > last_inserted [[
#if __has_cpp_attribute(indeterminate)
                indeterminate,
#endif
                no_unique_address
            ]];
};

static_assert( std::movable<ShM_Resource<std::set>> );
static_assert( std::movable<ShM_Resource<std::unordered_set>> );


IPCATOR_CLOSE_NAMESPACE
template <template <typename... T> class set_t>
struct std::formatter<
#ifdef IPCATOR_NAMESPACE
        IPCATOR_NAMESPACE::
#endif
        ShM_Resource<set_t>
    > {
    constexpr auto parse(const auto& parser) {
        if (const auto p = parser.begin(); p != parser.end() && *p != '}')
            throw std::format_error("不支持任何格式化动词.");
        else
            return p;
    }
    auto format(const auto& resrc, auto& context) const {
        const auto size = std::size(resrc.get_resources());
        const auto resources_values =
#if defined __cpp_lib_ranges_join_with && defined __cpp_lib_ranges_to_container
            resrc.get_resources()
            | std::views::transform([](auto& shm) static {return std::format("{}", shm);})
            | std::views::join_with(",\n"s)
            | std::ranges::to<std::string>()
#else
            [&] {
                std::string arr;
                for (const auto& shm : resrc.get_resources())
                    arr += std::format("{}", shm) + ",\n";
                arr.pop_back(), arr.pop_back();
                return arr;
            }()
#endif
        ;
        if constexpr (std::decay_t<decltype(resrc)>::using_ordered_set)
            return std::vformat_to(
                context.out(),
                R":({{ "resources": {{ "|size|": {} }}, "constructor()": "ShM_Resource<std::set>" }}):",
                std::make_format_args(size)
            );
        else {
            const auto last_inserted = size ? std::format("\n{}", *resrc.last_inserted) : std::string{"null"};
            return std::vformat_to(
                context.out(),
                R":({{
    "resources":
    {{
        "|size|": {},
        "values":
        [
{}
        ]
    }},
    "last_inserted": {},
    "constructor()": "ShM_Resource<std::unordered_set>"
}}):",
                std::make_format_args(
                    size,
                    resources_values,
                    last_inserted
                )
            );
        }
    }
};
IPCATOR_OPEN_NAMESPACE


/**
 * @brief Allocator: 单调增长的共享内存 buffer.  它的 allocation 是链式的,
 *        其⬆️游是 `ShM_Resource<std::unordered_set>` 并拥有⬆️游的所有权.
 * @details 维护一个 buffer, 其中包含若干 POSIX shared memory, 因此 buffer
 *          也不是连续的.  Buffer 的累计大小单调增加, 它仅在析构 (或手动
 *          调用 `Monotonic_ShM_Buffer::release`) 时释放资源.  它的意图是
 *          提供非常快速的内存分配, 并于之后一次释放的情形 (进程退出也是
 *          适用的场景).
 * @note 在 <br />
 *       ▪️ **不需要 deallocation**, 分配的共享内存区域
 *         会一直被使用 <br />
 *       ▪️ 或 临时发起 **多次** allocation 请求, 并在
 *         **不久后就 *全部* 释放掉** <br />
 *       ▪️ 或 **注重时延** 而 内存占用相对不敏感 <br />
 *       的场合下, 有充分的理由使用该分配器.  因为它非常快, 只做
 *       简单的分配.  (See `Monotonic_ShM_Buffer::allocate`.)
 */
struct Monotonic_ShM_Buffer: std::pmr::monotonic_buffer_resource {
        /**
         * @brief Buffer 的构造函数.
         * @param initial_size Buffer 的初始长度, 越大的 size **保证** 越小的均摊时延.
         * @details 初次 allocation 是惰性的💤, 即构造时并不会立刻创建 buffer.
         * @note Buffer 的总大小未必是📄页表大小的整数倍, 但 `initial_size` 最好是.
         *       (该构造函数会自动将 `initial_size` 用  `ceil_to_page_size(const std::size_t)`
         *       向上取整.)
         * @warning `initial_size` 不可为 0.
         */
        Monotonic_ShM_Buffer(const std::size_t initial_size = 1)
#ifdef IPCATOR_OFAST
        noexcept
#endif
        : monotonic_buffer_resource{
            ceil_to_page_size(initial_size),
            new ShM_Resource<std::unordered_set>,
        } {
            assert(initial_size);
#if __has_cpp_attribute(assume)
            [[assume(initial_size)]];
#endif
        }
        ~Monotonic_ShM_Buffer() override {
            this->release();
            delete this->monotonic_buffer_resource::upstream_resource();
        }

        /**
         * @brief 获取指向⬆️游资源的指针.
         * @note example:
         * ```
         * auto buffer = Monotonic_ShM_Buffer{};
         * auto addr = (char *)buffer.allocate(100);
         * const Shared_Memory<true>& shm = buffer.upstream_resource()->find_arena(addr);
         * assert(
         *     std::to_address(std::cbegin(shm)) <= addr
         *     && addr < std::to_address(std::cend(shm))
         * );  // 新划取的区域一定位于 `upstream_resource()` 最近一次分配的内存块中.
         * ```
         */
        auto upstream_resource() const -> const auto * {
            return static_cast<ShM_Resource<std::unordered_set> *>(
                this->monotonic_buffer_resource::upstream_resource()
            );
        }
    protected:
        void *do_allocate [[using gnu: hot, returns_nonnull, alloc_size(2)]] (
            const std::size_t size, const std::size_t alignment
        )
#ifdef IPCATOR_OFAST
          noexcept
#endif
          override {
            const auto area = this->monotonic_buffer_resource::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }
        void do_deallocate [[gnu::nonnull(2)]] (
            void *const area [[clang::noescape]],
            const std::size_t size,
            const std::size_t alignment
        ) noexcept override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");

            // 虚晃一枪; actually no-op.
            // ‘std::pmr::monotonic_buffer_resource::deallocate’ 的函数体其实是空的.
            this->monotonic_buffer_resource::do_deallocate(area, size, alignment);
        }
#ifdef IPCATOR_IS_BEING_DOXYGENING  // stupid doxygen
        /**
         * @brief 强制释放所有已分配而未收回的内存.
         * @details 将当前缓冲区和下个缓冲区的大小设置为其构造时的
         *          `initial_size`.
         * @note 内存的释放仅代表 `Shared_Memory<true>` 的析构, 因此
         *       其它进程仍可能从这些内存中读取消息.  (See
         *       `Shared_Memory::~Shared_Memory()`.)
         */
        void release();
        /**
         * @brief 从某片 POSIX shared memory 区域中划出一块分配.
         * @param alignment 可选.
         * @details 首先检查 buffer 的剩余空间, 如果不够, 则向⬆️游
         *          获取新的 `Shared_Memory<true>` (每次向⬆️游申请
         *          的 shared memory 的大小以几何级数增加) 加入到
         *          剩余空间中.  然后, 从剩余空间中从中划出一块.
         */
        void *allocate(
            std::size_t size, std::size_t alignment = alignof(std::max_align_t)
        );
        /**
         * @brief 无操作.
         * @details Buffer 在分配时根本不追踪所有 allocation 的位置,
         *          它单纯地增长, 以此提高分配速度.  因此也无法根据
         *          指定位置响应 deallocation.
         */
        void deallocate(void *area) = delete;
#endif
};


/**
 * @brief Allocator: 共享内存池.  它的 allocation 是链式的, 其
 *        ⬆️游是 `ShM_Resource<std::set>` 并拥有⬆️游的所有权.
 *        它在析构时会调用 `ShM_Pool::release` 释放所有内存资源.
 *        该分配器的目标是减少内存碎片, 总是尝试在相邻位置分配.
 * @tparam sync 是否线程安全.  设为 false 时, 🚀速度更快.
 * @details ▪️ 持有若干 POSIX shared memory 区域, 每片区域被视为
 *            一个 pool.  一个 pool 会被切割成若干 chunks, 每个
 *            chunk 是特定 block size (见 `ShM_Pool::ShM_Pool(const std::pmr::pool_options&)`)
 *            的整数倍.  <br />
 *          ▪️ 当响应 size 大小的内存申请时, 从合适的 chunk 中划取
 *            即可.  <br />
 *          ▪️ 剩余空间不足时, 会创建新的 pool 以取得更多的 chunks.
 *            <br />
 *          ▪️ block size 可以有上限值, 大于此值的 allocation 请求
 *            会通过直接创建 `Shared_Memory<true>` 的方式响应, 而
 *            不再执行池子算法.  <br />
 * @note 在不确定要使用何种共享内存分配器时, 请选择该类.  即使对
 *       底层实现感到迷惑也能直接拿来使用.
 */
template <bool sync>
class ShM_Pool: public std::conditional_t<
    sync,
    std::pmr::synchronized_pool_resource,
    std::pmr::unsynchronized_pool_resource
> {
        using midstream_pool_t = std::conditional_t<
            sync,
            std::pmr::synchronized_pool_resource,
            std::pmr::unsynchronized_pool_resource
        >;
    protected:
        void *do_allocate [[using gnu: hot, returns_nonnull, alloc_size(2)]] (
            const std::size_t size, const std::size_t alignment
        )
#ifdef IPCATOR_OFAST
          noexcept
#endif
          override {
            const auto area = this->midstream_pool_t::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }

        void do_deallocate [[gnu::nonnull(2)]] (
            void *const area [[clang::noescape]],
            const std::size_t size,
            const std::size_t alignment
        )
#ifdef IPCATOR_OFAST
          noexcept
#endif
          override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");
            this->midstream_pool_t::do_deallocate(area, size, alignment);
        }
    public:
        /**
         * @brief 构造 pools.
         * @param options 设定: 最大的 block size, 每 chunk 的最大 blocks 数量.
         */
        ShM_Pool(const std::pmr::pool_options& options = {.largest_required_pool_block=1}) noexcept
        : midstream_pool_t{
            decltype(options){
                .max_blocks_per_chunk = options.max_blocks_per_chunk,
                .largest_required_pool_block = ceil_to_page_size(
                    options.largest_required_pool_block
                ),  // 向⬆️游申请内存的🚪≥页表大小, 避免零碎的请求.
            },
            new ShM_Resource<std::set>,
        } {}
        ~ShM_Pool() override {
            this->release();
            delete this->midstream_pool_t::upstream_resource();
        }

        /**
         * @brief 获取指向⬆️游资源的指针.
         * @note example:
         * ```
         * auto pools = ShM_Pool<false>{};
         * auto addr = (char *)pools.allocate(100);
         * auto& arr = (std::array<char, 10>&)addr[50];
         * const Shared_Memory<true>& shm = pools.upstream_resource()->find_arena(&arr);
         * assert(
         *     std::to_address(std::cbegin(shm)) <= (char *)&arr
         *     && (char *)&arr < std::to_address(std::cend(shm))
         * );
         * ```
         */
        auto upstream_resource() const -> const auto * {
            return static_cast<ShM_Resource<std::set> *>(
                this->midstream_pool_t::upstream_resource()
            );
        }

#ifdef IPCATOR_IS_BEING_DOXYGENING  // stupid doxygen
        /**
         * @brief 查看构造时指定的配置选项的实际值.
         * @details 这些选项的实际值未必和构造时提供
         *          的一致:
         *          - 零值会被替换为默认值;
         *          - 大小可能被取整到特定的粒度.
         * @note example:
         * ```
         * auto pools = ShM_Pool<false>{
         *     std::pmr::pool_options{
         *         .max_blocks_per_chunk = 0,
         *         .largest_required_pool_block = 8000,
         *     }
         * };
         * std::cout << pools.options().largest_required_pool_block << ", "
         *           << pools.options().max_blocks_per_chunk << '\n';
         * ```
         */
        std::pmr::pool_options options() const;
        /**
         * @brief 强制释放所有已分配而未收回的内存.
         * @note 内存的释放仅代表 `Shared_Memory<true>` 的析构, 因此
         *       其它进程仍可能从这些内存中读取消息.  (See
         *       `Shared_Memory::~Shared_Memory()`.)
         * @note example:
         * ```
         * auto pools = ShM_Pool<false>{};
         * auto _ = pools.allocate(1);
         * assert( std::size(pools.upstream_resource()->get_resources()) );
         * pools.release();
         * assert( std::size(pools.upstream_resource()->get_resources()) == 0 );
         * ```
         */
        void release();
        /**
         * @brief 从共享内存中分配 block.
         * @param alignment 对齐要求.
         */
        void *allocate(
            std::size_t size, std::size_t alignment = alignof(std::max_align_t)
        );
        /**
         * @brief 回收 block.
         * @param area `allocate` 的返回值
         * @param size 仅在未定义 `NDEBUG` 宏时调试用.  发布时, 可以将实参替换为任意常量.
         * @details 回收 block 之后可能会导致某个 pool (`Shared_Memory<true>`) 处于完全
         *          闲置的状态, 此时可能会触发🗑️GC, 也就是被析构, 然而时机是不确定的, 由
         *          `std::pmr::unsynchronized_pool_resource` 的实现决定.
         */
        void deallocate(void *area, std::size_t size);
#endif
};


/**
 * @brief 表示共享内存分配器.
 */
template <class ipcator_t>
concept IPCator = (
    std::same_as<ipcator_t, Monotonic_ShM_Buffer>
    || std::same_as<ipcator_t, ShM_Resource<std::set>>
    || std::same_as<ipcator_t, ShM_Resource<std::unordered_set>>
    || std::same_as<ipcator_t, ShM_Pool<true>>
    || std::same_as<ipcator_t, ShM_Pool<false>>
) && requires(ipcator_t ipcator) {  // PS, 这是个冗余条件, 但可以给 LSP 提供信息.
    /* 可公开情报 */
    requires std::derived_from<ipcator_t, std::pmr::memory_resource>;
    requires requires {
        { *ipcator.upstream_resource() } -> std::same_as<const ShM_Resource<std::set>&>;
    } || requires {
        { *ipcator.upstream_resource() } -> std::same_as<const ShM_Resource<std::unordered_set>&>;
    } || requires {
        {  ipcator.find_arena(new int) } -> std::same_as<const Shared_Memory<true>&>;
    };
};
static_assert(
    IPCator<Monotonic_ShM_Buffer>
    && IPCator<ShM_Resource<std::set>>
    && IPCator<ShM_Resource<std::unordered_set>>
    && IPCator<ShM_Pool<true>>
    && IPCator<ShM_Pool<false>>
);


/**
 * @brief 通用的跨进程消息读取器.
 * @tparam writable 读到消息之后是否允许对其进行修改.
 * @details `ShM_Reader<writable>` 内部缓存一系列
 *          `Shared_Memory<false, writable>`.  每当
 *          遇到不位于任何已知的 POSIX shared memory
 *          上的消息时, 都将📂新建 `Shared_Memory`
 *          并加入缓存.  后续的读取将不需要重复打开
 *          相同的目标文件和徒劳的🎯映射.
 *          `ShM_Reader` 会执行特定的策略, 控制缓存
 *          大小, 以限制自身占用的资源.  然而, 即使
 *          缓存被清空, 只要持有 `read` 方法的返回值
 *          (迭代器) 就能保证仍能访问对应的消息.
 */
template <auto writable=false>
struct ShM_Reader {
        /**
         * @brief 以 迭代器/智能指针 的形式获取消息的引用,
         *        在迭代器析构之前, **保证** 可以访问消息.
         * @param shm_name POSIX shared memory 的名字.
         * @param offset 消息体在 shared memory 中的偏移量.
         * @note 基于共享内存的 IPC 在传递消息时, 靠
         *       共享内存所对应的目标文件的名字 和
         *       消息体在共享内存中的偏移量 决定消息的位置.
         * @note example:
         * ```
         * // writer.cpp
         * using namespace literals;
         * auto shm = "/ipcator.1"_shm[1000];
         * auto arr = new(&shm[42]) std::array<char, 32>;
         * (*arr)[15] = 9;
         * // reader.cpp
         * auto rd = ShM_Reader{};
         * auto arr_from_other_proc = rd.template read<std::array<char, 32>>("/ipcator.1", 42);
         * assert( (*arr_from_other_proc)[15] == 9 );
         * ```
         */
        template <class T>
        auto read [[gnu::hot]] (
            const std::string_view shm_name, const std::size_t offset
        ) {
            class Iterator {
                    std::shared_ptr<const Shared_Memory<false, writable>> shm;
                    std::size_t offset;
                public:
                    Iterator(
                        decltype(Iterator::shm) shm,
                        const decltype(Iterator::offset) offset
                    ): shm{std::move(shm)}, offset{offset} {}
                    Iterator(const Iterator&) = default;
                    Iterator& operator=(const Iterator&) = default;

                    using element_type = std::conditional_t<writable, T, const T>;
                    static auto pointer_to(element_type&) = delete;

                    auto *operator->() const {
                        return (element_type *)(this->shm->data() + this->offset);
                    }
                    auto& operator*() const { return *this->operator->(); }
#ifdef IPCATOR_USED_BY_SEER_RBK
                    auto cnt_ref_shm() const { return this->shm.use_count(); }
#endif
            };

            return Iterator{this->select_shm(shm_name), offset};
        }

        /**
         * @brief 保留任何被由 `read` 返回的迭代器所引用的消息
         *        所在的共享内存, 缓存中其余的共享内存实例将被释放.
         * @return 释放的 `Shared_Memory<false, writable>` 的数量.
         */
        auto gc_ [[gnu::cold]] () noexcept {
            return std::erase_if(
                this->cache,
                [](const auto& shm)
#ifdef __cpp_static_call_operator
                static
#endif
                {
                    return
#if __cplusplus <= 201703L
                        shm.unique()
#else
                        shm.use_count() == 1
#endif
                    ;
                }
            );
        }

        auto select_shm(const std::string_view name) {
            if (
                const auto pshm =
#ifdef __cpp_lib_generic_unordered_lookup
                    this->cache.find(name)
#else
                    std::find_if(
                        this->cache.cbegin(), this->cache.cend(),
                        [&](const auto& shm) {
                            return ShM_As_Str{}(name) == ShM_As_Str{}(shm)
                                   && ShM_As_Str{}(name, shm);
                        }
                    )
#endif
                ;
                pshm != std::cend(this->cache)
            )
                return *pshm;
            else {
                const auto [inserted, ok] = this->cache.emplace(
                    std::make_shared<Shared_Memory<false, writable>>(std::string{name})
                );
                assert(ok);
#if __has_cpp_attribute(assume)
                [[assume(ok)]];
#endif
                return *inserted;
            }
        }
    private:
        struct ShM_As_Str {
            using is_transparent = int;

            static auto get_name(const auto& shm_or_name)
            -> std::string_view {
                if constexpr (
                    std::is_same_v<
                        std::decay_t<decltype(shm_or_name)>,
                        std::string_view
                    >
                )
                    return shm_or_name;
                else
                    return shm_or_name->get_name();
            }

            /* Hash */
#ifdef __cpp_static_call_operator
            static
#endif
            auto operator()(const auto& shm)
#ifndef __cpp_static_call_operator
            const
#endif
            noexcept -> std::size_t {
                const auto name = get_name(shm);
                return std::hash<std::decay_t<decltype(name)>>{}(name);
            }
            /* KeyEqual */
#ifdef __cpp_static_call_operator
            static
#endif
            bool operator()[[gnu::cold]](const auto& a, const auto& b)
#ifndef __cpp_static_call_operator
            const
#endif
            noexcept {
                return get_name(a) == get_name(b);
            }
        };
        std::unordered_set<
            std::shared_ptr<Shared_Memory<false, writable>>,
            ShM_As_Str, ShM_As_Str
        > cache;
};


IPCATOR_CLOSE_NAMESPACE
#if defined IPCATOR_USED_BY_SEER_RBK
using namespace
# ifdef IPCATOR_NAMESPACE
                IPCATOR_NAMESPACE::
# endif
                                   literals;
using namespace std::literals;
#endif
