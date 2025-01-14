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
auto name = generate_shm_UUName();
assert( name.length() + 1 == 248 );
assert( name.front() == '/' );
std::cout << name << '\n';
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
auto area = (std::uint8_t *)allocator.allocate(100);
int& i = (int&)area[5],
   & j = (int&)area[5 + sizeof(int)],
   & k = (int&)area[5 + 2 * sizeof(int)];
assert(
    std::data(allocator.find_arena(&i)) == std::data(allocator.find_arena(&j))
    && std::data(allocator.find_arena(&j)) == std::data(allocator.find_arena(&k))
);  // 都在同一片 POSIX shared memory 区域.
    }
    {
auto allocator = ShM_Resource<std::unordered_set>{};
auto addr = (std::uint8_t *)allocator.allocate(sizeof(int));
const Shared_Memory<true> *pshm;
for (auto& shm : allocator.get_resources())
    if (std::data(shm) <= addr && addr < std::data(shm) + std::size(shm)) {
        pshm = &shm;
        break;
    }
assert( pshm == allocator.last_inserted );
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
auto allocator = ShM_Resource<std::unordered_set>{};
auto addr = allocator.allocate(1);
assert( (std::uint8_t *)addr == std::data(*allocator.last_inserted) );
    }
}
