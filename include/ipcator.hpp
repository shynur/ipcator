#pragma once
// #define NDEBUG
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cassert>
#include <concepts>
#include <format>
#include <functional>
#include <memory_resource>
#include <memory>
#include <new>
#include <print>
#include <ranges>
#include <random>
#include <string>
#include <source_location>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <unistd.h>  // close, ftruncate, getpagesize
#include <sys/mman.h>  // mmap, shm_open, shm_unlink, PROT_{WRITE,READ}, MAP_{SHARED,FAILED}
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
     * 共享内存大小不必成为页表大小的整数倍, 但可以提高内存利用率.  
     */
    inline auto ceil_to_page_size(const std::size_t min_length) -> std::size_t {
        const auto current_num_pages = min_length / getpagesize();
        const bool need_one_more_page = min_length % getpagesize();
        return (current_num_pages + need_one_more_page) * getpagesize();
    }
}


namespace {
    /* 
     * 给定 shared memory object 的名字, 创建/打开 shm obj,
     * 并将其映射到进程自身的地址空间中.  对于 reader, 要求
     * 其提供的 `size' 恰好和 shm obj 的大小相等.
     */
    template <bool creat = false>
    auto map_shm(const std::string name, const std::size_t size) {
        const auto fd = shm_open(
            name.c_str(),
            creat ? O_CREAT|O_EXCL|O_RDWR : O_RDONLY,  // TODO: 需要细化.
            0666  // TODO: 细化参数 '0666'.  (下文同.)
        );
        assert(fd != -1);

        if constexpr (creat) {
            // 设置 shm obj 的大小:
            const auto result_resize = ftruncate(fd, size);
            assert(result_resize != -1);
        } else {
            // 校验 `size' 是否和 shm obj 的真实大小吻合.
            struct stat shm;
            fstat(fd, &shm);
            assert(size == shm.st_size + 0ul);
        }

        assert(size);
        const auto area = mmap(
            nullptr, size,
            (creat ? PROT_WRITE : 0) | PROT_READ,  // TODO: 需要细化.
            MAP_SHARED,  // TODO: 需要调整.
            fd, 0
        );
        close(fd);  // 映射完立即关闭, 对后续操作没啥影响.
        assert(area != MAP_FAILED);
        return area;
    }
}

/*
 * 不可变的最小单元, 表示一块共享内存区域.
 */
template <bool creat>
struct Shared_Memory {
    const std::string name;  // Shared memory object 的名字, 格式为 "/Abc123".

    // 必须先确认需求 (`size') 才能向 kernel 请求映射.
    const std::size_t size;  // 通常只有 writer 会关注该字段.
    std::conditional_t<creat, void, const void> *const area;  // Kernel 将共享内存映射到进程地址空间的位置.


    Shared_Memory(const std::string name, const std::size_t size) requires(creat)
    : name{name}, size{size}, area{map_shm<creat>(name, size)} {
        if (DEBUG)
            // 既读取又写入, 以确保这块内存被正确地映射了, 且已取得读写权限.
            for (auto& byte : *this)
                byte ^= byte;
    }
    Shared_Memory(const std::string name) requires(!creat)
    : name{name}, size{
        // `size' 可以在计算 `area' 的过程中生成, 但这会导致
        // 延迟初始化和相应的 warning.  所以必须在此计算.
        [&] -> decltype(this->size) {
            struct stat shm;
            const auto fd = shm_open(name.c_str(), O_RDONLY, 0444);
            assert(fd != -1);
            fstat(fd, &shm);
            close(fd);
            return shm.st_size;
        }()
    }, area{map_shm(name, this->size)} {
        if (DEBUG)
            // 只读取, 以确保这块内存被正确地映射了, 且已取得读权限.
            for (const auto& byte : std::as_const(*this))
                std::ignore = auto{byte};
    }
    Shared_Memory(const Shared_Memory& that) requires(!creat)
    : Shared_Memory{that.name} {
        // Reader 手上的多个 `Shared_Memory' 可以标识同一个 shared memory object,
        // 它们由 复制构造 得来.  但这不代表它们的从 shared memory object 映射得到
        // 的地址 (`area') 相同.  对于
        //     Shared_Memory a, b;
        // 若 a == b, 则恒有 a.pretty_memory_view() == b.pretty_memory_view().
    }
    ~Shared_Memory() {
        // Writer 将要拒绝任何新的连接请求:
        if constexpr (creat)
            shm_unlink(this->name.c_str());
            // 此后 `shm_open' 尝试都将失败.
            // 当所有 shm 都被 `munmap'ed 后, 共享内存将被 deallocate.

        munmap(const_cast<void *>(this->area), this->size);
    }

