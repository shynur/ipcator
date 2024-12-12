#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cassert>
#include <concepts>
#include <format>
#include <memory_resource>
#include <memory>
#include <print>
#include <ranges>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <unistd.h>  // close, ftruncate, getpagesize
#include <sys/mman.h>  // mmap, shm_open, shm_unlink, PROT_{WRITE,READ}, MAP_{SHARED,FAILED}
#include <sys/stat.h>  // fstat, struct stat
#include <fcntl.h>  // O_{CREAT,RDWR,RDONLY}
using std::operator""sv;


namespace {
    constexpr auto DEBUG
#ifdef NDEBUG
                        = false;
#else
                        = true;
#endif
}


namespace {
    /* 确保最终的 block 与页表对齐.  */
    inline auto ceil_to_page_size(const std::size_t min_length) -> std::size_t {
        assert(min_length > 0);

        const auto current_num_pages = min_length / getpagesize();
        const bool need_one_more_page = min_length % getpagesize();
        return (current_num_pages + need_one_more_page) * getpagesize();
    }
}


namespace {
    template <bool creat = false>
    auto map_shm(const std::string name, const std::size_t size) {
        // TODO: 细化参数 '0666'.
        const auto fd = shm_open(name.c_str(), creat ? O_CREAT | O_RDWR : O_RDONLY, 0666);
        assert(fd != -1);

        assert(size);
        if constexpr (creat) {
            const auto result_resize = ftruncate(fd, size);
            assert(result_resize != -1);
        }

        auto *const area = mmap(
            nullptr, size, (creat ? PROT_WRITE : 0) | PROT_READ, MAP_SHARED, fd, 0
        );
        close(fd);  // 映射完立即关闭, 对后续操作没啥影响.
        assert(area != MAP_FAILED);

        return area;
    }
}


template <bool creat>
struct Shared_Memory {
        const std::string name;  // Shared memory object 的名字, 格式为 "/Abc123".

        const std::size_t size;  // `size' 必须是 `getpagesize()' 的整数倍.
        //                ^^^^ 通常只有 writer 会关注该字段.
        void *const area;  // Kernel 将共享内存映射到进程地址空间的位置.


        Shared_Memory(const std::string name, const std::size_t size) requires(creat)
        : name{name}, size{size}, area{map_shm<creat>(name, this->size)} {
            if (DEBUG) {
                // 测试时, 允许我们随意分配任意大小的块.
            } else
                assert(size == ceil_to_page_size(size));
        }
        Shared_Memory(const std::string name) requires(!creat)
        : name{name}, size{[&] -> decltype(this->size) {
            /* `size' 本可以在计算 `area' 的过程中生成, 但这会导致延迟初始化和相应的 warning.  */
            struct stat shm;
            const auto fd = shm_open(name.c_str(), O_RDONLY, 0666);
            fstat(fd, &shm);
            close(fd);
            return shm.st_size;
        }()}, area{map_shm(name, this->size)} {}
        Shared_Memory(const Shared_Memory& that) requires(!creat): Shared_Memory{that.name} {}
        ~Shared_Memory() {
            if constexpr (creat)
                shm_unlink(this->name.c_str());
                // 此后, 新的 `shm_open' 尝试都将失败;
                // 当所有 mmap 都取消映射后, 共享内存将被 deallocate.

            munmap(this->area, this->size);
        }

        auto operator==(this const auto& self, const Shared_Memory& other) {
            if (&self == &other)
                return true;

            if constexpr (!creat) {
                const auto result = self.name == other.name;
                if (result)
                    assert(std::hash<Shared_Memory>{}(self) == std::hash<Shared_Memory>{}(other));
                return result;
            }

            return false;
        }

        auto operator[](const std::size_t i) const -> volatile std::uint8_t& {
            assert(i < this->size);
            return static_cast<std::uint8_t *>(this->area)[i];
        }

