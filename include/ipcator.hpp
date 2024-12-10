#include <string>
#include <cstdlib>
#include <memory_resource>
#include <array>
#include <cassert>
#include <utility>
#include <iomanip>
#include <ranges>
#include <cstddef>
#include <format>
#include <type_traits>
#include <unistd.h>  // ftruncate, sysconf
#include <sys/mman.h>  // mmap, shm_open
#include <sys/stat.h>  // fstat
#include <fcntl.h>  // O_{CREAT,RDWR,RDONLY}


namespace { constexpr auto DEBUG = true; }


namespace {
    // TODO: 页表大小可能会在运行时改变.
    const auto Page_Size = sysconf(_SC_PAGE_SIZE);

    /* 确保最终的 block 与页表对齐.  */
    auto ceil_to_page_size(const std::size_t min_length) -> std::size_t {
        assert(min_length > 0);

        const auto current_num_pages = min_length / Page_Size;
        const bool need_one_more_page = min_length % Page_Size;
        return (current_num_pages + need_one_more_page) * Page_Size;
    }
}


namespace {
    template <bool creat = false>
    auto map_shm(const std::string name, const std::size_t size) {
        // TODO: 细化参数 '0666'.
        const auto fd = shm_open(name.c_str(), creat ? O_CREAT | O_RDWR : O_RDONLY, 0666);
        assert(fd != -1);

        if constexpr (creat) {
            assert(size);
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
class Shared_Memory {
        const std::string name;  // Shared memory object 的名字, 格式为 "/Abc123".

        const std::size_t size;
        // `size' 必须是 `Page_Size' 的整数倍.  只有 writer 需要关注该字段.
        void *const area;  // Kernel 将共享内存映射到进程地址空间的位置.

        bool empty = false;
    public:
        Shared_Memory(const std::string name, const std::size_t size) requires(creat)
            : name{name}, size{DEBUG ? size : ceil_to_page_size(size)}, area{map_shm<creat>(name, this->size)} {}
        Shared_Memory(const std::string name) requires(!creat)
            : name{name}, size{[&] -> decltype(size) {
                struct stat shm;
                const auto fd = shm_open(name.c_str(), O_RDONLY, 0666);
                fstat(fd, &shm);
                close(fd);
                return shm.st_size;
            }()}, area{map_shm(name, this->size)} 
            { assert(this->size); }

        // Shared_Memory(const Shared_Memory&) requires(creat) = delete;
        Shared_Memory(const Shared_Memory& that) requires(!creat): Shared_Memory{that.name} {}

        Shared_Memory(Shared_Memory&& that) noexcept
            : name{that.name}, size{that.size}, area{that.area}, empty{that.empty}
            { that.empty = true; }
        friend void swap(Shared_Memory& a, Shared_Memory& b) {
            using Self_Raw_Bytes = std::array<std::byte, sizeof(Shared_Memory)>;
            std::swap<Self_Raw_Bytes>(
                reinterpret_cast<Self_Raw_Bytes&>(a),
                reinterpret_cast<Self_Raw_Bytes&>(b)
            );
        }
        auto& operator=(this auto& self, Shared_Memory other) {
            std::swap(self, other);
            return self;
        }

        ~Shared_Memory() {
            if (this->empty)
                return;

            if constexpr (creat)
                shm_unlink(this->name.c_str());
                // 此后, 新的 `shm_open' 尝试都将失败;
                // 当所有 mmap 都取消映射后, 共享内存将被 deallocate.

            munmap(this->area, this->size);
        }

        auto operator==(this const auto& self, const Shared_Memory& other) requires(!creat) {
            if (self.empty != other.empty)
                return false;
            if (self.empty && other.empty)
                return true;

            const auto result = self.name == other.name;

            if (result) 
                assert(
                    self.size == other.size
                    && std::hash<Shared_Memory>{}(self) == std::hash<Shared_Memory>{}(other)
                );

            return result;
        }

        auto operator[](const std::size_t i) const -> volatile std::uint8_t& {
            assert(i < this->size);
            return static_cast<std::uint8_t *>(this->area)[i];
        }

        auto pretty_memory_view(const std::size_t num_col = 32, const std::string space = " ") const -> std::string {
            assert(!this->empty);

            std::string view;
            for (const auto s : std::views::iota(0ul, this->size / num_col + (this->size % num_col != 0))
                                | std::views::transform([=, this](const auto i) {
                                    return std::views::iota(0ul, num_col) 
                                            | std::views::take_while([=, this](const auto j) {return i*num_col+j != this->size;})
                                            | std::views::transform([=, this](const auto j) {return std::format("{:02X}", (*this)[i*num_col+j]);})
                                            | std::views::join_with(space);
                                })
                                | std::views::join_with('\n'))
                view += s;
            return view;
        }
};
template <bool creat>
struct std::hash<Shared_Memory<creat>> {
    /* 仅参考 shm 所指向的块的字节数组进行 hash 计算.  */
    std::size_t operator()(const auto& shm) const noexcept { 
        return std::hash<std::string>{}(shm.pretty_memory_view()); 
    }
};


namespace {
    class Shared_Memory_Allocator: public std::pmr::memory_resource {
        void *do_allocate(std::size_t bytes, std::size_t alignment) override {
            
        }
        void do_deallocate(void *, std::size_t bytes, std::size_t alignment) {
        
        }
        bool do_is_equal(this const auto& self, const std::pmr::memory_resource& other) noexcept {
        
        }
    };
}


/* 
 * 仅供 writer 使用的进程间 allocator.
 */
class IPcator: public std::pmr::memory_resource {
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        
    }
    void do_deallocate(void *, std::size_t bytes, std::size_t alignment) {
        
    }
    bool do_is_equal(this const auto& self, const std::pmr::memory_resource& other) noexcept {
        
    }
};
