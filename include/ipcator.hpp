/**
 * @mainpage
 * @brief 用于 基于共享内存 的 IPC 的基础设施.
 * @note 有关 POSIX 共享内存的简单介绍:
 *       共享内存由临时文件映射而来.  若干进程将相同的目标文件映射到 RAM 中
 *       就能实现内存共享.  一个目标文件可以被同一个进程映射多次, 被映射后的
 *       起始地址一般不同.
 * @details 自顶向下, 包含 3 个共享内存分配器: `Monotonic_ShM_Buffer`, `ShM_Pool<true>`,
 *          `ShM_Pool<false>`.  <br />
 *          它们依赖于 2 个 POSIX shared memory 分配器 (即一次性分配若干连续的📄页面而
 *          不对其作任何切分, 这些📄页面由 kernel 分配) 之一: `ShM_Resource<std::set>`
 *          或 `ShM_Resource<std::unordered_set>`.  <br />
 *          `ShM_Resource` 拥有若干 `Shared_Memory<true>`, `Shared_Memory` 即是对 POSIX
 *          shared memory 的抽象.  <br />
 *          读取器有 `ShM_Reader`.  工具函数/类/概念有 `ceil_to_page_size(std::size_t)`,
 *          `generate_shm_UUName()`, [namespace literals](./namespaceliterals.html),
 *          [concepts](./concepts.html).
 * @note 要构建 release 版本, 请在文件范围内定义 `NDEBUG` 宏 以删除诸多非必要的校验措施.
 */

#pragma once
// #defined NDEBUG
#include <algorithm>  // ranges::fold_left
#include <atomic>  // atomic_uint, memory_order_relaxed
#include <cassert>
#include <chrono>
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
# else
#   include "fmt/format.h"
    namespace std {
        using ::fmt::format,
              ::fmt::formatter, ::fmt::format_error,
              ::fmt::vformat, ::fmt::vformat_to,
              ::fmt::make_format_args;
    }
# endif
#include <functional>  // bind{_back,}, bit_or, plus
#include <future>  // async, future_status::ready
#include <iostream>  // clog
#include <iterator>  // size, {,c}{begin,end}, data, empty
#include <memory_resource>  // pmr::{memory_resource,monotonic_buffer_resource,{,un}synchronized_pool_resource,pool_options}
#include <new>  // bad_alloc
#include <ostream>  // ostream
#include <random>  // mt19937, random_device, uniform_int_distribution
#include <ranges>  // views::{chunk,transform,join_with,iota}
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
#include <string>
#include <string_view>
#include <thread>  // this_thread::sleep_for
#include <tuple>  // ignore
#include <type_traits>  // conditional_t, is_const{_v,}, remove_reference{_t,}, is_same_v, decay_t, disjunction, is_lvalue_reference
#include <unordered_set>
#include <utility>  // as_const, move, swap, unreachable, hash, exchange, declval
#include <variant>  // monostate
#include <version>
#include <fcntl.h>  // O_{CREAT,RDWR,RDONLY,EXCL}
#include <sys/mman.h>  // m{,un}map, shm_{open,unlink}, PROT_{WRITE,READ}, MAP_{SHARED,FAILED,NORESERVE}
#include <sys/stat.h>  // fstat, struct stat
#include <unistd.h>  // close, ftruncate, getpagesize


using namespace std::literals;


constexpr auto DEBUG_ =
#ifdef NDEBUG
    false
#else
    true
