试运行:

```bash
make run
```

_____

`Shared_Memory<creat>`:  表示一块共享内存.
- \[with `creat` = `true` \]:  **拥有** 指向的 shared memory object.
- \[with `creat` = `false`\]:
  可复制.
  相等性保证 它和另一个被比较的对象 所指向的内存区域 是由同一个 shared memory object 映射而来.  因此地址不必相同, 但修改是同步变化的.

`ShM_Resource`
- 实现 `std::pmr::memory_resource` 接口.
- 管理多个 `Shared_Memory`.

`Monotonic_ShM_Buffer`:  单调增长的 buffer, 析构时释放所有资源.
- 实现 `std::pmr::memory_resource` 接口.
- 内含一个 `std::pmr::monotonic_buffer_resource`, 设置其上游资源为 `ShM_Resource`.

`ShM_Allocator<sync>`: 
