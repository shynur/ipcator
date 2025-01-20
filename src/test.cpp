#include "ipcator.hpp"

int main() {
{
Shared_Memory shm{"/ipcator.example-Shared_Memory-creator", 1234};
static_assert( std::is_same_v<decltype(shm), Shared_Memory<true, true>> );
}
{
Shared_Memory creator{"/ipcator.1", 1};
Shared_Memory accessor{"/ipcator.1"};
static_assert( std::is_same_v<decltype(accessor), Shared_Memory<false, false>> );
assert( std::size(accessor) == 1 );
}
{
Shared_Memory a{"/ipcator.move", 1};
auto b{std::move(a)};
assert( std::data(a) == nullptr );
assert( std::data(b) && std::size(b) == 1 );
}
{
auto creator = new Shared_Memory{"/ipcator.1", 1};
auto accessor = Shared_Memory<false, true>{creator->get_name()};
auto reader = Shared_Memory{creator->get_name()};
(*creator)[0] = 42;
assert( accessor[0] == 42 && reader[0] == 42 );
delete creator;
accessor[0] = 77;
assert( reader[0] == 77 );
}
{
auto a = Shared_Memory{"/ipcator.name", 1};
assert( a.get_name() == "/ipcator.name" );
}
{
auto a = Shared_Memory{"/ipcator.assign-1", 3};
a = {"/ipcator.assign-2", 5};
assert(
    a.get_name() == "/ipcator.assign-2" && std::size(a) == 5
);
}
{
std::cout << Shared_Memory{"/ipcator.print", 10} << '\n';
}
{
ShM_Resource<std::unordered_set> a;
auto _ = a.allocate(1);
assert( std::size(a.get_resources()) == 1 );
ShM_Resource<std::set> b{std::move(a)};
assert(
    std::size(a.get_resources()) == 0
    && std::size(b.get_resources()) == 1
);
}
{
auto allocator = ShM_Resource<std::set>{};
auto _ = allocator.allocate(12); _ = allocator.allocate(34, 8);
     allocator = ShM_Resource<std::unordered_set>{};
     _ = allocator.allocate(56), _ = allocator.allocate(78, 16);
}
{
auto allocator_1 = ShM_Resource<std::set>{};
allocator_1.deallocate(
    allocator_1.allocate(111), 111
);
auto allocator_2 = ShM_Resource<std::unordered_set>{};
allocator_2.deallocate(
    allocator_2.allocate(222), 222
);
}
{
auto allocator = ShM_Resource<std::set>{};
auto area = (char *)allocator.allocate(100);
int& i = (int&)area[8],
   & j = (int&)area[8 + sizeof(int)],
   & k = (int&)area[8 + 2 * sizeof(int)];
assert(
    allocator.find_arena(&i).get_name() == allocator.find_arena(&j).get_name()
    && allocator.find_arena(&j).get_name() == allocator.find_arena(&k).get_name()
);  // 都在同一片 POSIX shared memory 区域.
}
{
auto allocator = ShM_Resource<std::unordered_set>{};
auto addr = (char *)allocator.allocate(sizeof(int));
const Shared_Memory<true> *pshm;
for (auto& shm : allocator.get_resources())
    if (std::data(shm) <= addr && addr < std::data(shm) + std::size(shm)) {
        pshm = &shm;
        break;
    }
assert( pshm == &allocator.find_arena(addr) );
}
{
auto allocator = ShM_Resource<std::set>{};
auto _ = allocator.allocate(1);
     _ = allocator.allocate(2);
     _ = allocator.allocate(3);
assert( std::size(allocator.get_resources())== 3 );
allocator = {};
assert( std::size(allocator.get_resources())== 0 );
}
{
auto a = ShM_Resource<std::set>{};
auto _ = a.allocate(1);
auto b = ShM_Resource<std::unordered_set>{};
_ = b.allocate(2), _ = b.allocate(2);
std::cout << a << '\n'
          << b << '\n';
}
{
assert( ceil_to_page_size(0) == 0 );
std::cout << ceil_to_page_size(1);
}
{
auto name = generate_shm_UUName();
assert( name.length() + 1 == 248 );  // 计算时包括 NULL 字符.
assert( name.front() == '/' );
std::cout << name << '\n';
}
{
using namespace literals;
auto creator = "/ipcator.1"_shm[123];
creator[5] = 5;
auto accessor = +"/ipcator.1"_shm;
assert( accessor[5] == 5 );
auto reader = -"/ipcator.1"_shm;
accessor[9] = 9;
assert( reader[9] == 9 );
}
{
auto buffer = Monotonic_ShM_Buffer{};
auto addr = (char *)buffer.allocate(100);
const Shared_Memory<true>& shm = buffer.upstream_resource()->find_arena(addr);
assert(
    std::to_address(std::cbegin(shm)) <= addr
    && addr < std::to_address(std::cend(shm))
);  // 新划取的区域一定位于 `upstream_resource()` 最近一次分配的内存块中.
}
{
auto pools = ShM_Pool<false>{
    std::pmr::pool_options{
        .max_blocks_per_chunk = 0,
        .largest_required_pool_block = 8000,
    }
};
std::cout << pools.options().largest_required_pool_block << ", "
          << pools.options().max_blocks_per_chunk << '\n';
}
{
auto pools = ShM_Pool<false>{};
auto _ = pools.allocate(1);
assert( std::size(pools.upstream_resource()->get_resources()) );
pools.release();
assert( std::size(pools.upstream_resource()->get_resources()) == 0 );
}
{
auto pools = ShM_Pool<false>{};
auto addr = (char *)pools.allocate(100);
auto& arr = (std::array<char, 10>&)addr[50];
const Shared_Memory<true>& shm = pools.upstream_resource()->find_arena(&arr);
assert(
    std::to_address(std::cbegin(shm)) <= (char *)&arr
    && (char *)&arr < std::to_address(std::cend(shm))
);
}
{
// writer.cpp
using namespace literals;
auto shm = "/ipcator.1"_shm[1000];
auto arr = new(&shm[42]) std::array<char, 32>;
(*arr)[15] = 9;
// reader.cpp
auto rd = ShM_Reader{};
auto& arr_from_other_proc
    = rd.template read<std::array<char, 32>>("/ipcator.1", 42);
assert( arr_from_other_proc[15] == 9 );
}
}
