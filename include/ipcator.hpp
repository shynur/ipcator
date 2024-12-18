#pragma once
// #define NDEBUG
#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <ostream>
#include <random>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <unistd.h>  // close, ftruncate, getpagesize
#include <sys/mman.h>  // m{,un}map, shm_{open,unlink}, PROT_{WRITE,READ}, MAP_{SHARED,FAILED}
#include <sys/stat.h>  // fstat, struct stat
#include <fcntl.h>  // O_{CREAT,RDWR,RDONLY,EXCL}
using std::operator""s, std::operator""sv;


namespace {
    constexpr auto DEBUG
#ifdef NDEBUG
                        = false;
#else
                        = true;
#endif
}


namespace {
    /*
     * 共享内存大小不必成为📄页表大小的整数倍, 但可以提高内存♻️利用率.
     */
    inline auto ceil_to_page_size(const std::size_t min_length) -> std::size_t {
        const auto current_num_pages = min_length / getpagesize();
        const bool need_one_more_page = min_length % getpagesize();
        return (current_num_pages + need_one_more_page) * getpagesize();
    }
}


namespace {
    /*
     * 给定 shared memory object 的名字, 创建/打开
     * 📂 shm obj, 并将其映射到进程自身的地址空间中.
     * - 对于 writer, 使用 `map_shm<true>(name,size)->void*`,
     *   其中 ‘size’ 是要创建的 shm obj 的大小;
     * - 对于 reader, 使用 `map_shm<>(name)->{addr,length}`.
     */
    constexpr auto map_shm = []<bool creat=false>(const auto resolve) consteval {
        return [=] [[nodiscard]] (
            const std::string& name, const std::unsigned_integral auto... size
        ) requires (sizeof...(size) == creat) {

            assert(name.length() <= 255 - "/dev/shm"s.length());
            const auto fd = shm_open(
                name.c_str(),
                creat ? O_CREAT|O_EXCL|O_RDWR : O_RDONLY,
                0666
            );
            assert(fd != -1);

            if constexpr (creat) {
                // 设置 shm obj 的大小:
                const auto result_resize = ftruncate(fd, size...);
                assert(result_resize != -1);
            }

            return resolve(
                fd,
                [&] {
                    if constexpr (creat)
                        return [](const auto size, ...){return size;}(size...);
                    else {
                        struct stat shm;
                        fstat(fd, &shm);
                        return shm.st_size;
                    }
                }()
            );
        };
    }([](const auto fd, const std::size_t size) {
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


/*
 * 不可变的最小单元, 表示1️⃣块共享内存区域.
 */
template <bool creat>
class Shared_Memory {
        std::string name;  // Shared memory object 的名字, 格式为 “/Abc123”.
        std::span<
            std::conditional_t<
                creat,
                std::uint8_t, const std::uint8_t
            >
        > area;
    public:
        Shared_Memory(const std::string name, const std::size_t size) requires(creat)
        : name{name}, area{
            map_shm<creat>(name, size), size
        } {
            if (DEBUG)
                // 既读取又写入✏, 以确保这块内存被正确地映射了, 且已取得读写权限.
                for (auto& byte : this->area)
                    byte ^= byte;
        }
        /*
         * 根据名字打开对应的 shm obj.  不允许 reader 指定 ‘size’,
         * 因为这是🈚意义的.  Reader 打开的是已经存在于内存中的 shm
         * obj, 占用大小已经确定, 更小的 ‘size’ 并不能节约系统资源.
         */
        Shared_Memory(const std::string name) requires(!creat)
        : name{name}, area{
            [&] -> decltype(this->area) {
                const auto [addr, length] = map_shm(name);
                return {
                    static_cast<std::uint8_t *>(addr),
                    length
                };
            }()
        } {
            if (DEBUG)
                // 只读取, 以确保这块内存被正确地映射了, 且已取得读权限.
                for (auto byte : std::as_const(this->area))
                    std::ignore = auto{byte};
        }
        template <bool other_creat>
        Shared_Memory(const Shared_Memory<other_creat>& other) requires(!creat)
        : Shared_Memory{that.name} {
            // Reader 手上的多个 ‘Shared_Memory’ 可以标识同一个 shared memory object,
            // 它们由 复制构造 得来.  但这不代表它们的从 shared memory object 映射得到
            // 的地址 (‘area’) 相同.  对于
            //   ```Shared_Memory a, b;```
            // 若 a == b, 则恒有 a.pretty_memory_view() == b.pretty_memory_view().
        }
        Shared_Memory(Shared_Memory&& other) noexcept
        : name{std::move(other.name)}, area{std::move(other.area)} {
            other.area = {};  // TODO: 可能是不必要的
        }
        friend void swap(Shared_Memory& a, Shared_Memory& b) noexcept {
            std::swap(a.name, b.name);
            std::swap(a.area, b.area);
        }
        auto& operator=(this auto& self, Shared_Memory other) {
            std::swap(self, other);
            return self;
        }
        ~Shared_Memory() {
            if (this->area.data() == nullptr)
                return;

            // 🚫 Writer 将要拒绝任何新的连接请求:
            if constexpr (creat)
                shm_unlink(this->name.c_str());
                // 此后的 ‘shm_open’ 调用都将失败.
                // 当所有 shm 都被 ‘munmap’ed 后, 共享内存将被 deallocate.

            munmap(
                const_cast<void *>(this->area.data()),
                std::size(this->area)
            );
        }

        auto get_name() const -> std::string_view { return this->name; }

        template <bool other_creat>
        auto operator==(this const auto& self, const Shared_Memory<other_creat>& other) {
            // 只要内存区域是由同一个 shm obj 映射而来, 就视为相等.
            if (self.name == other.name) {
                assert(
                    std::hash<decltype(self)>{}(self) == std::hash<decltype(other)>{}(other)
                );
                return true;
            } else
                return false;
        }

        auto& operator[](this auto& self, const std::size_t i) {
            assert(i < std::size(self.area));

            if constexpr (std::is_const_v<std::remove_reference_t<decltype(self)>>)
                return std::as_const(self.area[i]);
            else
                return self.area[i];
        }

        /* 🖨️ 打印 shm 区域的内存布局.  */
        auto pretty_memory_view(const std::size_t num_col = 16, const std::string_view space = " ") const {
            return std::ranges::fold_left(
                this->area
                | std::views::chunk(num_col)
                | std::views::transform(std::bind_back(
                    std::bit_or<>{},
                    std::views::transform([](auto& B) { return std::format("{:02X}", B); })
                    | std::views::join_with(space)
                ))
                | std::views::join_with('\n'),
                ""s, std::plus<>{}
            );
        }

        friend auto operator<<(std::ostream& out, const Shared_Memory& shm) -> decltype(auto) {
            return out << std::format("{}", shm);
        }
};
Shared_Memory(
    std::convertible_to<std::string> auto, std::integral auto
) -> Shared_Memory<true>;
Shared_Memory(
    std::convertible_to<std::string> auto
) -> Shared_Memory<false>;

template <auto creat>
struct std::hash<Shared_Memory<creat>> {
    /* 只校验字节数组.  要判断是否相等, 使用更严格的 ‘operator==’.  */
    auto operator()(const auto& shm) const noexcept {
        return std::hash<
            decltype(shm.pretty_memory_view())
        >{}(shm.pretty_memory_view());
    }
};

template <auto creat>
struct std::formatter<Shared_Memory<creat>> {
    constexpr auto parse(const auto& parser) {
        auto p = parser.begin();

        if (p != parser.end() && *p != '}')
            throw std::format_error("不支持任何格式化动词.");

        return p;
    }
    auto format(const auto& shm, auto& context) const {
        constexpr auto obj_constructor = [] consteval {
            if (creat)
                return "Shared_Memory<true>";
            else
                return "Shared_Memory<false>";
        }();
        return std::format_to(
            context.out(),
            R"({{
    "&area": {},
    "size": {},
    "name": "{}",
    "constructor": "{}"
}})",
            shm.area, std::size(shm), shm.name, obj_constructor
        );
    }
};

