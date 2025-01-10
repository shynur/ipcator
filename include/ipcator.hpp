#pragma once
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
     * @brief 将数字向上取整, 成为📄页表大小的整数倍.
     * @details 用该返回值设置共享内存的大小, 可以提高内存♻️利用率.
     */
    inline auto ceil_to_page_size(const std::size_t min_length)
    -> std::size_t {
        const auto current_num_pages = min_length / getpagesize();
        const bool need_one_more_page = min_length % getpagesize();
        return (current_num_pages + need_one_more_page) * getpagesize();
    }
}


/**
 * @tparam creat 是否新建文件, 用来作为共享内存
 * @tparam writable 是否允许在共享内存区域写数据
 * @brief 对由指定目标文件映射而来的内存区域的抽象
 * @details 当前的共享内存模型是:
 *          writer 创建一个文件, 接着将它映射到内存中, writer 对这块共享内存默认是可读可写的;
 *          reader 通过 writer 创建的文件名找到目标文件, 然后将其映射至自身进程中, 默认只读.
 * @note 文档约定: 称 `Shared_Memory` **[*creat*=true]** 实例为 writer,
 *                    `Shared_Memory` **[*creat*=false]** 实例为 reader.
 */
template <bool creat, auto writable = creat>
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
         * @brief 作为 writer 创建一块共享内存 并 映射到 RAM 中, 可供其它进程读写.
         * @param name 共享内存是被映射到 RAM 中的文件, 这是目标文件名.
         *             形如 `/斜杠打头-没有空格`.  建议使用 `generate_shm_UUName()`
         *             自动生成该路径名.
         * @param size 目标文件的大小.  空间♻️利用率最高的做法是和📄页表对齐,
         *             建议使用 `ceil_to_page_size(std::size_t)` 自动生成.
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
         * @brief 作为 reader 打开📂一份文件, 将其映射到 RAM 中.
         * @param name 指定目标文件的路径.  这个路径通常是事先约定的,
         *             或者从其它实例的 `Shared_Memory::get_name()` 方法获取.
         * @details Reader 无法指定 ‘size’, 因为这是🈚意义的.  Reader 打开的是
         *          已经存在于 RAM 中的目标文件, 占用大小已经确定, 更小的 ‘size’
         *          并不能节约系统资源.
         *          Reader 也可能允许向共享内存写入数据, 要求由
         *          `Shared_Memory` **[writable=true]** 构造.
         * @note 没有定义 `NDEBUG` 宏时, reader 会等待 (片刻) 直至目标文件被 writer 创建.
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
         */
        Shared_Memory(Shared_Memory&& other) noexcept
        : name{std::move(other.name)}, area{
            // Self 的析构函数靠 ‘area’ 是否为空来判断
            // 是否持有所有权, 所以此处需要强制置空.
            std::exchange(other.area, {})
        } {}
        /**
         * @brief 在进程中映射和 `other` 相同的目标文件.
         * @details 这两块共享内存的数据是同步的, 但地址不同.
         * @note `Shared_Memory` **[writable=true]** 无法从 `Shared_Memory` **[writable=false]** 拷贝构造.
         */
        template <bool other_creates, bool writable_other>
            requires(writable ? writable_other : true)
        Shared_Memory(const Shared_Memory<other_creates, writable_other>& other)
            requires(!creat): Shared_Memory{other.get_name()} {}
        /**
         * @brief 同上.
         * @note Writer 之间是无法拷贝构造的, 仅允许移动.
         */
        Shared_Memory(const Shared_Memory& other)
            requires(!creat): Shared_Memory{other.get_name()} {
            // 同上.  但是 copy constructor 必须显式声明.
        }
        /**
         * @brief 交换两个实例.
         */
        friend void swap(Shared_Memory& a, decltype(a) b) noexcept {
            std::swap(a.name, b.name);
            std::swap(a.area, b.area);
        }
        /**
         * @brief 赋值语义.  左侧实例原本持有的共享内存区域会被卸载.
         */
        auto& operator=(Shared_Memory other) {
            swap(*this, other);
            return *this;
        }
        /**
         * @brief 将被映射的共享内存区域从自身进程中卸载.
         * @details 在 writer 卸载共享内存块之后, 其它 reader 仍可访问这片区域,
         *          但任何进程无法再执行新的映射 (即 创建新的 reader).  当 all
         *          the readers 也析构掉了, 目标文件的引用计数归零, 将被释放.
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
         * @brief 返回路径名, which is 共享内存区域对应的目标文件的路径.
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
            if constexpr (!writable)
                return self.area;
            else
                if constexpr (std::is_const_v<std::remove_reference_t<decltype(self)>>) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
                    return reinterpret_cast<const std::span<const unsigned char>&>(self.area);
#pragma GCC diagnostic pop
                } else
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
                0666
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
                    (writable ? PROT_WRITE : 0) | PROT_READ,
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


inline namespace literals {
    /**
     * @brief 创建 `Shared_Memory` 实例的快捷方式.
     * @details
     * - `"/filename"_shm[size]`: 创建指定大小的命名的共享内存, 以读写模式映射.
     * - `+"/filename"_shm`: 不创建, 只将目标文件以读写模式映射至本地.
     * - `-"/filename"_shm`: 不创建, 只将目标文件以只读模式映射至本地.
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
     * @note 格式为 `/固定前缀-原子自增的计数字段-进程专属的标识符`
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
 * 按需创建并拥有若干 ‘Shared_Memory<true>’,
 * 以向⬇️游提供 shm 页面作为 memory resource.
 */
