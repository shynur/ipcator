## Getting Started

一个简单的示例, 在 reader 进程中执行 writer 进程里的函数
(该示例未必能成功执行, 因为可能运行于容器 (参见 docker `--tmpfs` 参数) 等权限受限的环境中):

<https://github.com/shynur/ipcator/blob/57884946d70a59dd798fa79a87d7f8a2f8ab74a3/src/ipc-writer.cpp#L1-L23>
<https://github.com/shynur/ipcator/blob/57884946d70a59dd798fa79a87d7f8a2f8ab74a3/src/ipc-reader.cpp#L1-L13>

你可自己手动编译执行; 也可根据 [测试双进程间的通信](#测试双进程间的通信) 的提示,
将以上两段代码分别填到 [`src`](./src/) 目录下的 `ipc-*.cpp` 文件中,
再在仓库目录用 `make ipc` 自动执行.

## 试运行

### 在单个进程中测试

```bash
make test
```

### 测试双进程间的通信

将代码填入 [`src/ipc-writer.cpp`](./src/ipc-writer.cpp)
和 [`src/ipc-reader.cpp`](./src/ipc-reader.cpp), 然后

```bash
make ipc
```

就能看到结果.

## 注意事项

降低 C++ 标准 编译时, **无法实现所有语义** (例如封装性与 *const* 方法的重载).  <br />
特别是, 形如 异质查找 ([P0919R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html)) 等新的 STL 算法可能会被手工编写的代码代替, 严重降低性能.