static_assert( !std::movable<Shared_Memory<true>> );
static_assert( !std::movable<Shared_Memory<false>> );
static_assert( !std::copy_constructible<Shared_Memory<true>> );


namespace {
    /*
     * 创建一个全局唯一的名字提供给 shm obj.
     * 由于 (取名 + 构造 shm) 不是原子的, 可能在构造 shm obj 时
     * 和已有的 shm 的名字重合, 或者同时多个进程同时创建了同名 shm.
     * 所以生成的名字必须足够长, 📉降低碰撞率.
     */
    auto generate_shm_UUName() noexcept {
        constexpr auto prefix = "github_dot_com_slash_shynur_slash_ipcator"sv;
        constexpr auto available_chars = "0123456789"
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                         "abcdefghijklmnopqrstuvwxyz"sv;

        // 在 shm obj 的名字中包含一个顺序递增的计数字段:
        constinit static std::atomic_uint cnt;
        const auto base_name = std::format(
            "{}-{:06}", prefix,
            1 + cnt.fetch_add(1, std::memory_order_relaxed)
        );

        static const auto suffix = std::ranges::fold_left(
            std::views::iota(("/dev/shm/" + base_name + '.').length(), 255u)
            | std::views::transform([
                available_chars,
                gen = std::mt19937{std::random_device{}()},
                distri = std::uniform_int_distribution<>{0, available_chars.length()-1}
            ](auto......) mutable {
                return available_chars[distri(gen)];
            }),
            ""s, std::plus<>{}
        );

        assert(("/dev/shm/" + base_name + '.' + suffix).length() == 255);
        return '/' + base_name + '.' + suffix;
    }
}


