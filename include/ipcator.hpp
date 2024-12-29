#pragma once
#include <algorithm>  // ranges::fold_left
#include <atomic>  // atomic_uint, atomic_thread_fence, memory_order_release, memory_order_acquire
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
#   include "fmt/core.h"
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
# else
    namespace std {
        namespace source_location {
            struct current {
                auto function_name() const { return "某函数"; }
            };
        }
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


namespace {
    constexpr auto DEBUG =
#ifdef NDEBUG
        false
#else
        true
#endif
    ;
}


namespace {
    /**
     * 将数字向上取整, 成为📄页表大小的整数倍.
     * 像这样设置共享内存的大小, 可以提高内存♻️利用率.
     */
    inline auto ceil_to_page_size(const std::size_t min_length)
    -> std::size_t {
        const auto current_num_pages = min_length / getpagesize();
        const bool need_one_more_page = min_length % getpagesize();
        return (current_num_pages + need_one_more_page) * getpagesize();
    }
}


namespace {
    /**
     * 给定 shared memory object 的名字, 创建/打开
     * 📂 shm obj, 并将其映射到进程自身的地址空间中.
     * - 对于 writer, 使用 `map_shm<true>(name,size)->void*`,
     *   其中 ‘size’ 是要创建的 shm obj 的大小;
     * - 对于 reader, 使用 `map_shm<>(name)->{addr,length}`.
     */
    template <bool creat = false>
    constexpr auto map_shm = [](const auto resolve) consteval {
        return [=]
#if __cplusplus >= 202302L
            [[nodiscard]]
#endif
        (
            const std::string& name, const std::unsigned_integral auto... size
        ) requires (sizeof...(size) == creat) {

            assert("/dev/shm"s.length() + name.length() <= 255);
            const auto fd = [](const auto do_open) {
                if constexpr (creat)
                    return do_open();
                else {
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
                    if (opening.wait_for(0.5s) == std::future_status::ready) [[likely]]
                        return opening.get();
                    else
                        if constexpr (DEBUG)
                            std::exit(0);
                        else
                            assert(!"shm obj 仍未被创建, 导致 reader 等待超时");
                }
            }(std::bind(shm_open, name.c_str(), creat ? O_CREAT|O_EXCL|O_RDWR : O_RDONLY, 0666));
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

            return resolve(
                fd,
                [&] {
                    if constexpr (creat)
                        return
#ifdef __cpp_pack_indexing
                            size...[0]
#else
                            [](const auto size, ...) {return size;}(size...)
#endif
                        ;
                    else {
                        struct stat shm;
                        fstat(fd, &shm);
                        return shm.st_size;
                    }
                }()
            );
        };
    }([](const auto fd, const std::size_t size) {
        assert(size);
        const auto area_addr = mmap(
            nullptr, size,
            (creat ? PROT_WRITE : 0) | PROT_READ,
            MAP_SHARED | (!creat ? MAP_NORESERVE : 0),
            fd, 0
        );
        close(fd);  // 映射完立即关闭, 对后续操作🈚影响.
        assert(area_addr != MAP_FAILED);

        if constexpr (creat)
            return area_addr;
        else {
            const struct {
                const void *addr;
                std::size_t length;
            } area{area_addr, size};
            return area;
        }
    });
}


/**
 * 表示1️⃣块被映射的共享内存区域.
 * 对于 writer, 它还拥有对应的共享内存对象的所有权.
 */
template <bool creat>
class Shared_Memory {
        std::string name;  // Shared memory object 的名字, 格式为 “/Abc123”.
        std::span<
            std::conditional_t<
                creat,
                unsigned char, const unsigned char
            >
        > area;
    public:
        /**
         * 创建名字为 ‘name’ (用 ‘generate_shm_UUName’ 自动生成最方便), 大小为 ‘size’
         * (建议用 ‘ceil_to_page_size’ 向上取整) 的共享内存对象, 并映射到进程的地址空间中.
         */
        Shared_Memory(const std::string name, const std::size_t size) requires(creat)
        : name{name}, area{
            (unsigned char *)map_shm<creat>(name, size),
            size,
        } {
            if constexpr (DEBUG) {
                // 既读取又写入✏, 以确保这块内存被正确地映射了, 且已取得读写权限.
                for (auto& byte : this->area)
                    byte ^= byte;

                std::clog << std::format("创建了 Shared_Memory: \033[32m{}\033[0m", *this) + '\n';
            }
        }
        /**
         * 根据名字打开对应的 shm obj, 并映射到进程的地址空间中.  不允许 reader
         * 指定 ‘size’, 因为这是🈚意义的.  Reader 打开的是已经存在于内存中的
         * shm obj, 占用大小已经确定, 更小的 ‘size’ 并不能节约系统资源.
         */
        Shared_Memory(const std::string name) requires(!creat)
        : name{name}, area{
            [&]
#if __cplusplus <= 202002L
               ()
#endif
            -> decltype(this->area) {
                const auto [addr, length] = map_shm<>(name);
                return {
                    (const unsigned char *)addr,
                    length,
                };
            }()
        } {
            if constexpr (DEBUG) {
                // 只读取, 以确保这块内存被正确地映射了, 且已取得读权限.
                for (auto byte : std::as_const(this->area))
                    std::ignore =
#ifdef __cpp_auto_cast
                        auto{byte}
#else
                        decltype(byte){byte}
#endif
                    ;

                std::clog << std::format("创建了 Shared_Memory: \033[32m{}\033[0m\n", *this) + '\n';
            }
        }
        Shared_Memory(Shared_Memory&& other) noexcept
        : name{std::move(other.name)}, area{
            // Self 的析构函数靠 ‘area’ 是否为空来判断
            // 是否持有所有权, 所以此处需要强制置空.
            std::exchange(other.area, {})
        } {}
        /**
         * 在进程地址空间的另一处映射一个相同的 shm obj.
         */
        Shared_Memory(const Shared_Memory& other) requires(!creat)
        : Shared_Memory{other.name} {
            // 单个进程手上的多个 ‘Shared_Memory’ 可以标识同一个 shared memory object,
            // 它们由 复制构造 得来.  但这不代表它们的从 shared memory object 映射得到
            // 的地址 (‘area’) 相同.  对于
            //   ```Shared_Memory a, b;```
            // 若 a == b, 则恒有 a.pretty_memory_view() == b.pretty_memory_view().
        }
        /**
         * 在进程地址空间的另一处映射一个相同的 shm obj, 只读模式.
         */
        Shared_Memory(const Shared_Memory<!creat>& other) requires(!creat)
        : Shared_Memory{other.get_name()} { /* 同上 */ }
        friend void swap(Shared_Memory& a, decltype(a) b) noexcept {
            std::swap(a.name, b.name);
            std::swap(a.area, b.area);
        }
        /**
         * 映射等号与右侧同名的 shm obj, 左侧原有的 shm obj 被回收.
         */
        auto& operator=(Shared_Memory other) {
            swap(*this, other);
            return *this;
        }
        /**
         * 取消映射, 并在 shm obj 的被映射数目为 0 的时候自动销毁它.
         */
        ~Shared_Memory() noexcept  {
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

            if constexpr (DEBUG)
                std::clog << std::format("析构了 Shared_Memory: \033[31m{}\033[0m", *this) + '\n';
        }

        auto& get_name() const { return this->name; }
        auto get_area(
#ifndef __cpp_explicit_this_parameter
        ) const -> const auto& {
            auto& self = const_cast<Shared_Memory&>(*this);
#else
            this auto& self
        ) -> const auto& {
#endif
            if constexpr (!creat)
                return self.area;
            else
                if constexpr (std::is_const_v<std::remove_reference_t<decltype(self)>>)
                    return reinterpret_cast<const std::span<const unsigned char>&>(self.area);
                else
                    return self.area;
        }

        /**
         * 只要内存区域是由同一个 shm obj 映射而来 (即 同名), 就视为相等.
         */
        template <bool other_creat>
        auto operator==(const Shared_Memory<other_creat>& other) const {
            return this->get_name() == other.get_name();
        }

        /**
         * 🖨️ 打印 shm 区域的内存布局到一个字符串.  每行有 ‘num_col’
         * 个字节, 每个字节的 16 进制表示之间, 用 ‘space’ 填充.
         */
        auto pretty_memory_view(
            const std::size_t num_col = 16, const std::string_view space = " "
        ) const {
#if defined __cpp_lib_ranges_fold && defined __cpp_lib_ranges_chunk && defined __cpp_lib_ranges_join_with
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
         * 将该实例自身的属性以类似 JSON 的格式输出.
         */
        friend auto operator<<(std::ostream& out, const Shared_Memory& shm)
        -> decltype(auto) {
            return out << std::format("{}", shm);
        }

        /* impl std::ranges::range for Self */
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
        auto operator[](this auto& self, const std::size_t start, decltype(start) end) {
            assert(start <= end && end <= std::size(self));
            return std::span{
                std::begin(self) + start,
                std::begin(self) + end,
            };
        }
#endif
        /**
         * 被映射的起始地址.
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
         * 映射的区域大小.
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

template <auto creat>
struct std::formatter<Shared_Memory<creat>> {
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
                return "Shared_Memory<creat=true>";
            else
                return "Shared_Memory<creat=false>";
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


/**
 * 创建 指定大小的 名字随机生成的 shm obj, 以读写模式映射.
 */
auto operator""_shm(const unsigned long long size);
/**
 * - ‘"/name"_shm[size]’ 创建 指定大小的命名 shm obj, 以读写模式映射.
 * - ‘+"/name"_shm’ 将命名的 shm obj 以只读模式映射至本地.
 * 当 ‘size=1’ 时, 这两种操作成为进程间的同步机制.
 */
inline auto operator""_shm [[gnu::always_inline]] (const char *const name, [[maybe_unused]] std::size_t) {
    struct ShM_Constructor_Proxy {
        const char *const name;
        auto operator[] [[gnu::always_inline]] (const std::size_t size) const {
            auto&& rdwr_shm = Shared_Memory{name, size};

            if (size == 1) {
                auto flag = std::atomic_ref{rdwr_shm.get_area()[0]};
                flag.fetch_or(true, std::memory_order_release);
#if _GLIBCXX_RELEASE >= 14
                flag.notify_all();
#endif
            }

            return std::move(rdwr_shm);
        }
        auto operator+ [[gnu::always_inline]] () const {
            auto&& rdonly_shm = Shared_Memory{name};

            if (std::size(rdonly_shm.get_area()) == 1) {
                auto flag = std::atomic_ref{rdonly_shm.get_area()[0]};
#if _GLIBCXX_RELEASE >= 14
                flag.wait(false, std::memory_order_acquire);
#else
                while (!flag.load())
                    std::this_thread::sleep_for(10ms);
#endif
            }

            return rdonly_shm;
        }
    };
    return ShM_Constructor_Proxy{name};
}


namespace {
    /**
     * 创建一个全局唯一的名字提供给 shm obj.  该名字由
     *      固定前缀 + 计数字段 + 独属进程的后缀
     * 组成.
     */
    auto generate_shm_UUName() noexcept {
        constexpr auto prefix = "github_dot_com_slash_shynur_slash_ipcator";
        constexpr auto available_chars = "0123456789"
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                         "abcdefghijklmnopqrstuvwxyz"sv;

        // 在 shm obj 的名字中包含一个顺序递增的计数字段:
        constinit static std::atomic_uint cnt;
        const auto base_name = std::format(
            "{}-{:06}", prefix,
            1 + cnt.fetch_add(1, std::memory_order_relaxed)
        );

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

        assert(("/dev/shm/" + base_name + '.' + suffix).length() == 255);
        return '/' + base_name + '.' + suffix;
    }
}
auto operator""_shm(const unsigned long long size) {
    return Shared_Memory{generate_shm_UUName(), size};
}


#define IPCATOR_LOG_ALLO_OR_DEALLOC(color)  void(  \
    DEBUG && std::clog <<  \
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
#if __GNUC__ >= 14
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

            static auto get_addr(const auto& shm_or_ptr) noexcept
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
                this->last_inserted = &*inserted;

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
                .template extract<const void *>((const void *)area)
#else
                .extract(
                    std::find_if(
                        this->resources.cbegin(), this->resources.cend(),
                        [area=(const void *)area](const auto& shm) {
                            if constexpr (using_ordered_set)
                                return !ShM_As_Addr{}(shm, area) == !ShM_As_Addr{}(shm, area);
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
        ShM_Resource() = default;
        ShM_Resource(ShM_Resource&& other) noexcept
        : resources{
            std::move(other.resources)
        } {
            if constexpr (!using_ordered_set)
                this->last_inserted = std::move(other.last_inserted);
        }

#if !(__GNUC__ == 14 && __GNUC_MINOR__ == 2)
        friend class ShM_Resource<std::set>;  // see below:
#endif
        ShM_Resource(ShM_Resource<std::unordered_set>&& other) requires(using_ordered_set)
        : resources{[
#if __GNUC__ == 14 && __GNUC_MINOR__ == 2
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
        ~ShM_Resource() {
            if constexpr (DEBUG) {
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
                return std::move(self.resources);
        }

        friend auto operator<<(std::ostream& out, const ShM_Resource& resrc)
        -> decltype(auto) {
            return out << std::format("{}", resrc);
        }

        /**
         * 查询对象 (‘obj’) 位于哪个 ‘Shared_Memory’ 中.
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
struct std::formatter<ShM_Resource<set_t>> {
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
        else
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
         */
        Monotonic_ShM_Buffer(const std::size_t initial_size = 1)
        : monotonic_buffer_resource{
            ceil_to_page_size(initial_size),
            new ShM_Resource<std::unordered_set>,
        } {}
        ~Monotonic_ShM_Buffer() {
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
        ) override {
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
        ShM_Pool(const std::pmr::pool_options& options = {.largest_required_pool_block=1})
        : midstream_pool_t{
            decltype(options){
                .max_blocks_per_chunk = options.max_blocks_per_chunk,
                .largest_required_pool_block = ceil_to_page_size(
                    options.largest_required_pool_block
                ),  // 向⬆️游申请内存的🚪≥页表大小, 避免零碎的请求.
            },
            new ShM_Resource<std::set>,
        } {}
        ~ShM_Pool() {
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
        template <typename T, typename CharT = char> requires (sizeof(CharT) == 1)
        auto read(
            const std::basic_string_view<CharT> shm_name, const std::size_t offset
        ) -> const auto& {
            return *(T *)(
                std::data(
                    this->select_shm((const std::string_view&)shm_name).get_area()
                ) + offset
            );
        }

        auto select_shm(const std::string_view name) -> const
#if __GNUC__ == 14 && __GNUC_MINOR__ == 2
            auto
#else
            Shared_Memory<false>
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

            static auto get_name(const auto& shm_or_name) noexcept
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