    auto operator==(this const auto& self, const Shared_Memory& other) {
        if (&self == &other)
            return true;

        // 对于 reader 来说, 只要内存区域是由同一个 shm obj 映射而来, 就视为相等.
        if constexpr (!creat) {
            const auto result = self.name == other.name;
            if (result)
                assert(
                    std::hash<Shared_Memory>{}(self) == std::hash<Shared_Memory>{}(other)
                );
            return result;
        }

        return false;
    }

    auto operator[](const std::size_t i) const -> volatile auto& {
        assert(i < this->size);
        return static_cast<const std::uint8_t *>(this->area)[i];
    }
    auto operator[](const std::size_t i) -> volatile auto& requires(creat) {
        static_assert(creat);
        assert(i < this->size);
        return static_cast<std::uint8_t *>(this->area)[i];
    }

    /* 打印 shm 区域的内存布局.  */
    auto pretty_memory_view(const std::size_t num_col = 32, const std::string space = " ") const noexcept {
        return std::ranges::fold_left(
            std::views::iota(0u, this->size / num_col + (this->size % num_col != 0))
            | std::views::transform(
                [=, this](const auto linum) {
                    return std::views::iota(linum * num_col)
                        | std::views::take(num_col)
                        | std::views::take_while(std::bind_back(std::less<>{}, this->size))
                        | std::views::transform([this](auto idx) { return (*this)[idx]; })
                        | std::views::transform([](auto B) { return std::format("{:02X}", B); })
                        | std::views::join_with(space);
                }
            )
            | std::views::join_with('\n'),
            ""s,
            std::plus<>{}
        );
    }

    auto begin(this auto& self) { return &self[0]; }
    auto end(this auto& self)   { return self.begin() + self.size; }
    auto cbegin() const { return &(*this)[0]; }
    auto cend()   const { return this->cbegin() + this->size; }
};
template <auto creat>
struct std::hash<Shared_Memory<creat>> {
    auto operator()(const auto& shm) const noexcept {
        return std::hash<decltype(shm.area)>{}(shm.area)
               ^ std::hash<decltype(shm.size)>{}(shm.size); 
    }
};
static_assert(!std::movable<Shared_Memory<true>>);
static_assert(!std::movable<Shared_Memory<false>>);
static_assert(std::copy_constructible<Shared_Memory<false>>);


namespace {
    /*
     * 创建一个全局唯一的名字提供给 shm obj.
     * 由于 (取名 + 构造 shm) 不是原子的, 可能在构造 shm obj 时
     * 和已有的 shm 的名字重合, 或者同时多个进程同时创建了同名 shm.
     * 所以生成的名字必须足够长, 降低碰撞率.
     */
    auto generate_shm_UUName() noexcept {
        constexpr auto prefix = "/github_dot_com_slash_shynur_slash_ipcator"sv; 
        constexpr auto available_chars = "0123456789"
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                         "abcdefghijklmnopqrstuvwxyz"sv;

        // 在 shm obj 的名字中包含一个顺序递增的计数字段:
        constinit static std::atomic_uint cnt;
        auto name = std::format("{}--{:06}--", prefix, ++cnt);

        return std::ranges::fold_left(
            std::views::iota(name.length(), 255u)
            | std::views::transform([
                available_chars,
                gen = std::mt19937{DEBUG ? 0 : std::random_device{}()},
                distri = std::uniform_int_distribution<>{0, available_chars.length()-1}
            ](auto......) mutable {
                return available_chars[distri(gen)];
            }),
            name,
            std::plus<>{}
        );
    }
}