#define IPCATOR_LOG_ALLO_OR_DEALLOC(color)  void(  \
    DEBUG && std::clog <<  \
        std::source_location::current().function_name() + "\n"s  \
        + std::vformat(  \
            (color == "green"sv ? "\033[32m" : "\033[31m")  \
            + "\tsize={}, &area={}, alignment={}\033[0m\n"s,  \
            std::make_format_args(size, area, alignment)  \
        )  \
)


/*
 * 按需创建并拥有若干 ‘Shared_Memory<true>’,
 * 以向⬇️游提供 shm 页面作为 memory resource.
 */
template <template <typename K, typename V> class map_t = std::map>
class ShM_Resource: public std::pmr::memory_resource {
        map_t<const void *, std::unique_ptr<Shared_Memory<true>>> resources;
        // 恒有 ```resources.at(given_ptr)->area == given_ptr```.  TODO

    protected:
        void *do_allocate(const std::size_t size, const std::size_t alignment) noexcept(false) override {
            if (alignment > getpagesize() + 0u) {
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

            const auto shm = new Shared_Memory{generate_shm_UUName(), size};
            this->resources.emplace(shm->area, shm);
            if constexpr (requires {this->last_inserted = shm;})
                this->last_inserted = shm;

            const auto area = shm->area;
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }
        void do_deallocate(void *const area, const std::size_t size, const std::size_t alignment [[maybe_unused]]) override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");
            const auto whatcanisay_shm_out = std::move(
                this->resources.extract(area).mapped()
            );
            assert(
                size <= std::size(*whatcanisay_shm_out)
                && std::size(*whatcanisay_shm_out) <= ceil_to_page_size(size)
            );
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            if (const auto that = dynamic_cast<decltype(this)>(&other))
                return &this->resources == &that->resources;
            else
                return false;
        }

    public:
        ~ShM_Resource() {
            if (DEBUG) {
                /* 显式删除以打印日志.  */
                while (!this->resources.empty()) {
                    auto it = this->resources.begin();
                    this->deallocate(
                        const_cast<void *>(it->first),
                        std::size(*it->second)
                    );
                }
            }
        }