        auto pretty_memory_view(const std::size_t num_col = 32, const std::string space = " ") const {
            std::string view;
            for (const auto s : std::views::iota(0u, this->size / num_col + (this->size % num_col != 0))
                                | std::views::transform([=, this](const auto i) {
                                    return std::views::iota(0u, num_col) 
                                            | std::views::take_while([=, this](const auto j) {return i*num_col+j != this->size;})
                                            | std::views::transform([=, this](const auto j) {return std::format("{:02X}", (*this)[i*num_col+j]);})
                                            | std::views::join_with(space);
                                })
                                | std::views::join_with('\n'))
                view += s;
            return view;
        }
};
template <auto creat>
struct std::hash<Shared_Memory<creat>> {
    auto operator()(const auto& shm) const noexcept {
        return std::hash<decltype(shm.area)>{}(shm.area)
               ^ std::hash<decltype(shm.size)>{}(shm.size); 
    }
};


namespace {
    /*
     * 创建一个全局唯一的名字提供给 shm obj.
     * 由于 (取名 + 构造 shm) 不是原子的, 可能在构造 shm obj 时
     * 和已有的 shm 的名字重合, 或者同时多个进程同时创建了同名 shm.
     * 所以生成的名字必须足够长, 降低碰撞率.
     */
    auto generate_shm_UUName() {
        constexpr auto prefix = "/github_dot_com_slash_shynur_slash_ipcator"sv; 
        constexpr auto available_chars = "0123456789"
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                         "abcdefghijklmnopqrstuvwxyz"sv;

        constinit static std::atomic_uint cnt;
        auto name = std::format("{}--{:06}--", prefix, ++cnt);

        std::mt19937 gen{DEBUG ? 0 : std::random_device{}()};
        std::uniform_int_distribution<std::size_t> distrib{0, available_chars.length()-1};
        for (auto _ : std::views::iota(name.length(), 255u))
            name += available_chars[distrib(gen)];

        return name;
    }
}


class ShM_Resource: public std::pmr::memory_resource {
        std::unordered_map<void *, std::unique_ptr<Shared_Memory<true>>> shm_dict;

        void *do_allocate(const std::size_t size, [[maybe_unused]] std::size_t) override {
            const auto shm = new Shared_Memory<true>{generate_shm_UUName(), size};
            this->shm_dict.emplace(shm->area, shm);
            return shm->area;
        }
        void do_deallocate(void *const area, const std::size_t size, [[maybe_unused]] std::size_t) override {
            const auto shm = std::move(this->shm_dict.extract(area).mapped());
            assert(shm->size == ceil_to_page_size(size));
            // TODO: 下游的 `Monotonic_ShM_Buffer{}.buffer' 为什么总是不按照 页面大小 请求内存? 
        }
        bool do_is_equal(const std::pmr::memory_resource& that) const noexcept override {
            return this == &that;
        }
};
static_assert(std::movable<ShM_Resource>);


class Monotonic_ShM_Buffer: public std::pmr::memory_resource {
        ShM_Resource resrc = {};
        std::pmr::monotonic_buffer_resource buffer;

        void *do_allocate(const std::size_t size, const std::size_t alignment) override {
            return buffer.allocate(
                size,
                std::max(alignment, getpagesize() + 0ul)
            );
        }
        void do_deallocate(void *const area, const std::size_t size, const std::size_t alignment) override {
            return buffer.deallocate(
                area,
                size,
                std::max(alignment, getpagesize() + 0ul)
            );
        }
        bool do_is_equal(const std::pmr::memory_resource& that) const noexcept override {
            return this == &that;
        }
    public:
        Monotonic_ShM_Buffer(const std::size_t initial_size = getpagesize())
        : buffer{
            initial_size, &this->resrc
        } {}

        // TODO: 记录 allocation 日志.
};
static_assert(!std::movable<Monotonic_ShM_Buffer>);
static_assert(!std::copyable<Monotonic_ShM_Buffer>);


template <bool sync>
struct ShM_Allocator {
    Monotonic_ShM_Buffer upstream;
    std::conditional_t<
        sync,
        std::pmr::synchronized_pool_resource,
        std::pmr::unsynchronized_pool_resource
    > pool;

    ShM_Allocator(/* TODO */): upstream{/* args */}, pool{/* options, */ &this->upstream} {}
};