#define IPCATOR_LOG_ALLOC()  (  \
    !DEBUG ? void()  \
    : std::println(  \
        stderr, "`{}`\n" "\033[32m\tsize={}, alignment={}\033[0m",  \
        std::source_location::current().function_name(),  \
        size, alignment  \
    )  \
)
#define IPCATOR_LOG_DEALLOC()  (  \
    !DEBUG ? void()  \
    : std::println(  \
        stderr, "`{}`\n" "\033[31m\tarea={}, size={}\033[0m",  \
        std::source_location::current().function_name(),  \
        area, size  \
    )  \
)
/*
 * 按需创建并拥有若干 `Shared_Memory<true>',
 * 以向下游提供 shm 页面作为 memory resource.
 */
class ShM_Resource: public std::pmr::memory_resource {
    std::unordered_map<void *, std::unique_ptr<Shared_Memory<true>>> shm_dict;
    // 恒有 "shm_dict.at(given_ptr)->area == given_ptr".

    void *do_allocate(const std::size_t size, const std::size_t alignment) noexcept(false) override {
        IPCATOR_LOG_ALLOC();

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

        const auto shm = new Shared_Memory<true>{generate_shm_UUName(), size};
        this->shm_dict.emplace(shm->area, shm);

        return shm->area;
    }
    void do_deallocate(void *const area, const std::size_t size, [[maybe_unused]] std::size_t) override {
        IPCATOR_LOG_DEALLOC();
        const auto whatcanisay_shm_out = std::move(this->shm_dict.extract(area).mapped());
        assert(whatcanisay_shm_out->size == size);
    }
    bool do_is_equal(const std::pmr::memory_resource& that) const noexcept override {
        return this == &that;
    }

    public: auto& get_shm_dict() const { return this->shm_dict; }
};
static_assert(std::movable<ShM_Resource>);


/*
 * 以 `ShM_Resource' 为上游的单调增长 buffer.  优先使用上游上次
 * 下发内存时未能用到的区域响应 `allocate', 而不是再次申请内存资源.
 */
class Monotonic_ShM_Buffer: public std::pmr::memory_resource {
        ShM_Resource resrc = {};
        std::pmr::monotonic_buffer_resource buffer;

        void *do_allocate(const std::size_t size, const std::size_t alignment) override {
            IPCATOR_LOG_ALLOC();
            return this->buffer.allocate(size, alignment);
        }
        void do_deallocate(void *const area, const std::size_t size, const std::size_t alignment) override {
            IPCATOR_LOG_DEALLOC();

            // Actually no-op; 虚晃一枪.
            // `std::pmr::monotonic_buffer_resource::deallocate' 的函数体是空的.
            this->buffer.deallocate(area, size, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& that) const noexcept override {
            // 每个 Self 类实例占有自己的 `resrc', 所以不可比较.
            return this == &that;
        }
    public:
        /* 
         * 设定缓冲区的初始大小, 但实际是惰性分配的.
         * `initial_size' 如果不是页表大小的整数倍, 几乎_一定_会浪费空间.
         */
        Monotonic_ShM_Buffer(const std::size_t initial_size = getpagesize())
        : buffer{
            ceil_to_page_size(initial_size or 1),
            &this->resrc
        } {}

        void release() {
            this->buffer.release();
            assert(this->resrc.get_shm_dict().empty());
        }
        auto upstream_resource() const { return &this->resrc; }

        // TODO: 记录 allocation 日志.
};
static_assert(!std::movable<Monotonic_ShM_Buffer>);
static_assert(!std::copyable<Monotonic_ShM_Buffer>);


/*
 * 以 `ShM_Resource' 为上游的单调增长 buffer.  优先使用上游上次
 * 下发内存时未能用到的区域响应 `allocate', 而不是再次申请内存资源.
 */
template <bool sync>
struct ShM_Allocator {
    std::conditional_t<
        sync,
        std::pmr::synchronized_pool_resource,
        std::pmr::unsynchronized_pool_resource
    > pool;

    ShM_Allocator(/* args */)
    : pool{
        std::pmr::pool_options{
            .largest_required_pool_block = getpagesize()
        }
    } {}
};