        auto get_resources(this auto&& self) -> decltype(auto) {
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

        friend auto operator<<(std::ostream& out, const ShM_Resource& resrc) -> decltype(auto) {
            return out << std::format("{}", resrc);
        }

        /*
         * 查询对象 (‘obj’) 位于哪个 ‘Shared_Memory’ 中, 返回它的无所有权指针.
         * 允许上层用 ‘const_cast’ 通过指针对 shm 进行修改, 在此不是未定义行为.
         * 不允许调用它的 destructor!
         */
        auto find_arena(const void *const obj) const -> const auto * requires(
            std::is_same_v<
                decltype(this->resources),
                std::map<
                    typename decltype(this->resources)::key_type,
                    typename decltype(this->resources)::mapped_type
                >
            >
        ) {
            const auto& [arena, shm] = *(
                --this->resources.upper_bound(obj)
            );
            assert(arena <= obj);

            return shm.get();
        }
        /*
         * 记录最近一次创建的 ‘Shared_Memory’.  允许上层用
         * ‘const_cast’ 通过指针对 shm 进行修改, 在此不是
         * 未定义行为.  不允许调用它的 destructor!
         */
        std::conditional_t<
            std::is_same_v<
                decltype(resources),
                std::unordered_map<
                    typename decltype(resources)::key_type,
                    typename decltype(resources)::mapped_type
                >
            >,
            const Shared_Memory<true> *, std::monostate
        > last_inserted [[indeterminate,no_unique_address]];
};

template <template <typename K, typename V> class map_t>
struct std::formatter<ShM_Resource<map_t>> {
    constexpr auto parse(const auto& parser) {
        auto p = parser.begin();

        if (p != parser.end() && *p != '}')
            throw std::format_error("不支持任何格式化动词.");

        return p;
    }
    auto format(const auto& resrc, auto& context) const {
        if constexpr (requires {resrc.find_arena(nullptr);})
            return std::format_to(
                context.out(),
                R"({{ "resources": {{ "size": {} }}, "constructor": "ShM_Resource<std::map>" }})",
                resrc.get_resources().size()
            );
        else if constexpr (sizeof resrc.last_inserted)
            return std::format_to(
                context.out(),
                R"({{
    "resources": {{ "size": {} }},
    "last_inserted":
{},
    "constructor": "ShM_Resource<std::unordered_map>"
}})",
                resrc.get_resources().size(), *resrc.last_inserted
            );
        else
            std::unreachable();
    }
};

static_assert( std::movable<ShM_Resource<std::map>> );
static_assert( std::movable<ShM_Resource<std::unordered_map>> );


/*
 * 以 ‘ShM_Resource’ 为⬆️游的单调增长 buffer.  优先使用⬆️游上次
 * 下发内存时未能用到的区域响应 ‘allocate’, 而不是再次申请内存资源.
 */
struct Monotonic_ShM_Buffer: std::pmr::monotonic_buffer_resource {
        /*
         * 设定缓冲区的初始大小, 但实际是惰性分配的💤.
         * ‘initial_size’ 如果不是📄页表大小的整数倍,
         * 几乎_一定_会浪费空间.
         */
        Monotonic_ShM_Buffer(const std::size_t initial_size = 1)
        : monotonic_buffer_resource{
            ceil_to_page_size(initial_size),
            new ShM_Resource<std::unordered_map>,
        } {}
        ~Monotonic_ShM_Buffer() {
            this->release();
            delete static_cast<ShM_Resource<std::unordered_map> *>(
                this->upstream_resource()
            );
        }

    protected:
        void *do_allocate(const std::size_t size, const std::size_t alignment) override {
            const auto area = this->monotonic_buffer_resource::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }
        void do_deallocate(void *const area, const std::size_t size, const std::size_t alignment) override {
            IPCATOR_LOG_ALLO_OR_DEALLOC("red");

            // 虚晃一枪; actually no-op.
            // ‘std::pmr::monotonic_buffer_resource::deallocate’ 的函数体其实是空的.
            this->monotonic_buffer_resource::deallocate(area, size, alignment);
        }
};


/*
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
        void *do_allocate(const std::size_t size, const std::size_t alignment) override {
            const auto area = this->midstream_pool_t::do_allocate(
                size, alignment
            );
            IPCATOR_LOG_ALLO_OR_DEALLOC("green");
            return area;
        }
        void do_deallocate(void *const area, const std::size_t size, const std::size_t alignment) override {
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
            new ShM_Resource<std::map>,
        } {}
        ~ShM_Pool() {
            this->release();
            delete static_cast<ShM_Resource<std::map> *>(
                this->upstream_resource()
            );
        }
};