template <template <typename... T> class set_t = std::set>
class ShM_Resource: public std::pmr::memory_resource {
    public:
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
        }();
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
        void do_deallocate(
            void *const area, const std::size_t size, const std::size_t alignment [[maybe_unused]]
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
        ShM_Resource() noexcept = default;
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
        ShM_Resource(ShM_Resource<std::unordered_set>&& other) requires(using_ordered_set)
        : resources{[
#if __GNUC__ != 15 && (__clang_major__ < 16 || 20 < __clang_major__)  // ipcator#3
            other_resources=std::move(other).get_resources()
#else
            &other_resources=other.resources
#endif
            , this
        ]() mutable {
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

        friend void swap(ShM_Resource& a, decltype(a) b) noexcept {
            std::swap(a.resources, b.resources);

            if constexpr (!using_ordered_set)
                std::swap(a.last_inserted, b.last_inserted);
        }
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
                return
#ifdef __cpp_explicit_this_parameter
                    std::move
#endif
                    (self.resources);
        }

        friend auto operator<<(std::ostream& out, const ShM_Resource& resrc)
        -> decltype(auto) {
            return out << std::format("{}", resrc);
        }

        /**
         * 查询对象 (‘obj’) 位于哪个 ‘Shared_Memory’ 中.
         * 但首先, 你得确保 ‘obj’ 确实在 self 的注册表中.
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
         * 记录最近一次创建的 ‘Shared_Memory’.
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
 * 以 ‘ShM_Resource’ 为⬆️游的单调增长 buffer.  优先使用⬆️游上次
 * 下发内存时未能用到的区域响应 ‘allocate’, 而不是再次申请内存资源.
 */
struct Monotonic_ShM_Buffer: std::pmr::monotonic_buffer_resource {
        /**
         * 设定缓冲区的初始大小, 但实际是惰性分配的💤.
         * ‘initial_size’ 如果不是📄页表大小的整数倍,
         * 几乎_一定_会浪费空间.
         * ‘initial_size’ 必须是*正数*!
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

        auto upstream_resource() const -> const auto * {
            return static_cast<ShM_Resource<std::unordered_set> *>(
                this->monotonic_buffer_resource::upstream_resource()
            );
        }

    protected:
        void *do_allocate(
            const std::size_t size, const std::size_t alignment
        ) override {
            const auto area = this->monotonic_buffer_resource::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }

        void do_deallocate(
            void *const area, const std::size_t size, const std::size_t alignment
        ) noexcept override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");

            // 虚晃一枪; actually no-op.
            // ‘std::pmr::monotonic_buffer_resource::deallocate’ 的函数体其实是空的.
            this->monotonic_buffer_resource::do_deallocate(area, size, alignment);
        }
};


/**
 * 以 ‘ShM_Resource’ 为⬆️游的内存池.  目标是减少内存碎片, 首先尝试
 * 在相邻位置分配请求的资源, 优先使用已分配的空闲区域.  当有大片
 * 连续的内存块处于空闲状态时, 会触发🗑️GC, 将资源释放并返还给⬆️游,
 * 时机是不确定的.
 * 模板参数 ‘sync’ 表示是否线程安全.  令 ‘sync=false’ 有🚀更好的性能.
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

        auto upstream_resource() const -> const auto * {
            return static_cast<ShM_Resource<std::set> *>(
                this->midstream_pool_t::upstream_resource()
            );
        }
};


template <class ipcator_t>
concept IPCator = (
    std::same_as<ipcator_t, Monotonic_ShM_Buffer>
    || std::same_as<ipcator_t, ShM_Pool<true>>
    || std::same_as<ipcator_t, ShM_Pool<false>>
) && requires(ipcator_t ipcator) {  // PS, 这是个冗余条件, 但可以给 LSP 提供信息.
    /* 可公开情报 */
    requires std::derived_from<ipcator_t, std::pmr::memory_resource>;
    requires requires {
        {  ipcator.upstream_resource()->find_arena(new int) } -> std::same_as<const Shared_Memory<true>&>;
    } || requires {
        { *ipcator.upstream_resource()->last_inserted       } -> std::same_as<const Shared_Memory<true>&>;
    };
};
static_assert(
    IPCator<Monotonic_ShM_Buffer>
    && IPCator<ShM_Pool<true>>
    && IPCator<ShM_Pool<false>>
);


/**
 * 给定 共享内存对象的名字 和 偏移量, 读取对应位置上的 对象.  每当
 * 遇到一个陌生的 shm obj 名字, 都需要打开这个新的 📂 shm obj, 并
 * 将其映射🎯到进程的地址空间.  默认情况下, 这些被映射的片段 (即类
 * ‘Shared_Memory<false>’ 的实例) 会缓存起来, 直到数目达到预设策略
 * 所指定的上限值 (例如, 用 LRU 算法时可以限制缓存的最大值).
 */
struct ShM_Reader {
        template <typename T>
        auto read(
            const std::string_view shm_name, const std::size_t offset
        ) -> const auto& {
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
        std::unordered_set<Shared_Memory<false>, ShM_As_Str, ShM_As_Str> cache;
        // TODO: LRU GC
};
