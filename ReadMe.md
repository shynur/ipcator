## Getting Started

一个简单的示例, 在 reader 进程中执行 writer 进程里的函数
(该示例未必能成功执行, 因为可能运行于容器 (参见 docker `--tmpfs` 参数) 等权限受限的环境中):

<https://github.com/shynur/ipcator/blob/b1f4f27de52dbb5789062bd69e6858ca7a6ffbf7/src/ipc-writer.cpp#L1-L21>
<https://github.com/shynur/ipcator/blob/b1f4f27de52dbb5789062bd69e6858ca7a6ffbf7/src/ipc-reader.cpp#L1-L13>

你可自己手动编译执行; 也可根据 [测试双进程间的通信](#测试双进程间的通信) 的提示,
将以上两段代码分别填到 [`src`](./src/) 目录下的 `ipc-*.cpp` 文件中,
再在仓库目录用 `NDEBUG=1 CXX=g++-10 ISOCPP=2a make ipc` (自己调整 `CXX` 和 `ISOCPP`) 自动执行.

## 功能

提供 POSIX-compatible 的共享内存分配器与读取器.

**执行 `make doc` 以生成文档 (在 `docs/html/`).**

## 下载

```bash
git clone -b master --single-branch --recurse-submodule https://github.com/shynur/ipcator.git
(cd ipcator
 git checkout `git tag --sort=-creatordate | grep [0-9]\$ - | head -n 1`)
```

### 工具链要求

- Bash, `awk`, `sed`, `grep`.

- Doxygen, Graphviz.

- `g++-10` 及以上, 或 `clang++-16` 及以上.

- 直接执行 `make print-vars`, 不飘红就行; 否则, 根据提示设置正确的 编译器 和 C++ 标准.  <br />
  例如, 在我的机器上有报错: `g++: error: unrecognized command line option ‘-std=c++26’; did you mean ‘-std=c++2a’?`,
  说明编译器 (`g++-9`) 太旧导致有些选项无法识别, 而 C++ 标准太新.  重新执行

  ```bash
  CXX=g++-10 ISOCPP=2a make print-vars
  ```

### 依赖项

IPCator 抗拒使用第三方库,
除了 同样是 mono-header-only 的库 和 某些标准库的前身:

#### `<format>` ➡️ `fmt/format.h`

C++20 开始提供 `<format>`, 但 `g++-10 -std=c++2a` 实际只支持部分新特性.

通过执行 `make print-vars | grep LIBS -` 查看 `LIBS` 变量中是否包含 `fmt`.
如果*是*, 说明本地缺少 `<format>` 库, 需要额外执行:

```bash
git submodule init lib/fmt
git submodule update lib/fmt
```

后续的 make 会在必要时编译生成 `libfmt.a`.

## 试运行

### 在单个进程中测试

```bash
make test
# 或
NDEBUG=1 make test  # 更好的性能, 更少的日志
```

### 测试双进程间的通信

将代码填入 [`src/ipc-writer.cpp`](./src/ipc-writer.cpp)
和 [`src/ipc-reader.cpp`](./src/ipc-reader.cpp), 然后

```bash
make ipc
# 或
NDEBUG=1 make ipc
```

就能看到结果.

### 兼容性测试

默认使用 `g++` 编译, 标准为 C++26.
但是可以:

```bash
export CXX=clang++-20  # 替换编译器为 LLVM Clang
export ISOCPP=2a  # 使用 C++2a
export NDEBUG=1  # 如果不使用 GCC, 那么这一步是必须的!
# 这 ^^^^^^^^^^^ 会排除许多独属 GCC 而 Clang 无法识别的 debug 选项.
make clean  # 删除用原来的编译器生成的链接库 e.g. `libfmt`.
make test ipc
```

## 用法

> <code>#define IPCATOR_NAMESPACE some_namespace
> #include "<a href="./include/ipcator.hpp">ipcator.hpp</a>"</code>

如果 `IPCATOR_NAMESPACE` 宏 未定义, 则 API 置于 *global namespace*.

### API 使用示例

详见 [`include/tester.hpp`](./include/tester.hpp).

## 注意事项

降级 [`$ISOCPP`](###### "-std=c++$ISOCPP") 编译时, **无法实现所有语义** (例如封装性与 *const* 方法的重载).  <br />
特别是, 形如 异质查找 ([P0919R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html)) 等新的 STL 算法可能会被手工编写的代码代替, 严重降低性能.