#endif
;


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
    inline auto ceil_to_page_size(const std::size_t min_length)
    -> std::size_t {
        const auto current_num_pages = min_length / getpagesize();
        const bool need_one_more_page = min_length % getpagesize();
        return (current_num_pages + need_one_more_page) * getpagesize();
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
class Shared_Memory {
        std::string name;
        std::span<
            std::conditional_t<
                writable,
                unsigned char,
                const unsigned char
            >
        > area;
    public:
        /**
         * @brief 创建 shared memory 并映射, 可供其它进程打开以读写.
         * @param name 这是目标文件名.  POSIX 要求的格式是 `/path-to-shm`.
         *             建议使用 `generate_shm_UUName()` 自动生成该路径名.
         * @param size 目标文件的大小, 亦即 shared memory 的长度.  建议
         *             使用 `ceil_to_page_size(std::size_t)` 自动生成.
         * @note Shared memory 的长度是固定的, 一旦创建, 无法再改变.
         * @warning POSIX 规定 `size` 不可为 0.
         * @details 根据 `name` 创建一个临时文件, 并将其映射到进程自身的 RAM 中.
         *          临时文件的文件描述符在构造函数返回前就会被删除.
         * @warning `name` 不能和已有 POSIX shared memory 重复.
         * @note example (该 constructor 会推导类的模板实参):
         * ```
         * Shared_Memory shm{"/ipcator.example-Shared_Memory-creator", 1234};
         * static_assert( std::is_same_v<decltype(shm), Shared_Memory<true, true>> );
         * ```
         */
        Shared_Memory(const std::string name, const std::size_t size) requires(creat)
        : name{name}, area{
            Shared_Memory::map_shm(name, size),
            size,
        } {
            if constexpr (DEBUG_)
                std::clog << std::format("创建了 Shared_Memory: \033[32m{}\033[0m", *this) + '\n';
        }
        /**
         * @brief 打开📂目标文件, 将其映射到 RAM 中.
         * @param name 目标文件的路径名.  这个路径通常是事先约定的, 或者
         *             从其它实例的 `Shared_Memory::get_name()` 方法获取.
         * @details 目标文件的描述符在构造函数返回前就会被删除.
         * @warning 目标文件必须存在, 否则会 crash.
         * @note 没有定义 `NDEBUG` 宏时, 会尝试短暂地阻塞以等待对应的
         *       creator 被创建.
         * @note example (该 constructor 会推导类的模板实参):
         * ```
         * Shared_Memory creator{"/ipcator.1", 1};
         * Shared_Memory accessor{"/ipcator.1"};
         * static_assert( std::is_same_v<decltype(shm), Shared_Memory<false, false>> );
         * assert( std::size(accessor) == 1 );
         * ```
         */
        Shared_Memory(const std::string name) requires(!creat)
        : name{name}, area{
            [&]
#if __cplusplus <= 202002L
               ()
#endif
            -> decltype(this->area) {
                const auto [addr, length] = Shared_Memory::map_shm(name);
                return {addr, length};
            }()
        } {
            if constexpr (DEBUG_)
                std::clog << std::format("创建了 Shared_Memory: \033[32m{}\033[0m\n", *this) + '\n';
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
        : name{std::move(other.name)}, area{
            // Self 的 destructor 靠 `area` 是否为空来
            // 判断是否持有所有权, 所以此处需要强制置空.
            std::exchange(other.area, {})
        } {}
        /**
         * @brief 实现交换语义.
         */
        friend void swap(Shared_Memory& a, decltype(a) b) noexcept {
            std::swap(a.name, b.name);
            std::swap(a.area, b.area);
        }
        /**
         * @brief 实现赋值语义.
         * @note example:
         * ```
         * auto a = Shared_Memory{"/ipcator.assign-1", 3};
         * a = {"/ipcator.assign-2", 5};
         * assert(
         *     a.get_name == "/ipcator.assign-2" && std::size(a) == 5
         * );
         * ```
         */
        auto& operator=(Shared_Memory other) {
            swap(*this, other);
            return *this;
        }
        /**
         * @brief 将 shared memory **unmap**.  对于 creator, 还会阻止对关联的
         *        目标文件的新的映射.
         * @details 如果是 creator 析构了, 其它 accessories 仍可访问对应的 POSIX
         *          shared memory, 但新的 accessor 的构造将导致进程 crash.  当余下
         *          的 accessories 都析构掉之后, 目标文件的引用计数归零, 将被释放.
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
            if (std::data(this->area) == nullptr)
                return;

            // 🚫 Writer 将要拒绝任何新的连接请求:
            if constexpr (creat)
                shm_unlink(this->name.c_str());
                // 此后的 ‘shm_open’ 调用都将失败.
                // 当所有 shm 都被 ‘munmap’ed 后, 共享内存将被 deallocate.

            munmap(
                const_cast<unsigned char *>(std::data(this->area)),
                std::size(this->area)
            );

            if constexpr (DEBUG_)
                std::clog << std::format("析构了 Shared_Memory: \033[31m{}\033[0m", *this) + '\n';
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
        const auto& get_area(
#ifndef __cpp_explicit_this_parameter
        ) const {
            auto& self = const_cast<Shared_Memory&>(*this);
#else
            this auto& self
        ) {
#endif
            return self.area;
        }

#if __cplusplus >= 202302L
        [[nodiscard]]
#endif
        static auto map_shm(
            const std::string& name, const std::unsigned_integral auto... size
        ) requires(sizeof...(size) == creat) {
            assert("/dev/shm"s.length() + name.length() <= 255);
            const auto fd = [](const auto do_open) {
                if constexpr (creat || !DEBUG_)
                    return do_open();
                else /* !creat and DEBUG_ */ {
                    std::future opening = std::async(
                        [&] {
                            while (true)
                                if (const auto fd = do_open(); fd != -1)
                                    return fd;
                                else
                                    std::this_thread::sleep_for(50ms);
                        }
                    );
                    // 阻塞直至目标共享内存对象存在:
                    if (opening.wait_for(0.5s) == std::future_status::ready)
                        [[likely]] return opening.get();
                    else
                        assert(!"shm obj 仍未被创建, 导致 reader 等待超时");
                }
            }(std::bind(
                shm_open,
                name.c_str(),
                (creat ? O_CREAT|O_EXCL : 0) | (writable ? O_RDWR : O_RDONLY),
                0777
            ));
            assert(fd != -1);

            if constexpr (creat) {
                // 设置 shm obj 的大小:
                const auto result_resize = ftruncate(
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
                    else {
                        struct stat shm;
                        do {
                            fstat(fd, &shm);
                        } while (DEBUG_ && shm.st_size == 0);  // 等到 creator resize 完 shm obj.
                        return shm.st_size +
#ifdef __cpp_size_t_suffix
                            0zu
#else
                            0ull
#endif
                        ;
                    }
                }()
            ] {
                assert(size);
#if __has_cpp_attribute(assume)
                [[assume(size)]];
#endif
                const auto area_addr = (unsigned char *)mmap(
                    nullptr, size,
                    PROT_READ | (writable ? PROT_WRITE : 0) | PROT_EXEC,
                    MAP_SHARED | (!writable ? MAP_NORESERVE : 0),
                    fd, 0
                );
                close(fd);  // 映射完立即关闭, 对后续操作🈚影响.
                assert(area_addr != MAP_FAILED);

                if constexpr (creat)
                    return area_addr;
                else {
                    const struct {
                        std::conditional_t<
                            writable,
                            unsigned char, const unsigned char
                        > *const addr;
                        const std::size_t length;
                    } area{area_addr, size};
                    return area;
                }
            }();
        }

        /**
         * @brief 🖨️ 打印内存布局到一个字符串.  调试用.
         * @details 一个造型是多行多列的矩阵, 每个元素用 16 进制表示对应的 byte.
         * @param num_col 列数
         * @param space 每个 byte 之间的填充字符串
         */
        auto pretty_memory_view(
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
                        std::views::transform([](auto& B) {
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
         * @brief 将 self 以类似 JSON 的格式输出.  调试用.
         * @note 也可用 `std::println("{}", self)` 打印 (since C++23).
         */
        friend auto operator<<(std::ostream& out, const Shared_Memory& shm)
        -> decltype(auto) {
            return out << std::format("{}", shm);
        }

        /* impl std::ranges::range for Self */
        /**
         * @brief 获取指定位置的 byte 的引用.
         * @note Reader 在默认情况下只能用 `const&` 接收该引用,
         *       这个检查发生在编译期, 因此误用会导致编译出错.
         */
        auto& operator[](
#ifndef __cpp_explicit_this_parameter
            const std::size_t i
        ) const {
            auto& self = const_cast<Shared_Memory&>(*this);
#else
            this auto& self, const std::size_t i
        ) {
#endif
            assert(i < std::size(self));
            return *(std::begin(self) + i);
        }
#ifdef __cpp_multidimensional_subscript
        auto operator[](
# ifndef __cpp_explicit_this_parameter
            const std::size_t start, decltype(start) end
        ) const {
            auto& self = const_cast<Shared_Memory&>(*this);
# else
            this auto& self,
            const std::size_t start, decltype(start) end
        ) {
# endif
            assert(start <= end && end <= std::size(self));
            return std::span{
                std::begin(self) + start,
                std::begin(self) + end,
            };
        }
#endif
        /**
         * @brief 共享内存区域的首地址.
         */
        auto data(
#ifndef __cpp_explicit_this_parameter
        ) const {
            auto& self = const_cast<Shared_Memory&>(*this);
#else
            this auto& self
        ) {
#endif
            return std::to_address(std::begin(self));
        }
        /**
         * @brief 迭代器, 按 byte 遍历共享内存区域.
         */
        auto begin(
#ifndef __cpp_explicit_this_parameter
        ) const {
            auto& self = const_cast<Shared_Memory&>(*this);
#else
            this auto& self
        ) {
#endif
            return std::begin(self.get_area());
        }
        /**
         * @brief 迭代器, 按 byte 遍历共享内存区域.
         */
        auto end(
#ifndef __cpp_explicit_this_parameter
        ) const {
            auto& self = const_cast<Shared_Memory&>(*this);
#else
            this auto& self
        ) {
#endif
            return std::begin(self) + std::size(self);
        }
        /**
         * @brief 共享内存区域的长度.
         */
        auto size() const { return std::size(this->area); }
};
Shared_Memory(
    std::convertible_to<std::string> auto, std::integral auto
) -> Shared_Memory<true>;
Shared_Memory(
    std::convertible_to<std::string> auto
) -> Shared_Memory<false>;

static_assert( !std::copy_constructible<Shared_Memory<true>> );
static_assert(
    std::ranges::contiguous_range<Shared_Memory<false>>
    && std::ranges::sized_range<Shared_Memory<false>>
);

template <auto creat, auto writable>
struct
#if !(defined __GNUG__ && __GNUC__ <= 15)
    ::
#endif
    std::formatter<Shared_Memory<creat, writable>> {
    constexpr auto parse(const auto& parser) {
        if (const auto p = parser.begin(); p != parser.end() && *p != '}')
            throw std::format_error("不支持任何格式化动词.");
        else
            return p;
    }
    auto format(const auto& shm, auto& context) const {
        constexpr auto obj_constructor = []
#if __cplusplus <= 202002L
            ()
#endif
        consteval {
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
        const auto addr = (const void *)std::data(shm.get_area());
        const auto length = std::size(shm.get_area());
        const auto name = [&] {
            const auto name = shm.get_name();
            if (name.length() <= 57)
                return name;
            else
                return name.substr(0, 54) + "...";
        }();
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


namespace literals {
    /**
     * @brief 创建 `Shared_Memory` 实例的快捷方式.
     * @details
     * - 创建指定大小的命名的共享内存, 以读写模式映射:
     *   ```
     *   auto writer = "/filename"_shm[size];
     *   ```
     * - 不创建, 只将目标文件以读写模式映射至本地:
     *   ```
     *   auto writable_reader = +"/filename"_shm;
     *   ```
     * - 不创建, 只将目标文件以只读模式映射至本地:
     *   ```
     *   auto reader = -"/filename"_shm;
     *   ```
     */
    auto operator""_shm(const char *const name, [[maybe_unused]] std::size_t) {
        struct ShM_Constructor_Proxy {
            const char *const name;
            auto operator[](const std::size_t size) const {
                return Shared_Memory{name, size};
            }
            auto operator+() const {
                return Shared_Memory<false, true>{name};
            }
            auto operator-() const {
                return Shared_Memory<false>{name};
            }
        };
        return ShM_Constructor_Proxy{name};
    }
}


inline namespace utils {
    /**
     * @brief 创建一个 **全局唯一** 的路径名, 不知道该给共享内存起什么名字时就用它.
     * @see Shared_Memory::Shared_Memory(std::string, std::size_t)
     * @note 格式为 `/固定前缀-原子自增的计数字段-进程专属的标识符`.
     * @details 名字的长度为 248, 加上偏移量 (`std::size_t`) 后正好 256.
     *          248 足够大, 使得重名率几乎为 0; 256 刚好可以对齐, 提高
     *          传递消息 (目标内存 + 偏移量) 的速度.
     */
    auto generate_shm_UUName() noexcept {
        constexpr auto prefix = "github_dot_com_slash_shynur_slash_ipcator";
        constexpr auto available_chars = "0123456789"
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                         "abcdefghijklmnopqrstuvwxyz"sv;

        // 在 shm obj 的名字中包含一个顺序递增的计数字段:
        constinit static std::atomic_uint cnt;
        const auto base_name =
#ifdef __cpp_lib_format
            std::format(
                "{}-{:06}", prefix,
                1 + cnt.fetch_add(1, std::memory_order_relaxed)
            )
#else
            prefix + "-"s
            + [&] {
                auto seq_id = std::to_string(
                    1 + cnt.fetch_add(1, std::memory_order_relaxed)
                );
                while (seq_id.length() != 6)
                    seq_id.insert(seq_id.cbegin(), '0');
                return seq_id;
            }()
#endif
        ;

        // 由于 (取名 + 构造 shm) 不是原子的, 可能在构造 shm obj 时
        // 和已有的 shm 的名字重合, 或者同时多个进程同时创建了同名 shm.
        // 所以生成的名字必须足够长, 📉降低碰撞率.
        static const auto suffix =
#ifdef __cpp_lib_ranges_fold
            std::ranges::fold_left(
                std::views::iota(("/dev/shm/" + base_name + '.').length(), 255u)
                | std::views::transform([
                    available_chars,
                    gen = std::mt19937{std::random_device{}()},
                    distri = std::uniform_int_distribution<>{0, available_chars.length()-1}
                ](...) mutable {
                    return available_chars[distri(gen)];
                }),
                ""s, std::plus<>{}
            )
#else
            [&] {
                auto gen = std::mt19937{std::random_device{}()};
                auto distri = std::uniform_int_distribution<>{0, available_chars.length()-1};
                std::string suffix;
                for (auto current_len = ("/dev/shm/" + base_name + '.').length(); current_len++ != 255u; )
                    suffix += available_chars[distri(gen)];
                return suffix;
            }()
#endif
        ;

        return '/' + base_name + '.' + suffix;
    }
}


#define IPCATOR_LOG_ALLO_OR_DEALLOC(color)  void(  \
    DEBUG_ && std::clog <<  \
        std::source_location::current().function_name() + "\n"s  \
        + std::vformat(  \
            (color == "green"sv ? "\033[32m" : "\033[31m")  \
            + "\tsize={}, &area={}, alignment={}\033[0m"s,  \
            std::make_format_args(size, (const void *const&)area, alignment)  \
        ) + '\n'  \
)


/**
 * @brief Allocator: 给⬇️游分配共享内存块.  本质上是一系列 `Shared_Memory<true>` 的集合.
 * @details 粗粒度的共享内存分配器, 每次固定分配单块共享内存.  持有所有权.
 * @tparam set_t 用来存储 `Shared_Memory<true>` 的集合类型.  可选值:
 *         - `std::set`: 给定任意的对象指针, 可以 **快速** 确定
 *                       该对象位于哪块 `Shared_Memory<true>` 上.
 *                       (See `ShM_Resource::find_arena`.)
 *         - `std::unordered_set`: 记录最近一次分配的共享内存的首地址.
 *                                 (See `ShM_Resource::last_inserted`.)
 */
template <template <typename... T> class set_t = std::set>
class ShM_Resource: public std::pmr::memory_resource {
    public:
        /**
         * @cond
         * 请 Doxygen 忽略该变量, 因为它总是显示初始值 (而我不想这样).
         */
        static constexpr bool using_ordered_set = []
#if __cplusplus <= 202002L
            ()
#endif
        consteval {
            if constexpr (requires {
                requires std::same_as<set_t<int>, std::set<int>>;
            })
                return true;
            else if constexpr (std::is_same_v<set_t<int>, std::unordered_set<int>>)
                return false;
            else {
#if !__GNUG__ || __GNUC__ >= 13  // P2593R1
                static_assert(false, "只接受 ‘std::{,unordered_}set’ 作为注册表格式.");
#elifdef __cpp_lib_unreachable
                std::unreachable();
#else
                assert(false);
                return bool{};
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
                    return std::data(shm_or_ptr.get_area());
            }

            /* As A Comparator */
            bool operator()(const auto& a, const auto& b) const noexcept {
                const auto pa = get_addr(a), pb = get_addr(b);

                if constexpr (using_ordered_set)
                    return pa < pb;
                else
                    return pa == pb;
            }
            /* As A Hasher */
            auto operator()(const auto& shm) const noexcept
            -> std::size_t {
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
        /**
         * @brief 分配共享内存块.
         * @param size 要分配的 `Shared_Memory` 的大小.
         * @param alignment 共享内存对象映射到地址空间时的对齐要求.  可选.
         * @details 每次调用该函数, 都会创建 **一整块** 新的 `Shared_Memory<true>`.
         *          因此这是粒度最粗的分配器.
         * @return 分配到的共享内存块的首地址.
         * @note 一般不直接调用此函数, 而是:
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto _ = allocator.allocate(12); _ = allocator.allocate(34, 8);
         * //                 ^^^^^^^^                    ^^^^^^^^
         * allocator = ShM_Resource<std::unordered_set>{};
         * _ = allocator.allocate(56), _ = allocator.allocate(78, 16);
         * //            ^^^^^^^^                    ^^^^^^^^
         * ```
         */
        void *do_allocate(
            const std::size_t size, const std::size_t alignment
        ) noexcept(false) override {
            if (alignment > getpagesize() + 0u) [[unlikely]] {
                struct TooLargeAlignment: std::bad_alloc {
                    const std::string message;
                    TooLargeAlignment(const std::size_t demanded_alignment) noexcept
                    : message{
                        std::format(
                            "请求分配的字节数组要求按 {} 对齐, 超出了页表大小 (即 {}).",
                            demanded_alignment,
                            getpagesize()
                        )
                    } {}
                    const char *what() const noexcept override {
                        return message.c_str();
                    }
                };
                throw TooLargeAlignment{alignment};
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
#if __GNUC__ == 10  // GCC 的 bug, 见 ipcator#2.
                    &*
#endif
                    inserted
                );

            const auto area = std::data(
                const_cast<Shared_Memory<true>&>(*inserted).get_area()
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }
        /**
         * @brief 回收共享内存.
         * @param area 要回收的共享内存的起始地址.
         * @param size 若定义了 `NDEBUG` 宏, 传入任意值即可; 否则,
         *             表示共享内存的大小, 必须与请求分配 (`allocate`)
         *             时的大小 (`size`) 一致.
         * @param alignment 传入随意值即可.
         * @note 一般不直接调用此函数, 而是:
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto addr = allocator.allocate(4096);
         * allocator.deallocate(addr, 0, 0);
         * //        ^^^^^^^^^^
         * ```
         */
        void do_deallocate(
            void *const area,
            const std::size_t size [[maybe_unused]],
            const std::size_t alignment [[maybe_unused]]
        ) override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");

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
            assert(
                size <= std::size(whatcanisay_shm_out.get_area())
                && std::size(whatcanisay_shm_out.get_area()) <= ceil_to_page_size(size)
            );
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
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
        : resources{
            std::move(other.resources)
        } {
            if constexpr (!using_ordered_set)
                this->last_inserted = std::move(other.last_inserted);
        }

#if __GNUC__ == 15 || (16 <= __clang_major__ && __clang_major__ <= 20)  // ipcator#3
        friend class ShM_Resource<std::set>;
#endif
        /**
         * @brief 同上.
         * @details `ShM_Resource<std::set>` 从 `ShM_Resource<std::unordered_set>` 移动构造:
         * ```
         * ShM_Resource<std::unordered_set> a;
         * ShM_Resource<std::set> b{std::move(a)};
         * ```
         */
        ShM_Resource(ShM_Resource<std::unordered_set>&& other) requires(using_ordered_set)
        : resources{[
#if __GNUC__ != 15 && (__clang_major__ < 16 || 20 < __clang_major__)  // ipcator#3
            other_resources=std::move(other).get_resources()
#else
            &other_resources=other.resources
#endif
            , this
        ]
#if __cplusplus <= 202002L
         ()
#endif
            mutable {
                decltype(this->resources) resources;

                while (!std::empty(other_resources))
                    resources.insert(std::move(
                        other_resources
                        .extract(std::cbegin(other_resources))
                        .value()
                    ));

                return resources;
            }()
        } {}

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
         * @note 演示 强制回收所有来自该分配器的共享内存:
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto p1 = allocator.allocate(1),
         *      p2 = allocator.allocate(2),
         *      p3 = allocator.allocate(3);
         * allocator = {};  // 清零.
         * ```
         */
        auto& operator=(ShM_Resource other) {
            swap(*this, other);
            return *this;
        }
        ~ShM_Resource() noexcept {
            if constexpr (DEBUG_) {
                // 显式删除以触发日志输出.
                while (!std::empty(this->resources)) {
                    const auto& area = std::cbegin(this->resources)->get_area();
                    this->deallocate(
                        const_cast<unsigned char *>(std::data(area)),
                        std::size(area)
                    );
                }
            }
        }

        /**
         * @brief 获取 `Shared_Memory<true>` 的集合的引用.
         * @details 它包含了所有的 已分配而未 deallocated 的共享内存块.
         * @note 常见的用法是 遍历它的返回值, 从而得知某个对象位于哪块共享内存:
         * ```
         * auto allocator = ShM_Resource<std::unordered_set>{};
         * auto addr = (std::uint8_t *)allocator.allocate(sizeof(int));
         * const Shared_Memory<true> *pshm;
         * for (auto& shm : allocator.get_resources())
         *     if (std::data(shm) <= addr && addr < std::data(shm) + std::size(shm)) {
         *         pshm = &shm;
         *         break;
         *     }
         * assert(pshm == allocator.last_inserted);
         * // 或称 ‘last_allocated’ ^^^^^^^^^^^^^
         * ```
         * @see ShM_Resource::last_inserted
         */
        auto get_resources(
#ifndef __cpp_explicit_this_parameter
        ) const -> decltype(auto) {
            auto&& self = std::move(const_cast<ShM_Resource&>(*this));
#else
            this auto&& self
        ) -> decltype(auto) {
#endif
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

        /**
         * @brief 将 self 以类似 JSON 的格式输出.  调试用.
         * @note 也可用 `std::println("{}", self)` 打印 (since C++23).
         */
        friend auto operator<<(std::ostream& out, const ShM_Resource& resrc)
        -> decltype(auto) {
            return out << std::format("{}", resrc);
        }

        /**
         * @brief 查询给定对象位于哪块共享内存 (`Shared_Memory<true>`).
         * @param obj 被查询的对象的指针 (可以是 `void *`).
         * @return 对象所在的共享内存块的引用.
         * @note 仅当类的模板参数 `set_t` 是 `std::set` 时, 才 **存在** 此方法.
         *       因为当使用 `std::unordered_set` 时, 不存在高效的反查算法.
         * @warning `obj` 必须确实位于来自此分配器给出的共享内存中, 否则结果未定义.
         * @note example:
         * ```
         * auto allocator = ShM_Resource<std::set>{};
         * auto area = (std::uint8_t *)allocator.allocate(100);
         * int& i = (int&)area[5],
         *    & j = (int&)area[5 + sizeof(int)],
         *    & k = (int&)area[5 + 2 * sizeof(int)];
         * assert(
         *     std::data(allocator.find_arena(&i)) == std::data(&allocator.find_arena(&j))
         *     && std::data(&allocator.find_arena(&j)) == std::data(&allocator.find_arena(&k))
         * );  // 都在同一块共享内存上.
         * ```
         */
        auto find_arena(const auto *const obj) const
        -> const auto& requires(using_ordered_set) {
            const auto& shm = *(
                --this->resources.upper_bound((const void *)obj)
            );
            assert(
                (const unsigned char *)obj + [&]{
                    if constexpr (requires {*obj;})
                        return sizeof *obj;
                    else
                        return 1;
                }() <= &*std::cend(shm.get_area())
            );

            return shm;
        }
        /**
         * @brief 指向最近一次 allocate 的共享内存块 (`Shared_Memory<true>`).
         * @note 仅当类的模板参数 `set_t` 是 `std::unordered_set` 时, 此成员变量才 **有意义**.
         * @details `ShM_Resource<std::unordered_set>` 分配器在 allocate 共享内存时, 下游
         *          只能拿到这块共享内存的首地址.  也无法 *高效地* 根据地址 反查出它指向
         *          哪块共享内存 (特别是当你需要知道这块共享内存块的 name 时).  该变量在
         *          一定程度上缓解这个问题, 因为你通常只需要知道刚刚分配的共享内存的信息.
         * @note example:
         * ```
         * auto allocator = ShM_Resource<std::unordered_set>{};
         * auto addr = allocator.allocate(1);
         * assert( (std::uint8_t *)addr == std::data(*allocator.last_inserted) );
         * ```
         */
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


template <template <typename... T> class set_t>
struct
#if !(defined __GNUG__ && __GNUC__ <= 15)
    ::
#endif
    std::formatter<ShM_Resource<set_t>> {
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
            | std::views::transform([](auto& shm) {return std::format("{}", shm);})
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

        if constexpr (ShM_Resource<set_t>::using_ordered_set)
            return std::vformat_to(
                context.out(),
                R":({{ "resources": {{ "|size|": {} }}, "constructor()": "ShM_Resource<std::set>" }}):",
                std::make_format_args(size)
            );
        else {
            // 对于 ‘ShM_Resource<std::unordered_set>’, 因为有字段
            // ‘last_inserted’ (指针), 必须保证它没有 dangling.
            // 也就是, 得排除 ‘size’ 为 0 的情形.  这没有关系, 因为
            // 查看一个空 ‘ShM_Resource’ 的 JSON 没什么意义.
            assert(size);
#if __has_cpp_attribute(assume)
            [[assume(size)]];
#endif
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
    "last_inserted":
{},
    "constructor()": "ShM_Resource<std::unordered_set>"
}}):",
                std::make_format_args(
                    size,
                    resources_values,
                    *resrc.last_inserted
                )
            );
        }
    }
};


/**
 * @brief Allocator: 单调增长的共享内存 buffer.  它的 allocation 是链式的,
 *        其⬆️游是 `ShM_Resource<std::unordered_set>` 并拥有⬆️游的所有权.
 * @details 维护一个 buffer, 其中包含若干共享内存块, 因此 buffer 也不是连续的.  Buffer 的
 *          大小单调增加, 它仅在析构 (或手动调用 `Monotonic_ShM_Buffer::release`) 时释放
 *          分配的内存.  它的意图是提供非常快速的内存分配, 并于之后一次释放的情形 (进程退出
 *          也是适用的场景).
 * @note 在 <br />
 *       ▪️ **不需要 deallocation**, 分配的共享内存区域
 *         会一直被使用 <br />
 *       ▪️ 或 临时发起 **多次** allocation 请求, 并在
 *         **不久后就 *全部* 释放掉** <br />
 *       ▪️ 或 **注重时延** 而 内存占用相对不敏感 <br />
 *       的场合下, 有充分的理由使用该分配器.  因为它非常快, 只做
 *       简单的分配.  (See `Monotonic_ShM_Buffer::do_allocate`.)
 */
struct Monotonic_ShM_Buffer: std::pmr::monotonic_buffer_resource {
        /**
         * @brief Buffer 的构造函数.
         * @param initial_size Buffer 的初始长度, 越大的 size **保证** 越小的均摊时延.
         * @details 初次 allocation 是惰性的💤, 即构造时并不会立刻创建 buffer.
         * @note Buffer 的总大小未必是📄页表大小的整数倍, 但
         *       `initial_size` 最好是.  (该构造函数会自动将
         *       `initial_size` 用  `ceil_to_page_size(const std::size_t)`
         *       向上取整.)
         * @warning `initial_size` 不可为 0.
         */
        Monotonic_ShM_Buffer(const std::size_t initial_size = 1)
        : monotonic_buffer_resource{
            ceil_to_page_size(initial_size),
            new ShM_Resource<std::unordered_set>,
        } {
            assert(initial_size);
#if __has_cpp_attribute(assume)
            [[assume(initial_size)]];
#endif
        }
        ~Monotonic_ShM_Buffer() noexcept {
            this->release();
            delete this->monotonic_buffer_resource::upstream_resource();
        }

        /**
         * @brief 获取指向⬆️游资源的指针.
         * @note example:
         * ```
         * auto buffer = Monotonic_ShM_Buffer{};
         * auto addr = (std::uint8_t *)buffer.allocate(100);
         * const Shared_Memory<true>& shm = *buffer.upstream_resource().last_inserted;
         * assert(
         *     std::data(shm) <= addr
         *     && addr < std::data(shm) + std::size(shm)
         * );  // 新划取的区域一定位于 `upstream_resource()` 最近一次分配的内存块中.
         * ```
         */
        auto upstream_resource() const -> const auto * {
            return static_cast<ShM_Resource<std::unordered_set> *>(
                this->monotonic_buffer_resource::upstream_resource()
            );
        }

    protected:
        /**
         * @brief 在共享内存区域中分配内存.
         * @param size 从共享内存区域中划取的大小.
         * @param alignment 可选.
         * @details 首先检查 buffer 的剩余空间, 如果不够, 则向⬆️游
         *          获取新的共享内存块 (每次向⬆️游申请的块的大小以
         *          几何级数增加).  然后, 从剩余空间中从中划出一段.
         * @note 一般不直接调用此函数, 而是 `allocate`, 所以用法
         *       类似 `ShM_Resource` (见 `ShM_Resource::do_allocate`).
         */
        void *do_allocate(
            const std::size_t size, const std::size_t alignment
        ) override {
            const auto area = this->monotonic_buffer_resource::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }

        /**
         * @brief 无操作.
         * @details Buffer 在分配时根本不追踪所有 allocation 的位置,
         *          它单纯地增长, 以此提高分配速度.  因此也无法根据
         *          指定位置响应 deallocation.
         */
        void do_deallocate(
            void *const area, const std::size_t size, const std::size_t alignment
        ) noexcept override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");

            // 虚晃一枪; actually no-op.
            // ‘std::pmr::monotonic_buffer_resource::deallocate’ 的函数体其实是空的.
            this->monotonic_buffer_resource::do_deallocate(area, size, alignment);
        }
#ifdef IPCATOR_IS_DOXYGENING  // stupid doxygen
        /**
         * @brief 强制释放所有已分配而未收回的内存.
         * @details 将当前缓冲区和下个缓冲区的大小设置为其构造时的 `initial_size`.
         */
        void release();
#endif
};


/**
 * @brief Allocator: 共享内存池.  它的 allocation 是链式的, 其
 *        ⬆️游是 `ShM_Resource<std::set>` 并拥有⬆️游的所有权.
 *        它在析构时会调用 `ShM_Pool::release` 释放所有内存资源.
 * @tparam sync 是否线程安全.  设为 false 时, 🚀速度更快.
 * @details ▪️ 持有若干块共享内存 (`Shared_Memory<true>`), 每块被视为一个 pool.  一个
 *            pool 会被切割成若干 chunks, 每个 chunk 是特定 size 的整数倍.  <br />
 *          ▪️ 当响应 size 大小的内存申请时, 从合适的 chunk 中划取即可.  <br />
 *          ▪️ 剩余空间不足时, 会创建新的 pool 以取得更多的 chunks.  <br />
 *          ▪️ size 可以有上限值, 大于此值的 allocation 请求会通过直接创建
 *            `Shared_Memory<true>` 的方式响应, 而不再执行池子算法.  <br />
 *          目标是减少内存碎片, 首先尝试在相邻位置分配 block.
 * @note 在不确定要使用何种共享内存分配器时, 请选择该类.
 *       即使对底层实现感到迷惑也能直接拿来使用.
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
        void *do_allocate(
            const std::size_t size, const std::size_t alignment
        ) override {
            const auto area = this->midstream_pool_t::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }

        void do_deallocate(
            void *const area, const std::size_t size, const std::size_t alignment
        ) override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");
            this->midstream_pool_t::do_deallocate(area, size, alignment);
        }

    public:
        /**
         * @brief 构造 pools
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
        ~ShM_Pool() noexcept {
            this->release();
            delete this->midstream_pool_t::upstream_resource();
        }

        /**
         * @brief 获取指向⬆️游资源的指针.
         * @note example:
         * ```
         * auto pools = ShM_Pool<false>{};
         * auto addr = (std::uint8_t *)pools.allocate(100);
         * auto& obj = (std::array<char, 10>&)addr[50];
         * const Shared_Memory<true>& shm = pools.upstream_resource().find_arena(&obj);
         * assert(
         *     std::data(shm) <= (std::uint8_t *)&obj
         *     && (std::uint8_t *)&obj < std::data(shm) + std::size(shm)
         * );
         * ```
         */
        auto upstream_resource() const -> const auto * {
            return static_cast<ShM_Resource<std::set> *>(
                this->midstream_pool_t::upstream_resource()
            );
        }

#ifdef IPCATOR_IS_DOXYGENING  // stupid doxygen
        /**
         * @brief 强制释放所有已分配而未收回的内存.
         */
        void release();
        /**
         * @brief 从共享内存中分配 block
         * @param alignment 对齐要求.
         */
        void *allocate(
            std::size_t size, std::size_t alignment = alignof(std::max_align_t)
        );
        /**
         * @brief 回收 block
         * @param area `allocate` 的返回值
         * @param size 仅在未定义 `NDEBUG` 宏时调试用.  发布时, 可以将实参替换为任意常量.
         * @details 回收 block 之后可能会导致某个 pool (`Shared_Memory<true>`) 处于
         *          完全闲置的状态, 此时可能会触发🗑️GC, 也就是被析构, 然而时机是
         *          不确定的, 由 `std::pmr::unsynchronized_pool_resource` 的实现决定.
         */
        void deallocate(void *area, std::size_t size);
#endif
};


/**
 * @brief 表示共享内存分配器
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
    } || requires {
        { *ipcator.last_inserted       } -> std::same_as<const Shared_Memory<true>&>;
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
 * @brief 通用的跨进程消息读取器
 * @details `ShM_Reader<writable>` 内部缓存一系列 `Shared_Memory<false, writable>`.
 *          每当遇到不位于任何已知的 `Shared_Memory` 上的消息时, 都将📂新建
 *          `Shared_Memory` 并加入缓存.  后续的读取将不需要重复创建相同的共享内存
 *          🎯映射.
 * @tparam writable 读到消息之后是否允许对其进行修改
 */
template <auto writable=false>
struct ShM_Reader {
        /**
         * @brief 获取消息的引用
         * @param shm_name 共享内存的路径名
         * @details 基于共享内存的 IPC 在传递消息时, 靠 共享内存的路径名
         *          和 消息体在共享内存中的偏移量 决定消息的位置.
         * @note example
         * ```
         * auto rd = ShM_Reader{};
         * auto& arr_from_other_proc
         *     = rd.template read<std::array<char, 32>>("/some-shm", 10);
         * ```
         */
        template <typename T>
        auto& read(
            const std::string_view shm_name, const std::size_t offset
        ) {
            return *(T *)(
                std::data(this->select_shm(shm_name).get_area())
                + offset
            );
        }

        auto select_shm(const std::string_view name) -> const
#if __GNUC__ == 15 || (16 <= __clang_major__ && __clang_major__ <= 20)  // ipcator#3
            Shared_Memory<false>
#else
            auto
#endif
        & {
            if (
                const auto shm =
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
                shm != std::cend(this->cache)
            )
                return *shm;
            else {
                const auto [inserted, ok] = this->cache.emplace(std::string{name});
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
                if constexpr (std::is_same_v<
                    std::decay_t<decltype(shm_or_name)>,
                    std::string_view
                >)
                    return shm_or_name;
                else
                    return shm_or_name.get_name();
            }

            /* Hash */
            auto operator()(const auto& shm) const noexcept
            -> std::size_t {
                const auto name = get_name(shm);
                return std::hash<std::decay_t<decltype(name)>>{}(name);
            }
            /* KeyEqual */
            bool operator()(const auto& a, const auto& b) const noexcept {
                return get_name(a) == get_name(b);
            }
        };
        std::unordered_set<Shared_Memory<false, writable>, ShM_As_Str, ShM_As_Str> cache;
        // TODO: LRU GC
};
